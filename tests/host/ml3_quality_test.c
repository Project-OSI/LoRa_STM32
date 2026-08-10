#include "ml3_measurement.h"
#include "ml3_quality.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      failures += 1;                                                            \
    }                                                                           \
  } while (0)

static ml3_quality_thresholds_t synthetic_thresholds(void)
{
  ml3_quality_thresholds_t thresholds;

  thresholds.zero_ambiguity_guard_uv = UINT64_C(1000);
  thresholds.common_mode_min_uv = INT64_C(10000);
  thresholds.common_mode_max_uv = INT64_C(100000);
  thresholds.v5_min_uv = UINT64_C(4900000);
  thresholds.v5_max_uv = UINT64_C(5100000);
  thresholds.noise_warn_uv = UINT64_C(500);
  thresholds.noise_invalid_uv = UINT64_C(1000);
  thresholds.warmup_drift_warn_uv = UINT64_C(1000);
  thresholds.warmup_drift_invalid_uv = UINT64_C(2000);
  thresholds.vdda_drift_warn_ppm = UINT32_C(2000);
  thresholds.vdda_drift_invalid_ppm = UINT32_C(5000);
  thresholds.die_temp_min_centic = INT32_C(-4000);
  thresholds.die_temp_max_centic = INT32_C(8500);
  return thresholds;
}

static ml3_quality_input_t complete_input(void)
{
  ml3_quality_input_t input;
  size_t index;

  (void)memset(&input, 0, sizeof(input));
  input.valid_cycles = 4U;
  input.burst_cycles = 4U;
  input.has_rail_samples = true;
  input.rail_sample_count = 8U;
  for (index = 0U; index < 8U; ++index) {
    input.hi_samples_uv[index] = INT64_C(500000);
    input.lo_samples_uv[index] = INT64_C(50000);
  }
  input.has_mean_hi_uv = true;
  input.mean_hi_uv = INT64_C(500000);
  input.has_median_diff_uv = true;
  input.median_diff_uv = INT64_C(450000);
  input.has_mean_lo_uv = true;
  input.mean_lo_uv = INT64_C(50000);
  input.has_noise_sd_uv = true;
  input.noise_sd_uv = UINT64_C(100);
  input.has_warmup_drift_uv = true;
  input.warmup_drift_uv = INT64_C(500);
  input.has_vdda_uv = true;
  input.vdda_pre_uv = UINT64_C(1000000);
  input.vdda_post_uv = UINT64_C(1000000);
  input.has_v5_uv = true;
  input.v5_pre_uv = UINT64_C(5000000);
  input.v5_post_uv = UINT64_C(5000000);
  input.has_die_temp_centic = true;
  input.die_temp_centic = INT32_C(2000);
  input.has_calibration_status = true;
  input.calibration_valid = true;
  input.has_thermistor_status = true;
  input.thermistor_valid = true;
  return input;
}

