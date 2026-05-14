#include "chameleon_payload.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_EQ_U32(actual, expected, label) do {                             \
    if ((uint32_t)(actual) != (uint32_t)(expected)) {                           \
        fprintf(stderr, "FAIL %s: expected %u got %u (%s:%d)\n",                \
                (label), (unsigned)(expected), (unsigned)(actual),              \
                __FILE__, __LINE__);                                            \
        exit(1);                                                                \
    }                                                                           \
} while (0)

static void test_encode_known_sample(void) {
    chameleon_sample_t s = {
        .adc_pa0_mv       = 1010,
        .adc_pa1_mv       = 2020,
        .adc_pa4_mv       = 3030,
        .mod3_status      = 0x08,
        .battery_mv       = 3300,
        .status_flags     = 0,
        .soil_temp_c_x100 = 1987,
        .r1_ohm_comp      = 1100,
        .r2_ohm_comp      = 10100,
        .r3_ohm_comp      = 101200,
        .r1_ohm_raw       = 1200,
        .r2_ohm_raw       = 10200,
        .r3_ohm_raw       = 102200,
        .array_id         = {0x28, 0x6D, 0x6A, 0xDB, 0x0F, 0x00, 0x00, 0xF1},
    };
    uint8_t buf[46];
    memset(buf, 0xAA, sizeof(buf));
    size_t n = chameleon_payload_encode_v1(buf, sizeof(buf), &s);
    ASSERT_EQ_U32(n, 44, "len");

    /* Stock MOD=3 ADC/status prefix, big-endian. */
    ASSERT_EQ_U32(buf[0], 0x03, "adc0 hi");
    ASSERT_EQ_U32(buf[1], 0xF2, "adc0 lo");
    ASSERT_EQ_U32(buf[2], 0x07, "adc1 hi");
    ASSERT_EQ_U32(buf[3], 0xE4, "adc1 lo");
    ASSERT_EQ_U32(buf[4], 0x0B, "adc4 hi");
    ASSERT_EQ_U32(buf[5], 0xD6, "adc4 lo");
    ASSERT_EQ_U32(buf[6], 0x08, "mod3 status");
    ASSERT_EQ_U32(buf[7], 0x21, "battery / 100");
    ASSERT_EQ_U32(buf[8], 0x01, "version");
    ASSERT_EQ_U32(buf[9], 0x00, "flags");
    ASSERT_EQ_U32(buf[10], 0x07, "temp hi");
    ASSERT_EQ_U32(buf[11], 0xC3, "temp lo");
    ASSERT_EQ_U32(buf[12], 0x00, "r1 b0");
    ASSERT_EQ_U32(buf[13], 0x00, "r1 b1");
    ASSERT_EQ_U32(buf[14], 0x04, "r1 b2");
    ASSERT_EQ_U32(buf[15], 0x4C, "r1 b3");
    ASSERT_EQ_U32(buf[16], 0x00, "r2 b0");
    ASSERT_EQ_U32(buf[17], 0x00, "r2 b1");
    ASSERT_EQ_U32(buf[18], 0x27, "r2 b2");
    ASSERT_EQ_U32(buf[19], 0x74, "r2 b3");
    ASSERT_EQ_U32(buf[20], 0x00, "r3 b0");
    ASSERT_EQ_U32(buf[21], 0x01, "r3 b1");
    ASSERT_EQ_U32(buf[22], 0x8B, "r3 b2");
    ASSERT_EQ_U32(buf[23], 0x50, "r3 b3");
    ASSERT_EQ_U32(buf[24], 0x00, "raw r1 b0");
    ASSERT_EQ_U32(buf[25], 0x00, "raw r1 b1");
    ASSERT_EQ_U32(buf[26], 0x04, "raw r1 b2");
    ASSERT_EQ_U32(buf[27], 0xB0, "raw r1 b3");
    ASSERT_EQ_U32(buf[28], 0x00, "raw r2 b0");
    ASSERT_EQ_U32(buf[29], 0x00, "raw r2 b1");
    ASSERT_EQ_U32(buf[30], 0x27, "raw r2 b2");
    ASSERT_EQ_U32(buf[31], 0xD8, "raw r2 b3");
    ASSERT_EQ_U32(buf[32], 0x00, "raw r3 b0");
    ASSERT_EQ_U32(buf[33], 0x01, "raw r3 b1");
    ASSERT_EQ_U32(buf[34], 0x8F, "raw r3 b2");
    ASSERT_EQ_U32(buf[35], 0x38, "raw r3 b3");
    ASSERT_EQ_U32(buf[36], 0x28, "id 0");
    ASSERT_EQ_U32(buf[43], 0xF1, "id 7");
    ASSERT_EQ_U32(buf[44], 0xAA, "no overflow");
}

