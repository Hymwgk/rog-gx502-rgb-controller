#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

/* Board-only pin and peripheral access. CTRL levels are raw electrical levels. */
void board_init(void);
/* Bits 0..3 correspond to PB12..15; 1 means pressed (pin low). */
uint8_t board_buttons_pressed(void);
void board_use_fast_msi(void);
int board_use_wave_clock(void);
void board_serial_word_fast(uint16_t data, uint8_t latch_last);
void board_serial_bit(uint8_t data, uint8_t le);
void board_le_low(void);
void board_gclk_stop(void);
uint32_t board_gclk_start(uint32_t requested_hz);
void board_ctrl_release_all(void);
int board_ctrl_drive(uint8_t index, uint8_t level);

/* First 128 bytes of the dedicated data EEPROM are reserved for settings.
 * Writes block: call only with all LED banks disabled and GCLK stopped. */
#define BOARD_SETTINGS_WORDS 32U
uint32_t board_settings_read(uint8_t word);
int board_settings_write(uint8_t word, uint32_t value);

#endif