static void test_public_contract(void)
{
  CHECK(ML3_QUALITY_FLAG_ADC_INIT == UINT16_C(0x0001));
  CHECK(ML3_QUALITY_FLAG_ADC_CAL == UINT16_C(0x0002));
  CHECK(ML3_QUALITY_FLAG_ADC_TIMEOUT == UINT16_C(0x0004));
  CHECK(ML3_QUALITY_FLAG_ADC_OVERRUN == UINT16_C(0x0008));
  CHECK(ML3_QUALITY_FLAG_HI_OVER == UINT16_C(0x0010));
  CHECK(ML3_QUALITY_FLAG_LOW_RAIL_CLIPPED == UINT16_C(0x0020));
  CHECK(ML3_QUALITY_FLAG_DIFF_RANGE == UINT16_C(0x0040));
  CHECK(ML3_QUALITY_FLAG_CM_RANGE == UINT16_C(0x0080));
  CHECK(ML3_QUALITY_FLAG_VREF_DRIFT == UINT16_C(0x0100));
  CHECK(ML3_QUALITY_FLAG_V5_LOW == UINT16_C(0x0200));
  CHECK(ML3_QUALITY_FLAG_V5_HIGH == UINT16_C(0x0400));
  CHECK(ML3_QUALITY_FLAG_NOISE_HIGH == UINT16_C(0x0800));
  CHECK(ML3_QUALITY_FLAG_WARMUP_DRIFT == UINT16_C(0x1000));
  CHECK(ML3_QUALITY_FLAG_CAL_INVALID == UINT16_C(0x2000));
  CHECK(ML3_QUALITY_FLAG_TEMP_RANGE == UINT16_C(0x4000));
  CHECK(ML3_QUALITY_FLAG_THERM_FAULT == UINT16_C(0x8000));
  CHECK(ML3_QUALITY_INVALIDATING_MASK == UINT16_C(0x027f));
  CHECK(ML3_QUALITY_HI_MARGIN_UV == UINT64_C(100000));
  CHECK(ML3_QUALITY_DIFF_MIN_UV == INT64_C(-20000));
  CHECK(ML3_QUALITY_DIFF_MAX_UV == INT64_C(1200000));

  CHECK(ML3_QUALITY_STATE_VALID == 0);
  CHECK(ML3_QUALITY_STATE_DEGRADED == 1);
  CHECK(ML3_QUALITY_STATE_INVALID == 2);

  CHECK((uint16_t)ML3_MEASUREMENT_FAULT_ADC_INIT ==
        (uint16_t)ML3_QUALITY_FLAG_ADC_INIT);
  CHECK((uint16_t)ML3_MEASUREMENT_FAULT_ADC_CAL ==
        (uint16_t)ML3_QUALITY_FLAG_ADC_CAL);
  CHECK((uint16_t)ML3_MEASUREMENT_FAULT_ADC_TIMEOUT ==
        (uint16_t)ML3_QUALITY_FLAG_ADC_TIMEOUT);
  CHECK((uint16_t)ML3_MEASUREMENT_FAULT_ADC_OVERRUN ==
        (uint16_t)ML3_QUALITY_FLAG_ADC_OVERRUN);
  CHECK((uint16_t)ML3_MEASUREMENT_FAULT_THERM_FAULT ==
        (uint16_t)ML3_QUALITY_FLAG_THERM_FAULT);
}

static void test_pending_config_preserves_output(void)
{
  ml3_quality_thresholds_t thresholds;
  ml3_quality_thresholds_t before;

  (void)memset(&thresholds, 0xa5, sizeof(thresholds));
  before = thresholds;
  CHECK(ml3_quality_thresholds_from_config(&thresholds) ==
        ML3_QUALITY_STATUS_CONFIG_PENDING);
  CHECK(memcmp(&thresholds, &before, sizeof(thresholds)) == 0);
  CHECK(ml3_quality_thresholds_from_config(NULL) ==
        ML3_QUALITY_STATUS_INVALID_ARGUMENT);
}

static void test_clear_complete_evidence_is_valid(void)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input = complete_input();
  ml3_quality_result_t result;

  (void)memset(&result, 0xa5, sizeof(result));
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(result.flags == 0U);
  CHECK(result.state == ML3_QUALITY_STATE_VALID);
  CHECK(result.valid_cycles == 4U);
  CHECK(result.invalidating_signature == 0U);
  CHECK(result.incomplete_reason == ML3_QUALITY_INCOMPLETE_NONE);
}

static void expect_malformed_thresholds_preserve_output(
  const ml3_quality_thresholds_t* thresholds)
{
  const ml3_quality_input_t input = complete_input();
  ml3_quality_result_t result;
  ml3_quality_result_t before;

  (void)memset(&result, 0x5a, sizeof(result));
  before = result;
  CHECK(ml3_quality_evaluate(&input, thresholds, &result) ==
        ML3_QUALITY_STATUS_INVALID_ARGUMENT);
  CHECK(memcmp(&result, &before, sizeof(result)) == 0);
}

