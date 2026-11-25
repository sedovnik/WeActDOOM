#ifndef WEACT_CONTROLS_H
#define WEACT_CONTROLS_H

#include <stdint.h>

enum {
    PAD_UP = 1u << 0, PAD_DOWN = 1u << 1, PAD_LEFT = 1u << 2, PAD_RIGHT = 1u << 3,
    PAD_FIRE = 1u << 4, PAD_USE = 1u << 5, PAD_STRAFE = 1u << 6, PAD_WEAPON = 1u << 7,
    PAD_MENU = 1u << 8, PAD_K1 = 1u << 9,
    PAD_INPUT_COUNT = 10,
};

void controls_init(void);
void controls_poll(void);
uint16_t controls_raw_state(void);
uint16_t controls_state(void);
uint16_t controls_take_presses(void);

#endif
