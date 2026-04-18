#include "dendrometer.h"
#include "mock_board.h"
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
        fprintf(stderr, "FAIL %s: %s (%s:%d)\n", (label), #cond,                \
                __FILE__, __LINE__);                                            \
        exit(1);                                                                \
    }                                                                           \
} while (0)

static void test_average_of_constant(void) {
    mock_board_reset();
    mock_board_set_signal_constant(2048);
    mock_board_set_reference_constant(2048);

    dendrometer_result_t r;
    dendrometer_measure(&r);

    ASSERT_EQ_U32(r.adc_signal_avg_raw,    2048, "sig avg");
    ASSERT_EQ_U32(r.adc_reference_avg_raw, 2048, "ref avg");
    ASSERT_TRUE(r.flags & DENDRO_FLAG_VALID,            "VALID set");
    ASSERT_TRUE(!(r.flags & DENDRO_FLAG_REF_LOW),       "REF_LOW clear");
    ASSERT_TRUE(!(r.flags & DENDRO_FLAG_REF_HIGH),      "REF_HIGH clear");
    ASSERT_TRUE(!(r.flags & DENDRO_FLAG_ADC_FAIL),      "ADC_FAIL clear");

    puts("  PASS average_of_constant");
}

static void test_reference_low(void) {
    mock_board_reset();
    mock_board_set_signal_constant(1024);
    mock_board_set_reference_constant(50);   /* < DENDRO_REF_MIN_RAW (128) */

    dendrometer_result_t r;
    dendrometer_measure(&r);

    ASSERT_EQ_U32(r.adc_signal_avg_raw,    1024, "sig avg (low ref)");
    ASSERT_EQ_U32(r.adc_reference_avg_raw, 50,   "ref avg (low ref)");
    ASSERT_TRUE(r.flags & DENDRO_FLAG_REF_LOW,    "REF_LOW set");
    ASSERT_TRUE(!(r.flags & DENDRO_FLAG_VALID),   "VALID clear");
    puts("  PASS reference_low");
}

static void test_reference_high(void) {
    mock_board_reset();
    mock_board_set_signal_constant(1024);
    mock_board_set_reference_constant(4095); /* > DENDRO_REF_MAX_RAW (4080) */

    dendrometer_result_t r;
    dendrometer_measure(&r);

    ASSERT_EQ_U32(r.adc_reference_avg_raw, 4095, "ref avg (high ref)");
    ASSERT_TRUE(r.flags & DENDRO_FLAG_REF_HIGH,   "REF_HIGH set");
    ASSERT_TRUE(!(r.flags & DENDRO_FLAG_VALID),   "VALID clear");
    puts("  PASS reference_high");
}

static void test_adc_fail_signal_zero(void) {
    mock_board_reset();
    mock_board_set_signal_constant(0);       /* channel dead */
    mock_board_set_reference_constant(2048);

    dendrometer_result_t r;
    dendrometer_measure(&r);

    ASSERT_TRUE(r.flags & DENDRO_FLAG_ADC_FAIL,   "ADC_FAIL set (sig dead)");
    ASSERT_TRUE(!(r.flags & DENDRO_FLAG_VALID),   "VALID clear");
    /* averages still reported */
    ASSERT_EQ_U32(r.adc_signal_avg_raw,    0,    "sig avg still 0");
    ASSERT_EQ_U32(r.adc_reference_avg_raw, 2048, "ref avg still 2048");
    puts("  PASS adc_fail_signal_zero");
}

static void test_adc_fail_reference_zero(void) {
    mock_board_reset();
    mock_board_set_signal_constant(1024);
    mock_board_set_reference_constant(0);    /* reference dead */

    dendrometer_result_t r;
    dendrometer_measure(&r);

    ASSERT_TRUE(r.flags & DENDRO_FLAG_ADC_FAIL,   "ADC_FAIL set (ref dead)");
    ASSERT_TRUE(!(r.flags & DENDRO_FLAG_VALID),   "VALID clear");
    puts("  PASS adc_fail_reference_zero");
}

static int last_event_is(mock_event_kind_t kind) {
    size_t n = mock_board_event_count();
    if (n == 0) return 0;
    return mock_board_events()[n - 1].kind == kind;
}

static int first_event_is(mock_event_kind_t kind) {
    if (mock_board_event_count() == 0) return 0;
    return mock_board_events()[0].kind == kind;
}

