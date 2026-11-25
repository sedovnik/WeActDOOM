#include <stdbool.h>
#include "stm32h7xx_hal.h"
#include "ff.h"
#include "diskio.h"

static SD_HandleTypeDef card;
static DSTATUS status = STA_NOINIT;

static bool sd_pins_and_clock(void)
{
    RCC_PeriphCLKInitTypeDef clk = {0};
    // HSE / 5 * 80 / 8 = 50 MHz kernel clock.
    clk.PeriphClockSelection = RCC_PERIPHCLK_SDMMC;
    clk.SdmmcClockSelection = RCC_SDMMCCLKSOURCE_PLL2;
    clk.PLL2.PLL2M = 5;
    clk.PLL2.PLL2N = 80;
    clk.PLL2.PLL2P = 4;
    clk.PLL2.PLL2Q = 4;
    clk.PLL2.PLL2R = 8;
    clk.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_2;
    clk.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
    if (HAL_RCCEx_PeriphCLKConfig(&clk) != HAL_OK) return false;

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_SDMMC1_CLK_ENABLE();
    GPIO_InitTypeDef pins = {
        .Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11,
        .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_PULLUP,
        .Speed = GPIO_SPEED_FREQ_VERY_HIGH, .Alternate = GPIO_AF12_SDMMC1,
    };
    HAL_GPIO_Init(GPIOC, &pins);
    pins.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOD, &pins);
    pins.Pin = GPIO_PIN_12;
    pins.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &pins);
    return true;
}

DSTATUS disk_initialize(BYTE drive)
{
    if (drive != 0) return STA_NOINIT;
    if (status == 0 || !sd_pins_and_clock()) return status;

    card.Instance = SDMMC1;
    card.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    card.Init.BusWide = SDMMC_BUS_WIDE_1B;
    card.Init.ClockDiv = 1;
    if (HAL_SD_Init(&card) == HAL_OK) {
        if (HAL_SD_ConfigWideBusOperation(&card, SDMMC_BUS_WIDE_4B) == HAL_OK) {
            status = 0;
        } else {
            (void)HAL_SD_DeInit(&card);
            card.Init.BusWide = SDMMC_BUS_WIDE_1B;
            if (HAL_SD_Init(&card) == HAL_OK) status = 0;
        }
    }
    return status;
}

DSTATUS disk_status(BYTE drive)
{
    return drive == 0 ? status : STA_NOINIT;
}

static DRESULT wait_transfer(void)
{
    uint32_t started = HAL_GetTick();
    while (HAL_SD_GetCardState(&card) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() - started >= 250) return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count)
{
    if (drive != 0 || buffer == NULL || count == 0) return RES_PARERR;
    if (status != 0) return RES_NOTRDY;
    if (HAL_SD_ReadBlocks(&card, buffer, sector, count, 500) != HAL_OK) return RES_ERROR;
    return wait_transfer();
}

DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count)
{
    if (drive != 0 || buffer == NULL || count == 0) return RES_PARERR;
    if (status != 0) return RES_NOTRDY;
    if (HAL_SD_WriteBlocks(&card, (BYTE *)buffer, sector, count, 1000) != HAL_OK) return RES_ERROR;
    return wait_transfer();
}

DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer)
{
    if (drive != 0) return RES_PARERR;
    if (status != 0) return RES_NOTRDY;
    if (command == CTRL_SYNC) return wait_transfer();
    if (buffer == NULL) return RES_PARERR;
    HAL_SD_CardInfoTypeDef info;
    if (HAL_SD_GetCardInfo(&card, &info) != HAL_OK) return RES_ERROR;
    switch (command) {
    case GET_SECTOR_COUNT: *(LBA_t *)buffer = info.LogBlockNbr; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buffer = (WORD)info.LogBlockSize; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buffer = 1; return RES_OK;
    default: return RES_PARERR;
    }
}

