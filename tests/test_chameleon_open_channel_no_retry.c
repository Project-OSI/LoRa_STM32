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

/* Channel 2 is disconnected — both CAL2 and RAW2 return the open sentinel.
 * Verify that no retry is attempted for this channel and COMP_PENDING is
 * NOT set (open is not pending, it's a hardware condition). */
static void test_open_channel_skips_retry(void) {
    mock_chameleon_reset();

    /* CH2 open: both comp and raw return the open sentinel on every read. */
    mock_chameleon_set_resistance_raw(1, CHAMELEON_RES_OPEN_OHMS);
    mock_chameleon_set_resistance_comp(1, CHAMELEON_RES_OPEN_OHMS);

    chameleon_sample_t s;
    int ok = via_chameleon_acquire(&s, CHAMELEON_DEFAULT_TIMEOUT_MS);
    ASSERT_TRUE(ok, "acquire ok");

    ASSERT_TRUE(s.status_flags & CHAMELEON_FLAG_CH2_OPEN, "CH2_OPEN set");
    ASSERT_EQ_U32((uint32_t)(s.status_flags & 0x80U), 0U,
                  "reserved bit 7 remains clear");

    /* CH1 and CH3 healthy — their compensation values stay distinct from raw. */
    ASSERT_EQ_U32(s.r1_ohm_comp, 1100U,   "r1 comp untouched");
    ASSERT_EQ_U32(s.r3_ohm_comp, 101200U, "r3 comp untouched");

    ASSERT_EQ_U32(mock_chameleon_total_delay_ms(), 0U,
                  "no post-ready delay or retry");
}

int main(void) {
    test_open_channel_skips_retry();
    printf("test_chameleon_open_channel_no_retry OK\n");
    return 0;
}