static void test_power_sequence_happy_path(void) {
    mock_board_reset();
    mock_board_set_signal_constant(2048);
    mock_board_set_reference_constant(2048);

    dendrometer_result_t r;
    dendrometer_measure(&r);

    ASSERT_TRUE(first_event_is(MOCK_EVT_5V_ON), "first event is 5V_ON");
    ASSERT_TRUE(last_event_is(MOCK_EVT_5V_OFF), "last event is 5V_OFF");
    puts("  PASS power_sequence_happy_path");
}

static void test_power_sequence_ref_low(void) {
    mock_board_reset();
    mock_board_set_signal_constant(1024);
    mock_board_set_reference_constant(10);

    dendrometer_result_t r;
    dendrometer_measure(&r);

    ASSERT_TRUE(last_event_is(MOCK_EVT_5V_OFF), "5V_OFF still fires on REF_LOW");
    puts("  PASS power_sequence_ref_low");
}

static void test_power_sequence_adc_fail(void) {
    mock_board_reset();
    mock_board_set_signal_constant(0);
    mock_board_set_reference_constant(2048);

    dendrometer_result_t r;
    dendrometer_measure(&r);

    ASSERT_TRUE(last_event_is(MOCK_EVT_5V_OFF), "5V_OFF still fires on ADC_FAIL");
    puts("  PASS power_sequence_adc_fail");
}

static void test_settle_delay_happens(void) {
    mock_board_reset();
    mock_board_set_signal_constant(2048);
    mock_board_set_reference_constant(2048);

    dendrometer_result_t r;
    dendrometer_measure(&r);

    /* Second event must be the settle DELAY(50). */
    ASSERT_TRUE(mock_board_event_count() >= 2, "enough events recorded");
    const mock_event_t *evts = mock_board_events();
    ASSERT_TRUE(evts[1].kind == MOCK_EVT_DELAY,      "second event is DELAY");
    ASSERT_EQ_U32(evts[1].value, DENDRO_SETTLE_MS,   "settle delay value");
    puts("  PASS settle_delay_happens");
}

static void test_pack_known_result(void) {
    dendrometer_result_t m = {
        .adc_signal_avg_raw    = 0x0A0B,
        .adc_reference_avg_raw = 0x0C0D,
        .flags                 = DENDRO_FLAG_VALID,
    };
    uint8_t dst[8] = {0};
    uint8_t n = dendrometer_pack_payload(&m, 0x0C80, 0x8A, dst);

    ASSERT_EQ_U32(n, 8, "pack size");
    ASSERT_EQ_U32(dst[0], 0x0C, "battery HI"); /* 3200 = 0x0C80 */
    ASSERT_EQ_U32(dst[1], 0x80, "battery LO");
    ASSERT_EQ_U32(dst[2], 0x0A, "sig HI");
    ASSERT_EQ_U32(dst[3], 0x0B, "sig LO");
    ASSERT_EQ_U32(dst[4], 0x0C, "ref HI");
    ASSERT_EQ_U32(dst[5], 0x0D, "ref LO");
    ASSERT_EQ_U32(dst[6], 0x8A, "status preserved");
    ASSERT_EQ_U32(dst[7], 0x01, "flags VALID");
    puts("  PASS pack_known_result");
}

static void test_pack_combined_flags(void) {
    dendrometer_result_t m = {
        .adc_signal_avg_raw    = 0,
        .adc_reference_avg_raw = 0,
        .flags = DENDRO_FLAG_REF_LOW | DENDRO_FLAG_ADC_FAIL,
    };
    uint8_t dst[8] = {0};
    (void)dendrometer_pack_payload(&m, 0, 0x08, dst);
    ASSERT_EQ_U32(dst[7], 0x0A, "REF_LOW|ADC_FAIL == 0x0A");
    puts("  PASS pack_combined_flags");
}

static void test_pack_battery_max(void) {
    dendrometer_result_t m = { 0, 0, DENDRO_FLAG_VALID };
    uint8_t dst[8] = {0};
    (void)dendrometer_pack_payload(&m, 0xFFFF, 0x08, dst);
    ASSERT_EQ_U32(dst[0], 0xFF, "battery HI max");
    ASSERT_EQ_U32(dst[1], 0xFF, "battery LO max");
    puts("  PASS pack_battery_max");
}

int main(void) {
    test_average_of_constant();
    test_reference_low();
    test_reference_high();
    test_adc_fail_signal_zero();
    test_adc_fail_reference_zero();
    test_power_sequence_happy_path();
    test_power_sequence_ref_low();
    test_power_sequence_adc_fail();
    test_settle_delay_happens();
    test_pack_known_result();
    test_pack_combined_flags();
    test_pack_battery_max();
    puts("PASS dendrometer");
    return 0;
}