static void test_malformed_thresholds_are_rejected_transactionally(void)
{
  ml3_quality_thresholds_t thresholds = synthetic_thresholds();

  thresholds.zero_ambiguity_guard_uv = 0U;
  expect_malformed_thresholds_preserve_output(&thresholds);

  thresholds = synthetic_thresholds();
  thresholds.common_mode_max_uv = thresholds.common_mode_min_uv;
  expect_malformed_thresholds_preserve_output(&thresholds);

  thresholds = synthetic_thresholds();
  thresholds.common_mode_max_uv = thresholds.common_mode_min_uv - INT64_C(1);
  expect_malformed_thresholds_preserve_output(&thresholds);

  thresholds = synthetic_thresholds();
  thresholds.v5_max_uv = thresholds.v5_min_uv;
  expect_malformed_thresholds_preserve_output(&thresholds);

  thresholds = synthetic_thresholds();
  thresholds.v5_max_uv = thresholds.v5_min_uv - UINT64_C(1);
  expect_malformed_thresholds_preserve_output(&thresholds);

  thresholds = synthetic_thresholds();
  thresholds.noise_warn_uv = 0U;
  expect_malformed_thresholds_preserve_output(&thresholds);

  thresholds = synthetic_thresholds();
  thresholds.noise_invalid_uv = thresholds.noise_warn_uv;
  expect_malformed_thresholds_preserve_output(&thresholds);

  thresholds = synthetic_thresholds();
  thresholds.noise_invalid_uv = thresholds.noise_warn_uv - UINT64_C(1);
  expect_malformed_thresholds_preserve_output(&thresholds);

  thresholds = synthetic_thresholds();
  thresholds.warmup_drift_warn_uv = 0U;
  expect_malformed_thresholds_preserve_output(&thresholds);

  thresholds = synthetic_thresholds();
  thresholds.warmup_drift_invalid_uv = thresholds.warmup_drift_warn_uv;
  expect_malformed_thresholds_preserve_output(&thresholds);

  thresholds = synthetic_thresholds();
  thresholds.warmup_drift_invalid_uv =
    thresholds.warmup_drift_warn_uv - UINT64_C(1);
  expect_malformed_thresholds_preserve_output(&thresholds);

  thresholds = synthetic_thresholds();
  thresholds.vdda_drift_warn_ppm = 0U;
  expect_malformed_thresholds_preserve_output(&thresholds);

  thresholds = synthetic_thresholds();
  thresholds.vdda_drift_invalid_ppm = thresholds.vdda_drift_warn_ppm;
  expect_malformed_thresholds_preserve_output(&thresholds);

  thresholds = synthetic_thresholds();
  thresholds.vdda_drift_invalid_ppm =
    thresholds.vdda_drift_warn_ppm - UINT32_C(1);
  expect_malformed_thresholds_preserve_output(&thresholds);

  thresholds = synthetic_thresholds();
  thresholds.die_temp_max_centic = thresholds.die_temp_min_centic;
  expect_malformed_thresholds_preserve_output(&thresholds);

  thresholds = synthetic_thresholds();
  thresholds.die_temp_max_centic =
    thresholds.die_temp_min_centic - INT32_C(1);
  expect_malformed_thresholds_preserve_output(&thresholds);
}

static void test_cycle_count_boundaries(void)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input = complete_input();
  ml3_quality_result_t result;
  ml3_quality_result_t before;
  size_t index;

  input.valid_cycles = 3U;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(result.state == ML3_QUALITY_STATE_VALID);

  input.valid_cycles = 8U;
  input.burst_cycles = 8U;
  input.rail_sample_count = 16U;
  for (index = 8U; index < 16U; ++index) {
    input.hi_samples_uv[index] = INT64_C(500000);
    input.lo_samples_uv[index] = INT64_C(50000);
  }
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(result.state == ML3_QUALITY_STATE_VALID);

  input = complete_input();
  input.valid_cycles = 2U;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(result.state == ML3_QUALITY_STATE_INVALID);
  CHECK(result.invalidating_signature ==
        ML3_QUALITY_INVALID_REASON_VALID_CYCLES);

  input.valid_cycles = 9U;
  (void)memset(&result, 0x5a, sizeof(result));
  before = result;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_INVALID_ARGUMENT);
  CHECK(memcmp(&result, &before, sizeof(result)) == 0);
}

