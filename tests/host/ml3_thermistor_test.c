#include "ml3_thermistor.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

static int failures;

#define EXPECT_TRUE(value, label) \
  do { \
    if (!(value)) { \
      (void)fprintf(stderr, "FAIL: %s\n", (label)); \
      ++failures; \
    } \
  } while (0)

#define EXPECT_STATUS(expected, actual, label) \
  do { \
    ml3_thermistor_status_t expected_ = (expected); \
    ml3_thermistor_status_t actual_ = (actual); \
    if (expected_ != actual_) { \
      (void)fprintf(stderr, "FAIL: %s: expected=%d actual=%d\n", \
        (label), (int)expected_, (int)actual_); \
      ++failures; \
    } \
  } while (0)

#define EXPECT_U32(expected, actual, label) \
  do { \
    uint32_t expected_ = (expected); \
    uint32_t actual_ = (actual); \
    if (expected_ != actual_) { \
      (void)fprintf(stderr, "FAIL: %s: expected=%" PRIu32 " actual=%" PRIu32 "\n", \
        (label), expected_, actual_); \
      ++failures; \
    } \
  } while (0)

#define EXPECT_I32(expected, actual, label) \
  do { \
    int32_t expected_ = (expected); \
    int32_t actual_ = (actual); \
    if (expected_ != actual_) { \
      (void)fprintf(stderr, "FAIL: %s: expected=%" PRId32 " actual=%" PRId32 "\n", \
        (label), expected_, actual_); \
      ++failures; \
    } \
  } while (0)

static void test_ratiometric_resistance_conversion(void) {
  uint32_t resistance_ohm = UINT32_C(0xA5A5A5A5);

  EXPECT_STATUS(
    ML3_THERMISTOR_OK,
    ml3_thermistor_code_to_resistance(32760U, 10000U, &resistance_ohm),
    "half scale conversion status");
  EXPECT_U32(10000U, resistance_ohm, "half scale equals reference resistance");

  EXPECT_STATUS(
    ML3_THERMISTOR_OK,
    ml3_thermistor_code_to_resistance(16380U, 10000U, &resistance_ohm),
    "quarter scale conversion status");
  EXPECT_U32(3333U, resistance_ohm, "quarter scale truncates resistance");
}

static void test_resistance_boundaries_and_errors(void) {
  uint32_t resistance_ohm = UINT32_C(0xA5A5A5A5);

  EXPECT_STATUS(
    ML3_THERMISTOR_OK,
    ml3_thermistor_code_to_resistance(0U, 10000U, &resistance_ohm),
    "zero code conversion status");
  EXPECT_U32(0U, resistance_ohm, "zero code produces zero resistance");

  resistance_ohm = UINT32_C(0xA5A5A5A5);
  EXPECT_STATUS(
    ML3_THERMISTOR_OK,
    ml3_thermistor_code_to_resistance(32760U, UINT32_MAX, &resistance_ohm),
    "maximum representable resistance status");
  EXPECT_U32(UINT32_MAX, resistance_ohm, "maximum resistance is representable");

  resistance_ohm = UINT32_C(0xA5A5A5A5);
  EXPECT_STATUS(
    ML3_THERMISTOR_OVERFLOW,
    ml3_thermistor_code_to_resistance(32761U, UINT32_MAX, &resistance_ohm),
    "resistance overflow status");
  EXPECT_U32(UINT32_C(0xA5A5A5A5), resistance_ohm, "overflow preserves output");

  EXPECT_STATUS(
    ML3_THERMISTOR_CODE_OUT_OF_RANGE,
    ml3_thermistor_code_to_resistance(ML3_THERMISTOR_FULL_SCALE_CODE, 10000U, &resistance_ohm),
    "full scale avoids division by zero");
  EXPECT_U32(UINT32_C(0xA5A5A5A5), resistance_ohm, "full scale preserves output");

  EXPECT_STATUS(
    ML3_THERMISTOR_CODE_OUT_OF_RANGE,
    ml3_thermistor_code_to_resistance(UINT16_MAX, 10000U, &resistance_ohm),
    "code above full scale is rejected");
  EXPECT_STATUS(
    ML3_THERMISTOR_INVALID_ARGUMENT,
    ml3_thermistor_code_to_resistance(100U, 0U, &resistance_ohm),
    "zero reference resistance is rejected");
  EXPECT_STATUS(
    ML3_THERMISTOR_INVALID_ARGUMENT,
    ml3_thermistor_code_to_resistance(100U, 10000U, NULL),
    "null resistance output is rejected");
}

