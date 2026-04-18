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

/* ---- helpers over the mock event log ------------------------------- */

#define DENDRO_SAMPLES_EXPECTED 20u

static size_t count_events_of_kind(mock_event_kind_t kind) {
    size_t n = mock_board_event_count();
    const mock_event_t *e = mock_board_events();
    size_t c = 0;
    for (size_t i = 0; i < n; i++) {
        if (e[i].kind == kind) c++;
    }
    return c;
}

static uint32_t sum_delay_ms(void) {
    size_t n = mock_board_event_count();
    const mock_event_t *e = mock_board_events();
    uint32_t total = 0;
    for (size_t i = 0; i < n; i++) {
        if (e[i].kind == MOCK_EVT_DELAY) total += e[i].value;
    }
    return total;
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

/* ---- tests --------------------------------------------------------- */

static void test_constant_inputs_averaged(void) {
    mock_board_reset();
    mock_board_set_signal_constant(2000);
    mock_board_set_reference_constant(3000);

    dendrometer_result_t r;
    dendrometer_measure(&r);

    ASSERT_EQ_U32(r.signal_raw,    2000, "signal_raw avg");
    ASSERT_EQ_U32(r.reference_raw, 3000, "reference_raw avg");
    puts("  PASS constant_inputs_averaged");
}

static void test_paired_sample_count(void) {
    mock_board_reset();
    mock_board_set_signal_constant(1);
    mock_board_set_reference_constant(1);

    dendrometer_result_t r;
    dendrometer_measure(&r);

    ASSERT_EQ_U32(count_events_of_kind(MOCK_EVT_ADC_SIG), DENDRO_SAMPLES_EXPECTED, "20 signal reads");
    ASSERT_EQ_U32(count_events_of_kind(MOCK_EVT_ADC_REF), DENDRO_SAMPLES_EXPECTED, "20 reference reads");
    puts("  PASS paired_sample_count");
}

static void test_total_delay_budget(void) {
    /* 1 settle × 50 ms + 20 spacing × 10 ms = 250 ms. */
    mock_board_reset();
    mock_board_set_signal_constant(1);
    mock_board_set_reference_constant(1);

    dendrometer_result_t r;
    dendrometer_measure(&r);

    ASSERT_EQ_U32(sum_delay_ms(), 250u, "total delay budget");
    puts("  PASS total_delay_budget");
}

static void test_5v_disabled_on_all_exit_paths(void) {
    mock_board_reset();
    mock_board_set_signal_constant(0);
    mock_board_set_reference_constant(0);

    dendrometer_result_t r;
    dendrometer_measure(&r);

    ASSERT_TRUE(first_event_is(MOCK_EVT_5V_ON),  "5V_ON first");
    ASSERT_TRUE(last_event_is(MOCK_EVT_5V_OFF),  "5V_OFF last, even with zero readings");
    ASSERT_EQ_U32(count_events_of_kind(MOCK_EVT_5V_ON),  1u, "one 5V_ON");
    ASSERT_EQ_U32(count_events_of_kind(MOCK_EVT_5V_OFF), 1u, "one 5V_OFF");
    puts("  PASS 5v_disabled_on_all_exit_paths");
}

static void test_null_result_pointer_safe(void) {
    mock_board_reset();
    mock_board_set_signal_constant(2000);
    mock_board_set_reference_constant(3000);

    dendrometer_measure(0);
    /* Must not crash, must not touch the board. */
    ASSERT_EQ_U32(mock_board_event_count(), 0u, "no board calls for null result");
    puts("  PASS null_result_pointer_safe");
}

int main(void) {
    test_constant_inputs_averaged();
    test_paired_sample_count();
    test_total_delay_budget();
    test_5v_disabled_on_all_exit_paths();
    test_null_result_pointer_safe();
    puts("PASS dendrometer");
    return 0;
}