static void test_fixed_mask_controls_state_and_signature(void)
{
  static const uint16_t invalidating_flags[] = {
    ML3_QUALITY_FLAG_ADC_INIT,
    ML3_QUALITY_FLAG_ADC_CAL,
    ML3_QUALITY_FLAG_ADC_TIMEOUT,
    ML3_QUALITY_FLAG_ADC_OVERRUN,
    ML3_QUALITY_FLAG_HI_OVER,
    ML3_QUALITY_FLAG_LOW_RAIL_CLIPPED,
    ML3_QUALITY_FLAG_DIFF_RANGE,
    ML3_QUALITY_FLAG_V5_LOW
  };
  static const uint16_t warning_flags[] = {
    ML3_QUALITY_FLAG_CM_RANGE,
    ML3_QUALITY_FLAG_VREF_DRIFT,
    ML3_QUALITY_FLAG_V5_HIGH,
    ML3_QUALITY_FLAG_NOISE_HIGH,
    ML3_QUALITY_FLAG_WARMUP_DRIFT,
    ML3_QUALITY_FLAG_CAL_INVALID,
    ML3_QUALITY_FLAG_TEMP_RANGE,
    ML3_QUALITY_FLAG_THERM_FAULT
  };
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input;
  ml3_quality_result_t result;
  size_t index;

  for (index = 0U;
       index < sizeof(invalidating_flags) / sizeof(invalidating_flags[0]);
       ++index) {
    input = complete_input();
    input.seed_flags = invalidating_flags[index];
    CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
          ML3_QUALITY_STATUS_OK);
    CHECK(result.state == ML3_QUALITY_STATE_INVALID);
    CHECK(result.flags == invalidating_flags[index]);
    CHECK(result.invalidating_signature ==
          (uint32_t)invalidating_flags[index]);
  }

  for (index = 0U;
       index < sizeof(warning_flags) / sizeof(warning_flags[0]);
       ++index) {
    input = complete_input();
    input.seed_flags = warning_flags[index];
    CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
          ML3_QUALITY_STATUS_OK);
    CHECK(result.state == ML3_QUALITY_STATE_DEGRADED);
    CHECK(result.flags == warning_flags[index]);
    CHECK(result.invalidating_signature == 0U);
  }

  input = complete_input();
  input.seed_flags = (uint16_t)(ML3_QUALITY_FLAG_ADC_TIMEOUT |
                                ML3_QUALITY_FLAG_NOISE_HIGH);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(result.state == ML3_QUALITY_STATE_INVALID);
  CHECK(result.flags == input.seed_flags);
  CHECK(result.invalidating_signature ==
        (uint32_t)ML3_QUALITY_FLAG_ADC_TIMEOUT);
}