static void test_table_interpolation_in_both_orders(void) {
  static const ml3_thermistor_point_t decreasing_table[] = {
    {30000U, -1000},
    {20000U, 0},
    {10000U, 1000}
  };
  static const ml3_thermistor_point_t increasing_table[] = {
    {10000U, 1000},
    {20000U, 0},
    {30000U, -1000}
  };
  static const ml3_thermistor_point_t truncation_table[] = {
    {100U, 0},
    {103U, -2}
  };
  static const ml3_thermistor_point_t reversed_truncation_table[] = {
    {103U, -2},
    {100U, 0}
  };
  int32_t temperature_centic = INT32_C(0x12345678);

  EXPECT_STATUS(
    ML3_THERMISTOR_OK,
    ml3_thermistor_interpolate(decreasing_table, 3U, 30000U, &temperature_centic),
    "decreasing first endpoint status");
  EXPECT_I32(-1000, temperature_centic, "decreasing first endpoint");
  EXPECT_STATUS(
    ML3_THERMISTOR_OK,
    ml3_thermistor_interpolate(decreasing_table, 3U, 25000U, &temperature_centic),
    "decreasing interior status");
  EXPECT_I32(-500, temperature_centic, "decreasing interior interpolation");
  EXPECT_STATUS(
    ML3_THERMISTOR_OK,
    ml3_thermistor_interpolate(decreasing_table, 3U, 10000U, &temperature_centic),
    "decreasing last endpoint status");
  EXPECT_I32(1000, temperature_centic, "decreasing last endpoint");

  EXPECT_STATUS(
    ML3_THERMISTOR_OK,
    ml3_thermistor_interpolate(increasing_table, 3U, 25000U, &temperature_centic),
    "increasing interior status");
  EXPECT_I32(-500, temperature_centic, "increasing interior interpolation");

  EXPECT_STATUS(
    ML3_THERMISTOR_OK,
    ml3_thermistor_interpolate(truncation_table, 2U, 101U, &temperature_centic),
    "negative interpolation truncation status");
  EXPECT_I32(0, temperature_centic, "negative fraction truncates toward zero");
  EXPECT_STATUS(
    ML3_THERMISTOR_OK,
    ml3_thermistor_interpolate(truncation_table, 2U, 102U, &temperature_centic),
    "negative interpolation second point status");
  EXPECT_I32(-1, temperature_centic, "negative interpolation keeps integer part");
  EXPECT_STATUS(
    ML3_THERMISTOR_OK,
    ml3_thermistor_interpolate(reversed_truncation_table, 2U, 101U, &temperature_centic),
    "reversed negative interpolation truncation status");
  EXPECT_I32(0, temperature_centic, "reversing a non-integral segment preserves interpolation");
}

static void test_interpolation_rejects_bad_tables_and_extrapolation(void) {
  static const ml3_thermistor_point_t increasing_table[] = {
    {10000U, 1000},
    {20000U, 0},
    {30000U, -1000}
  };
  static const ml3_thermistor_point_t duplicate_table[] = {
    {10000U, 1000},
    {10000U, 0}
  };
  static const ml3_thermistor_point_t later_duplicate_increasing_table[] = {
    {10000U, 1000},
    {20000U, 0},
    {20000U, -1000}
  };
  static const ml3_thermistor_point_t later_duplicate_decreasing_table[] = {
    {30000U, -1000},
    {20000U, 0},
    {20000U, 1000}
  };
  static const ml3_thermistor_point_t nonmonotonic_table[] = {
    {10000U, 1000},
    {30000U, -1000},
    {20000U, 0}
  };
  static const ml3_thermistor_point_t zero_resistance_table[] = {
    {0U, 1000},
    {10000U, 0}
  };
  static const ml3_thermistor_point_t extreme_table[] = {
    {1U, INT32_MIN},
    {UINT32_MAX, INT32_MAX}
  };
  int32_t temperature_centic = INT32_C(0x12345678);

  EXPECT_STATUS(
    ML3_THERMISTOR_RESISTANCE_OUT_OF_RANGE,
    ml3_thermistor_interpolate(increasing_table, 3U, 9999U, &temperature_centic),
    "lower extrapolation is rejected");
  EXPECT_I32(INT32_C(0x12345678), temperature_centic, "lower extrapolation preserves output");
  EXPECT_STATUS(
    ML3_THERMISTOR_RESISTANCE_OUT_OF_RANGE,
    ml3_thermistor_interpolate(increasing_table, 3U, 30001U, &temperature_centic),
    "upper extrapolation is rejected");
  EXPECT_STATUS(
    ML3_THERMISTOR_INVALID_TABLE,
    ml3_thermistor_interpolate(duplicate_table, 2U, 10000U, &temperature_centic),
    "duplicate resistance is rejected");
  EXPECT_STATUS(
    ML3_THERMISTOR_INVALID_TABLE,
    ml3_thermistor_interpolate(later_duplicate_increasing_table, 3U, 20000U, &temperature_centic),
    "later duplicate in increasing table is rejected");
  EXPECT_STATUS(
    ML3_THERMISTOR_INVALID_TABLE,
    ml3_thermistor_interpolate(later_duplicate_decreasing_table, 3U, 20000U, &temperature_centic),
    "later duplicate in decreasing table is rejected");
  EXPECT_STATUS(
    ML3_THERMISTOR_INVALID_TABLE,
    ml3_thermistor_interpolate(nonmonotonic_table, 3U, 20000U, &temperature_centic),
    "nonmonotonic resistance is rejected");
  EXPECT_STATUS(
    ML3_THERMISTOR_INVALID_TABLE,
    ml3_thermistor_interpolate(zero_resistance_table, 2U, 5000U, &temperature_centic),
    "zero resistance knot is rejected");
  EXPECT_STATUS(
    ML3_THERMISTOR_INVALID_TABLE,
    ml3_thermistor_interpolate(increasing_table, 1U, 10000U, &temperature_centic),
    "one-point table is rejected");
  EXPECT_STATUS(
    ML3_THERMISTOR_INVALID_ARGUMENT,
    ml3_thermistor_interpolate(NULL, 3U, 10000U, &temperature_centic),
    "null table is rejected");
  EXPECT_STATUS(
    ML3_THERMISTOR_INVALID_ARGUMENT,
    ml3_thermistor_interpolate(increasing_table, 3U, 10000U, NULL),
    "null temperature output is rejected");

  EXPECT_STATUS(
    ML3_THERMISTOR_OK,
    ml3_thermistor_interpolate(extreme_table, 2U, UINT32_MAX - 1U, &temperature_centic),
    "full-domain interpolation status");
  EXPECT_I32(INT32_MAX - 2, temperature_centic, "full-domain interpolation uses exact unsigned product");
}

