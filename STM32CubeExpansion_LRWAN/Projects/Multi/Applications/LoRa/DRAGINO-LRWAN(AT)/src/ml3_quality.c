#include "ml3_quality.h"

#include "ml3_config.h"

#include <stddef.h>

static bool ml3_quality_thresholds_valid(
  const ml3_quality_thresholds_t* thresholds)
{
  return (thresholds->zero_ambiguity_guard_uv != 0U) &&
         (thresholds->common_mode_min_uv < thresholds->common_mode_max_uv) &&
         (thresholds->v5_min_uv < thresholds->v5_max_uv) &&
         (thresholds->noise_warn_uv != 0U) &&
         (thresholds->noise_warn_uv < thresholds->noise_invalid_uv) &&
         (thresholds->warmup_drift_warn_uv != 0U) &&
         (thresholds->warmup_drift_warn_uv <
          thresholds->warmup_drift_invalid_uv) &&
         (thresholds->vdda_drift_warn_ppm != 0U) &&
         (thresholds->vdda_drift_warn_ppm <
          thresholds->vdda_drift_invalid_ppm) &&
         (thresholds->die_temp_min_centic < thresholds->die_temp_max_centic);
}

static bool ml3_quality_input_shape_valid(const ml3_quality_input_t* input)
{
  uint8_t expected_samples;

  if ((input->valid_cycles > ML3_QUALITY_MAX_BURST_CYCLES) ||
      (input->burst_cycles > ML3_QUALITY_MAX_BURST_CYCLES) ||
      (input->valid_cycles > input->burst_cycles)) {
    return false;
  }
  if (!input->has_rail_samples) {
    return true;
  }

  expected_samples = (uint8_t)(2U * input->burst_cycles);
  return (input->burst_cycles != 0U) &&
         (input->rail_sample_count == expected_samples);
}

static bool ml3_quality_has_complete_evidence(const ml3_quality_input_t* input)
{
  return input->has_rail_samples &&
         input->has_mean_hi_uv &&
         input->has_median_diff_uv &&
         input->has_mean_lo_uv &&
         input->has_noise_sd_uv &&
         input->has_warmup_drift_uv &&
         input->has_vdda_uv &&
         (input->vdda_pre_uv != 0U) &&
         input->has_v5_uv &&
         input->has_die_temp_centic &&
         input->has_calibration_status &&
         input->has_thermistor_status;
}

static bool ml3_quality_hi_over(const ml3_quality_input_t* input)
{
  if (!input->has_mean_hi_uv || !input->has_vdda_uv ||
      (input->vdda_pre_uv == 0U) || (input->mean_hi_uv < 0)) {
    return false;
  }
  if (input->vdda_pre_uv <= ML3_QUALITY_HI_MARGIN_UV) {
    return true;
  }
  return (uint64_t)input->mean_hi_uv >=
         (input->vdda_pre_uv - ML3_QUALITY_HI_MARGIN_UV);
}

static bool ml3_quality_at_or_below_guard(
  int64_t sample_uv,
  uint64_t guard_uv)
{
  return (sample_uv <= 0) || ((uint64_t)sample_uv <= guard_uv);
}

static bool ml3_quality_low_rail_clipped(
  const ml3_quality_input_t* input,
  uint64_t guard_uv)
{
  uint8_t hi_count = 0U;
  uint8_t lo_count = 0U;
  uint8_t index;

  if (!input->has_rail_samples) {
    return false;
  }
  for (index = 0U; index < input->rail_sample_count; ++index) {
    if (ml3_quality_at_or_below_guard(input->hi_samples_uv[index], guard_uv)) {
      hi_count = (uint8_t)(hi_count + 1U);
    }
    if (ml3_quality_at_or_below_guard(input->lo_samples_uv[index], guard_uv)) {
      lo_count = (uint8_t)(lo_count + 1U);
    }
  }

  /* Code zero cannot distinguish a true zero from a clipped negative input. */
  return (hi_count >= input->burst_cycles) ||
         (lo_count >= input->burst_cycles);
}

static uint64_t ml3_quality_abs_i64(int64_t value)
{
  if (value >= 0) {
    return (uint64_t)value;
  }
  return UINT64_C(0) - (uint64_t)value;
}

typedef struct {
  uint64_t high;
  uint64_t low;
} ml3_quality_u96_t;

static ml3_quality_u96_t ml3_quality_multiply_u64_u32(
  uint64_t left,
  uint32_t right)
{
  const uint64_t low_product =
    (left & UINT64_C(0xffffffff)) * (uint64_t)right;
  const uint64_t high_product = (left >> 32) * (uint64_t)right;
  const uint64_t shifted_high = high_product << 32;
  ml3_quality_u96_t product;

  product.low = low_product + shifted_high;
  product.high = (high_product >> 32) +
                 ((product.low < low_product) ? UINT64_C(1) : UINT64_C(0));
  return product;
}

static bool ml3_quality_u96_greater(
  ml3_quality_u96_t left,
  ml3_quality_u96_t right)
{
  return (left.high > right.high) ||
         ((left.high == right.high) && (left.low > right.low));
}

