#ifndef ML3_QUALITY_H
#define ML3_QUALITY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ML3_QUALITY_FLAG_ADC_INIT = UINT16_C(0x0001),
  ML3_QUALITY_FLAG_ADC_CAL = UINT16_C(0x0002),
  ML3_QUALITY_FLAG_ADC_TIMEOUT = UINT16_C(0x0004),
  ML3_QUALITY_FLAG_ADC_OVERRUN = UINT16_C(0x0008),
  ML3_QUALITY_FLAG_HI_OVER = UINT16_C(0x0010),
  ML3_QUALITY_FLAG_LOW_RAIL_CLIPPED = UINT16_C(0x0020),
  ML3_QUALITY_FLAG_DIFF_RANGE = UINT16_C(0x0040),
  ML3_QUALITY_FLAG_CM_RANGE = UINT16_C(0x0080),
  ML3_QUALITY_FLAG_VREF_DRIFT = UINT16_C(0x0100),
  ML3_QUALITY_FLAG_V5_LOW = UINT16_C(0x0200),
  ML3_QUALITY_FLAG_V5_HIGH = UINT16_C(0x0400),
  ML3_QUALITY_FLAG_NOISE_HIGH = UINT16_C(0x0800),
  ML3_QUALITY_FLAG_WARMUP_DRIFT = UINT16_C(0x1000),
  ML3_QUALITY_FLAG_CAL_INVALID = UINT16_C(0x2000),
  ML3_QUALITY_FLAG_TEMP_RANGE = UINT16_C(0x4000),
  ML3_QUALITY_FLAG_THERM_FAULT = UINT16_C(0x8000)
} ml3_quality_flag_t;

#define ML3_QUALITY_INVALIDATING_MASK UINT16_C(0x027f)

#define ML3_QUALITY_HI_MARGIN_UV UINT64_C(100000)
#define ML3_QUALITY_DIFF_MIN_UV INT64_C(-20000)
#define ML3_QUALITY_DIFF_MAX_UV INT64_C(1100000)

#define ML3_QUALITY_INVALID_REASON_VALID_CYCLES UINT32_C(0x00010000)
#define ML3_QUALITY_INVALID_REASON_VREF_DRIFT UINT32_C(0x00020000)
#define ML3_QUALITY_INVALID_REASON_NOISE UINT32_C(0x00040000)
#define ML3_QUALITY_INVALID_REASON_WARMUP_DRIFT UINT32_C(0x00080000)
#define ML3_QUALITY_INVALID_REASON_INCOMPLETE_EVIDENCE UINT32_C(0x00100000)

typedef enum {
  ML3_QUALITY_STATE_VALID = 0,
  ML3_QUALITY_STATE_DEGRADED = 1,
  ML3_QUALITY_STATE_INVALID = 2
} ml3_quality_state_t;

typedef enum {
  ML3_QUALITY_STATUS_OK = 0,
  ML3_QUALITY_STATUS_INVALID_ARGUMENT = 1,
  ML3_QUALITY_STATUS_CONFIG_PENDING = 2,
  ML3_QUALITY_STATUS_INCOMPLETE = 3
} ml3_quality_status_t;

typedef struct {
  uint64_t zero_ambiguity_guard_uv;
  int64_t common_mode_min_uv;
  int64_t common_mode_max_uv;
  uint64_t v5_min_uv;
  uint64_t v5_max_uv;
  uint64_t noise_warn_uv;
  uint64_t noise_invalid_uv;
  uint64_t warmup_drift_warn_uv;
  uint64_t warmup_drift_invalid_uv;
  uint32_t vdda_drift_warn_ppm;
  uint32_t vdda_drift_invalid_ppm;
  int32_t die_temp_min_centic;
  int32_t die_temp_max_centic;
} ml3_quality_thresholds_t;

#define ML3_QUALITY_MAX_BURST_CYCLES 8U
#define ML3_QUALITY_MAX_RAIL_SAMPLES 16U

typedef struct {
  uint16_t seed_flags;
  uint8_t valid_cycles;
  uint8_t burst_cycles;

  bool has_rail_samples;
  uint8_t rail_sample_count;
  /* Interleaved H1/H2 and L1/L2 retained samples, 2 per burst cycle. */
  int64_t hi_samples_uv[ML3_QUALITY_MAX_RAIL_SAMPLES];
  int64_t lo_samples_uv[ML3_QUALITY_MAX_RAIL_SAMPLES];

  bool has_mean_hi_uv;
  int64_t mean_hi_uv;
  bool has_median_diff_uv;
  int64_t median_diff_uv;
  bool has_mean_lo_uv;
  int64_t mean_lo_uv;
  bool has_noise_sd_uv;
  uint64_t noise_sd_uv;
  bool has_warmup_drift_uv;
  int64_t warmup_drift_uv;

  bool has_vdda_uv;
  uint64_t vdda_pre_uv;
  uint64_t vdda_post_uv;
  bool has_v5_uv;
  uint64_t v5_pre_uv;
  uint64_t v5_post_uv;
  bool has_die_temp_centic;
  int32_t die_temp_centic;

  bool has_calibration_status;
  bool calibration_valid;
  bool has_thermistor_status;
  bool thermistor_valid;
} ml3_quality_input_t;

typedef enum {
  ML3_QUALITY_INCOMPLETE_NONE = 0,
  ML3_QUALITY_INCOMPLETE_MEASUREMENT_EVIDENCE = 1
} ml3_quality_incomplete_reason_t;

typedef struct {
  uint16_t flags;
  uint8_t valid_cycles;
  ml3_quality_state_t state;
  /* Local diagnostic key; this field is not part of the transmitted flags. */
  uint32_t invalidating_signature;
  ml3_quality_incomplete_reason_t incomplete_reason;
} ml3_quality_result_t;

ml3_quality_status_t ml3_quality_thresholds_from_config(
  ml3_quality_thresholds_t* out);

ml3_quality_status_t ml3_quality_evaluate(
  const ml3_quality_input_t* input,
  const ml3_quality_thresholds_t* thresholds,
  ml3_quality_result_t* out);

#ifdef __cplusplus
}
#endif

#endif /* ML3_QUALITY_H */