static void test_hard_faults_do_not_require_numeric_evidence(void)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input;
  ml3_quality_result_t result;

  (void)memset(&input, 0, sizeof(input));
  input.seed_flags = ML3_QUALITY_FLAG_ADC_TIMEOUT;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(result.flags == ML3_QUALITY_FLAG_ADC_TIMEOUT);
  CHECK(result.valid_cycles == 0U);
  CHECK(result.state == ML3_QUALITY_STATE_INVALID);
  CHECK(result.incomplete_reason == ML3_QUALITY_INCOMPLETE_NONE);
  CHECK(result.invalidating_signature ==
        ((uint32_t)ML3_QUALITY_FLAG_ADC_TIMEOUT |
         ML3_QUALITY_INVALID_REASON_VALID_CYCLES));

  (void)memset(&input, 0, sizeof(input));
  input.seed_flags = ML3_QUALITY_FLAG_ADC_TIMEOUT;
  input.valid_cycles = 4U;
  input.burst_cycles = 4U;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(result.flags == ML3_QUALITY_FLAG_ADC_TIMEOUT);
  CHECK(result.state == ML3_QUALITY_STATE_INVALID);
  CHECK(result.incomplete_reason == ML3_QUALITY_INCOMPLETE_NONE);

  (void)memset(&input, 0, sizeof(input));
  input.valid_cycles = 0U;
  input.burst_cycles = 0U;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(result.flags == 0U);
  CHECK(result.state == ML3_QUALITY_STATE_INVALID);
  CHECK(result.invalidating_signature ==
        ML3_QUALITY_INVALID_REASON_VALID_CYCLES);
  CHECK(result.incomplete_reason == ML3_QUALITY_INCOMPLETE_NONE);
}

static void test_missing_evidence_fails_closed_without_fabricated_flag(void)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input = complete_input();
  ml3_quality_result_t result;

  input.has_mean_hi_uv = false;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_INCOMPLETE);
  CHECK(result.flags == 0U);
  CHECK(result.valid_cycles == 4U);
  CHECK(result.state == ML3_QUALITY_STATE_INVALID);
  CHECK(result.incomplete_reason ==
        ML3_QUALITY_INCOMPLETE_MEASUREMENT_EVIDENCE);
  CHECK(result.invalidating_signature ==
        ML3_QUALITY_INVALID_REASON_INCOMPLETE_EVIDENCE);

  input = complete_input();
  input.vdda_pre_uv = 0U;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_INCOMPLETE);
  CHECK(result.flags == 0U);
  CHECK(result.state == ML3_QUALITY_STATE_INVALID);
  CHECK(result.invalidating_signature ==
        ML3_QUALITY_INVALID_REASON_INCOMPLETE_EVIDENCE);
}

static void test_malformed_input_preserves_output(void)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input = complete_input();
  ml3_quality_result_t result;
  ml3_quality_result_t before;

  input.valid_cycles = 4U;
  input.burst_cycles = 3U;
  (void)memset(&result, 0x5a, sizeof(result));
  before = result;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_INVALID_ARGUMENT);
  CHECK(memcmp(&result, &before, sizeof(result)) == 0);

  input = complete_input();
  input.rail_sample_count = 7U;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_INVALID_ARGUMENT);
  CHECK(memcmp(&result, &before, sizeof(result)) == 0);

  input = complete_input();
  CHECK(ml3_quality_evaluate(NULL, &thresholds, &result) ==
        ML3_QUALITY_STATUS_INVALID_ARGUMENT);
  CHECK(memcmp(&result, &before, sizeof(result)) == 0);
  CHECK(ml3_quality_evaluate(&input, NULL, &result) ==
        ML3_QUALITY_STATUS_INVALID_ARGUMENT);
  CHECK(memcmp(&result, &before, sizeof(result)) == 0);
  CHECK(ml3_quality_evaluate(&input, &thresholds, NULL) ==
        ML3_QUALITY_STATUS_INVALID_ARGUMENT);
}

static void test_hi_over_uses_pre_vdda_inclusively(void)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input = complete_input();
  ml3_quality_result_t result;

  input.mean_hi_uv = INT64_C(899999);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_HI_OVER) == 0U);

  input.mean_hi_uv = INT64_C(900000);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_HI_OVER) != 0U);
  CHECK(result.state == ML3_QUALITY_STATE_INVALID);

  input.vdda_post_uv = UINT64_C(2000000);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_HI_OVER) != 0U);

  input.mean_hi_uv = INT64_C(899999);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_HI_OVER) == 0U);
}

