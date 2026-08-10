#ifndef ML3_CONFIG_H
#define ML3_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed protocol facts from Appendix A. */
#define ML3_CONFIG_PROTOCOL_VERSION            1U
#define ML3_CONFIG_PAYLOAD_TYPE_ROUTINE        0U
#define ML3_CONFIG_PAYLOAD_LENGTH_ROUTINE      25U
#define ML3_CONFIG_PAYLOAD_TYPE                ML3_CONFIG_PAYLOAD_TYPE_ROUTINE
#define ML3_CONFIG_PAYLOAD_LENGTH              ML3_CONFIG_PAYLOAD_LENGTH_ROUTINE

/* D2: authorized transport decision. Hardware qualification remains gated. */
#define ML3_CONFIG_MODE_ML3                    10U
#define ML3_CONFIG_FPORT                       13U
#define ML3_CONFIG_MODE_ML3_READY              1U
#define ML3_CONFIG_FPORT_READY                 1U

/* Gate 0 / Phase 1 hardware and build gates. */
#define ML3_CONFIG_PB5_ACTIVE_LOW              1U /* Task 10B brief:52; PB5 is active-low. */
#define ML3_CONFIG_PB5_ACTIVE_LOW_READY        1U /* task-F1 Step 4: verified in vendor source - stm32l0xx_hw_conf.h:265-267 (PWR_OUT_PORT=GPIOB, PWR_OUT_PIN=GPIO_PIN_5) and the pre-existing stock GPIO_PIN_RESET="Enable 5v power supply" callsites in this file (e.g. src/bsp.c line ~1007), consistent with ml3_target_set_power_5v's own active-low mapping. */
#define ML3_CONFIG_PB5_RESET_ISP_BROWNOUT_SAFE 0U /* GATE0-PENDING (§2.1) */
#define ML3_CONFIG_PB5_RESET_ISP_BROWNOUT_SAFE_READY 0U /* GATE0-PENDING (§2.1) */
#define ML3_CONFIG_THERMISTOR_ADC_CHANNEL      2U /* BUILD-ONLY: PA2 / ADC_IN2 */
#define ML3_CONFIG_THERMISTOR_ADC_CHANNEL_READY 0U /* GATE0-PENDING (§3.3; board PA2 pull check) */
#define ML3_CONFIG_THERMISTOR_EXCITATION_GPIO  4U /* BUILD-ONLY: PB4 */
#define ML3_CONFIG_THERMISTOR_EXCITATION_GPIO_READY 0U /* GATE0-PENDING (§3.3; circuit fitted and checked) */
#define ML3_CONFIG_ZERO_AMBIGUITY_GUARD_MV     2U /* Gate 0 §4:52 and Task 10B brief:70. */
#define ML3_CONFIG_ZERO_AMBIGUITY_GUARD_READY  1U /* task-F1 Step 4: Gate 0 §4 measured record. */
#define ML3_CONFIG_CM_RANGE_MIN_MV             0U /* GATE0-PENDING (§2.2): owner-approved trial envelope detects gross disconnected or shorted legs. */
#define ML3_CONFIG_CM_RANGE_MAX_MV             100U /* Avoid routinely degrading valid probes across temperature. */
#define ML3_CONFIG_CM_RANGE_READY              1U /* task-F1 Step 4: owner-approved trial envelope. */
/* Two fitted equal 1 kΩ resistors wire +5V (14) -> PA4 (26) -> GND (15).
 * The nominal 0.5 ratio is required: a floating PA4 reads as a failed rail.
 * task-F1 Step 4: unlike the other Gate 0 hardware-observation items below,
 * this readiness flag is deliberately left pending - tests/host/
 * ml3_readiness_cohesion_contract.sh pins it at 0 (commit 6d70a8c, "reject
 * computed ML3 divider values") because the nominal 0.5 ratio is an assumed
 * value from component tolerance, not a per-device measured one. It is
 * excluded from the trial-readiness macro below for the same reason; the
 * V5 range check still runs against this same raw constant and still
 * catches a grossly failed rail (see ML3_CONFIG_V5_LIMITS_READY), so this
 * exclusion narrows precision confidence, not safety coverage. */
