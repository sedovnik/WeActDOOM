#include "board.h"
#include "controls.h"
#include "stm32h7xx_hal.h"
#include "st7735.h"

static SPI_HandleTypeDef spi;
static ST7735_Object_t lcd;
static volatile bool controls_ready;

static bool clocks_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    RCC_PeriphCLKInitTypeDef periph = {0};

    if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK) return false;
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    uint32_t start = HAL_GetTick();
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {
        if (HAL_GetTick() - start >= 100) return false;
    }

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 5;
    osc.PLL.PLLN = 104;
    osc.PLL.PLLP = 1;
    osc.PLL.PLLQ = 4;
    osc.PLL.PLLR = 2;
    osc.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    osc.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) return false;

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1 |
                    RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.SYSCLKDivider = RCC_SYSCLK_DIV1;
    clk.AHBCLKDivider = RCC_HCLK_DIV2;
    clk.APB3CLKDivider = RCC_APB3_DIV2;
    clk.APB1CLKDivider = RCC_APB1_DIV2;
    clk.APB2CLKDivider = RCC_APB2_DIV2;
    clk.APB4CLKDivider = RCC_APB4_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_3) != HAL_OK) return false;

    // SPI4: 48 MHz kernel clock, 12 MHz on the display bus.
    periph.PeriphClockSelection = RCC_PERIPHCLK_SPI4;
    periph.Spi45ClockSelection = RCC_SPI45CLKSOURCE_PLL3;
    periph.PLL3.PLL3M = 25;
    periph.PLL3.PLL3N = 192;
    periph.PLL3.PLL3P = 2;
    periph.PLL3.PLL3Q = 4;
    periph.PLL3.PLL3R = 2;
    periph.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_0;
    periph.PLL3.PLL3VCOSEL = RCC_PLL3VCOMEDIUM;
    return HAL_RCCEx_PeriphCLKConfig(&periph) == HAL_OK;
}

static void gpio_init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3 | GPIO_PIN_10 | GPIO_PIN_11, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13, GPIO_PIN_RESET);

    GPIO_InitTypeDef pins = {
        .Pin = GPIO_PIN_3 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_13,
        .Mode = GPIO_MODE_OUTPUT_PP,
    };
    HAL_GPIO_Init(GPIOE, &pins);
    pins.Pin = GPIO_PIN_13;
    pins.Mode = GPIO_MODE_INPUT;
    pins.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOC, &pins);

    pins.Pin = GPIO_PIN_12 | GPIO_PIN_14;
    pins.Mode = GPIO_MODE_AF_PP;
    pins.Pull = GPIO_NOPULL;
    pins.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    pins.Alternate = GPIO_AF5_SPI4;
    HAL_GPIO_Init(GPIOE, &pins);
}

static int32_t lcd_io_init(void)
{
    return ST7735_OK;
}

static int32_t lcd_tick(void)
{
    return (int32_t)HAL_GetTick();
}

static int32_t lcd_write(uint8_t reg, uint8_t *data, uint32_t length)
{
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_11 | GPIO_PIN_13, GPIO_PIN_RESET);
    HAL_StatusTypeDef status = HAL_SPI_Transmit(&spi, &reg, 1, 50);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13, GPIO_PIN_SET);
    if (status == HAL_OK && length != 0) {
        status = HAL_SPI_Transmit(&spi, data, (uint16_t)length, 50);
    }
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_11, GPIO_PIN_SET);
    return status == HAL_OK ? ST7735_OK : ST7735_ERROR;
}

static int32_t lcd_send(uint8_t *data, uint32_t length)
{
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_11, GPIO_PIN_RESET);
    HAL_StatusTypeDef status = HAL_SPI_Transmit(&spi, data, (uint16_t)length, 50);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_11, GPIO_PIN_SET);
    return status == HAL_OK ? ST7735_OK : ST7735_ERROR;
}

bool board_init(void)
{
    HAL_Init();
    gpio_init();
    if (!clocks_init()) return false;
    SCB_EnableICache();
    controls_init();
    controls_ready = true;
    __HAL_RCC_SPI4_CLK_ENABLE();

    spi.Instance = SPI4;
    spi.Init.Mode = SPI_MODE_MASTER;
    spi.Init.Direction = SPI_DIRECTION_1LINE;
    spi.Init.DataSize = SPI_DATASIZE_8BIT;
    spi.Init.NSS = SPI_NSS_SOFT;
    spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
    spi.Init.CRCPolynomial = 7;
    spi.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
    if (HAL_SPI_Init(&spi) != HAL_OK) return false;

    ST7735_IO_t io = {
        .Init = lcd_io_init, .WriteReg = lcd_write,
        .SendData = lcd_send, .GetTick = lcd_tick,
    };
    ST7735_Ctx_t ctx = {
        .Orientation = ST7735_ORIENTATION_LANDSCAPE,
        .Panel = HannStar_Panel, .Type = ST7735_0_9_inch_screen,
    };
    HAL_Delay(10);
    return ST7735_RegisterBusIO(&lcd, &io) == ST7735_OK &&
           ST7735_Init(&lcd, ST7735_FORMAT_RBG565, &ctx) == ST7735_OK;
}

bool board_present(uint16_t *pixels)
{
    if (ST7735_WriteFrame(&lcd, (uint8_t *)pixels) != ST7735_OK) return false;
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_10, GPIO_PIN_RESET);
    return true;
}

bool board_button(void)
{
    return HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET;
}

void board_error(unsigned flashes)
{
    for (;;) {
        for (unsigned i = 0; i < flashes; ++i) {
            HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_RESET);
            HAL_Delay(140);
            HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_SET);
            HAL_Delay(140);
        }
        HAL_Delay(850);
    }
}

void SysTick_Handler(void)
{
    HAL_IncTick();
    if (controls_ready) controls_poll();
}
