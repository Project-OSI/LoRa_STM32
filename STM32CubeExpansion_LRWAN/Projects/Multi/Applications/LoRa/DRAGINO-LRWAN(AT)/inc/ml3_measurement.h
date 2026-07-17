/* SPDX-License-Identifier: MIT */
#ifndef ML3_MEASUREMENT_H
#define ML3_MEASUREMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "adc_precision.h"
#include "ml3_quality.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ML3_MEASUREMENT_MAX_ABBA_CYCLES 8U
/*
 * A configured cycle count below ML3_MEASUREMENT_FIXED_MIN_VALID_CYCLES can
 * never produce a valid reading (see ml3_quality.c), so the configurable
 * floor must not be lower than that fixed minimum.
 */
#define ML3_MEASUREMENT_MIN_ABBA_CYCLES 3U
#define ML3_MEASUREMENT_FIXED_MIN_VALID_CYCLES 3U
#define ML3_MEASUREMENT_MIN_WARMUP_MS 500U
#define ML3_MEASUREMENT_MAX_WARMUP_MS 3000U
#define ML3_MEASUREMENT_CHANNEL_VREFINT 17U
#define ML3_MEASUREMENT_CHANNEL_DIE_TEMP 18U

typedef enum {
  ML3_STATE_IDLE,
  ML3_STATE_PREPARE,
  ML3_STATE_POWER_ON,
  ML3_STATE_WARMUP,
  ML3_STATE_ADC_CONFIGURE,
  ML3_STATE_ADC_CALIBRATE,
  ML3_STATE_REFERENCE_PRE,
  ML3_STATE_SAMPLE_ABBA,
  ML3_STATE_REFERENCE_POST,
  ML3_STATE_POWER_OFF,
  ML3_STATE_VERIFY_DISCHARGE,
  ML3_STATE_SAMPLE_THERMISTOR,
  ML3_STATE_PROCESS,
  ML3_STATE_BUILD_PAYLOAD,
  ML3_STATE_QUEUE_TX,
  ML3_STATE_ERROR
} ml3_state_t;

typedef enum {
  ML3_MEASUREMENT_OK = 0,
  ML3_MEASUREMENT_ERR_INVALID_ARGUMENT = 1,
  ML3_MEASUREMENT_ERR_BUSY = 2,
  ML3_MEASUREMENT_ERR_STATE = 3,
  ML3_MEASUREMENT_ERR_ADC_PREPARE = 4,
  ML3_MEASUREMENT_ERR_ADC_TIMEOUT = 5,
  ML3_MEASUREMENT_ERR_ADC_OVERRUN = 6,
  ML3_MEASUREMENT_ERR_ADC_INTERNAL = 7,
  ML3_MEASUREMENT_ERR_CALLBACK = 8,
  ML3_MEASUREMENT_ERR_ABORTED = 9
} ml3_measurement_error_t;

typedef enum {
  ML3_MEASUREMENT_STEP_DONE = 0,
  ML3_MEASUREMENT_STEP_BUSY = 1,
  ML3_MEASUREMENT_STEP_ERROR = 2
} ml3_measurement_step_t;

typedef enum {
  ML3_MEASUREMENT_FAULT_NONE = 0U,
  ML3_MEASUREMENT_FAULT_ADC_INIT = ML3_QUALITY_FLAG_ADC_INIT,
  ML3_MEASUREMENT_FAULT_ADC_CAL = ML3_QUALITY_FLAG_ADC_CAL,
  ML3_MEASUREMENT_FAULT_ADC_TIMEOUT = ML3_QUALITY_FLAG_ADC_TIMEOUT,
  ML3_MEASUREMENT_FAULT_ADC_OVERRUN = ML3_QUALITY_FLAG_ADC_OVERRUN,
  ML3_MEASUREMENT_FAULT_THERM_FAULT = ML3_QUALITY_FLAG_THERM_FAULT
} ml3_measurement_fault_t;

typedef struct {
  uint16_t channel_hi;
  uint16_t channel_lo;
  uint16_t channel_v5;
  uint16_t channel_thermistor;
  uint16_t abba_cycles;
  uint16_t vrefint_calibration_word;
  uint32_t warmup_ms;
  uint32_t discharge_threshold_mv;
  uint32_t discharge_timeout_ms;
  uint32_t therm_settle_ms;
} ml3_measurement_config_t;

struct ml3_measurement_result_t;

typedef bool (*ml3_measurement_process_fn)(
  void*,
  const struct ml3_measurement_result_t*);
typedef bool (*ml3_measurement_build_fn)(
  void*,
  const struct ml3_measurement_result_t*);
typedef bool (*ml3_measurement_queue_fn)(
  void*,
  const struct ml3_measurement_result_t*);

typedef struct {
  void* context;
  uint32_t (*now_ms)(void*);
  uint32_t (*read_reset_cause)(void*);
  bool (*request_radio_sleep)(void*);
  bool (*configure_analog_pins)(void*);
  bool (*set_power_5v)(void*, bool);
  bool (*set_thermistor_excitation)(void*, bool);
  bool (*watchdog_refresh)(void*);
  ml3_measurement_process_fn on_process;
  ml3_measurement_build_fn on_build_payload;
  ml3_measurement_queue_fn on_queue;
} ml3_measurement_port_t;

