#include "chameleon_payload.h"

static void put_u16_be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFFU);
    p[1] = (uint8_t)(v & 0xFFU);
}

static void put_u32_be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFFU);
    p[1] = (uint8_t)((v >> 16) & 0xFFU);
    p[2] = (uint8_t)((v >> 8) & 0xFFU);
    p[3] = (uint8_t)(v & 0xFFU);
}

size_t chameleon_payload_encode_v1(uint8_t *buf, size_t buf_len,
                                   const chameleon_sample_t *sample) {
    if (buf == 0 || sample == 0) { return 0; }
    if (buf_len < CHAMELEON_PAYLOAD_LEN_V1) { return 0; }

    put_u16_be(&buf[0], sample->adc_pa0_mv);
    put_u16_be(&buf[2], sample->adc_pa1_mv);
    put_u16_be(&buf[4], sample->adc_pa4_mv);
    buf[6] = sample->mod3_status;
    buf[7] = (uint8_t)(sample->battery_mv / 100U);
    buf[8] = CHAMELEON_PAYLOAD_VERSION_V1;
    buf[9] = (uint8_t)(sample->status_flags & CHAMELEON_FLAGS_V1_MASK);
    put_u16_be(&buf[10], (uint16_t)sample->soil_temp_c_x100);
    put_u32_be(&buf[12], sample->r1_ohm_comp);
    put_u32_be(&buf[16], sample->r2_ohm_comp);
    put_u32_be(&buf[20], sample->r3_ohm_comp);
    put_u32_be(&buf[24], sample->r1_ohm_raw);
    put_u32_be(&buf[28], sample->r2_ohm_raw);
    put_u32_be(&buf[32], sample->r3_ohm_raw);
    for (size_t i = 0; i < 8; i++) { buf[36 + i] = sample->array_id[i]; }
    return CHAMELEON_PAYLOAD_LEN_V1;
}
