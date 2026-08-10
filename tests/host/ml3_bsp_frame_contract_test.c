/* SPDX-License-Identifier: MIT */
/*
 * bsp.c (the target ADC adapter) is not host-compilable: it pulls in the
 * STM32 HAL/CMSIS/LoRa vendor tree and, via
 * ml3_target_die_temp_centic, dereferences fixed flash calibration
 * addresses that do not exist in a host process. It is exercised on real
 * ARM cross-compiled bytes by tests/host/ml3_target_integration_contract.sh
 * instead (see task-F1-report.md for what that contract now pins).
 *
 * This file instead pins the *contract* bsp.c's redesigned
 * ml3_target_fill_quality_input / ml3_target_on_process /
 * ml3_target_on_build_payload must uphold: given the exact input shapes
 * those functions now construct from an ml3_measurement_result_t (reduced
 * valid-cycle count passed through unmodified; absent statistics mapped to
 * unavailable fields instead of an all-or-nothing rejection), the
 * unmodified, protected ml3_quality.c / ml3_payload.c modules must still
 * transmit a well-formed frame rather than have the caller drop it. It
 * links the real objects for those two modules, so every input structure
 * shape below is real, host-executed evidence for the redesigned bsp.c
 * logic - not a re-implementation of it.
 *
 * Thresholds for the no-suppression scenarios below are a fixed synthetic
 * baseline, independent of ml3_config.h: that contract must hold
 * regardless of what the eventual threshold values are. The Step 3 trial
 * decision itself - noise/drift/die-temperature thresholds set beyond any
 * physically plausible reading so they never fire, while the fixed checks
 * (cycle floor, ADC fault flags, V5 range) keep functioning - is pinned
 * separately below by reading ml3_config.h's actual values directly.
 */
#include "ml3_config.h"
#include "ml3_payload.h"
#include "ml3_quality.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                          \
      (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      failures += 1;                                                            \
    }                                                                           \
  } while (0)

static uint16_t read_u16_be(const uint8_t* input)
{
  return (uint16_t)(((uint16_t)input[0] << 8U) | input[1]);
}

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

/* Mirrors ml3_quality_thresholds_from_config's candidate construction,
 * without the ML3_CONFIG_*_READY gate (that gate is exercised elsewhere;
 * this is about whether the trial's numeric values are actually
 * permissive for a physically plausible reading, and whether the checks
 * the trial deliberately left active still fire). */
static ml3_quality_thresholds_t config_thresholds(void)
{
  ml3_quality_thresholds_t thresholds;

  thresholds.zero_ambiguity_guard_uv =
    (uint64_t)ML3_CONFIG_ZERO_AMBIGUITY_GUARD_MV * UINT64_C(1000);
  thresholds.common_mode_min_uv =
    (int64_t)ML3_CONFIG_CM_RANGE_MIN_MV * INT64_C(1000);
  thresholds.common_mode_max_uv =
    (int64_t)ML3_CONFIG_CM_RANGE_MAX_MV * INT64_C(1000);
  thresholds.v5_min_uv = (uint64_t)ML3_CONFIG_V5_MINIMUM_MV * UINT64_C(1000);
  thresholds.v5_max_uv = (uint64_t)ML3_CONFIG_V5_MAXIMUM_MV * UINT64_C(1000);
  thresholds.noise_warn_uv = (uint64_t)ML3_CONFIG_NOISE_WARN_UV;
  thresholds.noise_invalid_uv = (uint64_t)ML3_CONFIG_NOISE_INVALID_UV;
  thresholds.warmup_drift_warn_uv = (uint64_t)ML3_CONFIG_WARMUP_DRIFT_WARN_UV;
  thresholds.warmup_drift_invalid_uv =
    (uint64_t)ML3_CONFIG_WARMUP_DRIFT_INVALID_UV;
  thresholds.vdda_drift_warn_ppm = (uint32_t)ML3_CONFIG_VDDA_DRIFT_WARN_PPM;
  thresholds.vdda_drift_invalid_ppm =
    (uint32_t)ML3_CONFIG_VDDA_DRIFT_INVALID_PPM;
  thresholds.die_temp_min_centic = (int32_t)ML3_CONFIG_DIE_TEMP_MIN_CENTIC;
  thresholds.die_temp_max_centic = (int32_t)ML3_CONFIG_DIE_TEMP_MAX_CENTIC;
  return thresholds;
}

