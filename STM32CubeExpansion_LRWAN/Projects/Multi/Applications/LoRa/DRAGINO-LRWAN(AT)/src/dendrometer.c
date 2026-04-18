/*
 * dendrometer.c — ratiometric dendrometer measurement module for LSN50V2.
 * See dendrometer.h and the design spec in osi-os for rationale.
 */
#include "dendrometer.h"

/* Sampling cadence mirrors stock Dragino firmware (bsp.c MOD=3/8 loop):
 *   - 10 ms between samples = half a 50 Hz mains period. Consecutive samples
 *     land on opposite phases of any 50 Hz pickup and cancel in pairs.
 *   - Sample count MUST stay even so every sample has a pair partner; odd
 *     counts leak residual mains hum into the average.
 *   - 20 samples × 10 ms = 200 ms = 10 full 50 Hz cycles → perfect rejection
 *     of 50 Hz mains and +5.4 dB random-noise averaging vs stock's 6 samples.
 */
#define DENDRO_SAMPLE_COUNT        20U   /* must be even (see comment above) */
#define DENDRO_SAMPLE_SPACING_MS   10U   /* half of 50 Hz mains period */
#define DENDRO_SETTLE_MS           50U

/* Board primitives (implemented in bsp.c for ARM; tests/mock_board.c for host). */
extern void     dendro_board_5v_on(void);
extern void     dendro_board_5v_off(void);
extern uint16_t dendro_board_adc_read_signal(void);     /* PA0 */
extern uint16_t dendro_board_adc_read_reference(void);  /* PA1 */
extern void     dendro_board_delay_ms(uint32_t ms);

void dendrometer_measure(dendrometer_result_t *result) {
    if (result == 0) { return; }
    result->signal_raw    = 0;
    result->reference_raw = 0;

    dendro_board_5v_on();
    dendro_board_delay_ms(DENDRO_SETTLE_MS);

    uint32_t sig_sum = 0;
    uint32_t ref_sum = 0;
    for (uint32_t i = 0; i < DENDRO_SAMPLE_COUNT; i++) {
        sig_sum += dendro_board_adc_read_signal();
        ref_sum += dendro_board_adc_read_reference();
        dendro_board_delay_ms(DENDRO_SAMPLE_SPACING_MS);
    }

    dendro_board_5v_off();

    result->signal_raw    = (uint16_t)(sig_sum / DENDRO_SAMPLE_COUNT);
    result->reference_raw = (uint16_t)(ref_sum / DENDRO_SAMPLE_COUNT);
}