static ml3_thermistor_config_t valid_config(void) {
  static const ml3_thermistor_point_t synthetic_table[] = {
    {20000U, 0},
    {10000U, 1000},
    {5000U, 2000}
  };
  ml3_thermistor_config_t config;

  config.reference_resistance_ohm = 10000U;
  config.rail_guard_code = 100U;
  config.settle_time_ms = 5U;
  config.table = synthetic_table;
  config.table_count = 3U;
  config.reference_resistance_ready = true;
  config.rail_guard_ready = true;
  config.settle_time_ready = true;
  config.table_ready = true;
  return config;
}

static void test_conversion_uses_ready_config_and_inclusive_rail_guard(void) {
  ml3_thermistor_config_t config = valid_config();
  ml3_thermistor_result_t result = {
    UINT32_C(0xA5A5A5A5),
    INT32_C(0x12345678)
  };

  EXPECT_STATUS(
    ML3_THERMISTOR_OK,
    ml3_thermistor_convert(&config, 32760U, &result),
    "configured conversion status");
  EXPECT_U32(10000U, result.resistance_ohm, "configured conversion resistance");
  EXPECT_I32(1000, result.temperature_centic, "configured conversion temperature");

  result.resistance_ohm = UINT32_C(0xA5A5A5A5);
  result.temperature_centic = INT32_C(0x12345678);
  EXPECT_STATUS(
    ML3_THERMISTOR_RAIL_FAULT,
    ml3_thermistor_convert(&config, 0U, &result),
    "zero code is lower rail fault");
  EXPECT_STATUS(
    ML3_THERMISTOR_RAIL_FAULT,
    ml3_thermistor_convert(&config, 100U, &result),
    "lower guard equality is rail fault");
  EXPECT_STATUS(
    ML3_THERMISTOR_RESISTANCE_OUT_OF_RANGE,
    ml3_thermistor_convert(&config, 101U, &result),
    "code above lower guard is not rail fault");
  EXPECT_STATUS(
    ML3_THERMISTOR_RESISTANCE_OUT_OF_RANGE,
    ml3_thermistor_convert(&config, 65419U, &result),
    "code below upper guard is not rail fault");
  EXPECT_STATUS(
    ML3_THERMISTOR_RAIL_FAULT,
    ml3_thermistor_convert(&config, 65420U, &result),
    "upper guard equality is rail fault");
  EXPECT_STATUS(
    ML3_THERMISTOR_RAIL_FAULT,
    ml3_thermistor_convert(&config, ML3_THERMISTOR_FULL_SCALE_CODE, &result),
    "full scale code is upper rail fault");
  EXPECT_STATUS(
    ML3_THERMISTOR_CODE_OUT_OF_RANGE,
    ml3_thermistor_convert(&config, ML3_THERMISTOR_FULL_SCALE_CODE + 1U, &result),
    "code above ADC full scale is rejected before guard arithmetic");
  EXPECT_U32(UINT32_C(0xA5A5A5A5), result.resistance_ohm, "fault preserves result resistance");
  EXPECT_I32(INT32_C(0x12345678), result.temperature_centic, "fault preserves result temperature");
}