static void test_differential_and_common_mode_endpoints(void)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input = complete_input();
  ml3_quality_result_t result;

  input.median_diff_uv = ML3_QUALITY_DIFF_MIN_UV;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_DIFF_RANGE) == 0U);

  /* The owner-approved 1.2 V ceiling is accepted; the next microvolt is not. */
  input.median_diff_uv = INT64_C(1200000);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_DIFF_RANGE) == 0U);

  input.median_diff_uv = ML3_QUALITY_DIFF_MIN_UV - INT64_C(1);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_DIFF_RANGE) != 0U);
  CHECK(result.state == ML3_QUALITY_STATE_INVALID);

  input.median_diff_uv = INT64_C(1200001);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_DIFF_RANGE) != 0U);

  input = complete_input();
  input.mean_lo_uv = thresholds.common_mode_min_uv;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_CM_RANGE) == 0U);

  input.mean_lo_uv = thresholds.common_mode_max_uv;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_CM_RANGE) == 0U);

  input.mean_lo_uv = thresholds.common_mode_min_uv - INT64_C(1);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_CM_RANGE) != 0U);
  CHECK(result.state == ML3_QUALITY_STATE_DEGRADED);

  input.mean_lo_uv = thresholds.common_mode_max_uv + INT64_C(1);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_CM_RANGE) != 0U);
}

static void test_low_rail_counts_each_input_separately(void)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input = complete_input();
  ml3_quality_result_t result;
  size_t index;

  for (index = 0U; index < 4U; ++index) {
    input.hi_samples_uv[index] =
      (int64_t)thresholds.zero_ambiguity_guard_uv;
  }
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_LOW_RAIL_CLIPPED) != 0U);
  CHECK(result.state == ML3_QUALITY_STATE_INVALID);

  input = complete_input();
  for (index = 0U; index < 4U; ++index) {
    input.lo_samples_uv[index] =
      (int64_t)thresholds.zero_ambiguity_guard_uv;
  }
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_LOW_RAIL_CLIPPED) != 0U);

  input = complete_input();
  for (index = 0U; index < 3U; ++index) {
    input.hi_samples_uv[index] =
      (int64_t)thresholds.zero_ambiguity_guard_uv;
    input.lo_samples_uv[index] =
      (int64_t)thresholds.zero_ambiguity_guard_uv;
  }
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_LOW_RAIL_CLIPPED) == 0U);

  input.hi_samples_uv[3] =
    (int64_t)thresholds.zero_ambiguity_guard_uv - INT64_C(1);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_LOW_RAIL_CLIPPED) != 0U);
}

static void test_v5_and_die_temperature_endpoints(void)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input = complete_input();
  ml3_quality_result_t result;

  input.v5_pre_uv = thresholds.v5_min_uv;
  input.v5_post_uv = thresholds.v5_max_uv;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags &
         (ML3_QUALITY_FLAG_V5_LOW | ML3_QUALITY_FLAG_V5_HIGH)) == 0U);

  input.v5_pre_uv = thresholds.v5_min_uv - UINT64_C(1);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_V5_LOW) != 0U);
  CHECK(result.state == ML3_QUALITY_STATE_INVALID);

  input = complete_input();
  input.v5_post_uv = thresholds.v5_max_uv + UINT64_C(1);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_V5_HIGH) != 0U);
  CHECK(result.state == ML3_QUALITY_STATE_DEGRADED);

  input = complete_input();
  input.v5_post_uv = thresholds.v5_min_uv - UINT64_C(1);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_V5_LOW) != 0U);

  input = complete_input();
  input.v5_pre_uv = thresholds.v5_max_uv + UINT64_C(1);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_V5_HIGH) != 0U);

  input = complete_input();
  input.die_temp_centic = thresholds.die_temp_min_centic;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_TEMP_RANGE) == 0U);

  input.die_temp_centic = thresholds.die_temp_max_centic;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_TEMP_RANGE) == 0U);

  input.die_temp_centic = thresholds.die_temp_min_centic - INT32_C(1);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_TEMP_RANGE) != 0U);
  CHECK(result.state == ML3_QUALITY_STATE_DEGRADED);

  input.die_temp_centic = thresholds.die_temp_max_centic + INT32_C(1);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_TEMP_RANGE) != 0U);
}

