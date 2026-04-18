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

int main(void) {
    test_average_of_constant();
    puts("PASS dendrometer");
    return 0;
}
