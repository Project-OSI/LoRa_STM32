/*
 * dendrometer.h — ratiometric dendrometer measurement module for LSN50V2.
 *
 * Pure C99. No HAL dependencies. Host-testable.
 *
 * See docs/superpowers/specs/2026-04-18-lsn50v2-dendrometer-claude-design.md
 */
#ifndef __DENDROMETER_H__
#define __DENDROMETER_H__

#include <stdint.h>
#include <stdbool.h>

/* ---- Compile-time tunables ------------------------------------------ */
#ifndef DENDRO_SAMPLE_COUNT
#define DENDRO_SAMPLE_COUNT         50u
#endif
#ifndef DENDRO_SETTLE_MS
#define DENDRO_SETTLE_MS            50u
#endif
#ifndef DENDRO_INTER_SAMPLE_MS
#define DENDRO_INTER_SAMPLE_MS      1u
#endif
#ifndef DENDRO_REF_MIN_RAW
#define DENDRO_REF_MIN_RAW          128u    /* below = reference rail failed  */
#endif
#ifndef DENDRO_REF_MAX_RAW
#define DENDRO_REF_MAX_RAW          4080u   /* above = reference rail saturated */
#endif

/* ---- Flag bits (single payload byte) -------------------------------- */
#define DENDRO_FLAG_VALID           0x01u
#define DENDRO_FLAG_REF_LOW         0x02u
#define DENDRO_FLAG_REF_HIGH        0x04u
#define DENDRO_FLAG_ADC_FAIL        0x08u
/* bits 4..7 reserved */

/* ---- Result type ---------------------------------------------------- */
typedef struct {
    uint16_t adc_signal_avg_raw;
    uint16_t adc_reference_avg_raw;
    uint8_t  flags;
} dendrometer_result_t;

/* ---- Board primitives (implemented in bsp.c for ARM; mock_board.c for tests) */
void     dendro_board_5v_on(void);
void     dendro_board_5v_off(void);
uint16_t dendro_board_adc_read_signal(void);
uint16_t dendro_board_adc_read_reference(void);
void     dendro_board_delay_ms(uint32_t ms);

/* ---- Public API (implemented in dendrometer.c) ---------------------- */
void     dendrometer_measure(dendrometer_result_t *out);

/*
 * Packs the MOD=3 dendrometer frame (8 bytes, big-endian) into dst.
 * Layout:
 *   [0-1] battery_mv
 *   [2-3] adc_signal_avg_raw
 *   [4-5] adc_reference_avg_raw
 *   [6]   status_byte  (caller-provided; see bsp.c)
 *   [7]   flags
 * Returns the number of bytes written (always 8).
 * dst MUST have at least 8 bytes of space.
 */
uint8_t  dendrometer_pack_payload(const dendrometer_result_t *m,
                                   uint16_t battery_mv,
                                   uint8_t  status_byte,
                                   uint8_t *dst);

#endif /* __DENDROMETER_H__ */