static bool ml3_quality_vdda_drift_exceeds(
  uint64_t pre_uv,
  uint64_t post_uv,
  uint32_t threshold_ppm)
{
  const uint64_t difference = (pre_uv >= post_uv) ? (pre_uv - post_uv)
                                                   : (post_uv - pre_uv);
  const ml3_quality_u96_t scaled_difference =
    ml3_quality_multiply_u64_u32(difference, UINT32_C(1000000));
  const ml3_quality_u96_t scaled_threshold =
    ml3_quality_multiply_u64_u32(pre_uv, threshold_ppm);

  return ml3_quality_u96_greater(scaled_difference, scaled_threshold);
}

ml3_quality_status_t ml3_quality_thresholds_from_config(
  ml3_quality_thresholds_t* out)
{
  ml3_quality_thresholds_t candidate = {0};

  if (out == NULL) {
    return ML3_QUALITY_STATUS_INVALID_ARGUMENT;
  }

  if (!(ML3_CONFIG_ZERO_AMBIGUITY_GUARD_READY &&
        ML3_CONFIG_CM_RANGE_READY &&
        ML3_CONFIG_V5_LIMITS_READY &&
        ML3_CONFIG_NOISE_WARN_READY &&
        ML3_CONFIG_NOISE_INVALID_READY &&
        ML3_CONFIG_WARMUP_DRIFT_WARN_READY &&
        ML3_CONFIG_WARMUP_DRIFT_INVALID_READY &&
        ML3_CONFIG_VDDA_DRIFT_WARN_READY &&
        ML3_CONFIG_VDDA_DRIFT_INVALID_READY &&
        ML3_CONFIG_DIE_TEMP_RANGE_READY)) {
    return ML3_QUALITY_STATUS_CONFIG_PENDING;
  }

  candidate.zero_ambiguity_guard_uv =
    (uint64_t)ML3_CONFIG_ZERO_AMBIGUITY_GUARD_MV * UINT64_C(1000);
  candidate.common_mode_min_uv =
    (int64_t)ML3_CONFIG_CM_RANGE_MIN_MV * INT64_C(1000);
  candidate.common_mode_max_uv =
    (int64_t)ML3_CONFIG_CM_RANGE_MAX_MV * INT64_C(1000);
  candidate.v5_min_uv =
    (uint64_t)ML3_CONFIG_V5_MINIMUM_MV * UINT64_C(1000);
  candidate.v5_max_uv =
    (uint64_t)ML3_CONFIG_V5_MAXIMUM_MV * UINT64_C(1000);
  candidate.noise_warn_uv = (uint64_t)ML3_CONFIG_NOISE_WARN_UV;
  candidate.noise_invalid_uv = (uint64_t)ML3_CONFIG_NOISE_INVALID_UV;
  candidate.warmup_drift_warn_uv =
    (uint64_t)ML3_CONFIG_WARMUP_DRIFT_WARN_UV;
  candidate.warmup_drift_invalid_uv =
    (uint64_t)ML3_CONFIG_WARMUP_DRIFT_INVALID_UV;
  candidate.vdda_drift_warn_ppm =
    (uint32_t)ML3_CONFIG_VDDA_DRIFT_WARN_PPM;
  candidate.vdda_drift_invalid_ppm =
    (uint32_t)ML3_CONFIG_VDDA_DRIFT_INVALID_PPM;
  candidate.die_temp_min_centic =
    (int32_t)ML3_CONFIG_DIE_TEMP_MIN_CENTIC;
  candidate.die_temp_max_centic =
    (int32_t)ML3_CONFIG_DIE_TEMP_MAX_CENTIC;

  if (!ml3_quality_thresholds_valid(&candidate)) {
    return ML3_QUALITY_STATUS_INVALID_ARGUMENT;
  }
  *out = candidate;
  return ML3_QUALITY_STATUS_OK;
}

