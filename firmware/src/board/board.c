#include "board.h"

/* STM32L051C8, reset MSI clock ~= 2.097152 MHz; no system clock change. */
#define BOARD_TIMER_HZ 2097152U
static uint32_t timer_hz = BOARD_TIMER_HZ;
#define REG32(a) (*(volatile uint32_t *)(a))
#define RCC_IOPENR REG32(0x4002102CU)
#define RCC_APB2ENR REG32(0x40021034U)
#define RCC_ICSCR REG32(0x40021004U)
#define RCC_CR REG32(0x40021000U)
#define RCC_CFGR REG32(0x4002100CU)
#define RCC_APB1ENR REG32(0x40021038U)
#define PWR_CR REG32(0x40007000U)
#define PWR_CSR REG32(0x40007004U)
#define FLASH_ACR REG32(0x40022000U)
#define GPIOA_MODER REG32(0x50000000U)
#define GPIOA_OSPEEDR REG32(0x50000008U)
#define GPIOA_BSRR REG32(0x50000018U)
#define GPIOA_AFRL REG32(0x50000020U)
#define GPIOB_MODER REG32(0x50000400U)
#define GPIOB_PUPDR REG32(0x5000040CU)
#define GPIOB_IDR REG32(0x50000410U)
#define GPIOB_BSRR REG32(0x50000418U)
#define TIM22_CR1 REG32(0x40011400U)
#define TIM22_EGR REG32(0x40011414U)
#define TIM22_CCMR1 REG32(0x40011418U)
#define TIM22_CCER REG32(0x40011420U)
#define TIM22_PSC REG32(0x40011428U)
#define TIM22_ARR REG32(0x4001142CU)
#define TIM22_CCR1 REG32(0x40011434U)

#define LE (1U << 4)
#define DCLK (1U << 5)
#define GCLK (1U << 6)
#define SDI (1U << 7)

/* CTRL0..7: PB0, PB1, PB8, PB9, PB10, PB11, PA9, PA10. */
static const uint8_t ctrl_pin[8] = {0, 1, 8, 9, 10, 11, 9, 10};
static const uint8_t ctrl_port_a[8] = {0, 0, 0, 0, 0, 0, 1, 1};

static void gpio_mode(volatile uint32_t *moder, uint8_t pin, uint8_t mode)
{
    uint32_t shift = 2U * pin;
    *moder = (*moder & ~(3U << shift)) | ((uint32_t)mode << shift);
}

void board_ctrl_release_all(void)
{
    for (uint8_t i = 0; i < 8U; ++i) {
        gpio_mode(ctrl_port_a[i] ? &GPIOA_MODER : &GPIOB_MODER,
                  ctrl_pin[i], 0U); /* High impedance until polarity is known. */
    }
}

int board_ctrl_drive(uint8_t index, uint8_t level)
{
    if (index >= 8U || level > 1U) return -1;
    uint32_t bit = 1U << ctrl_pin[index];
    if (ctrl_port_a[index]) {
        GPIOA_BSRR = level ? bit : bit << 16;
        gpio_mode(&GPIOA_MODER, ctrl_pin[index], 1U);
    } else {
        GPIOB_BSRR = level ? bit : bit << 16;
        gpio_mode(&GPIOB_MODER, ctrl_pin[index], 1U);
    }
    return 0;
}

void board_le_low(void) { GPIOA_BSRR = LE << 16; }

void board_serial_bit(uint8_t data, uint8_t le)
{
    GPIOA_BSRR = DCLK << 16;
    GPIOA_BSRR = data ? SDI : SDI << 16;
    GPIOA_BSRR = le ? LE : LE << 16;
    GPIOA_BSRR = DCLK;
    GPIOA_BSRR = DCLK << 16;
}

void board_gclk_stop(void)
{
    TIM22_CCER &= ~1U;
    TIM22_CR1 &= ~1U;
    GPIOA_BSRR = GCLK << 16;
    gpio_mode(&GPIOA_MODER, 6U, 1U);
}

uint32_t board_gclk_start(uint32_t requested_hz)
{
    uint32_t period;
    if (timer_hz == 32000000U) {
        if (requested_hz == 8000000U) period = 4U;
        else if (requested_hz == 4000000U) period = 8U;
        else if (requested_hz == 2000000U) period = 16U;
        else if (requested_hz == 1000000U) period = 32U;
        else return 0U;
    } else if (requested_hz == 100000U) period = timer_hz == BOARD_TIMER_HZ ? 21U : 42U;
    else if (requested_hz == 500000U) period = timer_hz == BOARD_TIMER_HZ ? 4U : 8U;
    else if (requested_hz == 1000000U) period = timer_hz == BOARD_TIMER_HZ ? 2U : 4U;
    else if (requested_hz == 2000000U && timer_hz != BOARD_TIMER_HZ) period = 2U;
    else return 0U;
    board_gclk_stop();
    TIM22_PSC = 0U;
    TIM22_ARR = period - 1U;
    TIM22_CCR1 = period / 2U;
    TIM22_CCMR1 = (TIM22_CCMR1 & ~0x7CU) | (6U << 4) | (1U << 3);
    TIM22_EGR = 1U;
    GPIOA_AFRL = (GPIOA_AFRL & ~(15U << 24)) | (5U << 24);
    gpio_mode(&GPIOA_MODER, 6U, 2U); /* PA6 AF5 = TIM22_CH1. */
    TIM22_CCER |= 1U;
    TIM22_CR1 |= 1U | (1U << 7);
    return timer_hz / period;
}

