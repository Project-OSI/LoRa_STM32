/*
 * dendrometer.c — ratiometric dendrometer measurement module for LSN50V2.
 * See dendrometer.h and the design spec in osi-os for rationale.
 */
#include "dendrometer.h"

void dendrometer_measure(dendrometer_result_t *out) {
    out->adc_signal_avg_raw    = 0;
    out->adc_reference_avg_raw = 0;
    out->flags                 = 0;

    dendro_board_5v_on();
    dendro_board_delay_ms(DENDRO_SETTLE_MS);

    uint32_t sum1 = 0, sum2 = 0;
    uint16_t zeros1 = 0, zeros2 = 0;
    for (uint16_t i = 0; i < DENDRO_SAMPLE_COUNT; ++i) {
        uint16_t s1 = dendro_board_adc_read_signal();
        uint16_t s2 = dendro_board_adc_read_reference();
        sum1 += s1; if (s1 == 0) ++zeros1;
        sum2 += s2; if (s2 == 0) ++zeros2;
        dendro_board_delay_ms(DENDRO_INTER_SAMPLE_MS);
    }

    out->adc_signal_avg_raw    = (uint16_t)(sum1 / DENDRO_SAMPLE_COUNT);
    out->adc_reference_avg_raw = (uint16_t)(sum2 / DENDRO_SAMPLE_COUNT);

    dendro_board_5v_off();

    if (zeros1 == DENDRO_SAMPLE_COUNT || zeros2 == DENDRO_SAMPLE_COUNT) {
        out->flags |= DENDRO_FLAG_ADC_FAIL;
    } else if (out->adc_reference_avg_raw < DENDRO_REF_MIN_RAW) {
        out->flags |= DENDRO_FLAG_REF_LOW;
    } else if (out->adc_reference_avg_raw > DENDRO_REF_MAX_RAW) {
        out->flags |= DENDRO_FLAG_REF_HIGH;
    } else {
        out->flags |= DENDRO_FLAG_VALID;
    }
}

uint8_t dendrometer_pack_payload(const dendrometer_result_t *m,
                                  uint16_t battery_mv,
                                  uint8_t  status_byte,
                                  uint8_t *dst) {
    dst[0] = (uint8_t)(battery_mv >> 8);
    dst[1] = (uint8_t)(battery_mv & 0xFF);
    dst[2] = (uint8_t)(m->adc_signal_avg_raw >> 8);
    dst[3] = (uint8_t)(m->adc_signal_avg_raw & 0xFF);
    dst[4] = (uint8_t)(m->adc_reference_avg_raw >> 8);
    dst[5] = (uint8_t)(m->adc_reference_avg_raw & 0xFF);
    dst[6] = status_byte;
    dst[7] = m->flags;
    return 8;
}