ml3_quality_status_t ml3_quality_evaluate(
  const ml3_quality_input_t* input,
  const ml3_quality_thresholds_t* thresholds,
  ml3_quality_result_t* out)
{
  ml3_quality_result_t candidate = {0};
  uint32_t dynamic_signature = 0U;
  uint64_t absolute_warmup_drift_uv;

  if ((input == NULL) || (thresholds == NULL) || (out == NULL)) {
    return ML3_QUALITY_STATUS_INVALID_ARGUMENT;
  }
  if (!ml3_quality_thresholds_valid(thresholds)) {
    return ML3_QUALITY_STATUS_INVALID_ARGUMENT;
  }
  if (!ml3_quality_input_shape_valid(input)) {
    return ML3_QUALITY_STATUS_INVALID_ARGUMENT;
  }

  candidate.flags = input->seed_flags;
  candidate.valid_cycles = input->valid_cycles;
  candidate.invalidating_signature =
    (uint32_t)(input->seed_flags & ML3_QUALITY_INVALIDATING_MASK);
  if (input->valid_cycles < 3U) {
    candidate.invalidating_signature |=
      ML3_QUALITY_INVALID_REASON_VALID_CYCLES;
  }
  if (!ml3_quality_has_complete_evidence(input) &&
      (candidate.invalidating_signature == 0U)) {
    candidate.state = ML3_QUALITY_STATE_INVALID;
    candidate.invalidating_signature =
      ML3_QUALITY_INVALID_REASON_INCOMPLETE_EVIDENCE;
    candidate.incomplete_reason =
      ML3_QUALITY_INCOMPLETE_MEASUREMENT_EVIDENCE;
    *out = candidate;
    return ML3_QUALITY_STATUS_INCOMPLETE;
  }

  if (ml3_quality_hi_over(input)) {
    candidate.flags |= ML3_QUALITY_FLAG_HI_OVER;
  }
  if (ml3_quality_low_rail_clipped(
        input,
        thresholds->zero_ambiguity_guard_uv)) {
    candidate.flags |= ML3_QUALITY_FLAG_LOW_RAIL_CLIPPED;
  }
  if (input->has_median_diff_uv &&
      ((input->median_diff_uv < ML3_QUALITY_DIFF_MIN_UV) ||
       (input->median_diff_uv > ML3_QUALITY_DIFF_MAX_UV))) {
    candidate.flags |= ML3_QUALITY_FLAG_DIFF_RANGE;
  }
  if (input->has_mean_lo_uv &&
      ((input->mean_lo_uv < thresholds->common_mode_min_uv) ||
       (input->mean_lo_uv > thresholds->common_mode_max_uv))) {
    candidate.flags |= ML3_QUALITY_FLAG_CM_RANGE;
  }
  if (input->has_v5_uv &&
      ((input->v5_pre_uv < thresholds->v5_min_uv) ||
       (input->v5_post_uv < thresholds->v5_min_uv))) {
    candidate.flags |= ML3_QUALITY_FLAG_V5_LOW;
  }
  if (input->has_v5_uv &&
      ((input->v5_pre_uv > thresholds->v5_max_uv) ||
       (input->v5_post_uv > thresholds->v5_max_uv))) {
    candidate.flags |= ML3_QUALITY_FLAG_V5_HIGH;
  }
  if (input->has_die_temp_centic &&
      ((input->die_temp_centic < thresholds->die_temp_min_centic) ||
       (input->die_temp_centic > thresholds->die_temp_max_centic))) {
    candidate.flags |= ML3_QUALITY_FLAG_TEMP_RANGE;
  }
  if (input->has_calibration_status && !input->calibration_valid) {
    candidate.flags |= ML3_QUALITY_FLAG_CAL_INVALID;
  }
  if (input->has_thermistor_status && !input->thermistor_valid) {
    candidate.flags |= ML3_QUALITY_FLAG_THERM_FAULT;
  }
  if (input->has_noise_sd_uv &&
      (input->noise_sd_uv > thresholds->noise_warn_uv)) {
    candidate.flags |= ML3_QUALITY_FLAG_NOISE_HIGH;
    if (input->noise_sd_uv > thresholds->noise_invalid_uv) {
      dynamic_signature |= ML3_QUALITY_INVALID_REASON_NOISE;
    }
  }
  if (input->has_warmup_drift_uv) {
    absolute_warmup_drift_uv =
      ml3_quality_abs_i64(input->warmup_drift_uv);
    if (absolute_warmup_drift_uv > thresholds->warmup_drift_warn_uv) {
      candidate.flags |= ML3_QUALITY_FLAG_WARMUP_DRIFT;
      if (absolute_warmup_drift_uv >
          thresholds->warmup_drift_invalid_uv) {
        dynamic_signature |= ML3_QUALITY_INVALID_REASON_WARMUP_DRIFT;
      }
    }
  }
  if (input->has_vdda_uv && (input->vdda_pre_uv != 0U) &&
      ml3_quality_vdda_drift_exceeds(
        input->vdda_pre_uv,
        input->vdda_post_uv,
        thresholds->vdda_drift_warn_ppm)) {
    candidate.flags |= ML3_QUALITY_FLAG_VREF_DRIFT;
    if (ml3_quality_vdda_drift_exceeds(
          input->vdda_pre_uv,
          input->vdda_post_uv,
          thresholds->vdda_drift_invalid_ppm)) {
      dynamic_signature |= ML3_QUALITY_INVALID_REASON_VREF_DRIFT;
    }
  }
  candidate.invalidating_signature =
    (uint32_t)(candidate.flags & ML3_QUALITY_INVALIDATING_MASK) |
    dynamic_signature;
  if (input->valid_cycles < 3U) {
    candidate.invalidating_signature |=
      ML3_QUALITY_INVALID_REASON_VALID_CYCLES;
  }
  if (candidate.invalidating_signature != 0U) {
    candidate.state = ML3_QUALITY_STATE_INVALID;
  } else if (candidate.flags != 0U) {
    candidate.state = ML3_QUALITY_STATE_DEGRADED;
  } else {
    candidate.state = ML3_QUALITY_STATE_VALID;
  }
  candidate.incomplete_reason = ML3_QUALITY_INCOMPLETE_NONE;
  *out = candidate;
  return ML3_QUALITY_STATUS_OK;
}

enum { ML3_QUALITY_TRANSLATION_UNIT = 0 };
