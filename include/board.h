#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#define SCREEN_W 160
#define SCREEN_H 80

bool board_init(void);
bool board_button(void);
bool board_present(uint16_t *pixels);
void board_error(unsigned flashes) __attribute__((noreturn));

#endif
