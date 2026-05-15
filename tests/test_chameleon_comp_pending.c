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

/* Channel 3 keeps returning CAL == RAW on every read (peripheral never
 * computed compensation for it). Channels 1 and 2 are healthy. */
static void test_comp_pending_when_retry_exhausted(void) {
    mock_chameleon_reset();

    /* CH1: defaults — comp 1100, raw 1200 (distinct). */
    /* CH2: defaults — comp 10100, raw 10200 (distinct). */

    /* CH3: comp and raw both 102200, every read. */
    mock_chameleon_set_resistance_raw(2, 102200U);
    mock_chameleon_set_resistance_comp(2, 102200U);

    chameleon_sample_t s;
    int ok = via_chameleon_acquire(&s, CHAMELEON_DEFAULT_TIMEOUT_MS);
    ASSERT_TRUE(ok, "acquire ok");

    /* CH1 and CH2 are still properly compensated. */
    ASSERT_EQ_U32(s.r1_ohm_comp, 1100U,  "r1 comp");
    ASSERT_EQ_U32(s.r2_ohm_comp, 10100U, "r2 comp");

    /* CH3 comp == raw — we did NOT overwrite with garbage; the value is
     * the (still-uncompensated) read. */
    ASSERT_EQ_U32(s.r3_ohm_comp, 102200U, "r3 comp == raw");
    ASSERT_EQ_U32(s.r3_ohm_raw,  102200U, "r3 raw");

    /* COMP_PENDING is set because at least one non-open channel finished
     * with CAL == RAW. */
    ASSERT_TRUE(s.status_flags & CHAMELEON_FLAG_COMP_PENDING,
                "COMP_PENDING set");

    /* No other flags spuriously set (no I2C_MISSING, TIMEOUT, etc.). */
    ASSERT_EQ_U32((uint32_t)(s.status_flags & ~CHAMELEON_FLAG_COMP_PENDING),
                  0U, "no other flags");
}

int main(void) {
    test_comp_pending_when_retry_exhausted();
    printf("test_chameleon_comp_pending OK\n");
    return 0;
}
