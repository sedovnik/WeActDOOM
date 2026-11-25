#include "nor.h"
#include "stm32h7xx_hal.h"

#define COMMAND_TIMEOUT 100u

static OSPI_HandleTypeDef bus;
static bool quad_read;

static OSPI_RegularCmdTypeDef command(uint8_t op, uint32_t address,
                                       uint32_t length, bool addressed)
{
    return (OSPI_RegularCmdTypeDef){
        .OperationType = HAL_OSPI_OPTYPE_COMMON_CFG,
        .FlashId = HAL_OSPI_FLASH_ID_1,
        .Instruction = op,
        .InstructionMode = HAL_OSPI_INSTRUCTION_1_LINE,
        .InstructionSize = HAL_OSPI_INSTRUCTION_8_BITS,
        .Address = address,
        .AddressMode = addressed ? HAL_OSPI_ADDRESS_1_LINE : HAL_OSPI_ADDRESS_NONE,
        .AddressSize = HAL_OSPI_ADDRESS_24_BITS,
        .DataMode = length ? HAL_OSPI_DATA_1_LINE : HAL_OSPI_DATA_NONE,
        .NbData = length,
        .SIOOMode = HAL_OSPI_SIOO_INST_EVERY_CMD,
    };
}

static bool send(OSPI_RegularCmdTypeDef cmd)
{
    return HAL_OSPI_Command(&bus, &cmd, COMMAND_TIMEOUT) == HAL_OK;
}

static bool receive_register(uint8_t addr, uint8_t *bytes, uint32_t length)
{
    return send(command(addr, 0, length, false)) &&
           HAL_OSPI_Receive(&bus, bytes, COMMAND_TIMEOUT) == HAL_OK;
}

static bool wait_ready(uint32_t timeout)
{
    uint32_t start = HAL_GetTick();
    uint8_t status;
    do {
        if (!receive_register(0x05, &status, 1)) return false;
        if (!(status & 1u)) return true;
    } while (HAL_GetTick() - start < timeout);
    return false;
}

static bool write_enable(void)
{
    uint8_t status;
    return send(command(0x06, 0, 0, false)) &&
           receive_register(0x05, &status, 1) && (status & 2u);
}

bool nor_init(void)
{
    RCC_PeriphCLKInitTypeDef clock = {
        .PeriphClockSelection = RCC_PERIPHCLK_OSPI,
        .OspiClockSelection = RCC_OSPICLKSOURCE_PLL,
    };
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) return false;
    __HAL_RCC_OCTOSPIM_CLK_ENABLE();
    __HAL_RCC_OSPI1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    GPIO_InitTypeDef pins = {
        .Pin = GPIO_PIN_2, .Mode = GPIO_MODE_AF_PP,
        .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
        .Alternate = GPIO_AF9_OCTOSPIM_P1,
    };
    HAL_GPIO_Init(GPIOE, &pins);
    HAL_GPIO_Init(GPIOB, &pins);
    pins.Pin = GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;
    HAL_GPIO_Init(GPIOD, &pins);
    pins.Pin = GPIO_PIN_6;
    pins.Alternate = GPIO_AF10_OCTOSPIM_P1;
    HAL_GPIO_Init(GPIOB, &pins);

    bus.Instance = OCTOSPI1;
    bus.Init = (OSPI_InitTypeDef){
        .FifoThreshold = 4,
        .MemoryType = HAL_OSPI_MEMTYPE_MICRON,
        .DeviceSize = 23,
        .ChipSelectHighTime = 2,
        .ClockMode = HAL_OSPI_CLOCK_MODE_3,
        .ClockPrescaler = 4,
        .SampleShifting = HAL_OSPI_SAMPLE_SHIFTING_HALFCYCLE,
        .DelayBlockBypass = HAL_OSPI_DELAY_BLOCK_BYPASSED,
    };
    if (HAL_OSPI_Init(&bus) != HAL_OK) return false;
    OSPIM_CfgTypeDef io = {
        .ClkPort = 1, .NCSPort = 1, .IOLowPort = HAL_OSPIM_IOPORT_1_LOW,
    };
    if (HAL_OSPIM_Config(&bus, &io, COMMAND_TIMEOUT) != HAL_OK) return false;

    // Allow a page/sector operation from an interrupted boot to finish.
    if (!wait_ready(600)) return false;
    uint8_t id[3];
    if (!receive_register(0x9f, id, sizeof(id)) ||
        (id[0] != 0xef && id[0] != 0x85) || id[2] != 0x17) return false;
    uint8_t status;
    quad_read = receive_register(0x35, &status, 1) && (status & 2u);
    return true;
}

static OSPI_RegularCmdTypeDef read_command(uint32_t address, uint32_t length)
{
    OSPI_RegularCmdTypeDef cmd = command(quad_read ? 0x6b : 0x0b, address, length, true);
    if (quad_read) cmd.DataMode = HAL_OSPI_DATA_4_LINES;
    cmd.DummyCycles = 8;
    return cmd;
}

bool nor_read(uint32_t address, void *data, uint32_t length)
{
    return length && address < NOR_BYTES && length <= NOR_BYTES - address &&
           send(read_command(address, length)) &&
           HAL_OSPI_Receive(&bus, data, COMMAND_TIMEOUT) == HAL_OK;
}

bool nor_program(uint32_t address, const void *data, uint32_t length)
{
    return length && address < NOR_BYTES &&
           length <= NOR_PAGE - address % NOR_PAGE &&
           write_enable() && send(command(0x02, address, length, true)) &&
           HAL_OSPI_Transmit(&bus, (uint8_t *)data, COMMAND_TIMEOUT) == HAL_OK &&
           wait_ready(20);
}

bool nor_erase(uint32_t address)
{
    return address < NOR_BYTES && address % NOR_SECTOR == 0 &&
           write_enable() && send(command(0x20, address, 0, true)) && wait_ready(600);
}

bool nor_erase_block(uint32_t address)
{
    return address < NOR_BYTES && address % NOR_BLOCK == 0 &&
           write_enable() && send(command(0xd8, address, 0, true)) && wait_ready(3000);
}

bool nor_map(void)
{
    OSPI_RegularCmdTypeDef read = read_command(0, 1);
    read.OperationType = HAL_OSPI_OPTYPE_READ_CFG;
    OSPI_RegularCmdTypeDef write = command(0x02, 0, 1, true);
    write.OperationType = HAL_OSPI_OPTYPE_WRITE_CFG;
    OSPI_MemoryMappedTypeDef mapped = {.TimeOutActivation = HAL_OSPI_TIMEOUT_COUNTER_DISABLE};
    if (!send(read) || !send(write) || HAL_OSPI_MemoryMapped(&bus, &mapped) != HAL_OK) return false;

    MPU_Region_InitTypeDef region = {
        .Enable = MPU_REGION_ENABLE,
        .Number = MPU_REGION_NUMBER0,
        .BaseAddress = NOR_BASE,
        .Size = MPU_REGION_SIZE_8MB,
        .AccessPermission = MPU_REGION_PRIV_RO_URO,
        .DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE,
        .IsCacheable = MPU_ACCESS_CACHEABLE,
        .IsBufferable = MPU_ACCESS_NOT_BUFFERABLE,
        .IsShareable = MPU_ACCESS_NOT_SHAREABLE,
        .TypeExtField = MPU_TEX_LEVEL0,
    };
    HAL_MPU_Disable();
    HAL_MPU_ConfigRegion(&region);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
    return true;
}
