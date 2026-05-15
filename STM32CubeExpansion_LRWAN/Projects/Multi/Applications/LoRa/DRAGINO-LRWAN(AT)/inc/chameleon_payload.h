#ifndef CHAMELEON_PAYLOAD_H
#define CHAMELEON_PAYLOAD_H

#include <stdint.h>
#include <stddef.h>

#define CHAMELEON_PAYLOAD_VERSION_V1   0x01
#define CHAMELEON_PAYLOAD_LEN_V1       44U

#define CHAMELEON_FLAG_I2C_MISSING     (1U << 0)
#define CHAMELEON_FLAG_TIMEOUT         (1U << 1)
#define CHAMELEON_FLAG_TEMP_FAULT      (1U << 2)
#define CHAMELEON_FLAG_ID_FAULT        (1U << 3)
#define CHAMELEON_FLAG_CH1_OPEN        (1U << 4)
#define CHAMELEON_FLAG_CH2_OPEN        (1U << 5)
#define CHAMELEON_FLAG_CH3_OPEN        (1U << 6)
#define CHAMELEON_FLAG_COMP_PENDING    (1U << 7)  /* CAL[i] == RAW[i] for at least one non-open channel after retry */

typedef struct {
    uint16_t adc_pa0_mv;
    uint16_t adc_pa1_mv;
    uint16_t adc_pa4_mv;
    uint8_t  mod3_status;
    uint16_t battery_mv;
    uint8_t  status_flags;
    int16_t  soil_temp_c_x100;
    uint32_t r1_ohm_comp;
    uint32_t r2_ohm_comp;
    uint32_t r3_ohm_comp;
    uint32_t r1_ohm_raw;
    uint32_t r2_ohm_raw;
    uint32_t r3_ohm_raw;
    uint8_t  array_id[8];
} chameleon_sample_t;

/* Encode a sample into a 44-byte stock-MOD=3-aligned big-endian frame.
 * Returns the number of bytes written (always 44 for v1) on success, 0 if buf
 * is NULL, sample is NULL, or buf_len < 44. */
size_t chameleon_payload_encode_v1(uint8_t *buf, size_t buf_len,
                                   const chameleon_sample_t *sample);

#endif /* CHAMELEON_PAYLOAD_H */
