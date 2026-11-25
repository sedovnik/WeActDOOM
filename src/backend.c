#include "board.h"
#include "campaign.h"
#include "controls.h"
#include "pad_test.h"
#include "wad_store.h"
#include "stm32h7xx_hal.h"

#include "doomdef.h"
#include "d_event.h"
#include "d_main.h"
#include "global_data.h"
#include "i_sound.h"
#include "i_system_e32.h"
#include "i_video.h"
#include "z_zone.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define VIEW_WIDTH 108u
#define VIEW_LEFT ((SCREEN_W - VIEW_WIDTH) / 2u)

// Doom renders 240x160 palette indices, with pitches expressed in halfwords.
static uint16_t framebuffer[SCREENWIDTH * SCREENHEIGHT]
    __attribute__((section(".fast_buffers")));
static uint16_t frontbuffer[SCREENWIDTH * SCREENHEIGHT]
    __attribute__((section(".fast_buffers")));
static uint16_t panel[SCREEN_W * SCREEN_H] __attribute__((section(".fast_buffers")));
static uint16_t palette[256];
static uint32_t time_origin;
static volatile char engine_error_message[96];

void handheld_progress(unsigned done, unsigned total)
{
    unsigned width = total ? (SCREEN_W - 16u) * done / total : 0;
    for (unsigned y = 0; y < SCREEN_H; ++y) {
        for (unsigned x = 0; x < SCREEN_W; ++x) {
            bool mark = y >= 38 && y <= 42 && x >= 8 && x < SCREEN_W - 8;
            panel[y * SCREEN_W + x] = mark ? (x < 8 + width ? 0xff07 : 0x0821) : 0;
        }
    }
    if (!board_present(panel)) board_error(DISPLAY_ERROR);
}

static void error_screen(unsigned code)
{
    for (unsigned y = 0; y < SCREEN_H; ++y) {
        for (unsigned x = 0; x < SCREEN_W; ++x) {
            bool mark = y >= 34 && y < 46 && x >= 10 && x < 10 + 18 * code &&
                        (x - 10) % 18 < 12;
            panel[y * SCREEN_W + x] = mark ? 0x00f8 : 0;
        }
    }
    (void)board_present(panel);
    board_error(code);
}

int main(void)
{
    if (!board_init()) board_error(DISPLAY_ERROR);
    handheld_progress(0, 0);
    unsigned result = campaign_boot(pad_test_check_boot(panel));
    if (result != WAD_READY) error_screen(result);

    // The NOR is read-only in memory-mapped mode. There is no SD or SPI DMA.
    SCB_EnableDCache();
    memset(palette, 0, sizeof(palette));
    time_origin = HAL_GetTick();
    (void)controls_take_presses();
    I_PreInitGraphics();
    Z_Init();
    InitGlobals();
    D_DoomMain();
    return 0;
}

void I_InitScreen_e32(void) {}
void I_CreateBackBuffer_e32(void)
{
    memset(framebuffer, 0, sizeof(framebuffer));
    memset(frontbuffer, 0, sizeof(frontbuffer));
}
unsigned short *I_GetBackBuffer(void) { return framebuffer; }
unsigned short *I_GetFrontBuffer(void) { return frontbuffer; }
int I_GetVideoWidth_e32(void) { return SCREENWIDTH; }
int I_GetVideoHeight_e32(void) { return SCREENHEIGHT; }
int I_GetTime_e32(void)
{
    uint32_t elapsed = HAL_GetTick() - time_origin;
    return (int)((elapsed / 1000u) * TICRATE + (elapsed % 1000u) * TICRATE / 1000u);
}

void I_SetPallete_e32(const unsigned short *source)
{
    for (unsigned i = 0; i < 256; ++i) {
        unsigned rgb15 = source[i];
        unsigned r = rgb15 & 31u;
        unsigned g = (rgb15 >> 5) & 31u;
        unsigned b = (rgb15 >> 10) & 31u;
        palette[i] = (uint16_t)__REV16((r << 11) | (g << 6) | ((g >> 4) << 5) | b);
    }
}

void I_FinishUpdate_e32(const byte *source, const unsigned short *colors,
                        unsigned width, unsigned height)
{
    (void)colors;
    if (!source || width != SCREENWIDTH || height != SCREENHEIGHT) return;
    // Approximately 4:3, with centred nearest-neighbour sampling.
    const unsigned source_width = 2u * SCREENWIDTH;
    for (unsigned y = 0; y < SCREEN_H; ++y) {
        const byte *row = source + (2 * y + 1) * source_width;
        uint16_t *dest = panel + y * SCREEN_W;
        memset(dest, 0, SCREEN_W * sizeof(*dest));
        for (unsigned x = 0; x < VIEW_WIDTH; ++x) {
            unsigned sample = (x * source_width + source_width / 2u) / VIEW_WIDTH;
            dest[VIEW_LEFT + x] = palette[row[sample]];
        }
    }
    if (!board_present(panel)) board_error(DISPLAY_ERROR);
    if (source == (const byte *)framebuffer) memcpy(frontbuffer, framebuffer, sizeof(frontbuffer));
}

void I_ProcessKeyEvents(void)
{
    static uint16_t previous;
    uint16_t controls = controls_state() | controls_take_presses();
    uint16_t keys = 0;
    bool strafe = (controls & PAD_STRAFE) != 0;
    if (controls & PAD_UP) keys |= 1u << KEYD_UP;
    if (controls & PAD_DOWN) keys |= 1u << KEYD_DOWN;
    if (controls & PAD_LEFT) keys |= 1u << (strafe ? KEYD_L : KEYD_LEFT);
    if (controls & PAD_RIGHT) keys |= 1u << (strafe ? KEYD_R : KEYD_RIGHT);
    if (controls & PAD_FIRE) keys |= 1u << KEYD_B;
    if (controls & PAD_USE) keys |= 1u << KEYD_A;
    if (controls & PAD_WEAPON) keys |= 1u << (strafe ? KEYD_SELECT : KEYD_WEAPON);
    if (controls & (PAD_MENU | PAD_K1)) keys |= 1u << KEYD_START;

    for (unsigned key = KEYD_A; key <= KEYD_WEAPON; ++key) {
        uint16_t bit = 1u << key;
        if ((keys ^ previous) & bit) {
            event_t event = {.type = keys & bit ? ev_keydown : ev_keyup, .data1 = key};
            D_PostEvent(&event);
        }
    }
    previous = keys;
}

void I_Error(const char *format, ...)
{
    char message[sizeof(engine_error_message)];
    va_list args;
    va_start(args, format);
    (void)vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    size_t length = strlen(message) + 1;
    for (size_t i = 0; i < length; ++i) engine_error_message[i] = message[i];
    error_screen(ENGINE_ERROR);
}

void I_Quit_e32(void) { NVIC_SystemReset(); }

void I_InitSound(void) {}
int I_StartSound(int id, int channel, int vol, int sep)
{
    (void)id; (void)channel; (void)vol; (void)sep;
    return -1;
}
int I_SoundIsPlaying(int channel) { (void)channel; return 0; }
void I_SetMusicVolume(int volume) { (void)volume; }
void I_PauseSong(int handle) { (void)handle; }
void I_ResumeSong(int handle) { (void)handle; }
void I_PlaySong(int handle, int looping) { (void)handle; (void)looping; }
void I_StopSong(int handle) { (void)handle; }