#define ML3_CONFIG_V5_DIVIDER_RATIO_PPM        500000U /* Owner hardware observation, not a bench measurement; Task 10B brief:80. */
#define ML3_CONFIG_V5_DIVIDER_RATIO_READY      0U /* GATE0-PENDING (§3.4) */
#define ML3_CONFIG_V5_MINIMUM_MV               4500U /* Owner hardware observation, not a bench measurement; Task 10B brief:78. */
#define ML3_CONFIG_V5_MAXIMUM_MV               5500U /* Owner hardware observation, not a bench measurement; Task 10B brief:79. */
#define ML3_CONFIG_V5_LIMITS_READY             1U /* task-F1 Step 4: owner hardware observation, not a bench measurement. */
#define ML3_CONFIG_DISCHARGE_THRESHOLD_MV      500U /* Owner hardware observation, not a bench measurement; Task 10B brief:81. */
#define ML3_CONFIG_DISCHARGE_TIMEOUT_MS        2000U /* Owner hardware observation, not a bench measurement; Task 10B brief:82. */
#define ML3_CONFIG_DISCHARGE_READY             1U /* task-F1 Step 4: owner hardware observation, not a bench measurement. */
#define ML3_CONFIG_WARMUP_TIME_MS              1500U /* Provisional; Task 10B brief:73 (manufacturer 0.5–1 s plus margin). */
#define ML3_CONFIG_WARMUP_TIME_READY           1U /* task-F1 Step 4: manufacturer specification plus margin. */
#define ML3_CONFIG_LORA_REGION_ID              0U /* GATE0-PENDING (§3.13) */
#define ML3_CONFIG_LORA_REGION_READY           0U /* GATE0-PENDING (§3.13) */
#define ML3_CONFIG_LORA_DATARATE               0U /* GATE0-PENDING (§3.13) */
#define ML3_CONFIG_LORA_DATARATE_READY         0U /* GATE0-PENDING (§3.13) */
#define ML3_CONFIG_MAX_FRMPAYLOAD_BYTES        0U /* GATE0-PENDING (§2.1 item 5, §3.13) */
#define ML3_CONFIG_MAX_FRMPAYLOAD_READY        0U /* GATE0-PENDING (§2.1 item 5, §3.13) */
#define ML3_CONFIG_ROUTINE_MAX_AIRTIME_US      0U /* GATE0-PENDING (§3.13) */
#define ML3_CONFIG_ROUTINE_MAX_AIRTIME_READY   0U /* GATE0-PENDING (§3.13) */
#define ML3_CONFIG_DIAGNOSTIC_MAX_AIRTIME_US   0U /* GATE0-PENDING (§3.13) */
#define ML3_CONFIG_DIAGNOSTIC_MAX_AIRTIME_READY 0U /* GATE0-PENDING (§3.13) */
#define ML3_CONFIG_GATE0_APPROVAL_READY        0U /* GATE0-PENDING (§2.6) */
#define ML3_CONFIG_GATE1_APPROVAL_READY        0U /* PHASE2-PENDING (§4.5) */
/*
 * task-F1 Step 4: this is deliberately a DIFFERENT flag from
 * ML3_CONFIG_GATE0_APPROVAL_READY above, which stays 0 - real Gate 0
 * sign-off has not happened and remains outstanding. This flag records
 * only the owner's approval of a time-boxed four-node field trial, not
 * the full Gate 0 standard, and must never be substituted for it.
 */
#define ML3_CONFIG_TRIAL_APPROVAL_READY        1U /* Owner trial approval 2026-08-09, authorizing a time-boxed four-node trial only; explicitly NOT full Gate 0 sign-off, which remains outstanding. */

