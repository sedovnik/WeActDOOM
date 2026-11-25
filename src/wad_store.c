#include "wad_store.h"
#include "crc32.h"
#include "nor.h"
#include "doom_iwad.h"
#include "ff.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define DATA_START NOR_SECTOR
#define CACHE_MAGIC UINT32_C(0x31444157)
#define CACHE_PENDING UINT32_C(0x7fffffff)
#define CACHE_READY UINT32_C(0x3fffffff)

typedef struct {
    uint32_t magic;
    uint32_t length;
    uint32_t crc;
    uint32_t check;
    uint32_t state;
    uint32_t extent;
    uint32_t extent_check;
} CacheHeader;

const unsigned char *doom_iwad;
unsigned int doom_iwad_len;
static uint32_t active_crc;

static uint8_t transfer[NOR_SECTOR] __attribute__((aligned(4)));

static uint32_t le32(const uint8_t *input)
{
    return (uint32_t)input[0] | ((uint32_t)input[1] << 8) |
           ((uint32_t)input[2] << 16) | ((uint32_t)input[3] << 24);
}

static bool read_file(FIL *file, void *output, unsigned length)
{
    UINT count;
    return f_read(file, output, length, &count) == FR_OK && count == length;
}

static bool read_at(FIL *file, uint32_t offset, void *output, unsigned length)
{
    return f_lseek(file, offset) == FR_OK && read_file(file, output, length);
}

static bool wad_valid(FIL *file, uint32_t length)
{
    uint8_t header[12];
    if (length < sizeof(header) || !read_at(file, 0, header, sizeof(header)) ||
        memcmp(header, "IWAD", 4) != 0) return false;
    uint32_t count = le32(header + 4);
    uint32_t dir = le32(header + 8);
    if (dir < sizeof(header) || (dir & 3u) || dir > length || count > (length - dir) / 16u) {
        return false;
    }

    static const char required[][8] = {
        "GBAVER", "GBAPAL", "GBASTATE", "GBAMOBJ", "GBAWEAP", "GBAMISC", "GBATEXT", "TITLEPIC",
    };
    uint32_t version_offset = 0;
    unsigned seen = 0;
    if (f_lseek(file, dir) != FR_OK) return false;
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t entry[16];
        if (!read_file(file, entry, sizeof(entry))) return false;
        uint32_t pos = le32(entry);
        uint32_t size = le32(entry + 4);
        if (pos > length || size > length - pos) return false;
        for (unsigned j = 0; j < sizeof(required) / sizeof(required[0]); ++j) {
            if (memcmp(entry + 8, required[j], 8) != 0) continue;
            if (!size || (pos & 3u)) return false;
            if (j == 0) {
                if (size != 4) return false;
                version_offset = pos;
            }
            seen |= 1u << j;
        }
    }
    uint8_t version[4];
    return seen == (1u << (sizeof(required) / sizeof(required[0]))) - 1u &&
           read_at(file, version_offset, version, sizeof(version)) &&
           le32(version) == DOOM_IWAD_VERSION;
}

uint32_t crc32_update(uint32_t crc, const uint8_t *data, unsigned length)
{
    // Four bits at a time. CRC-32/ISO-HDLC, as in zlib.
    static const uint32_t table[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
        0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
    };
    for (unsigned i = 0; i < length; ++i) {
        crc ^= data[i];
        crc = (crc >> 4) ^ table[crc & 15u];
        crc = (crc >> 4) ^ table[crc & 15u];
    }
    return crc;
}

static unsigned chunk_size(uint32_t remaining)
{
    return remaining > sizeof(transfer) ? sizeof(transfer) : remaining;
}

static bool file_crc(FIL *file, uint32_t length, uint32_t *result)
{
    uint32_t crc = UINT32_MAX;
    if (f_lseek(file, 0) != FR_OK) return false;
    for (uint32_t i = 0; i < length;) {
        unsigned chunk = chunk_size(length - i);
        if (!read_file(file, transfer, chunk)) return false;
        crc = crc32_update(crc, transfer, chunk);
        i += chunk;
    }
    *result = ~crc;
    return true;
}

static bool flash_crc(uint32_t length, uint32_t expected)
{
    uint32_t crc = UINT32_MAX;
    for (uint32_t i = 0; i < length;) {
        unsigned chunk = chunk_size(length - i);
        if (!nor_read(DATA_START + i, transfer, chunk)) return false;
        crc = crc32_update(crc, transfer, chunk);
        i += chunk;
    }
    return ~crc == expected;
}

static unsigned flash_blank(uint32_t from, uint32_t to)
{
    for (uint32_t addr = from; addr < to; addr += NOR_SECTOR) {
        if (!nor_read(addr, transfer, sizeof(transfer))) return NOR_ERROR;
        for (unsigned i = 0; i < sizeof(transfer); ++i) {
            if (transfer[i] != 0xff) return NOR_IN_USE;
        }
    }
    return WAD_READY;
}

static uint32_t allocated_end(uint32_t length)
{
    return DATA_START + (length + NOR_SECTOR - 1u) / NOR_SECTOR * NOR_SECTOR;
}

static bool cache_recognized(const CacheHeader *cache)
{
    return cache->magic == CACHE_MAGIC &&
           cache->length > 12 && cache->length <= NOR_BYTES - DATA_START &&
           cache->check == ~(cache->magic ^ cache->length ^ cache->crc) &&
           (cache->state == CACHE_READY || cache->state == CACHE_PENDING);
}

static uint32_t owned_end(const CacheHeader *cache)
{
    uint32_t minimum = allocated_end(cache->length);
    if (cache->extent >= minimum && cache->extent <= NOR_BYTES &&
        cache->extent % NOR_SECTOR == 0 && cache->extent_check == ~(cache->extent ^ cache->check)) {
        return cache->extent;
    }
    return minimum; // Header from the single-WAD firmware.
}

