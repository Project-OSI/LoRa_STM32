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

int main(void) {
    test_average_of_constant();
    test_reference_low();
    test_reference_high();
    test_adc_fail_signal_zero();
    test_adc_fail_reference_zero();
    puts("PASS dendrometer");
    return 0;
}