/* A plausible, unremarkable reading shared by every scenario below: rail
 * samples clear of the zero-ambiguity guard, mean/median within the
 * configured common-mode window, VDDA/V5 mid-range, no drift between pre
 * and post reference reads. */
static void fill_common_rail_samples(ml3_quality_input_t* input)
{
  size_t index;

  input->has_rail_samples = true;
  input->rail_sample_count = 8U;
  for (index = 0U; index < 8U; ++index) {
    input->hi_samples_uv[index] = INT64_C(500000);
    input->lo_samples_uv[index] = INT64_C(50000);
  }
  input->has_vdda_uv = true;
  input->vdda_pre_uv = UINT64_C(3000000);
  input->vdda_post_uv = UINT64_C(3000000);
  input->has_v5_uv = true;
  input->v5_pre_uv = UINT64_C(5000000);
  input->v5_post_uv = UINT64_C(5000000);
  input->has_calibration_status = true;
  input->calibration_valid = true;
  input->has_thermistor_status = true;
  input->thermistor_valid = true;
}

/*
 * (a) A 3-of-4 valid-cycle reading (one recoverable per-cycle ABBA fault,
 * commit 8202ae8) is a GOOD reading: bsp.c passes valid_cycles=3 straight
 * through with full statistics (compute_uv_stats populates them once
 * valid_count >= 3) and no seed fault flags (the measurement engine only
 * raises fault flags when the burst falls *below* the floor). It must
 * evaluate VALID, not be penalized for the reduced count, and must reach
 * a well-formed transmitted frame carrying valid_cycle_count == 3.
 */
static void test_reduced_valid_cycles_is_a_good_reading(void)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input;
  ml3_quality_result_t quality;
  ml3_payload_routine_t payload;
  uint8_t frame[ML3_PAYLOAD_ROUTINE_LENGTH];
  size_t frame_length = 0U;

  (void)memset(&input, 0, sizeof(input));
  input.seed_flags = 0U;
  input.valid_cycles = 3U;
  input.burst_cycles = 4U;
  fill_common_rail_samples(&input);
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
  input.has_die_temp_centic = true;
  input.die_temp_centic = INT32_C(3000);

  CHECK(ml3_quality_evaluate(&input, &thresholds, &quality) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(quality.state == ML3_QUALITY_STATE_VALID);
  CHECK(quality.flags == 0U);
  CHECK(quality.valid_cycles == 3U);

  (void)memset(&payload, 0, sizeof(payload));
  payload.status_flags = quality.flags;
  payload.sequence = UINT16_C(7);
  payload.mean_hi_available = true;
  payload.mean_hi_uncalibrated_uv = input.mean_hi_uv;
  payload.mean_lo_available = true;
  payload.mean_lo_uncalibrated_uv = input.mean_lo_uv;
  payload.vdda_available = true;
  payload.vdda_uv = (int64_t)input.vdda_pre_uv;
  payload.v5_available = true;
  payload.v5_uv = (int64_t)input.v5_pre_uv;
  payload.noise_available = true;
  payload.noise_uv = (int64_t)input.noise_sd_uv;
  payload.die_temperature_available = true;
  payload.die_temperature_millic = (int64_t)input.die_temp_centic * INT64_C(10);
  payload.quality_state = quality.state;
  payload.valid_cycle_count = quality.valid_cycles;

  CHECK(ml3_payload_build_routine(
          &payload, frame, sizeof(frame), sizeof(frame), &frame_length) ==
        ML3_PAYLOAD_OK);
  CHECK(frame_length == ML3_PAYLOAD_ROUTINE_LENGTH);
  CHECK((frame[ML3_PAYLOAD_ROUTINE_QUALITY_OFFSET] &
         ML3_PAYLOAD_QUALITY_CYCLE_MASK) == 3U);
  CHECK((frame[ML3_PAYLOAD_ROUTINE_QUALITY_OFFSET] >>
         ML3_PAYLOAD_QUALITY_STATE_SHIFT) ==
        (uint8_t)ML3_QUALITY_STATE_VALID);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_MEAN_HI_OFFSET]) !=
        ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(ml3_payload_validate_routine_frame(frame, frame_length) ==
        ML3_PAYLOAD_OK);
}

