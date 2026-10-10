#include "mbia045.h"
#include "board.h"

void mbia045_init(void) { board_init(); }

void mbia045_le_command(uint8_t clocks)
{
    for (uint8_t i = 0U; i < clocks; ++i) {
        board_serial_bit(0U, 1U);
    }
    board_le_low();
}

void mbia045_data_latch(void) { mbia045_le_command(1U); }
void mbia045_global_latch(void) { mbia045_le_command(3U); }
void mbia045_read_config_command(void) { mbia045_le_command(5U); }
void mbia045_write_config_command(void) { mbia045_le_command(11U); }
void mbia045_enable_write_config_command(void) { mbia045_le_command(15U); }

void mbia045_send_raw_bits(const uint8_t *bits, uint32_t count)
{
    if (!bits) return;
    for (uint32_t i = 0U; i < count; ++i) {
        board_serial_bit((bits[i / 8U] >> (7U - i % 8U)) & 1U, 0U);
    }
}

static void shift_frame(const mbia045_frame_t frame)
{
    for (int ch = 15; ch >= 0; --ch) {
        for (int ic = (int)MBIA045_NUM_IC - 1; ic >= 0; --ic) {
            uint16_t value = frame[ic][ch];
            for (int bit = 15; bit >= 0; --bit) {
                uint8_t last = (uint8_t)(ic == 0 && bit == 0);
                board_serial_bit((uint8_t)((value >> bit) & 1U), last);
            }
        }
        board_le_low(); /* Exactly one LE-high DCLK edge for data latch. */
    }
}

void mbia045_shift_frame_fast(const mbia045_frame_t frame)
{
    /* Accelerated GPIO preserves the board-tested serial format and LE. */
    for (int ch = 15; ch >= 0; --ch) {
        for (int ic = MBIA045_NUM_IC - 1; ic >= 0; --ic)
            board_serial_word_fast(frame[ic][ch], ic == 0);
        board_le_low();
    }
}

void mbia045_send_frame(const mbia045_frame_t frame)
{
    mbia045_gclk_stop();
    shift_frame(frame);
    mbia045_global_latch();
}

uint32_t mbia045_send_frame_live(const mbia045_frame_t frame, uint32_t hz)
{
    uint32_t primask;
    shift_frame(frame);
    /* GPIO MODER updates must not race the CTRL scan interrupt. */
    __asm volatile ("mrs %0, primask\ncpsid i" : "=r" (primask) :: "memory");
    mbia045_gclk_stop();
    mbia045_global_latch();
    uint32_t actual = mbia045_gclk_start(hz);
    __asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
    return actual;
}

uint32_t mbia045_gclk_start(uint32_t hz) { return board_gclk_start(hz); }
void mbia045_gclk_stop(void) { board_gclk_stop(); }