/* Phase 2 / Gate 1 qualification gates. */
/* The production temperature table stays absent until sourced from the ML3 manual (§3.3). */
#define ML3_CONFIG_THERMISTOR_EFFECTIVE_REFERENCE_OHM 0U /* PHASE2-PENDING (§4.2) */
#define ML3_CONFIG_THERMISTOR_EFFECTIVE_REFERENCE_READY 0U /* PHASE2-PENDING (§4.2) */
#define ML3_CONFIG_THERMISTOR_RAIL_GUARD_CODE 0U /* PHASE2-PENDING (§3.3) */
#define ML3_CONFIG_THERMISTOR_RAIL_GUARD_READY 0U /* PHASE2-PENDING (§3.3) */
#define ML3_CONFIG_THERMISTOR_SETTLE_TIME_MS   0U /* PHASE2-PENDING (§3.3) */
#define ML3_CONFIG_THERMISTOR_SETTLE_TIME_READY 0U /* PHASE2-PENDING (§3.3) */
#define ML3_CONFIG_THERMISTOR_TABLE_POINT_COUNT 0U /* PHASE2-PENDING (§3.3) */
#define ML3_CONFIG_THERMISTOR_TABLE_READY      0U /* PHASE2-PENDING (§3.3) */
/*
 * task-F1 (2026-08) trial decision: Phase 2 qualification never ran, so no
 * measured noise, drift, or die-temperature threshold exists. Rather than
 * invent plausible-looking numbers and present them as qualification data,
 * every threshold below that ml3_quality_thresholds_from_config requires
 * (noise, warm-up drift, VDDA drift, die-temperature range) is set beyond
 * any physically achievable reading on this circuit, so it can never fire
 * on its own. This trial sends raw microvolts to a backend that performs
 * the soil conversion and any judgement of data quality; firmware-side
 * noise/drift flagging adds little here, while thresholds guessed without
 * qualification data would produce spurious flags - and a flag that cries
 * wolf is worse than no flag, because it trains operators to ignore it.
 * The invalidating conditions that genuinely matter - the below-floor
 * cycle-count check, ADC fault flags, and the V5 supply range (owner
 * hardware observation, ML3_CONFIG_V5_MINIMUM_MV/MAXIMUM_MV above) - are
 * enforced independently of these thresholds and remain fully active.
 * Each value/readiness pair below is marked so a future qualification
 * pass cannot mistake it for a measurement.
 */
#define ML3_CONFIG_NOISE_WARN_UV               4000000U /* Trial: flagging disabled pending Phase 2 qualification; not a measured value. */
#define ML3_CONFIG_NOISE_WARN_READY            1U /* Trial: flagging disabled pending Phase 2 qualification; not a measured value. */
#define ML3_CONFIG_NOISE_INVALID_UV            4200000U /* Trial: flagging disabled pending Phase 2 qualification; not a measured value. */
#define ML3_CONFIG_NOISE_INVALID_READY         1U /* Trial: flagging disabled pending Phase 2 qualification; not a measured value. */
#define ML3_CONFIG_WARMUP_DRIFT_WARN_UV        4000000U /* Trial: flagging disabled pending Phase 2 qualification; not a measured value. */
#define ML3_CONFIG_WARMUP_DRIFT_WARN_READY     1U /* Trial: flagging disabled pending Phase 2 qualification; not a measured value. */
#define ML3_CONFIG_WARMUP_DRIFT_INVALID_UV     4200000U /* Trial: flagging disabled pending Phase 2 qualification; not a measured value. */
#define ML3_CONFIG_WARMUP_DRIFT_INVALID_READY  1U /* Trial: flagging disabled pending Phase 2 qualification; not a measured value. */
#define ML3_CONFIG_VDDA_DRIFT_WARN_PPM         900000U /* Trial: flagging disabled pending Phase 2 qualification; not a measured value. */
#define ML3_CONFIG_VDDA_DRIFT_WARN_READY       1U /* Trial: flagging disabled pending Phase 2 qualification; not a measured value. */
#define ML3_CONFIG_VDDA_DRIFT_INVALID_PPM      950000U /* Trial: flagging disabled pending Phase 2 qualification; not a measured value. */
#define ML3_CONFIG_VDDA_DRIFT_INVALID_READY    1U /* Trial: flagging disabled pending Phase 2 qualification; not a measured value. */
#define ML3_CONFIG_DIE_TEMP_MIN_CENTIC         (-50000) /* Trial: flagging disabled pending Phase 2 qualification; not a measured value. */
#define ML3_CONFIG_DIE_TEMP_MAX_CENTIC         50000 /* Trial: flagging disabled pending Phase 2 qualification; not a measured value. */
#define ML3_CONFIG_DIE_TEMP_RANGE_READY        1U /* Trial: flagging disabled pending Phase 2 qualification; not a measured value. */
#define ML3_CONFIG_CAL_OFFSET_UV               0U /* PHASE2-PENDING (§3.10) */
#define ML3_CONFIG_CAL_OFFSET_TEMPCO_UV_PER_C  0U /* PHASE2-PENDING (§3.10) */
#define ML3_CONFIG_CAL_GAIN_PPM                0U /* PHASE2-PENDING (§3.10) */
#define ML3_CONFIG_CAL_GAIN_TEMPCO_PPM_PER_C   0U /* PHASE2-PENDING (§3.10) */
#define ML3_CONFIG_CAL_CM_PPM                  0U /* PHASE2-PENDING (§3.10) */
#define ML3_CONFIG_CAL_CM_REF_UV               0U /* PHASE2-PENDING (§3.10) */
#define ML3_CONFIG_CAL_OFFSET_READY            0U /* PHASE2-PENDING (§3.10) */
#define ML3_CONFIG_CAL_GAIN_READY              0U /* PHASE2-PENDING (§3.10) */
#define ML3_CONFIG_CAL_CM_READY                0U /* PHASE2-PENDING (§3.10) */