static void test_encode_v2_known_sample(void) {
    chameleon_sample_t s = {
        .adc_pa0_mv       = 1010,
        .adc_pa1_mv       = 2020,
        .adc_pa4_mv       = 3030,
        .mod3_status      = 0x08,
        .battery_mv       = 3300,
        .status_flags     = 0,
        .soil_temp_c_x100 = 1987,
        .r1_ohm_comp      = 1100,
        .r2_ohm_comp      = 10100,
        .r3_ohm_comp      = 101200,
        .r1_ohm_raw       = 1200,
        .r2_ohm_raw       = 10200,
        .r3_ohm_raw       = 102200,
        .array_id         = {0x28, 0x6D, 0x6A, 0xDB, 0x0F, 0x00, 0x00, 0xF1},
    };
    uint8_t buf[34];
    memset(buf, 0xAA, sizeof(buf));
    size_t n = chameleon_payload_encode_v2(buf, sizeof(buf), &s);
    ASSERT_EQ_U32(n, 32, "v2 len");

    ASSERT_EQ_U32(buf[0], 0x03, "v2 adc0 hi");
    ASSERT_EQ_U32(buf[1], 0xF2, "v2 adc0 lo");
    ASSERT_EQ_U32(buf[2], 0x07, "v2 adc1 hi");
    ASSERT_EQ_U32(buf[3], 0xE4, "v2 adc1 lo");
    ASSERT_EQ_U32(buf[4], 0x0B, "v2 adc4 hi");
    ASSERT_EQ_U32(buf[5], 0xD6, "v2 adc4 lo");
    ASSERT_EQ_U32(buf[6], 0x08, "v2 mod3 status");
    ASSERT_EQ_U32(buf[7], 0x21, "v2 battery / 100");
    ASSERT_EQ_U32(buf[8], 0x02, "v2 version");
    ASSERT_EQ_U32(buf[9], 0x00, "v2 flags");
    ASSERT_EQ_U32(buf[10], 0x07, "v2 temp hi");
    ASSERT_EQ_U32(buf[11], 0xC3, "v2 temp lo");
    ASSERT_EQ_U32(buf[12], 0x00, "v2 r1 b0");
    ASSERT_EQ_U32(buf[13], 0x00, "v2 r1 b1");
    ASSERT_EQ_U32(buf[14], 0x04, "v2 r1 b2");
    ASSERT_EQ_U32(buf[15], 0x4C, "v2 r1 b3");
    ASSERT_EQ_U32(buf[16], 0x00, "v2 r2 b0");
    ASSERT_EQ_U32(buf[17], 0x00, "v2 r2 b1");
    ASSERT_EQ_U32(buf[18], 0x27, "v2 r2 b2");
    ASSERT_EQ_U32(buf[19], 0x74, "v2 r2 b3");
    ASSERT_EQ_U32(buf[20], 0x00, "v2 r3 b0");
    ASSERT_EQ_U32(buf[21], 0x01, "v2 r3 b1");
    ASSERT_EQ_U32(buf[22], 0x8B, "v2 r3 b2");
    ASSERT_EQ_U32(buf[23], 0x50, "v2 r3 b3");
    ASSERT_EQ_U32(buf[24], 0x28, "v2 id 0");
    ASSERT_EQ_U32(buf[25], 0x6D, "v2 id 1");
    ASSERT_EQ_U32(buf[26], 0x6A, "v2 id 2");
    ASSERT_EQ_U32(buf[27], 0xDB, "v2 id 3");
    ASSERT_EQ_U32(buf[28], 0x0F, "v2 id 4");
    ASSERT_EQ_U32(buf[29], 0x00, "v2 id 5");
    ASSERT_EQ_U32(buf[30], 0x00, "v2 id 6");
    ASSERT_EQ_U32(buf[31], 0xF1, "v2 id 7");
    ASSERT_EQ_U32(buf[32], 0xAA, "v2 no overflow");
}