void board_use_fast_msi(void)
{
    /* MSI range 6 = 4.194 MHz; no PLL, voltage or flash-latency change. */
    RCC_ICSCR = (RCC_ICSCR & ~(7U << 13)) | (6U << 13);
    timer_hz = 4194304U;
}

int board_use_wave_clock(void)
{
    /* Dedicated per-key path: voltage range 1, HSI16 x4 /2 = 32 MHz. */
    RCC_APB1ENR |= 1U << 28;
    (void)RCC_APB1ENR;
    PWR_CR = (PWR_CR & ~(3U << 11)) | (1U << 11);
    uint32_t voltage_timeout = 100000U;
    while ((PWR_CSR & (1U << 4)) && --voltage_timeout != 0U) { }
    if (voltage_timeout == 0U) return -1;
    FLASH_ACR |= 1U; /* Conservative one wait state before raising HCLK. */
    RCC_CR |= 1U;
    uint32_t timeout = 100000U;
    while ((RCC_CR & (1U << 2)) == 0U && --timeout != 0U) { }
    if (timeout == 0U) return -1;
    RCC_CFGR = (RCC_CFGR & ~3U) | 1U;
    timeout = 100000U;
    while ((RCC_CFGR & 12U) != 4U && --timeout != 0U) { }
    if (timeout == 0U) return -1;
    RCC_CR &= ~(1U << 24);
    timeout = 100000U;
    while ((RCC_CR & (1U << 25)) && --timeout != 0U) { }
    if (timeout == 0U) return -1;
    RCC_CFGR = (RCC_CFGR & ~((1U << 16) | (15U << 18) | (3U << 22))) |
               (1U << 18) | (1U << 22);
    RCC_CR |= 1U << 24;
    timeout = 100000U;
    while ((RCC_CR & (1U << 25)) == 0U && --timeout != 0U) { }
    if (timeout == 0U) return -1;
    RCC_CFGR = (RCC_CFGR & ~3U) | 3U;
    timeout = 100000U;
    while ((RCC_CFGR & 12U) != 12U && --timeout != 0U) { }
    if (timeout == 0U) return -1;
    timer_hz = 32000000U;
    /* Fast LE edges are essential: otherwise the last-bit latch is missed. */
    GPIOA_OSPEEDR |= (3U << 8) | (3U << 10) | (3U << 12) | (3U << 14);
    return 0;
}

void board_serial_word_fast(uint16_t data, uint8_t latch_last)
{
    uint32_t value = (uint32_t)data << 16;
    uint32_t shift, bits;
    /* Payload only: 15 exact clocks, then the LE-aware final clock below. */
    __asm volatile (
        ".syntax unified\n"
        ".rept 15\n"
        "lsrs %[shift], %[value], #31\n"
        "lsls %[shift], %[shift], #4\n"
        "movs %[bits], %[reset]\n"
        "lsrs %[bits], %[bits], %[shift]\n"
        "str %[bits], [%[port]]\n"
        "str %[high], [%[port]]\n"
        "str %[low], [%[port]]\n"
        "lsls %[value], %[value], #1\n"
        ".endr\n"
        ".syntax divided\n"
        : [value] "+l" (value),
          [shift] "=&l" (shift), [bits] "=&l" (bits)
        : [port] "l" (&GPIOA_BSRR), [high] "l" (DCLK),
          [low] "l" (DCLK << 16), [reset] "l" (SDI << 16)
        : "cc", "memory");
    board_serial_bit(value >> 31, latch_last);
}

uint8_t board_buttons_pressed(void)
{
    /* PB12..15: external switches to GND, internal pull-ups. */
    return (uint8_t)((~GPIOB_IDR >> 12) & 15U);
}

void board_init(void)
{
    RCC_IOPENR |= 3U;
    (void)RCC_IOPENR; /* Complete the GPIO clock enable before accessing ports. */
    for (uint8_t pin = 12U; pin <= 15U; ++pin)
        gpio_mode(&GPIOB_MODER, pin, 0U);
    GPIOB_PUPDR = (GPIOB_PUPDR & ~0xFF000000U) | 0x55000000U;
    RCC_APB2ENR |= 1U << 5; /* TIM22 */
    GPIOA_BSRR = (LE | DCLK | GCLK | SDI) << 16;
    for (uint8_t pin = 4U; pin <= 7U; ++pin) gpio_mode(&GPIOA_MODER, pin, 1U);
    board_gclk_stop();
    board_ctrl_release_all();
    /* KB_LED_TYPE is on PA? as yet unmapped on this board: leave untouched. */
}
