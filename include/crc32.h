#ifndef WEACT_CRC32_H
#define WEACT_CRC32_H
#include <stdint.h>
uint32_t crc32_update(uint32_t crc, const uint8_t *data, unsigned length);
#endif