typedef struct ml3_measurement_result_t {
  bool has_sequence;
  bool has_reset_cause;
  bool has_pre_reference_raw;
  bool has_post_reference_raw;
  bool has_pre_v5_raw;
  bool has_post_v5_raw;
  bool has_die_temp_raw;
  bool has_thermistor_raw;
  bool has_vdda_pre_uv;
  bool has_vdda_post_uv;
  bool has_faults;
  bool has_abba_raw;
  bool has_mean_hi_uv;
  bool has_mean_lo_uv;
  bool has_common_mode_uv;
  bool has_mean_diff_uv;
  bool has_median_diff_uv;
  bool has_drift_uv;
  bool has_sd_uv;
  bool has_mad_uv;
  bool has_min_diff_uv;
  bool has_max_diff_uv;
  bool has_valid_cycle_count;

  uint16_t sequence;
  uint32_t reset_cause;

  uint16_t pre_reference_raw;
  uint16_t post_reference_raw;
  uint16_t pre_v5_raw;
  uint16_t post_v5_raw;
  uint16_t die_temp_raw;
  uint16_t thermistor_raw;

  uint32_t vdda_pre_uv;
  uint32_t vdda_post_uv;

  uint16_t valid_cycle_count;
  uint16_t abba_raw_cycle_count;
  uint16_t fault_flags;

  uint16_t abba_h1_raw[ML3_MEASUREMENT_MAX_ABBA_CYCLES];
  uint16_t abba_l1_raw[ML3_MEASUREMENT_MAX_ABBA_CYCLES];
  uint16_t abba_l2_raw[ML3_MEASUREMENT_MAX_ABBA_CYCLES];
  uint16_t abba_h2_raw[ML3_MEASUREMENT_MAX_ABBA_CYCLES];

  int64_t mean_hi_uv;
  int64_t mean_lo_uv;
  int64_t common_mode_uv;
  int64_t mean_diff_uv;
  int64_t median_diff_uv;
  int64_t drift_uv;
  int64_t sd_uv;
  int64_t mad_uv;
  int64_t min_diff_uv;
  int64_t max_diff_uv;
} ml3_measurement_result_t;

typedef struct ml3_measurement_ctx_t {
  ml3_state_t state;
  ml3_measurement_config_t config;
  ml3_measurement_port_t port;
  adc_precision_context_t* adc_ctx;
  adc_precision_timeouts_t adc_timeouts;
  ml3_measurement_error_t last_error;
  uint16_t sequence;
  ml3_measurement_result_t last_result;
  uint32_t fault_flags;
  bool initialized;
  bool active;
  uint32_t state_enter_ms;
  uint16_t current_abba_cycle;
  uint16_t abba_substep;
  bool discharge_timed_out;
  int64_t cycle_hi_uv[ML3_MEASUREMENT_MAX_ABBA_CYCLES];
  int64_t cycle_lo_uv[ML3_MEASUREMENT_MAX_ABBA_CYCLES];
  bool cycle_valid[ML3_MEASUREMENT_MAX_ABBA_CYCLES];
  /*
   * Per-cycle ABBA fault tolerance (plan section 3.9): a conversion-class
   * failure (timeout/overrun) inside the H1/L1/L2/H2 sub-steps invalidates
   * only the affected cycle. The fault classes seen are accumulated here
   * and raised as fault flags after the burst only when the surviving
   * valid-cycle count falls below ML3_MEASUREMENT_FIXED_MIN_VALID_CYCLES;
   * otherwise the reading stands and the reduced valid-cycle count is the
   * visible evidence.
   */
  uint32_t abba_cycle_fault_flags;
  ml3_measurement_error_t abba_cycle_last_error;
  int64_t abba_h1_uv;
  int64_t abba_l1_uv;
  int64_t abba_l2_uv;
  int64_t abba_h2_uv;
  uint16_t abba_h1_raw;
  uint16_t abba_l1_raw;
  uint16_t abba_l2_raw;
  uint16_t abba_h2_raw;
} ml3_measurement_ctx_t;

ml3_measurement_error_t ml3_measurement_init(
  ml3_measurement_ctx_t* ctx,
  const ml3_measurement_config_t* config,
  const ml3_measurement_port_t* port,
  adc_precision_context_t* adc_ctx,
  const adc_precision_timeouts_t* adc_timeouts);

ml3_measurement_error_t ml3_measurement_start(ml3_measurement_ctx_t* ctx);
ml3_measurement_step_t ml3_measurement_step(ml3_measurement_ctx_t* ctx);
void ml3_measurement_abort(ml3_measurement_ctx_t* ctx);
ml3_state_t ml3_measurement_state(const ml3_measurement_ctx_t* ctx);
const ml3_measurement_result_t* ml3_measurement_last_result(const ml3_measurement_ctx_t* ctx);
ml3_measurement_error_t ml3_measurement_last_error(const ml3_measurement_ctx_t* ctx);

void ml3_measurement_compute_uv_stats(
  const int64_t* cycle_hi_uv,
  const int64_t* cycle_lo_uv,
  const bool* cycle_valid,
  size_t cycle_count,
  ml3_measurement_result_t* out);

#ifdef __cplusplus
}
#endif

#endif /* ML3_MEASUREMENT_H */