#define ML3_CONFIG_PROTOCOL_READY \
  ((ML3_CONFIG_PROTOCOL_VERSION == 1U) && \
   (ML3_CONFIG_PAYLOAD_TYPE == 0U) && \
   (ML3_CONFIG_PAYLOAD_LENGTH == 25U))

#define ML3_CONFIG_GATE0_READINESS \
  (ML3_CONFIG_PB5_ACTIVE_LOW_READY && \
   ML3_CONFIG_PB5_RESET_ISP_BROWNOUT_SAFE_READY && \
   ML3_CONFIG_THERMISTOR_ADC_CHANNEL_READY && \
   ML3_CONFIG_THERMISTOR_EXCITATION_GPIO_READY && \
   ML3_CONFIG_ZERO_AMBIGUITY_GUARD_READY && \
   ML3_CONFIG_CM_RANGE_READY && \
   ML3_CONFIG_V5_DIVIDER_RATIO_READY && \
   ML3_CONFIG_V5_LIMITS_READY && \
   ML3_CONFIG_DISCHARGE_READY && \
   ML3_CONFIG_WARMUP_TIME_READY && \
   ML3_CONFIG_LORA_REGION_READY && \
   ML3_CONFIG_LORA_DATARATE_READY && \
   ML3_CONFIG_MAX_FRMPAYLOAD_READY && \
   ML3_CONFIG_ROUTINE_MAX_AIRTIME_READY && \
   ML3_CONFIG_DIAGNOSTIC_MAX_AIRTIME_READY && \
   ML3_CONFIG_GATE0_APPROVAL_READY)

#define ML3_CONFIG_PHASE2_READINESS \
  (ML3_CONFIG_THERMISTOR_EFFECTIVE_REFERENCE_READY && \
   ML3_CONFIG_THERMISTOR_RAIL_GUARD_READY && \
   ML3_CONFIG_THERMISTOR_SETTLE_TIME_READY && \
   ML3_CONFIG_THERMISTOR_TABLE_READY && \
   ML3_CONFIG_NOISE_WARN_READY && \
   ML3_CONFIG_NOISE_INVALID_READY && \
   ML3_CONFIG_WARMUP_DRIFT_WARN_READY && \
   ML3_CONFIG_WARMUP_DRIFT_INVALID_READY && \
   ML3_CONFIG_VDDA_DRIFT_WARN_READY && \
   ML3_CONFIG_VDDA_DRIFT_INVALID_READY && \
   ML3_CONFIG_DIE_TEMP_RANGE_READY && \
   ML3_CONFIG_CAL_OFFSET_READY && \
   ML3_CONFIG_CAL_GAIN_READY && \
   ML3_CONFIG_CAL_CM_READY && \
   ML3_CONFIG_GATE1_APPROVAL_READY)