/*
 * (b) A below-floor reading (2 recoverable-class faults out of 4 cycles,
 * so only 1 cycle survives) is exactly the state compute_uv_stats leaves
 * behind once valid_count < 3: the mean/median/noise/drift fields are
 * absent, and per ml3_measurement.c's post-burst decision the recorded
 * ADC fault class is raised because the burst fell below the floor. bsp.c
 * must still queue a frame: sentinels for the missing fields (forced by
 * ml3_payload_build_routine's core-ADC-failure gate, since the raised
 * fault flag is one of ADC_INIT/ADC_CAL/ADC_TIMEOUT/ADC_OVERRUN), plus the
 * INVALID quality state, the fault flag, and valid_cycle_count == 1.
 */
static void test_below_floor_reading_still_transmits(void)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input;
  ml3_quality_result_t quality;
  ml3_payload_routine_t payload;
  uint8_t frame[ML3_PAYLOAD_ROUTINE_LENGTH];
  size_t frame_length = 0U;

  (void)memset(&input, 0, sizeof(input));
  input.seed_flags = (uint16_t)ML3_QUALITY_FLAG_ADC_TIMEOUT;
  input.valid_cycles = 1U;
  input.burst_cycles = 4U;
  fill_common_rail_samples(&input);
  input.has_die_temp_centic = true;
  input.die_temp_centic = INT32_C(3000);
  /* has_mean_hi_uv / has_median_diff_uv / has_mean_lo_uv / has_noise_sd_uv /
   * has_warmup_drift_uv all stay false: exactly what compute_uv_stats
   * leaves when valid_count < 3. */

  CHECK(ml3_quality_evaluate(&input, &thresholds, &quality) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(quality.state == ML3_QUALITY_STATE_INVALID);
  CHECK(quality.flags == (uint16_t)ML3_QUALITY_FLAG_ADC_TIMEOUT);
  CHECK(quality.valid_cycles == 1U);

  (void)memset(&payload, 0, sizeof(payload));
  payload.status_flags = quality.flags;
  payload.sequence = UINT16_C(8);
  /* Mirrors bsp.c wiring _available from has_*, which is false here for
   * every burst statistic; vdda/v5/die-temp are marked available (bsp.c
   * derived them successfully) to prove the core-ADC-failure gate, not
   * the individual _available flags, is what forces their sentinels. */
  payload.mean_hi_available = false;
  payload.mean_lo_available = false;
  payload.vdda_available = true;
  payload.vdda_uv = (int64_t)input.vdda_pre_uv;
  payload.v5_available = true;
  payload.v5_uv = (int64_t)input.v5_pre_uv;
  payload.noise_available = false;
  payload.die_temperature_available = true;
  payload.die_temperature_millic = (int64_t)input.die_temp_centic * INT64_C(10);
  payload.quality_state = quality.state;
  payload.valid_cycle_count = quality.valid_cycles;

  CHECK(ml3_payload_build_routine(
          &payload, frame, sizeof(frame), sizeof(frame), &frame_length) ==
        ML3_PAYLOAD_OK);
  CHECK(frame_length == ML3_PAYLOAD_ROUTINE_LENGTH);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_FLAGS_OFFSET]) ==
        (uint16_t)ML3_QUALITY_FLAG_ADC_TIMEOUT);
  CHECK(frame[ML3_PAYLOAD_ROUTINE_QUALITY_OFFSET] ==
        (uint8_t)(((uint8_t)ML3_QUALITY_STATE_INVALID <<
                    ML3_PAYLOAD_QUALITY_STATE_SHIFT) | 1U));
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_MEAN_HI_OFFSET]) ==
        ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_MEAN_LO_OFFSET]) ==
        ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_VDDA_OFFSET]) ==
        ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_V5_OFFSET]) ==
        ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_NOISE_OFFSET]) ==
        ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_DIE_TEMP_OFFSET]) ==
        ML3_PAYLOAD_TEMPERATURE_SENTINEL);
  CHECK(ml3_payload_validate_routine_frame(frame, frame_length) ==
        ML3_PAYLOAD_OK);
}

