#include "campaign.h"
#include "crc32.h"
#include "doom_iwad.h"
#include "ff.h"
#include "nor.h"
#include "wad_store.h"
#include "stm32h7xx_hal.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define MAX_MAPS 36
#define PROFILE_MAGIC UINT32_C(0x32564153)

typedef struct {
    uint32_t length;
    uint32_t crc;
} Pack;

typedef struct {
    char magic[8];
    uint8_t id[16];
    uint8_t mode;
    uint8_t count;
    uint8_t reserved[2];
    uint8_t map_pack[MAX_MAPS];
    Pack packs[MAX_MAPS];
    uint32_t crc;
} Manifest;

typedef struct {
    uint32_t magic;
    uint32_t sequence;
    uint8_t id[16];
    uint32_t pending;
    Checkpoint slots[CHECKPOINT_SLOTS];
    uint8_t settings[32];
    uint32_t crc;
} Profile;

_Static_assert(sizeof(Manifest) == 356, "campaign manifest format");
_Static_assert(sizeof(Checkpoint) == 112, "checkpoint format");
_Static_assert(sizeof(Profile) == 960, "save journal format");

static FATFS card_volume;
static Manifest manifest;
static Profile profile;
static bool mounted;
static unsigned current_pack;

static uint32_t checksum(const void *data, unsigned size)
{
    return ~crc32_update(UINT32_MAX, data, size);
}

static int map_index(int episode, int map)
{
    if (manifest.mode == 2) return map >= 1 && map <= 32 ? map - 1 : -1;
    if (episode < 1 || episode > 4 || map < 1 || map > 9) return -1;
    return (episode - 1) * 9 + map - 1;
}

static unsigned pack_for(int episode, int map)
{
    int index = map_index(episode, map);
    return manifest.count && index >= 0 ? manifest.map_pack[index] : 0;
}

int campaign_mode(void)
{
    return manifest.count ? manifest.mode : -1;
}

bool campaign_has_map(int episode, int map)
{
    return pack_for(episode, map) != 0;
}

bool campaign_needs_pack(int episode, int map)
{
    unsigned pack = pack_for(episode, map);
    return pack && pack != current_pack;
}

static FRESULT read_exact(const char *path, void *output, UINT size)
{
    FIL file;
    FRESULT result = f_open(&file, path, FA_READ);
    if (result != FR_OK) return result;
    UINT read = 0;
    if (f_size(&file) != size) result = FR_INT_ERR;
    else {
        result = f_read(&file, output, size, &read);
        if (result == FR_OK && read != size) result = FR_INT_ERR;
    }
    (void)f_close(&file);
    return result;
}

static bool manifest_valid(void)
{
    if (memcmp(manifest.magic, "WACTC01", 8) != 0 ||
        manifest.mode > 3 || !manifest.count || manifest.count > MAX_MAPS ||
        !manifest.map_pack[0] || manifest.crc != checksum(&manifest, offsetof(Manifest, crc))) {
        return false;
    }
    for (unsigned i = 0; i < MAX_MAPS; ++i) {
        if (manifest.map_pack[i] > manifest.count) return false;
    }
    for (unsigned i = 0; i < manifest.count; ++i) {
        if (manifest.packs[i].length < 12 || manifest.packs[i].length > NOR_BYTES - NOR_SECTOR) {
            return false;
        }
    }
    return true;
}

static void journal_path(char path[41], unsigned bank)
{
    static const char hex[] = "0123456789ABCDEF";
    path[0] = 'S';
    for (unsigned i = 0; i < sizeof(profile.id); ++i) {
        path[1 + 2 * i] = hex[profile.id[i] >> 4];
        path[2 + 2 * i] = hex[profile.id[i] & 15];
    }
    path[33] = bank ? '1' : '0';
    memcpy(path + 34, ".DAT", 5);
}