static void test_known_calibration_and_thermistor_faults_degrade(void)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input = complete_input();
  ml3_quality_result_t result;

  input.calibration_valid = false;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_CAL_INVALID) != 0U);
  CHECK(result.state == ML3_QUALITY_STATE_DEGRADED);

  input = complete_input();
  input.thermistor_valid = false;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_THERM_FAULT) != 0U);
  CHECK(result.state == ML3_QUALITY_STATE_DEGRADED);
}

static void test_noise_thresholds_use_strict_boundaries(void)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input = complete_input();
  ml3_quality_result_t result;

  input.noise_sd_uv = thresholds.noise_warn_uv;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_NOISE_HIGH) == 0U);
  CHECK(result.state == ML3_QUALITY_STATE_VALID);

  input.noise_sd_uv = thresholds.noise_warn_uv + UINT64_C(1);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_NOISE_HIGH) != 0U);
  CHECK(result.state == ML3_QUALITY_STATE_DEGRADED);
  CHECK(result.invalidating_signature == 0U);

  input.noise_sd_uv = thresholds.noise_invalid_uv;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_NOISE_HIGH) != 0U);
  CHECK(result.state == ML3_QUALITY_STATE_DEGRADED);
  CHECK(result.invalidating_signature == 0U);

  input.noise_sd_uv = thresholds.noise_invalid_uv + UINT64_C(1);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(result.state == ML3_QUALITY_STATE_INVALID);
  CHECK(result.invalidating_signature ==
        ML3_QUALITY_INVALID_REASON_NOISE);
}

static void test_warmup_drift_uses_safe_absolute_strict_boundaries(void)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input = complete_input();
  ml3_quality_result_t result;

  input.warmup_drift_uv = (int64_t)thresholds.warmup_drift_warn_uv;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_WARMUP_DRIFT) == 0U);

  input.warmup_drift_uv =
    -(int64_t)(thresholds.warmup_drift_warn_uv + UINT64_C(1));
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_WARMUP_DRIFT) != 0U);
  CHECK(result.state == ML3_QUALITY_STATE_DEGRADED);
  CHECK(result.invalidating_signature == 0U);

  input.warmup_drift_uv =
    -(int64_t)thresholds.warmup_drift_invalid_uv;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(result.state == ML3_QUALITY_STATE_DEGRADED);
  CHECK(result.invalidating_signature == 0U);

  input.warmup_drift_uv =
    (int64_t)(thresholds.warmup_drift_invalid_uv + UINT64_C(1));
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(result.state == ML3_QUALITY_STATE_INVALID);
  CHECK(result.invalidating_signature ==
        ML3_QUALITY_INVALID_REASON_WARMUP_DRIFT);

  input.warmup_drift_uv = INT64_MIN;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(result.state == ML3_QUALITY_STATE_INVALID);
  CHECK(result.invalidating_signature ==
        ML3_QUALITY_INVALID_REASON_WARMUP_DRIFT);
}

static void expect_vdda_case(
  uint64_t post_uv,
  bool expect_flag,
  ml3_quality_state_t expected_state,
  uint32_t expected_signature)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input = complete_input();
  ml3_quality_result_t result;

  input.vdda_post_uv = post_uv;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(((result.flags & ML3_QUALITY_FLAG_VREF_DRIFT) != 0U) ==
        expect_flag);
  CHECK(result.state == expected_state);
  CHECK(result.invalidating_signature == expected_signature);
}