static void test_configuration_readiness_fails_closed(void) {
  ml3_thermistor_config_t config = valid_config();
  ml3_thermistor_result_t result = {
    UINT32_C(0xA5A5A5A5),
    INT32_C(0x12345678)
  };

  EXPECT_TRUE(ml3_thermistor_config_is_ready(&config), "complete synthetic config is ready");
  EXPECT_TRUE(!ml3_thermistor_config_is_ready(NULL), "null config is not ready");

  config.reference_resistance_ready = false;
  EXPECT_TRUE(!ml3_thermistor_config_is_ready(&config), "reference readiness is required");
  EXPECT_STATUS(ML3_THERMISTOR_CONFIG_NOT_READY,
    ml3_thermistor_convert(&config, 32760U, &result), "missing reference readiness fails closed");
  config = valid_config();
  config.rail_guard_ready = false;
  EXPECT_STATUS(ML3_THERMISTOR_CONFIG_NOT_READY,
    ml3_thermistor_convert(&config, 32760U, &result), "missing guard readiness fails closed");
  config = valid_config();
  config.settle_time_ready = false;
  EXPECT_STATUS(ML3_THERMISTOR_CONFIG_NOT_READY,
    ml3_thermistor_convert(&config, 32760U, &result), "missing settle readiness fails closed");
  config = valid_config();
  config.table_ready = false;
  EXPECT_STATUS(ML3_THERMISTOR_CONFIG_NOT_READY,
    ml3_thermistor_convert(&config, 32760U, &result), "missing table readiness fails closed");

  config = valid_config();
  config.reference_resistance_ohm = 0U;
  EXPECT_STATUS(ML3_THERMISTOR_CONFIG_NOT_READY,
    ml3_thermistor_convert(&config, 32760U, &result), "zero effective reference fails closed");
  config = valid_config();
  config.rail_guard_code = 0U;
  EXPECT_STATUS(ML3_THERMISTOR_CONFIG_NOT_READY,
    ml3_thermistor_convert(&config, 32760U, &result), "zero rail guard fails closed");
  config = valid_config();
  config.rail_guard_code = ML3_THERMISTOR_FULL_SCALE_CODE / 2U;
  EXPECT_STATUS(ML3_THERMISTOR_CONFIG_NOT_READY,
    ml3_thermistor_convert(&config, 32760U, &result), "overlapping rail guards fail closed");
  config = valid_config();
  config.settle_time_ms = 0U;
  EXPECT_STATUS(ML3_THERMISTOR_CONFIG_NOT_READY,
    ml3_thermistor_convert(&config, 32760U, &result), "zero settle time fails closed");
  config = valid_config();
  config.table = NULL;
  EXPECT_STATUS(ML3_THERMISTOR_CONFIG_NOT_READY,
    ml3_thermistor_convert(&config, 32760U, &result), "missing table fails closed");
  config = valid_config();
  config.table_count = 1U;
  EXPECT_STATUS(ML3_THERMISTOR_INVALID_TABLE,
    ml3_thermistor_convert(&config, 32760U, &result), "invalid ready table is reported");

  EXPECT_STATUS(ML3_THERMISTOR_INVALID_ARGUMENT,
    ml3_thermistor_convert(NULL, 32760U, &result), "null config is rejected");
  config = valid_config();
  EXPECT_STATUS(ML3_THERMISTOR_INVALID_ARGUMENT,
    ml3_thermistor_convert(&config, 32760U, NULL), "null conversion output is rejected");
  EXPECT_U32(UINT32_C(0xA5A5A5A5), result.resistance_ohm, "config errors preserve result resistance");
  EXPECT_I32(INT32_C(0x12345678), result.temperature_centic, "config errors preserve result temperature");
}

int main(void) {
  test_ratiometric_resistance_conversion();
  test_resistance_boundaries_and_errors();
  test_table_interpolation_in_both_orders();
  test_interpolation_rejects_bad_tables_and_extrapolation();
  test_conversion_uses_ready_config_and_inclusive_rail_guard();
  test_configuration_readiness_fails_closed();

  if (failures != 0) {
    (void)fprintf(stderr, "ml3_thermistor_test: %d failure(s)\n", failures);
    return 1;
  }

  (void)printf("ml3_thermistor_test: OK\n");
  return 0;
}
