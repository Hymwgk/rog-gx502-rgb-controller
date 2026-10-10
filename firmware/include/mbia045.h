#ifndef MBIA045_H
#define MBIA045_H

#include <stdint.h>

#ifndef MBIA045_NUM_IC
#define MBIA045_NUM_IC 3U
#endif
#define MBIA045_CH_PER_IC 16U
#define MBIA045_RAW_CHANNELS (MBIA045_NUM_IC * MBIA045_CH_PER_IC)

typedef uint16_t mbia045_frame_t[MBIA045_NUM_IC][MBIA045_CH_PER_IC];

/* The raw frame API assumes SDO-to-SDI cascading; physical order is unverified. */
void mbia045_init(void);
void mbia045_le_command(uint8_t clocks);
void mbia045_data_latch(void);
void mbia045_global_latch(void);
void mbia045_read_config_command(void);
void mbia045_write_config_command(void);
void mbia045_enable_write_config_command(void);
void mbia045_send_raw_bits(const uint8_t *bits, uint32_t count);
void mbia045_send_frame(const mbia045_frame_t frame);
/* Shift while displaying; pause GCLK only for the final global latch. */
uint32_t mbia045_send_frame_live(const mbia045_frame_t frame, uint32_t hz);
void mbia045_shift_frame_fast(const mbia045_frame_t frame);
uint32_t mbia045_gclk_start(uint32_t requested_hz);
void mbia045_gclk_stop(void);

#endif