/*
 * (d) Bonus coverage beyond the brief's minimum: a full 4-of-4 burst with
 * no fault flags at all, but one derived field (die temperature) the
 * adapter could not compute. ml3_quality_evaluate reports this via
 * ML3_QUALITY_STATUS_INCOMPLETE rather than STATUS_OK; bsp.c's
 * ml3_target_on_process now treats that as a populated, transmittable
 * result rather than a build failure. This pins that the resulting frame
 * still carries real values for every field that *was* available and a
 * sentinel only for the one that was not.
 */
static void test_incomplete_evidence_still_transmits(void)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input;
  ml3_quality_result_t quality;
  ml3_payload_routine_t payload;
  uint8_t frame[ML3_PAYLOAD_ROUTINE_LENGTH];
  size_t frame_length = 0U;

  (void)memset(&input, 0, sizeof(input));
  input.seed_flags = 0U;
  input.valid_cycles = 4U;
  input.burst_cycles = 4U;
  fill_common_rail_samples(&input);
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
  /* has_die_temp_centic stays false: the one field the adapter failed to
   * derive (e.g. an out-of-range calibration read). */

  CHECK(ml3_quality_evaluate(&input, &thresholds, &quality) ==
        ML3_QUALITY_STATUS_INCOMPLETE);
  CHECK(quality.state == ML3_QUALITY_STATE_INVALID);
  CHECK(quality.incomplete_reason ==
        ML3_QUALITY_INCOMPLETE_MEASUREMENT_EVIDENCE);
  CHECK(quality.flags == 0U);
  CHECK(quality.valid_cycles == 4U);

  (void)memset(&payload, 0, sizeof(payload));
  payload.status_flags = quality.flags;
  payload.sequence = UINT16_C(9);
  payload.mean_hi_available = true;
  payload.mean_hi_uncalibrated_uv = input.mean_hi_uv;
  payload.mean_lo_available = true;
  payload.mean_lo_uncalibrated_uv = input.mean_lo_uv;
  payload.vdda_available = true;
  payload.vdda_uv = (int64_t)input.vdda_pre_uv;
  payload.v5_available = true;
  payload.v5_uv = (int64_t)input.v5_pre_uv;
  payload.noise_available = true;
  payload.noise_uv = (int64_t)input.noise_sd_uv;
  payload.die_temperature_available = false;
  payload.quality_state = quality.state;
  payload.valid_cycle_count = quality.valid_cycles;

  CHECK(ml3_payload_build_routine(
          &payload, frame, sizeof(frame), sizeof(frame), &frame_length) ==
        ML3_PAYLOAD_OK);
  CHECK(frame_length == ML3_PAYLOAD_ROUTINE_LENGTH);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_MEAN_HI_OFFSET]) !=
        ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_VDDA_OFFSET]) !=
        ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_DIE_TEMP_OFFSET]) ==
        ML3_PAYLOAD_TEMPERATURE_SENTINEL);
  CHECK((frame[ML3_PAYLOAD_ROUTINE_QUALITY_OFFSET] >>
         ML3_PAYLOAD_QUALITY_STATE_SHIFT) ==
        (uint8_t)ML3_QUALITY_STATE_INVALID);
  CHECK(ml3_payload_validate_routine_frame(frame, frame_length) ==
        ML3_PAYLOAD_OK);
}

/*
 * Step 3 (task-F1): a clean, physically plausible full-cycle reading -
 * small but nonzero noise and warm-up drift, an ordinary die temperature -
 * must stay VALID under the trial's actual configured thresholds. If this
 * ever fails, the "beyond any physically plausible reading" values in
 * ml3_config.h are no longer permissive and need re-review.
 */
