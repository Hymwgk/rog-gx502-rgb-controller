#include "board.h"

/* STM32L051C8 data EEPROM: 0x08080000..0x080807ff (RM0377).
 * Only PELOCK is unlocked; program Flash and option bytes stay locked. */
#define REG32(a) (*(volatile uint32_t *)(a))
#define FLASH_PECR REG32(0x40022004U)
#define FLASH_PEKEYR REG32(0x4002200CU)
#define FLASH_SR REG32(0x40022018U)
#define FLASH_ERRORS 0x00032F00U
#define EEPROM ((volatile uint32_t *)0x08080000U)

static int wait_ready(void)
{
    uint32_t timeout = 1000000U;
    while ((FLASH_SR & 1U) && --timeout != 0U) { }
    return timeout == 0U ? -1 : 0;
}

uint32_t board_settings_read(uint8_t word)
{
    return word < BOARD_SETTINGS_WORDS ? EEPROM[word] : 0U;
}

int board_settings_write(uint8_t word, uint32_t value)
{
    if (word >= BOARD_SETTINGS_WORDS || wait_ready() != 0) return -1;
    if (EEPROM[word] == value) return 0;
    if (FLASH_PECR & 1U) {
        FLASH_PEKEYR = 0x89ABCDEFU;
        FLASH_PEKEYR = 0x02030405U;
    }
    if (FLASH_PECR & 1U) return -1;
    /* Automatic erase-before-write for nonzero words; no Flash erase command. */
    FLASH_PECR &= ~((1U << 3) | (1U << 8) | (1U << 9) | (1U << 10));
    FLASH_SR = FLASH_ERRORS | 2U;
    EEPROM[word] = value;
    int result = wait_ready();
    if ((FLASH_SR & FLASH_ERRORS) || EEPROM[word] != value) result = -1;
    FLASH_SR = FLASH_ERRORS | 2U;
    FLASH_PECR |= 1U;
    return result;
}