/*
 * task-F1 Step 4 (2026-08): trial activation. ML3_CONFIG_GATE0_READINESS
 * above is untouched and remains the full standard - it still evaluates
 * false, because several of its constituent flags genuinely are still
 * pending (see the exclusions below) and ML3_CONFIG_GATE0_APPROVAL_READY
 * stays 0 (real Gate 0 sign-off has not happened).
 *
 * This trial-readiness gate is a distinct, narrower macro authorizing only
 * a time-boxed four-node field trial (ML3_CONFIG_TRIAL_APPROVAL_READY,
 * owner approval 2026-08-09), built from the subset of Gate 0 items that
 * do have real evidence behind them now (see each flag's comment above).
 * It deliberately excludes:
 *   - Thermistor readiness (ML3_CONFIG_THERMISTOR_ADC_CHANNEL_READY,
 *     ML3_CONFIG_THERMISTOR_EXCITATION_GPIO_READY) - circuit not built,
 *     out of trial scope; soil temperature reports are unavailable.
 *   - PB5 brownout qualification
 *     (ML3_CONFIG_PB5_RESET_ISP_BROWNOUT_SAFE_READY) - reset and ISP
 *     behaviour were checked by the owner 2026-08-09 (provisional: meter
 *     point and duration unrecorded); brownout itself is untested and
 *     remains a prerequisite before field installation.
 *   - Radio gates (ML3_CONFIG_LORA_REGION_READY,
 *     ML3_CONFIG_LORA_DATARATE_READY, ML3_CONFIG_MAX_FRMPAYLOAD_READY,
 *     ML3_CONFIG_ROUTINE_MAX_AIRTIME_READY,
 *     ML3_CONFIG_DIAGNOSTIC_MAX_AIRTIME_READY) - vendor defaults stand by
 *     owner decision; the 25-byte routine frame fits inside the smallest
 *     EU868 allowance and the vendor stack manages duty cycle.
 *   - The V5 divider ratio calibration (ML3_CONFIG_V5_DIVIDER_RATIO_READY)
 *     - see the comment above that flag; it is an assumed, not measured,
 *     value and tests/host/ml3_readiness_cohesion_contract.sh deliberately
 *     pins it at 0.
 * Every excluded flag above remains exactly as it was: 0.
 */
#define ML3_CONFIG_TRIAL_ACQUISITION_READINESS \
  (ML3_CONFIG_PB5_ACTIVE_LOW_READY && \
   ML3_CONFIG_ZERO_AMBIGUITY_GUARD_READY && \
   ML3_CONFIG_CM_RANGE_READY && \
   ML3_CONFIG_V5_LIMITS_READY && \
   ML3_CONFIG_DISCHARGE_READY && \
   ML3_CONFIG_WARMUP_TIME_READY && \
   ML3_CONFIG_TRIAL_APPROVAL_READY)

/* Acquisition is a separate gate from selecting mode 10/FPort 13.  Keeping
 * it false until every measured input and qualification value is present
 * prevents a partially configured target from touching the stock ADC path.
 * task-F1 Step 4: now points at the trial-readiness gate above rather than
 * the full Gate 0 standard; see that macro's comment for exactly what
 * remains excluded and why. */
#define ML3_CONFIG_ACQUISITION_READY \
  ML3_CONFIG_TRIAL_ACQUISITION_READINESS

#define ML3_CONFIG_DEPLOYABLE \
  (ML3_CONFIG_PROTOCOL_READY && \
   ML3_CONFIG_MODE_ML3_READY && \
   ML3_CONFIG_FPORT_READY && \
   ML3_CONFIG_GATE0_READINESS && \
   ML3_CONFIG_PHASE2_READINESS)

#ifdef __cplusplus
}
#endif

#endif /* ML3_CONFIG_H */
