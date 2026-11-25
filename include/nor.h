#ifndef WEACT_NOR_H
#define WEACT_NOR_H

#include <stdbool.h>
#include <stdint.h>

#define NOR_BASE UINT32_C(0x90000000)
#define NOR_BYTES (8u * 1024u * 1024u)
#define NOR_SECTOR 4096u
#define NOR_PAGE 256u
#define NOR_BLOCK (64u * 1024u)

bool nor_init(void);
bool nor_read(uint32_t address, void *data, uint32_t length);
bool nor_program(uint32_t address, const void *data, uint32_t length);
bool nor_erase(uint32_t address);
bool nor_erase_block(uint32_t address);
bool nor_map(void);

#endif