static void test_vdda_drift_uses_exact_strict_cross_products(void)
{
  expect_vdda_case(
    UINT64_C(1002000),
    false,
    ML3_QUALITY_STATE_VALID,
    0U);
  expect_vdda_case(
    UINT64_C(1002001),
    true,
    ML3_QUALITY_STATE_DEGRADED,
    0U);
  expect_vdda_case(
    UINT64_C(1005000),
    true,
    ML3_QUALITY_STATE_DEGRADED,
    0U);
  expect_vdda_case(
    UINT64_C(1005001),
    true,
    ML3_QUALITY_STATE_INVALID,
    ML3_QUALITY_INVALID_REASON_VREF_DRIFT);

  expect_vdda_case(
    UINT64_C(998000),
    false,
    ML3_QUALITY_STATE_VALID,
    0U);
  expect_vdda_case(
    UINT64_C(997999),
    true,
    ML3_QUALITY_STATE_DEGRADED,
    0U);
  expect_vdda_case(
    UINT64_C(995000),
    true,
    ML3_QUALITY_STATE_DEGRADED,
    0U);
  expect_vdda_case(
    UINT64_C(994999),
    true,
    ML3_QUALITY_STATE_INVALID,
    ML3_QUALITY_INVALID_REASON_VREF_DRIFT);
}

static void test_vdda_cross_products_do_not_overflow(void)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input = complete_input();
  ml3_quality_result_t result;
  const uint64_t exact_or_below_delta = UINT64_MAX / UINT64_C(500);

  input.vdda_pre_uv = UINT64_MAX;
  input.vdda_post_uv = UINT64_MAX - exact_or_below_delta;
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_VREF_DRIFT) == 0U);

  input.vdda_post_uv = UINT64_MAX - (exact_or_below_delta + UINT64_C(1));
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK((result.flags & ML3_QUALITY_FLAG_VREF_DRIFT) != 0U);
  CHECK(result.state == ML3_QUALITY_STATE_DEGRADED);
}

static void test_warning_flag_churn_does_not_change_invalid_signature(void)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input = complete_input();
  ml3_quality_result_t result;

  input.noise_sd_uv = thresholds.noise_invalid_uv + UINT64_C(1);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(result.invalidating_signature ==
        ML3_QUALITY_INVALID_REASON_NOISE);

  input.seed_flags = (uint16_t)(ML3_QUALITY_FLAG_CM_RANGE |
                                ML3_QUALITY_FLAG_V5_HIGH |
                                ML3_QUALITY_FLAG_THERM_FAULT);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(result.invalidating_signature ==
        ML3_QUALITY_INVALID_REASON_NOISE);

  input.warmup_drift_uv =
    (int64_t)(thresholds.warmup_drift_invalid_uv + UINT64_C(1));
  CHECK(ml3_quality_evaluate(&input, &thresholds, &result) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(result.invalidating_signature ==
        (ML3_QUALITY_INVALID_REASON_NOISE |
         ML3_QUALITY_INVALID_REASON_WARMUP_DRIFT));
}

int main(void)
{
  test_public_contract();
  test_pending_config_preserves_output();
  test_clear_complete_evidence_is_valid();
  test_malformed_thresholds_are_rejected_transactionally();
  test_cycle_count_boundaries();
  test_fixed_mask_controls_state_and_signature();
  test_hard_faults_do_not_require_numeric_evidence();
  test_missing_evidence_fails_closed_without_fabricated_flag();
  test_malformed_input_preserves_output();
  test_hi_over_uses_pre_vdda_inclusively();
  test_differential_and_common_mode_endpoints();
  test_low_rail_counts_each_input_separately();
  test_v5_and_die_temperature_endpoints();
  test_known_calibration_and_thermistor_faults_degrade();
  test_noise_thresholds_use_strict_boundaries();
  test_warmup_drift_uses_safe_absolute_strict_boundaries();
  test_vdda_drift_uses_exact_strict_cross_products();
  test_vdda_cross_products_do_not_overflow();
  test_warning_flag_churn_does_not_change_invalid_signature();

  if (failures != 0) {
    (void)fprintf(stderr, "ml3 quality: %d failure(s)\n", failures);
    return 1;
  }
  (void)puts("ml3 quality: OK");
  return 0;
}
