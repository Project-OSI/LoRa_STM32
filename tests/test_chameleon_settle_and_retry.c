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

/* STATUS is polled at the vendor's 50 ms cadence. Once READY is returned,
 * register reads begin immediately and equality does not trigger a retry. */
static void test_status_cadence_without_post_ready_settle(void) {
    mock_chameleon_reset();
    mock_chameleon_set_status_ready_after_polls(3);
    mock_chameleon_set_resistance_raw(1, 10200U);
    mock_chameleon_set_resistance_comp_sequence(1, 10200U, 9800U);

    chameleon_sample_t s;
    int ok = via_chameleon_acquire(&s, CHAMELEON_DEFAULT_TIMEOUT_MS);
    ASSERT_TRUE(ok, "acquire ok");

    ASSERT_EQ_U32(s.r2_ohm_comp, 10200U, "first equal CAL2 retained");
    ASSERT_EQ_U32(s.r2_ohm_raw, 10200U, "RAW2 retained");
    ASSERT_EQ_U32(mock_chameleon_comp_read_count(1), 1U, "CAL2 not retried");
    ASSERT_EQ_U32(mock_chameleon_status_poll_count(), 4U, "four status polls");
    ASSERT_EQ_U32(mock_chameleon_total_delay_ms(), 150U, "three 50 ms delays");
    ASSERT_EQ_U32(s.status_flags, 0U, "no flags at all");
}

int main(void) {
    test_status_cadence_without_post_ready_settle();
    printf("test_chameleon_settle_and_retry OK\n");
    return 0;
}
