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

/* Channels 2 and 3 return CAL == RAW on the first read; on the retry they
 * return a properly compensated (distinct) value. Channel 1 returns a
 * properly compensated value on the first read (no retry needed). */
static void test_retry_recovers_ch2_and_ch3(void) {
    mock_chameleon_reset();

    /* CH1: comp=1100, raw=1200 (default, distinct) — no retry expected. */

    /* CH2: first comp read == raw (10200), retry returns 9800. */
    mock_chameleon_set_resistance_raw(1, 10200U);
    mock_chameleon_set_resistance_comp_sequence(1, 10200U, 9800U);

    /* CH3: first comp read == raw (102200), retry returns 95000. */
    mock_chameleon_set_resistance_raw(2, 102200U);
    mock_chameleon_set_resistance_comp_sequence(2, 102200U, 95000U);

    chameleon_sample_t s;
    int ok = via_chameleon_acquire(&s, CHAMELEON_DEFAULT_TIMEOUT_MS);
    ASSERT_TRUE(ok, "acquire ok");

    /* Compensated values are the post-retry (distinct) values. */
    ASSERT_EQ_U32(s.r1_ohm_comp, 1100U,  "r1 comp");
    ASSERT_EQ_U32(s.r2_ohm_comp, 9800U,  "r2 comp (retried)");
    ASSERT_EQ_U32(s.r3_ohm_comp, 95000U, "r3 comp (retried)");

    /* Raw values are unchanged. */
    ASSERT_EQ_U32(s.r1_ohm_raw,  1200U,   "r1 raw");
    ASSERT_EQ_U32(s.r2_ohm_raw,  10200U,  "r2 raw");
    ASSERT_EQ_U32(s.r3_ohm_raw,  102200U, "r3 raw");

    /* No COMP_PENDING because retry succeeded. */
    ASSERT_EQ_U32((uint32_t)(s.status_flags & CHAMELEON_FLAG_COMP_PENDING),
                  0U, "COMP_PENDING clear");

    /* No other flags. */
    ASSERT_EQ_U32(s.status_flags, 0U, "no flags at all");

    /* Total delay >= settle (250) + 2 retries * 150 = 550 ms.
     * (Plus any wait_ready polls; defaults are 0, so total is exactly 550.) */
    ASSERT_TRUE(mock_chameleon_total_delay_ms() >= 550U,
                "total delay >= 550 ms (settle + 2 retries)");
}

int main(void) {
    test_retry_recovers_ch2_and_ch3();
    printf("test_chameleon_settle_and_retry OK\n");
    return 0;
}
