#include "controls.h"
#include "stm32h7xx_hal.h"
#include <stdbool.h>

static uint8_t debounce[PAD_INPUT_COUNT];
static volatile uint16_t stable;
static volatile uint16_t presses;

void controls_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef pins = {
        .Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
               GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8,
        .Mode = GPIO_MODE_INPUT,
        .Pull = GPIO_PULLUP,
    };
    HAL_GPIO_Init(GPIOA, &pins);
}

uint16_t controls_raw_state(void)
{
    unsigned raw = (~GPIOA->IDR) & 0x1ff;
    if (GPIOC->IDR & GPIO_PIN_13) raw |= PAD_K1;
    return (uint16_t)raw;
}

void controls_poll(void)
{
    uint16_t raw = controls_raw_state();
    for (unsigned index = 0; index < sizeof(debounce); ++index) {
        unsigned bit = 1u << index;
        bool down = (raw & bit) != 0;
        if (down == ((stable & bit) != 0)) {
            debounce[index] = 0;
        } else if (++debounce[index] >= 8) {
            stable ^= bit;
            if (down) presses |= bit;
            debounce[index] = 0;
        }
    }
}

uint16_t controls_state(void)
{
    return stable;
}

uint16_t controls_take_presses(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    uint16_t result = presses;
    presses = 0;
    __set_PRIMASK(mask);
    return result;
}
