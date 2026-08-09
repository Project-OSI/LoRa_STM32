#include "via_chameleon.h"
#include "chameleon_payload.h"
#include "mock_chameleon_i2c.h"
#include <stdio.h>
#include <stdlib.h>

#define ASSERT_EQ_U32(actual, expected, label) do {                             \
    if ((uint32_t)(actual) != (uint32_t)(expected)) {                           \
        fprintf(stderr, "FAIL %s: expected %u got %u (%s:%d)\n",                \
                (label), (unsigned)(expected), (unsigned)(actual),              \
                __FILE__, __LINE__);                                            \
        exit(1);                                                                \
    }                                                                           \
} while (0)

#define ASSERT_TRUE(cond, label) do {                                           \
    if (!(cond)) {                                                              \
        fprintf(stderr, "FAIL %s (%s:%d)\n", (label), __FILE__, __LINE__);      \
        exit(1);                                                                \
    }                                                                           \
} while (0)

/* The vendor example contains equal raw and calibrated values on all three
 * connected channels. Equality is a valid result, not a pending state. */
static void test_vendor_equal_values_are_valid(void) {
    mock_chameleon_reset();
    mock_chameleon_set_resistance(0, 1100U);
    mock_chameleon_set_resistance(1, 10100U);
    mock_chameleon_set_resistance(2, 101200U);

    chameleon_sample_t s;
    int ok = via_chameleon_acquire(&s, CHAMELEON_DEFAULT_TIMEOUT_MS);
    ASSERT_TRUE(ok, "acquire ok");

    ASSERT_EQ_U32(s.r1_ohm_comp, 1100U, "r1 comp");
    ASSERT_EQ_U32(s.r1_ohm_raw, 1100U, "r1 raw");
    ASSERT_EQ_U32(s.r2_ohm_comp, 10100U, "r2 comp");
    ASSERT_EQ_U32(s.r2_ohm_raw, 10100U, "r2 raw");
    ASSERT_EQ_U32(s.r3_ohm_comp, 101200U, "r3 comp");
    ASSERT_EQ_U32(s.r3_ohm_raw, 101200U, "r3 raw");
    ASSERT_EQ_U32(s.status_flags, 0U, "no flags");
    ASSERT_EQ_U32(mock_chameleon_comp_read_count(0), 1U, "one CAL1 read");
    ASSERT_EQ_U32(mock_chameleon_comp_read_count(1), 1U, "one CAL2 read");
    ASSERT_EQ_U32(mock_chameleon_comp_read_count(2), 1U, "one CAL3 read");
    ASSERT_EQ_U32(mock_chameleon_total_delay_ms(), 0U, "no unsupported delay");
}

int main(void) {
    test_vendor_equal_values_are_valid();
    printf("test_chameleon_comp_pending OK\n");
    return 0;
}
