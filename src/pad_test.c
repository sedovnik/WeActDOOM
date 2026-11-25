#include "pad_test.h"
#include "board.h"
#include "controls.h"
#include "wad_store.h"
#include "stm32h7xx_hal.h"
#include <string.h>

// RGB565 values in SPI byte order.
#define INK      UINT16_C(0xffff)
#define DIM      UINT16_C(0x8c21)
#define RAW      UINT16_C(0xe0ff)
#define FILTERED UINT16_C(0xe007)
#define SEEN     UINT16_C(0xff07)

static const uint8_t digits[9][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7},
    {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7},
    {7, 4, 7, 5, 7}, {7, 1, 1, 2, 2}, {7, 5, 7, 5, 7},
};
static const uint8_t letters[26][5] = {
    ['A' - 'A'] = {2, 5, 7, 5, 5}, ['D' - 'A'] = {6, 5, 5, 5, 6},
    ['E' - 'A'] = {7, 4, 6, 4, 7}, ['H' - 'A'] = {5, 5, 7, 5, 5},
    ['K' - 'A'] = {5, 6, 4, 6, 5},
    ['L' - 'A'] = {4, 4, 4, 4, 7}, ['P' - 'A'] = {6, 5, 6, 4, 4},
    ['R' - 'A'] = {6, 5, 6, 5, 5}, ['S' - 'A'] = {7, 4, 7, 1, 7},
    ['T' - 'A'] = {7, 2, 2, 2, 2}, ['W' - 'A'] = {5, 5, 5, 7, 5},
};

static void rect(uint16_t *pixels, unsigned x, unsigned y, unsigned width,
                 unsigned height, uint16_t color)
{
    for (unsigned row = y; row < y + height; ++row) {
        for (unsigned col = x; col < x + width; ++col) {
            pixels[row * SCREEN_W + col] = color;
        }
    }
}

static void text(uint16_t *pixels, unsigned x, unsigned y, unsigned scale,
                 uint16_t color, const char *label)
{
    for (; *label != '\0'; ++label, x += 4 * scale) {
        const uint8_t *glyph;
        if (*label >= '0' && *label <= '8') glyph = digits[*label - '0'];
        else if (*label >= 'A' && *label <= 'Z') glyph = letters[*label - 'A'];
        else continue;
        for (unsigned row = 0; row < 5; ++row) {
            for (unsigned col = 0; col < 3; ++col) {
                if (glyph[row] & (4u >> col)) {
                    rect(pixels, x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

static void pad_test_run(uint16_t *pixels)
{
    static const char *const labels[PAD_INPUT_COUNT] = {
        "PA0", "PA1", "PA2", "PA3", "PA4", "PA5", "PA6", "PA7", "PA8", "K1",
    };
    uint16_t seen = 0;
    (void)controls_take_presses();
    for (;;) {
        uint16_t raw = controls_raw_state();
        uint16_t held = controls_state();
        seen |= held | controls_take_presses();
        memset(pixels, 0, SCREEN_W * SCREEN_H * sizeof(*pixels));
        text(pixels, 49, 3, 2, INK, "PAD TEST");
        for (unsigned i = 0; i < PAD_INPUT_COUNT; ++i) {
            unsigned x = 4 + (i % 5) * 31;
            unsigned y = 19 + (i / 5) * 25;
            unsigned bit = 1u << i;
            text(pixels, x + (i == 9 ? 10 : 8), y, 1, INK, labels[i]);
            rect(pixels, x + 4, y + 8, 8, 7, raw & bit ? RAW : DIM);
            rect(pixels, x + 16, y + 8, 8, 7, held & bit ? FILTERED : DIM);
            rect(pixels, x + 4, y + 17, 20, 2, seen & bit ? SEEN : DIM);
        }
        text(pixels, 35, 72, 1, RAW, "RAW");
        text(pixels, 66, 72, 1, FILTERED, "HELD");
        text(pixels, 101, 72, 1, SEEN, "SEEN");
        if (!board_present(pixels)) board_error(DISPLAY_ERROR);
        HAL_Delay(20);
    }
}

bool pad_test_check_boot(uint16_t *pixels)
{
    if (!board_button()) return false;
    uint32_t started = HAL_GetTick();
    while (board_button()) {
        if (HAL_GetTick() - started >= 1500) pad_test_run(pixels);
        HAL_Delay(10);
    }
    return true;
}