static bool map_data(uint32_t length, uint32_t crc)
{
    if (!nor_map()) return false;
    doom_iwad = (const unsigned char *)(NOR_BASE + DATA_START);
    if (memcmp(doom_iwad, "IWAD", 4) != 0) return false;
    doom_iwad_len = length;
    active_crc = crc;
    return true;
}

uint32_t wad_cached_crc(void)
{
    return active_crc;
}

static bool write_data(FIL *file, uint32_t length)
{
    if (f_lseek(file, 0) != FR_OK) return false;
    for (uint32_t i = 0; i < length;) {
        unsigned chunk = chunk_size(length - i);
        if (!read_file(file, transfer, chunk)) return false;
        for (unsigned j = 0; j < chunk; j += NOR_PAGE) {
            unsigned page = chunk - j > NOR_PAGE ? NOR_PAGE : chunk - j;
            if (!nor_program(DATA_START + i + j, transfer + j, page)) return false;
        }
        i += chunk;
        if (i % (128u * 1024u) == 0 || i == length) handheld_progress(i, length);
    }
    return true;
}

static unsigned import(FIL *file, const CacheHeader *old, bool request,
                       uint32_t expected_length, uint32_t expected_crc, uint32_t *loaded_length)
{
    FSIZE_t size = f_size(file);
    if (size < 12 || size > NOR_BYTES - DATA_START) return WAD_ERROR;
    uint32_t length = (uint32_t)size;
    if (expected_length && length != expected_length) return WAD_ERROR;
    if (!wad_valid(file, length)) return WAD_ERROR;
    uint32_t crc;
    if (!file_crc(file, length, &crc)) return SD_ERROR;
    if (expected_length && crc != expected_crc) return WAD_ERROR;

    bool owned = cache_recognized(old);
    bool same = owned && old->length == length && old->crc == crc;
    if (same && old->state == CACHE_READY && flash_crc(length, crc)) {
        *loaded_length = length;
        active_crc = crc;
        return WAD_READY;
    }
    if (owned && !same && !request) return NOR_IN_USE;

    uint32_t new_end = allocated_end(length);
    uint32_t old_end = owned ? owned_end(old) : 0;
    if (new_end > old_end) {
        unsigned blank = flash_blank(old_end, new_end);
        if (blank != WAD_READY) return blank;
    }

    CacheHeader pending = {
        .magic = CACHE_MAGIC, .length = length, .crc = crc,
        .check = ~(CACHE_MAGIC ^ length ^ crc), .state = CACHE_PENDING,
        .extent = new_end > old_end ? new_end : old_end,
    };
    pending.extent_check = ~(pending.extent ^ pending.check);
    if (!same || old->state != CACHE_PENDING) {
        // Until the commit marker is written a restart will retry with the same SD file.
        if ((owned && !nor_erase(0)) || !nor_program(0, &pending, sizeof(pending))) return WRITE_ERROR;
    }
    uint32_t erase_end = old_end < new_end ? old_end : new_end;
    for (uint32_t addr = DATA_START; addr < erase_end;) {
        if (addr % NOR_BLOCK == 0 && erase_end - addr >= NOR_BLOCK) {
            if (!nor_erase_block(addr)) return WRITE_ERROR;
            addr += NOR_BLOCK;
        } else {
            if (!nor_erase(addr)) return WRITE_ERROR;
            addr += NOR_SECTOR;
        }
    }
    if (!write_data(file, length) || !flash_crc(length, crc)) return WRITE_ERROR;
    uint32_t committed = CACHE_READY;
    if (!nor_program(offsetof(CacheHeader, state), &committed, sizeof(committed))) return WRITE_ERROR;
    *loaded_length = length;
    active_crc = crc;
    return WAD_READY;
}

unsigned wad_prepare(bool import_requested)
{
    CacheHeader cache;
    if (!nor_init() || !nor_read(0, &cache, sizeof(cache))) return NOR_ERROR;
    if (!import_requested && cache_recognized(&cache) && cache.state == CACHE_READY &&
        flash_crc(cache.length, cache.crc)) {
        return map_data(cache.length, cache.crc) ? WAD_READY : NOR_ERROR;
    }

    FATFS fs;
    FIL file;
    unsigned result = SD_ERROR;
    uint32_t length = 0;
    if (f_mount(&fs, "", 1) == FR_OK && f_open(&file, "DOOM.GBA", FA_READ) == FR_OK) {
        result = import(&file, &cache, import_requested, 0, 0, &length);
        (void)f_close(&file);
    }
    (void)f_mount(NULL, "", 0);
    if (result != WAD_READY) return result;
    return map_data(length, active_crc) ? WAD_READY : NOR_ERROR;
}

unsigned wad_prepare_pack(const char *name, uint32_t length, uint32_t crc, bool force)
{
    CacheHeader cache;
    if (!nor_init() || !nor_read(0, &cache, sizeof(cache))) return NOR_ERROR;
    if (!force && cache_recognized(&cache) && cache.state == CACHE_READY &&
        cache.length == length && cache.crc == crc && flash_crc(length, crc)) {
        return map_data(length, crc) ? WAD_READY : NOR_ERROR;
    }
    FIL file;
    if (f_open(&file, name, FA_READ) != FR_OK) return SD_ERROR;
    uint32_t loaded = 0;
    unsigned result = import(&file, &cache, true, length, crc, &loaded);
    (void)f_close(&file);
    if (result != WAD_READY) return result;
    return map_data(loaded, active_crc) ? WAD_READY : NOR_ERROR;
}
