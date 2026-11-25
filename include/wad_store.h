#ifndef WEACT_WAD_STORE_H
#define WEACT_WAD_STORE_H

#include <stdbool.h>
#include <stdint.h>

enum {
    WAD_READY, DISPLAY_ERROR,
    NOR_ERROR, SD_ERROR, WAD_ERROR, NOR_IN_USE, WRITE_ERROR, ENGINE_ERROR, INDEX_ERROR,
};

unsigned wad_prepare(bool import_requested);
unsigned wad_prepare_pack(const char *name, uint32_t length, uint32_t crc, bool force);
uint32_t wad_cached_crc(void);
void handheld_progress(unsigned done, unsigned total);

#endif