static void test_step3_permissive_thresholds_never_flag_plausible_reading(void)
{
  const ml3_quality_thresholds_t thresholds = config_thresholds();
  ml3_quality_input_t input;
  ml3_quality_result_t quality;

  (void)memset(&input, 0, sizeof(input));
  input.seed_flags = 0U;
  input.valid_cycles = 4U;
  input.burst_cycles = 4U;
  fill_common_rail_samples(&input);
  input.has_mean_hi_uv = true;
  input.mean_hi_uv = INT64_C(500000);
  input.has_median_diff_uv = true;
  input.median_diff_uv = INT64_C(450000);
  input.has_mean_lo_uv = true;
  input.mean_lo_uv = INT64_C(50000);
  input.has_noise_sd_uv = true;
  input.noise_sd_uv = UINT64_C(2000);
  input.has_warmup_drift_uv = true;
  input.warmup_drift_uv = INT64_C(3000);
  input.has_die_temp_centic = true;
  input.die_temp_centic = INT32_C(4500);

  CHECK(ml3_quality_evaluate(&input, &thresholds, &quality) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(quality.state == ML3_QUALITY_STATE_VALID);
  CHECK(quality.flags == 0U);
}

/*
 * Step 3's permissive noise/drift/die-temp thresholds must not weaken the
 * checks the trial deliberately kept active: the below-floor cycle-count
 * decision, ADC fault flags, and the V5 supply range (owner hardware
 * observation, unrelated to Phase 2 qualification).
 */
static void test_step3_fixed_invalidating_checks_still_function(void)
{
  const ml3_quality_thresholds_t thresholds = config_thresholds();
  ml3_quality_input_t input;
  ml3_quality_result_t quality;

  /* Below-floor cycle count, otherwise clean. */
  (void)memset(&input, 0, sizeof(input));
  input.seed_flags = 0U;
  input.valid_cycles = 2U;
  input.burst_cycles = 4U;
  fill_common_rail_samples(&input);
  input.has_die_temp_centic = true;
  input.die_temp_centic = INT32_C(3000);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &quality) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(quality.state == ML3_QUALITY_STATE_INVALID);

  /* ADC fault flag, otherwise a full clean 4-cycle burst. */
  (void)memset(&input, 0, sizeof(input));
  input.seed_flags = (uint16_t)ML3_QUALITY_FLAG_ADC_CAL;
  input.valid_cycles = 4U;
  input.burst_cycles = 4U;
  fill_common_rail_samples(&input);
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
  input.has_die_temp_centic = true;
  input.die_temp_centic = INT32_C(3000);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &quality) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(quality.state == ML3_QUALITY_STATE_INVALID);
  CHECK((quality.flags & (uint16_t)ML3_QUALITY_FLAG_ADC_CAL) != 0U);

  /* V5 supply below the owner-approved range, otherwise clean. */
  (void)memset(&input, 0, sizeof(input));
  input.seed_flags = 0U;
  input.valid_cycles = 4U;
  input.burst_cycles = 4U;
  fill_common_rail_samples(&input);
  input.v5_pre_uv = (uint64_t)ML3_CONFIG_V5_MINIMUM_MV * UINT64_C(1000)
    - UINT64_C(1000);
  input.v5_post_uv = input.v5_pre_uv;
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
  input.has_die_temp_centic = true;
  input.die_temp_centic = INT32_C(3000);
  CHECK(ml3_quality_evaluate(&input, &thresholds, &quality) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(quality.state == ML3_QUALITY_STATE_INVALID);
  CHECK((quality.flags & (uint16_t)ML3_QUALITY_FLAG_V5_LOW) != 0U);
}

/*
 * M-1 (task-F1, final review): a prepare-stage ADC fault
 * (ml3_measurement_set_prepare_fault_and_continue, e.g. an ADC
 * calibration failure before the burst ever starts) and a session-fatal
 * ADC fault (ml3_measurement_set_adc_fault_and_continue, e.g. a VREFINT
 * read failure outside the per-cycle-tolerant ABBA window) both invalidate
 * every has_* field via ml3_measurement_invalidate_measurement_data -
 * including has_abba_raw - then explicitly set has_valid_cycle_count true
 * with valid_cycle_count = 0, and route to PROCESS specifically so a
 * flagged frame still gets built (the "_and_continue" in both names).
 * bsp.c's ml3_target_fill_quality_input must not reject on !has_abba_raw:
 * doing so silently dropped every frame from a node whose ADC had already
 * failed, so it powered the rail, burned battery, and went radio-silent
 * every interval - indistinguishable from a dead node, exactly what plan
 * S3.9 exists to prevent. This shared helper builds the all-evidence-absent
 * input ml3_target_fill_quality_input now produces for either fault class,
 * differing only in which fault flag is seeded (ADC_CAL for the
 * prepare-stage class, ADC_INIT for the session-fatal class - the
 * respective fallback of ml3_measurement_map_prepare_fault_bits /
 * map_adc_fault_bits for a non-timeout, non-overrun error).
 */