static void test_encode_v2_simplifies_status_flags(void) {
    chameleon_sample_t s = {
        .adc_pa0_mv       = 0,
        .adc_pa1_mv       = 0,
        .adc_pa4_mv       = 0,
        .mod3_status      = 0x08,
        .battery_mv       = 3000,
        .status_flags     = CHAMELEON_FLAG_I2C_MISSING | CHAMELEON_FLAG_TIMEOUT |
                            CHAMELEON_FLAG_TEMP_FAULT | CHAMELEON_FLAG_ID_FAULT |
                            CHAMELEON_FLAG_CH1_OPEN | CHAMELEON_FLAG_CH2_OPEN |
                            CHAMELEON_FLAG_CH3_OPEN,
        .soil_temp_c_x100 = -12700,
        .r1_ohm_comp      = 10000000U,
        .r2_ohm_comp      = 0,
        .r3_ohm_comp      = 0,
        .r1_ohm_raw       = 10000000U,
        .r2_ohm_raw       = 0,
        .r3_ohm_raw       = 0,
        .array_id         = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    };
    uint8_t buf[32];
    size_t n = chameleon_payload_encode_v2(buf, sizeof(buf), &s);
    ASSERT_EQ_U32(n, 32, "v2 flag len");
    ASSERT_EQ_U32(buf[8], 0x02, "v2 flag version");
    ASSERT_EQ_U32(buf[9], 0x07, "v2 simplified flags");
    ASSERT_EQ_U32(buf[12], 0x00, "v2 10M b0");
    ASSERT_EQ_U32(buf[13], 0x98, "v2 10M b1");
    ASSERT_EQ_U32(buf[14], 0x96, "v2 10M b2");
    ASSERT_EQ_U32(buf[15], 0x80, "v2 10M b3");
}

static void test_encode_negative_temp_and_flags(void) {
    chameleon_sample_t s = {
        .adc_pa0_mv       = 0,
        .adc_pa1_mv       = 0,
        .adc_pa4_mv       = 0,
        .mod3_status      = 0x08,
        .battery_mv       = 3000,
        .status_flags     = CHAMELEON_FLAG_TEMP_FAULT | CHAMELEON_FLAG_ID_FAULT |
                            CHAMELEON_FLAG_CH1_OPEN,
        .soil_temp_c_x100 = -12700,
        .r1_ohm_comp      = 10000000U,
        .r2_ohm_comp      = 0,
        .r3_ohm_comp      = 0,
        .r1_ohm_raw       = 10000000U,
        .r2_ohm_raw       = 0,
        .r3_ohm_raw       = 0,
        .array_id         = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    };
    uint8_t buf[44];
    size_t n = chameleon_payload_encode_v1(buf, sizeof(buf), &s);
    ASSERT_EQ_U32(n, 44, "len");
    ASSERT_EQ_U32(buf[9], 0x1C, "combined flags");
    ASSERT_EQ_U32(buf[10], 0xCE, "neg temp hi");
    ASSERT_EQ_U32(buf[11], 0x64, "neg temp lo");
    ASSERT_EQ_U32(buf[12], 0x00, "10M b0");
    ASSERT_EQ_U32(buf[13], 0x98, "10M b1");
    ASSERT_EQ_U32(buf[14], 0x96, "10M b2");
    ASSERT_EQ_U32(buf[15], 0x80, "10M b3");
    ASSERT_EQ_U32(buf[24], 0x00, "raw 10M b0");
    ASSERT_EQ_U32(buf[25], 0x98, "raw 10M b1");
    ASSERT_EQ_U32(buf[26], 0x96, "raw 10M b2");
    ASSERT_EQ_U32(buf[27], 0x80, "raw 10M b3");
}

static void test_encode_rejects_short_buf(void) {
    chameleon_sample_t s = {0};
    uint8_t buf[10];
    size_t n = chameleon_payload_encode_v1(buf, sizeof(buf), &s);
    ASSERT_EQ_U32(n, 0, "short buf rejected");
}

static void test_encode_rejects_null(void) {
    uint8_t buf[44];
    ASSERT_EQ_U32(chameleon_payload_encode_v1(NULL, sizeof(buf), NULL), 0, "null buf");
    ASSERT_EQ_U32(chameleon_payload_encode_v1(buf, sizeof(buf), NULL), 0, "null sample");
}

int main(void) {
    test_encode_known_sample();
    test_encode_v2_known_sample();
    test_encode_v2_simplifies_status_flags();
    test_encode_negative_temp_and_flags();
    test_encode_rejects_short_buf();
    test_encode_rejects_null();
    printf("test_chameleon_payload OK\n");
    return 0;
}