static void open_profile(const uint8_t id[16])
{
    memset(&profile, 0, sizeof(profile));
    profile.magic = PROFILE_MAGIC;
    memcpy(profile.id, id, sizeof(profile.id));
    if (!mounted) return;
    for (unsigned bank = 0; bank < 2; ++bank) {
        Profile candidate;
        char path[41];
        journal_path(path, bank);
        if (read_exact(path, &candidate, sizeof(candidate)) == FR_OK &&
            candidate.magic == PROFILE_MAGIC && candidate.sequence % 2 == bank &&
            memcmp(candidate.id, id, sizeof(candidate.id)) == 0 &&
            candidate.crc == checksum(&candidate, offsetof(Profile, crc)) &&
            (!profile.sequence || (int32_t)(candidate.sequence - profile.sequence) > 0)) {
            profile = candidate;
        }
    }
}

static bool commit(Profile *next)
{
    if (!mounted) return false;
    next->sequence = profile.sequence + 1;
    next->crc = checksum(next, offsetof(Profile, crc));
    char path[41];
    journal_path(path, next->sequence % 2);
    FIL file;
    if (f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;
    UINT done = 0;
    FRESULT written = f_write(&file, next, sizeof(*next), &done);
    FRESULT closed = f_close(&file); // Syncs the file and the SD card.
    if (written != FR_OK || done != sizeof(*next) || closed != FR_OK) return false;
    profile = *next;
    return true;
}

unsigned campaign_boot(bool force)
{
    mounted = f_mount(&card_volume, "", 1) == FR_OK;
    FRESULT result = mounted ? read_exact("CAMPAIGN.IDX", &manifest, sizeof(manifest)) : FR_NO_FILE;
    if (result == FR_NO_FILE || result == FR_NO_PATH) {
        memset(&manifest, 0, sizeof(manifest));
        (void)f_mount(NULL, "", 0);
        unsigned status = wad_prepare(force);
        if (status != WAD_READY) return status;
        mounted = f_mount(&card_volume, "", 1) == FR_OK;
        uint32_t id[4] = {0x44415731, wad_cached_crc(), doom_iwad_len, 0};
        open_profile((const uint8_t *)id);
        return WAD_READY;
    }
    if (result != FR_OK || !manifest_valid()) return INDEX_ERROR;
    open_profile(manifest.id);
    const Checkpoint *automatic = campaign_slot(CHECKPOINT_AUTO_SLOT);
    current_pack = automatic ? pack_for(automatic->episode, automatic->map) : manifest.map_pack[0];
    if (!current_pack) return INDEX_ERROR;
    char name[13];
    (void)snprintf(name, sizeof(name), "P%03u.GBA", current_pack);
    Pack pack = manifest.packs[current_pack - 1];
    return wad_prepare_pack(name, pack.length, pack.crc, force);
}

bool campaign_request(const Checkpoint *entry)
{
    if (!campaign_needs_pack(entry->episode, entry->map)) return false;
    Profile next = profile;
    next.pending = 1;
    next.slots[CHECKPOINT_AUTO_SLOT] = *entry;
    if (!commit(&next)) return false;
    NVIC_SystemReset();
    return false;
}

const Checkpoint *campaign_pending(void)
{
    return profile.pending == 1 ? campaign_slot(CHECKPOINT_AUTO_SLOT) : NULL;
}

const Checkpoint *campaign_slot(unsigned slot)
{
    return slot < CHECKPOINT_SLOTS && profile.slots[slot].map > 0 ? &profile.slots[slot] : NULL;
}

bool campaign_save_slot(unsigned slot, const Checkpoint *entry)
{
    if (slot >= CHECKPOINT_AUTO_SLOT) return false;
    Profile next = profile;
    next.slots[slot] = *entry;
    return commit(&next);
}

bool campaign_save_auto(const Checkpoint *entry)
{
    Profile next = profile;
    next.pending = 0;
    next.slots[CHECKPOINT_AUTO_SLOT] = *entry;
    return commit(&next);
}

void campaign_read_settings(void *data, unsigned length)
{
    if (length <= sizeof(profile.settings)) memcpy(data, profile.settings, length);
}

bool campaign_write_settings(const void *data, unsigned length)
{
    if (length > sizeof(profile.settings)) return false;
    Profile next = profile;
    memcpy(next.settings, data, length);
    return commit(&next);
}
