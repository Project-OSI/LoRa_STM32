#include "via_chameleon.h"
#include "mock_chameleon_i2c.h"
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

int main(void) {
    mock_chameleon_reset();
    chameleon_sample_t s;
    memset(&s, 0xA5, sizeof(s));

    int ok = via_chameleon_acquire(&s, CHAMELEON_DEFAULT_TIMEOUT_MS);

    ASSERT_EQ_U32(ok, 1, "dummy acquire ok");
    ASSERT_EQ_U32(s.adc_pa0_mv, 0, "adc pa0 cleared");
    ASSERT_EQ_U32(s.adc_pa1_mv, 0, "adc pa1 cleared");
    ASSERT_EQ_U32(s.adc_pa4_mv, 0, "adc pa4 cleared");
    ASSERT_EQ_U32(s.mod3_status, 0, "mod3 status cleared");
    ASSERT_EQ_U32(s.status_flags, 0, "flags clear");
    ASSERT_EQ_U32(s.soil_temp_c_x100, 2000, "dummy temp");
    ASSERT_EQ_U32(s.r1_ohm_comp, 1600, "dummy r1 comp");
    ASSERT_EQ_U32(s.r2_ohm_comp, 100000, "dummy r2 comp");
    ASSERT_EQ_U32(s.r3_ohm_comp, 1600000, "dummy r3 comp");
    ASSERT_EQ_U32(s.r1_ohm_raw, 1600, "dummy r1 raw");
    ASSERT_EQ_U32(s.r2_ohm_raw, 100000, "dummy r2 raw");
    ASSERT_EQ_U32(s.r3_ohm_raw, 1600000, "dummy r3 raw");
    ASSERT_EQ_U32(s.array_id[0], 0xDE, "dummy id 0");
    ASSERT_EQ_U32(s.array_id[7], 0xEF, "dummy id 7");
    ASSERT_EQ_U32(s.battery_mv, 3300, "dummy battery");
    printf("test_chameleon_dummy OK\n");
    return 0;
}