static void check_hard_fault_still_transmits(uint16_t fault_flag)
{
  const ml3_quality_thresholds_t thresholds = synthetic_thresholds();
  ml3_quality_input_t input;
  ml3_quality_result_t quality;
  ml3_payload_routine_t payload;
  uint8_t frame[ML3_PAYLOAD_ROUTINE_LENGTH];
  size_t frame_length = 0U;

  (void)memset(&input, 0, sizeof(input));
  input.seed_flags = fault_flag;
  input.valid_cycles = 0U;
  input.burst_cycles = 0U;
  /* has_rail_samples, every mean/median/noise/drift/vdda/v5/die-temp field,
   * stay false/absent: exactly what ml3_measurement_invalidate_measurement_
   * data leaves, and what ml3_target_fill_quality_input now passes through
   * instead of rejecting. */
  input.has_calibration_status = true;
  input.calibration_valid = true;
  input.has_thermistor_status = true;
  input.thermistor_valid = true;

  CHECK(ml3_quality_evaluate(&input, &thresholds, &quality) ==
        ML3_QUALITY_STATUS_OK);
  CHECK(quality.state == ML3_QUALITY_STATE_INVALID);
  CHECK(quality.flags == fault_flag);
  CHECK(quality.valid_cycles == 0U);
  CHECK(quality.incomplete_reason == ML3_QUALITY_INCOMPLETE_NONE);

  (void)memset(&payload, 0, sizeof(payload));
  payload.status_flags = quality.flags;
  payload.sequence = UINT16_C(11);
  /* Every _available flag mirrors bsp.c: false, because the corresponding
   * has_* field is false. */
  payload.quality_state = quality.state;
  payload.valid_cycle_count = quality.valid_cycles;

  CHECK(ml3_payload_build_routine(
          &payload, frame, sizeof(frame), sizeof(frame), &frame_length) ==
        ML3_PAYLOAD_OK);
  CHECK(frame_length == ML3_PAYLOAD_ROUTINE_LENGTH);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_FLAGS_OFFSET]) == fault_flag);
  CHECK(frame[ML3_PAYLOAD_ROUTINE_QUALITY_OFFSET] ==
        (uint8_t)((uint8_t)ML3_QUALITY_STATE_INVALID <<
                   ML3_PAYLOAD_QUALITY_STATE_SHIFT));
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_CORRECTED_OFFSET]) ==
        ML3_PAYLOAD_CORRECTED_SENTINEL);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_MEAN_HI_OFFSET]) ==
        ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_MEAN_LO_OFFSET]) ==
        ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_VDDA_OFFSET]) ==
        ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_V5_OFFSET]) ==
        ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_NOISE_OFFSET]) ==
        ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_DIE_TEMP_OFFSET]) ==
        ML3_PAYLOAD_TEMPERATURE_SENTINEL);
  CHECK(read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_SOIL_TEMP_OFFSET]) ==
        ML3_PAYLOAD_TEMPERATURE_SENTINEL);
  CHECK(ml3_payload_validate_routine_frame(frame, frame_length) ==
        ML3_PAYLOAD_OK);
}

static void test_prepare_stage_fault_still_transmits(void)
{
  check_hard_fault_still_transmits((uint16_t)ML3_QUALITY_FLAG_ADC_CAL);
}

static void test_session_fatal_adc_fault_still_transmits(void)
{
  check_hard_fault_still_transmits((uint16_t)ML3_QUALITY_FLAG_ADC_INIT);
}

int main(void)
{
  test_reduced_valid_cycles_is_a_good_reading();
  test_below_floor_reading_still_transmits();
  test_incomplete_evidence_still_transmits();
  test_step3_permissive_thresholds_never_flag_plausible_reading();
  test_step3_fixed_invalidating_checks_still_function();
  test_prepare_stage_fault_still_transmits();
  test_session_fatal_adc_fault_still_transmits();

  if (failures != 0) {
    (void)fprintf(stderr, "ml3 bsp frame contract: %d failure(s)\n", failures);
    return 1;
  }
  (void)printf("ml3 bsp frame contract: OK\n");
  return 0;
}
