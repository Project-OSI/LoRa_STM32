/* SPDX-License-Identifier: MIT */
#include "ml3_measurement.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  ML3_MEASUREMENT_ABBA_PHASE_H1 = 0U,
  ML3_MEASUREMENT_ABBA_PHASE_L1 = 1U,
  ML3_MEASUREMENT_ABBA_PHASE_L2 = 2U,
  ML3_MEASUREMENT_ABBA_PHASE_H2 = 3U
};

enum {
  ML3_MEASUREMENT_THERM_PHASE_OFF = 0U,
  ML3_MEASUREMENT_THERM_PHASE_WAIT_SETTLE = 1U,
  ML3_MEASUREMENT_THERM_PHASE_READ = 2U
};

static bool ml3_measurement_timed_out(
  const ml3_measurement_ctx_t* ctx,
  uint32_t start_ms,
  uint32_t timeout_ms) {
  uint32_t now_ms = 0U;
  if (ctx == NULL) {
    return true;
  }
  now_ms = ctx->port.now_ms(ctx->port.context);
  return (now_ms - start_ms) >= timeout_ms;
}

static int64_t ml3_measurement_abs_i64(int64_t value) {
  if (value < 0LL) {
    return (value == INT64_MIN) ? INT64_MAX : -value;
  }
  return value;
}

static int64_t ml3_measurement_safe_average_int64(int64_t left, int64_t right) {
  if (((left < 0LL) && (right >= 0LL)) ||
      ((left >= 0LL) && (right < 0LL))) {
    return (left + right) / 2LL;
  }
  const int64_t q_left = left / 2LL;
  const int64_t q_right = right / 2LL;
  const int64_t r_left = left % 2LL;
  const int64_t r_right = right % 2LL;
  return q_left + q_right + (r_left + r_right) / 2LL;
}

static uint32_t ml3_measurement_u64_floor_sqrt(uint64_t value) {
  uint64_t bit = 1ULL << 62;
  uint64_t res = 0ULL;
  uint64_t n = value;

  while (bit > n) {
    bit >>= 2ULL;
  }

  while (bit != 0ULL) {
    uint64_t trial = res + bit;
    res >>= 1ULL;
    if (n >= trial) {
      n -= trial;
      res += bit;
    }
    bit >>= 2ULL;
  }
  return (uint32_t)res;
}

static int64_t ml3_measurement_floor_div_i64(int64_t dividend, int64_t divisor) {
  int64_t quotient = dividend / divisor;
  if ((dividend < 0LL) && ((dividend % divisor) != 0LL)) {
    --quotient;
  }
  return quotient;
}

static void ml3_measurement_clear_result(ml3_measurement_result_t* result) {
  if (result != NULL) {
    memset(result, 0, sizeof(*result));
  }
}

static bool ml3_measurement_validate_channel(uint16_t channel) {
  return channel <= ADC_PRECISION_CHANNEL_MAX;
}

static bool ml3_measurement_validate_config(const ml3_measurement_config_t* config) {
  if (config == NULL) {
    return false;
  }
  if (!ml3_measurement_validate_channel(config->channel_hi)) {
    return false;
  }
  if (!ml3_measurement_validate_channel(config->channel_lo)) {
    return false;
  }
  if (!ml3_measurement_validate_channel(config->channel_v5)) {
    return false;
  }
  if (!ml3_measurement_validate_channel(config->channel_thermistor)) {
    return false;
  }
  if (config->abba_cycles < ML3_MEASUREMENT_MIN_ABBA_CYCLES) {
    return false;
  }
  if (config->abba_cycles > ML3_MEASUREMENT_MAX_ABBA_CYCLES) {
    return false;
  }
  if ((config->warmup_ms < ML3_MEASUREMENT_MIN_WARMUP_MS) ||
      (config->warmup_ms > ML3_MEASUREMENT_MAX_WARMUP_MS)) {
    return false;
  }
  if (config->discharge_threshold_mv == 0U) {
    return false;
  }
  if (config->discharge_timeout_ms == 0U) {
    return false;
  }
  if (config->therm_settle_ms == 0U) {
    return false;
  }
  if (config->vrefint_calibration_word == 0U) {
    return false;
  }
  if (config->vrefint_calibration_word > ADC_PRECISION_VREFINT_CAL_MAX) {
    return false;
  }
  return true;
}

static bool ml3_measurement_validate_timeouts(const adc_precision_timeouts_t* timeouts) {
  if (timeouts == NULL) {
    return false;
  }
  return (timeouts->stop_ms > 0U) &&
    (timeouts->disable_ms > 0U) &&
    (timeouts->calibration_ms > 0U) &&
    (timeouts->ready_ms > 0U) &&
    (timeouts->vref_ready_ms > 0U) &&
    (timeouts->sensor_ready_ms > 0U) &&
    (timeouts->settle_ms > 0U) &&
    (timeouts->conversion_ms > 0U);
}

static bool ml3_measurement_validate_port(const ml3_measurement_port_t* port) {
  if (port == NULL) {
    return false;
  }
  if (port->context == NULL) {
    return false;
  }
  if (port->now_ms == NULL) {
    return false;
  }
  if (port->read_reset_cause == NULL) {
    return false;
  }
  if (port->request_radio_sleep == NULL) {
    return false;
  }
  if (port->configure_analog_pins == NULL) {
    return false;
  }
  if (port->set_power_5v == NULL) {
    return false;
  }
  if (port->set_thermistor_excitation == NULL) {
    return false;
  }
  if (port->watchdog_refresh == NULL) {
    return false;
  }
  if (port->on_process == NULL) {
    return false;
  }
  if (port->on_build_payload == NULL) {
    return false;
  }
  if (port->on_queue == NULL) {
    return false;
  }
  return true;
}

static void ml3_measurement_set_state(ml3_measurement_ctx_t* ctx, ml3_state_t state) {
  if (ctx != NULL) {
    ctx->state = state;
    ctx->state_enter_ms = ctx->port.now_ms(ctx->port.context);
    ctx->abba_substep = 0U;
  }
}

static ml3_measurement_error_t ml3_measurement_map_adc_error(adc_precision_error_t error) {
  if (error == ADC_PRECISION_OK) {
    return ML3_MEASUREMENT_OK;
  }
  if (error == ADC_PRECISION_ERROR_TIMEOUT) {
    return ML3_MEASUREMENT_ERR_ADC_TIMEOUT;
  }
  if (error == ADC_PRECISION_ERROR_OVERRUN) {
    return ML3_MEASUREMENT_ERR_ADC_OVERRUN;
  }
  return ML3_MEASUREMENT_ERR_ADC_INTERNAL;
}

static ml3_measurement_error_t ml3_measurement_map_prepare_adc_error(adc_precision_error_t error) {
  if (error == ADC_PRECISION_OK) {
    return ML3_MEASUREMENT_OK;
  }
  return ML3_MEASUREMENT_ERR_ADC_PREPARE;
}

static uint16_t ml3_measurement_map_adc_fault_bits(ml3_measurement_error_t error) {
  if (error == ML3_MEASUREMENT_ERR_ADC_TIMEOUT) {
    return ML3_MEASUREMENT_FAULT_ADC_TIMEOUT;
  }
  if (error == ML3_MEASUREMENT_ERR_ADC_OVERRUN) {
    return ML3_MEASUREMENT_FAULT_ADC_OVERRUN;
  }
  return ML3_MEASUREMENT_FAULT_ADC_INIT;
}

static uint16_t ml3_measurement_map_prepare_fault_bits(adc_precision_error_t error) {
  if (error == ADC_PRECISION_ERROR_CALIBRATION) {
    return ML3_MEASUREMENT_FAULT_ADC_CAL;
  }
  return ML3_MEASUREMENT_FAULT_ADC_INIT;
}

static void ml3_measurement_update_faults(ml3_measurement_ctx_t* ctx) {
  if (ctx != NULL) {
    ctx->last_result.fault_flags = (uint16_t)ctx->fault_flags;
    ctx->last_result.has_faults = (ctx->fault_flags != 0U);
  }
}

static void ml3_measurement_invalidate_measurement_data(ml3_measurement_result_t* result) {
  if (result == NULL) {
    return;
  }
  result->has_pre_reference_raw = false;
  result->has_post_reference_raw = false;
  result->has_pre_v5_raw = false;
  result->has_post_v5_raw = false;
  result->has_die_temp_raw = false;
  result->has_thermistor_raw = false;
  result->has_vdda_pre_uv = false;
  result->has_vdda_post_uv = false;
  result->has_abba_raw = false;
  result->abba_raw_cycle_count = 0U;
  memset(result->abba_h1_raw, 0, sizeof(result->abba_h1_raw));
  memset(result->abba_l1_raw, 0, sizeof(result->abba_l1_raw));
  memset(result->abba_l2_raw, 0, sizeof(result->abba_l2_raw));
  memset(result->abba_h2_raw, 0, sizeof(result->abba_h2_raw));
  result->has_mean_hi_uv = false;
  result->has_mean_lo_uv = false;
  result->has_common_mode_uv = false;
  result->has_mean_diff_uv = false;
  result->has_median_diff_uv = false;
  result->has_drift_uv = false;
  result->has_sd_uv = false;
  result->has_mad_uv = false;
  result->has_min_diff_uv = false;
  result->has_max_diff_uv = false;
}

static void ml3_measurement_set_fault(
  ml3_measurement_ctx_t* ctx,
  ml3_measurement_error_t error,
  uint16_t fault_bit) {
  if (ctx == NULL) {
    return;
  }
  if (fault_bit != 0U) {
    ctx->fault_flags |= fault_bit;
  }
  ml3_measurement_update_faults(ctx);
  ctx->last_error = error;
}

static bool ml3_measurement_cleanup_controls(ml3_measurement_ctx_t* ctx) {
  bool power_ok = true;
  bool therm_ok = true;

  if ((ctx == NULL) || (!ctx->initialized)) {
    return false;
  }

  if (ctx->port.set_power_5v != NULL) {
    power_ok = ctx->port.set_power_5v(ctx->port.context, false);
  }

  if (ctx->port.set_thermistor_excitation != NULL) {
    therm_ok = ctx->port.set_thermistor_excitation(ctx->port.context, false);
  }

  return power_ok && therm_ok;
}

static void ml3_measurement_set_adc_fault_and_continue(
  ml3_measurement_ctx_t* ctx,
  ml3_measurement_error_t error) {
  if (ctx == NULL) {
    return;
  }
  if ((ctx->state == ML3_STATE_ERROR) || (!ctx->initialized)) {
    return;
  }
  ml3_measurement_set_fault(
    ctx,
    error,
    ml3_measurement_map_adc_fault_bits(error));
  ml3_measurement_invalidate_measurement_data(&ctx->last_result);
  ctx->last_result.has_valid_cycle_count = true;
  ctx->last_result.valid_cycle_count = 0U;

  if (!ml3_measurement_cleanup_controls(ctx)) {
    ctx->active = false;
    ctx->state = ML3_STATE_ERROR;
    return;
  }

  ml3_measurement_set_state(ctx, ML3_STATE_PROCESS);
}

static void ml3_measurement_set_prepare_fault_and_continue(
  ml3_measurement_ctx_t* ctx,
  adc_precision_error_t adc_error) {
  ml3_measurement_error_t error = ml3_measurement_map_prepare_adc_error(adc_error);

  if (ctx == NULL) {
    return;
  }
  if ((ctx->state == ML3_STATE_ERROR) || (!ctx->initialized)) {
    return;
  }
  ml3_measurement_set_fault(
    ctx,
    error,
    ml3_measurement_map_prepare_fault_bits(adc_error));
  ml3_measurement_invalidate_measurement_data(&ctx->last_result);
  ctx->last_result.has_valid_cycle_count = true;
  ctx->last_result.valid_cycle_count = 0U;

  if (!ml3_measurement_cleanup_controls(ctx)) {
    ctx->active = false;
    ctx->state = ML3_STATE_ERROR;
    return;
  }

  ml3_measurement_set_state(ctx, ML3_STATE_PROCESS);
}

static void ml3_measurement_set_error(
  ml3_measurement_ctx_t* ctx,
  ml3_measurement_error_t error,
  uint16_t fault_bit) {
  if (ctx == NULL) {
    return;
  }
  ml3_measurement_set_fault(ctx, error, fault_bit);
  ctx->active = false;
  if (!ml3_measurement_cleanup_controls(ctx)) {
    ctx->state = ML3_STATE_ERROR;
    return;
  }

  if (ctx->state != ML3_STATE_ERROR) {
    ctx->state = ML3_STATE_ERROR;
  }
}

static ml3_measurement_error_t ml3_measurement_read_raw(
  ml3_measurement_ctx_t* ctx,
  uint16_t channel,
  uint16_t* raw) {
  adc_precision_error_t adc_error = ADC_PRECISION_OK;
  if (ctx == NULL || raw == NULL) {
    return ML3_MEASUREMENT_ERR_INVALID_ARGUMENT;
  }
  adc_error = adc_precision_read_raw(ctx->adc_ctx, channel, &ctx->adc_timeouts, raw);
  return ml3_measurement_map_adc_error(adc_error);
}

static ml3_measurement_error_t ml3_measurement_read_channel_uv(
  ml3_measurement_ctx_t* ctx,
  uint16_t channel,
  uint32_t vdda_uv,
  uint16_t* out_raw,
  int64_t* out_uv) {
  uint16_t raw = 0U;
  uint32_t uv = 0U;
  ml3_measurement_error_t err = ML3_MEASUREMENT_OK;
  adc_precision_error_t adc_error = ADC_PRECISION_OK;

  err = ml3_measurement_read_raw(ctx, channel, &raw);
  if (err != ML3_MEASUREMENT_OK) {
    return err;
  }
  adc_error = adc_precision_compute_channel_uv(raw, vdda_uv, &uv);
  if (adc_error != ADC_PRECISION_OK) {
    return ml3_measurement_map_adc_error(adc_error);
  }

  if (out_raw != NULL) {
    *out_raw = raw;
  }
  if (out_uv != NULL) {
    *out_uv = (int64_t)uv;
  }
  return ML3_MEASUREMENT_OK;
}

static ml3_measurement_error_t ml3_measurement_measure_vdda(
  ml3_measurement_ctx_t* ctx,
  uint16_t raw_vref,
  uint32_t* out_vdda_uv) {
  if (ctx == NULL || out_vdda_uv == NULL) {
    return ML3_MEASUREMENT_ERR_INVALID_ARGUMENT;
  }
  if (adc_precision_compute_vdda_uv(
      (uint32_t)ctx->config.vrefint_calibration_word,
      (uint32_t)raw_vref,
      out_vdda_uv) != ADC_PRECISION_OK) {
    return ML3_MEASUREMENT_ERR_ADC_INTERNAL;
  }
  return ML3_MEASUREMENT_OK;
}

static void ml3_measurement_collect_stats(ml3_measurement_ctx_t* ctx) {
  ml3_measurement_result_t stats;
  if (ctx == NULL) {
    return;
  }
  ml3_measurement_compute_uv_stats(
    ctx->cycle_hi_uv,
    ctx->cycle_lo_uv,
    ctx->cycle_valid,
    (size_t)ctx->current_abba_cycle,
    &stats);
  if (stats.has_mean_hi_uv) {
    ctx->last_result.mean_hi_uv = stats.mean_hi_uv;
    ctx->last_result.mean_lo_uv = stats.mean_lo_uv;
    ctx->last_result.common_mode_uv = stats.common_mode_uv;
    ctx->last_result.mean_diff_uv = stats.mean_diff_uv;
    ctx->last_result.median_diff_uv = stats.median_diff_uv;
    ctx->last_result.drift_uv = stats.drift_uv;
    ctx->last_result.sd_uv = stats.sd_uv;
    ctx->last_result.mad_uv = stats.mad_uv;
    ctx->last_result.min_diff_uv = stats.min_diff_uv;
    ctx->last_result.max_diff_uv = stats.max_diff_uv;
  }
  ctx->last_result.has_mean_hi_uv = stats.has_mean_hi_uv;
  ctx->last_result.has_mean_lo_uv = stats.has_mean_lo_uv;
  ctx->last_result.has_common_mode_uv = stats.has_common_mode_uv;
  ctx->last_result.has_mean_diff_uv = stats.has_mean_diff_uv;
  ctx->last_result.has_median_diff_uv = stats.has_median_diff_uv;
  ctx->last_result.has_drift_uv = stats.has_drift_uv;
  ctx->last_result.has_sd_uv = stats.has_sd_uv;
  ctx->last_result.has_mad_uv = stats.has_mad_uv;
  ctx->last_result.has_min_diff_uv = stats.has_min_diff_uv;
  ctx->last_result.has_max_diff_uv = stats.has_max_diff_uv;
  ctx->last_result.has_valid_cycle_count = true;
  ctx->last_result.valid_cycle_count = stats.valid_cycle_count;
  ml3_measurement_update_faults(ctx);
}

static ml3_measurement_step_t ml3_measurement_step_prepare(ml3_measurement_ctx_t* ctx) {
  bool ok = true;
  if ((ctx == NULL) ||
      (ctx->port.request_radio_sleep == NULL) ||
      (ctx->port.configure_analog_pins == NULL)) {
    ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_INVALID_ARGUMENT, 0U);
    return ML3_MEASUREMENT_STEP_ERROR;
  }

  ok = ctx->port.request_radio_sleep(ctx->port.context);
  ok = ok && ctx->port.configure_analog_pins(ctx->port.context);
  if (!ok) {
    ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_CALLBACK, 0U);
    return ML3_MEASUREMENT_STEP_ERROR;
  }

  ok = ctx->port.set_power_5v(ctx->port.context, false);
  if (!ok) {
    ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_CALLBACK, 0U);
    return ML3_MEASUREMENT_STEP_ERROR;
  }
  ok = ctx->port.set_thermistor_excitation(ctx->port.context, false);
  if (!ok) {
    ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_CALLBACK, 0U);
    return ML3_MEASUREMENT_STEP_ERROR;
  }

  ml3_measurement_set_state(ctx, ML3_STATE_POWER_ON);
  return ML3_MEASUREMENT_STEP_BUSY;
}

static ml3_measurement_step_t ml3_measurement_step_power_on(ml3_measurement_ctx_t* ctx) {
  bool ok = false;
  ok = ctx->port.watchdog_refresh(ctx->port.context);
  ok = ok && ctx->port.set_power_5v(ctx->port.context, true);
  if (!ok) {
    ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_CALLBACK, 0U);
    return ML3_MEASUREMENT_STEP_ERROR;
  }
  ml3_measurement_set_state(ctx, ML3_STATE_WARMUP);
  return ML3_MEASUREMENT_STEP_BUSY;
}

static ml3_measurement_step_t ml3_measurement_step_warmup(ml3_measurement_ctx_t* ctx) {
  if (!ml3_measurement_timed_out(ctx, ctx->state_enter_ms, ctx->config.warmup_ms)) {
    return ML3_MEASUREMENT_STEP_BUSY;
  }
  ml3_measurement_set_state(ctx, ML3_STATE_ADC_CONFIGURE);
  return ML3_MEASUREMENT_STEP_BUSY;
}

static ml3_measurement_step_t ml3_measurement_step_adc_configure(ml3_measurement_ctx_t* ctx) {
  ml3_measurement_set_state(ctx, ML3_STATE_ADC_CALIBRATE);
  return ML3_MEASUREMENT_STEP_BUSY;
}

static ml3_measurement_step_t ml3_measurement_step_adc_calibrate(ml3_measurement_ctx_t* ctx) {
  adc_precision_error_t adc_error = ADC_PRECISION_OK;
  if ((ctx == NULL) || (ctx->adc_ctx == NULL)) {
    ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_INVALID_ARGUMENT, 0U);
    return ML3_MEASUREMENT_STEP_ERROR;
  }
  if (ctx->adc_ctx->port != NULL) {
    adc_precision_init(ctx->adc_ctx, ctx->adc_ctx->port, ctx->adc_ctx->port_ctx);
  }
  adc_error = adc_precision_prepare(ctx->adc_ctx, &ctx->adc_timeouts);
  if (adc_error != ADC_PRECISION_OK) {
    ml3_measurement_set_prepare_fault_and_continue(ctx, adc_error);
    return ML3_MEASUREMENT_STEP_BUSY;
  }
  ml3_measurement_set_state(ctx, ML3_STATE_REFERENCE_PRE);
  return ML3_MEASUREMENT_STEP_BUSY;
}

static ml3_measurement_step_t ml3_measurement_step_reference_pre(ml3_measurement_ctx_t* ctx) {
  ml3_measurement_error_t err = ML3_MEASUREMENT_OK;
  uint16_t vref_raw = 0U;
  uint32_t vdda_uv = 0U;

  err = ml3_measurement_read_raw(ctx, ML3_MEASUREMENT_CHANNEL_VREFINT, &vref_raw);
  if (err != ML3_MEASUREMENT_OK) {
    ml3_measurement_set_adc_fault_and_continue(ctx, err);
    return ML3_MEASUREMENT_STEP_BUSY;
  }
  err = ml3_measurement_measure_vdda(ctx, vref_raw, &vdda_uv);
  if (err != ML3_MEASUREMENT_OK) {
    ml3_measurement_set_adc_fault_and_continue(ctx, err);
    return ML3_MEASUREMENT_STEP_BUSY;
  }

  ctx->last_result.pre_reference_raw = vref_raw;
  err = ml3_measurement_read_channel_uv(ctx, ctx->config.channel_v5, vdda_uv, &ctx->last_result.pre_v5_raw, NULL);
  if (err != ML3_MEASUREMENT_OK) {
    ml3_measurement_set_adc_fault_and_continue(ctx, err);
    return ML3_MEASUREMENT_STEP_BUSY;
  }
  ctx->last_result.has_pre_reference_raw = true;
  ctx->last_result.has_pre_v5_raw = true;
  ctx->last_result.has_vdda_pre_uv = true;

  ctx->last_result.vdda_pre_uv = vdda_uv;
  ctx->last_result.has_sequence = true;
  ml3_measurement_set_state(ctx, ML3_STATE_SAMPLE_ABBA);
  return ML3_MEASUREMENT_STEP_BUSY;
}

static ml3_measurement_step_t ml3_measurement_step_sample_abba(ml3_measurement_ctx_t* ctx) {
  ml3_measurement_error_t err = ML3_MEASUREMENT_OK;

  if (ctx->abba_substep == ML3_MEASUREMENT_ABBA_PHASE_H1) {
    if ((ctx->current_abba_cycle == 0U) && !ctx->port.watchdog_refresh(ctx->port.context)) {
      ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_CALLBACK, 0U);
      return ML3_MEASUREMENT_STEP_ERROR;
    }
    err = ml3_measurement_read_channel_uv(
      ctx,
      ctx->config.channel_hi,
      ctx->last_result.vdda_pre_uv,
      &ctx->abba_h1_raw,
      &ctx->abba_h1_uv);
    if (err != ML3_MEASUREMENT_OK) {
      ml3_measurement_set_adc_fault_and_continue(ctx, err);
      return ML3_MEASUREMENT_STEP_BUSY;
    }
    ++ctx->abba_substep;
    return ML3_MEASUREMENT_STEP_BUSY;
  }

  if (ctx->abba_substep == ML3_MEASUREMENT_ABBA_PHASE_L1) {
    err = ml3_measurement_read_channel_uv(
      ctx,
      ctx->config.channel_lo,
      ctx->last_result.vdda_pre_uv,
      &ctx->abba_l1_raw,
      &ctx->abba_l1_uv);
    if (err != ML3_MEASUREMENT_OK) {
      ml3_measurement_set_adc_fault_and_continue(ctx, err);
      return ML3_MEASUREMENT_STEP_BUSY;
    }
    ++ctx->abba_substep;
    return ML3_MEASUREMENT_STEP_BUSY;
  }

  if (ctx->abba_substep == ML3_MEASUREMENT_ABBA_PHASE_L2) {
    err = ml3_measurement_read_channel_uv(
      ctx,
      ctx->config.channel_lo,
      ctx->last_result.vdda_pre_uv,
      &ctx->abba_l2_raw,
      &ctx->abba_l2_uv);
    if (err != ML3_MEASUREMENT_OK) {
      ml3_measurement_set_adc_fault_and_continue(ctx, err);
      return ML3_MEASUREMENT_STEP_BUSY;
    }
    ++ctx->abba_substep;
    return ML3_MEASUREMENT_STEP_BUSY;
  }

  err = ml3_measurement_read_channel_uv(
    ctx,
    ctx->config.channel_hi,
    ctx->last_result.vdda_pre_uv,
    &ctx->abba_h2_raw,
    &ctx->abba_h2_uv);
  if (err != ML3_MEASUREMENT_OK) {
    ml3_measurement_set_adc_fault_and_continue(ctx, err);
    return ML3_MEASUREMENT_STEP_BUSY;
  }

  if (ctx->current_abba_cycle < ctx->config.abba_cycles) {
    ctx->last_result.abba_h1_raw[ctx->current_abba_cycle] = ctx->abba_h1_raw;
    ctx->last_result.abba_l1_raw[ctx->current_abba_cycle] = ctx->abba_l1_raw;
    ctx->last_result.abba_l2_raw[ctx->current_abba_cycle] = ctx->abba_l2_raw;
    ctx->last_result.abba_h2_raw[ctx->current_abba_cycle] = ctx->abba_h2_raw;
    ctx->cycle_hi_uv[ctx->current_abba_cycle] =
      (ctx->abba_h1_uv + ctx->abba_h2_uv) / 2LL;
    ctx->cycle_lo_uv[ctx->current_abba_cycle] =
      (ctx->abba_l1_uv + ctx->abba_l2_uv) / 2LL;
    ctx->cycle_valid[ctx->current_abba_cycle] = true;
    ++ctx->current_abba_cycle;
  }

  if (ctx->current_abba_cycle >= ctx->config.abba_cycles) {
    ctx->last_result.abba_raw_cycle_count = ctx->current_abba_cycle;
    ctx->last_result.has_abba_raw = true;
    if (!ctx->port.watchdog_refresh(ctx->port.context)) {
      ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_CALLBACK, 0U);
      return ML3_MEASUREMENT_STEP_ERROR;
    }
  }

  ctx->abba_substep = ML3_MEASUREMENT_ABBA_PHASE_H1;
  if (ctx->current_abba_cycle >= ctx->config.abba_cycles) {
    ml3_measurement_set_state(ctx, ML3_STATE_REFERENCE_POST);
  }
  return ML3_MEASUREMENT_STEP_BUSY;
}

static ml3_measurement_step_t ml3_measurement_step_reference_post(ml3_measurement_ctx_t* ctx) {
  ml3_measurement_error_t err = ML3_MEASUREMENT_OK;
  uint16_t vref_raw = 0U;

  if ((ctx == NULL) || (ctx->adc_ctx == NULL) || (ctx->current_abba_cycle == 0U)) {
    ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_INVALID_ARGUMENT, 0U);
    return ML3_MEASUREMENT_STEP_ERROR;
  }

  err = ml3_measurement_read_raw(ctx, ML3_MEASUREMENT_CHANNEL_VREFINT, &vref_raw);
  if (err != ML3_MEASUREMENT_OK) {
    ml3_measurement_set_adc_fault_and_continue(ctx, err);
    return ML3_MEASUREMENT_STEP_BUSY;
  }

  err = ml3_measurement_measure_vdda(ctx, vref_raw, &ctx->last_result.vdda_post_uv);
  if (err != ML3_MEASUREMENT_OK) {
    ml3_measurement_set_adc_fault_and_continue(ctx, err);
    return ML3_MEASUREMENT_STEP_BUSY;
  }
  ctx->last_result.post_reference_raw = vref_raw;
  ctx->last_result.has_post_reference_raw = true;
  ctx->last_result.has_vdda_post_uv = true;

  err = ml3_measurement_read_channel_uv(
    ctx,
    ctx->config.channel_v5,
    ctx->last_result.vdda_post_uv,
    &ctx->last_result.post_v5_raw,
    NULL);
  if (err != ML3_MEASUREMENT_OK) {
    ml3_measurement_set_adc_fault_and_continue(ctx, err);
    return ML3_MEASUREMENT_STEP_BUSY;
  }
  ctx->last_result.has_post_v5_raw = true;

  err = ml3_measurement_read_channel_uv(
    ctx,
    ML3_MEASUREMENT_CHANNEL_DIE_TEMP,
    ctx->last_result.vdda_post_uv,
    &ctx->last_result.die_temp_raw,
    NULL);
  if (err != ML3_MEASUREMENT_OK) {
    ml3_measurement_set_adc_fault_and_continue(ctx, err);
    return ML3_MEASUREMENT_STEP_BUSY;
  }
  ctx->last_result.has_die_temp_raw = true;

  ml3_measurement_collect_stats(ctx);
  ml3_measurement_set_state(ctx, ML3_STATE_POWER_OFF);
  return ML3_MEASUREMENT_STEP_BUSY;
}

static ml3_measurement_step_t ml3_measurement_step_power_off(ml3_measurement_ctx_t* ctx) {
  bool ok = true;
  if (ctx == NULL) {
    return ML3_MEASUREMENT_STEP_ERROR;
  }
  ok = ctx->port.set_power_5v(ctx->port.context, false);
  if (!ok) {
    ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_CALLBACK, 0U);
    return ML3_MEASUREMENT_STEP_ERROR;
  }
  ml3_measurement_set_state(ctx, ML3_STATE_VERIFY_DISCHARGE);
  return ML3_MEASUREMENT_STEP_BUSY;
}

static ml3_measurement_step_t ml3_measurement_step_verify_discharge(ml3_measurement_ctx_t* ctx) {
  ml3_measurement_error_t err = ML3_MEASUREMENT_OK;
  uint16_t discharge_vref_raw = 0U;
  uint16_t discharge_raw = 0U;
  uint32_t discharge_vdda_uv = 0U;
  uint32_t scaling_vdda_uv = 0U;
  uint32_t pa4_uv = 0U;
  uint64_t discharge_uv_uV = 0ULL;
  uint64_t threshold_uv = 0ULL;

  if (ctx == NULL) {
    return ML3_MEASUREMENT_STEP_ERROR;
  }

  err = ml3_measurement_read_raw(ctx, ML3_MEASUREMENT_CHANNEL_VREFINT, &discharge_vref_raw);
  if (err != ML3_MEASUREMENT_OK) {
    ml3_measurement_set_adc_fault_and_continue(ctx, err);
    return ML3_MEASUREMENT_STEP_BUSY;
  }
  err = ml3_measurement_measure_vdda(ctx, discharge_vref_raw, &discharge_vdda_uv);
  if (err != ML3_MEASUREMENT_OK) {
    ml3_measurement_set_adc_fault_and_continue(ctx, err);
    return ML3_MEASUREMENT_STEP_BUSY;
  }
  scaling_vdda_uv = discharge_vdda_uv;
  if (scaling_vdda_uv < ctx->last_result.vdda_post_uv) {
    scaling_vdda_uv = ctx->last_result.vdda_post_uv;
  }

  err = ml3_measurement_read_raw(ctx, ctx->config.channel_v5, &discharge_raw);
  if (err != ML3_MEASUREMENT_OK) {
    ml3_measurement_set_adc_fault_and_continue(ctx, err);
    return ML3_MEASUREMENT_STEP_BUSY;
  }

  if (adc_precision_compute_channel_uv(
      (uint32_t)discharge_raw,
      scaling_vdda_uv,
      &pa4_uv) != ADC_PRECISION_OK) {
    ml3_measurement_set_adc_fault_and_continue(ctx, ML3_MEASUREMENT_ERR_ADC_INTERNAL);
    return ML3_MEASUREMENT_STEP_BUSY;
  }

  discharge_uv_uV = (uint64_t)pa4_uv;
  threshold_uv = (uint64_t)ctx->config.discharge_threshold_mv * 1000ULL;
  if (discharge_uv_uV <= threshold_uv) {
    ml3_measurement_set_state(ctx, ML3_STATE_SAMPLE_THERMISTOR);
    return ML3_MEASUREMENT_STEP_BUSY;
  }

  if (ml3_measurement_timed_out(ctx, ctx->state_enter_ms, ctx->config.discharge_timeout_ms)) {
    ctx->discharge_timed_out = true;
    ml3_measurement_set_fault(ctx, ML3_MEASUREMENT_OK, ML3_MEASUREMENT_FAULT_THERM_FAULT);
    ml3_measurement_set_state(ctx, ML3_STATE_SAMPLE_THERMISTOR);
  }
  return ML3_MEASUREMENT_STEP_BUSY;
}

static ml3_measurement_step_t ml3_measurement_step_sample_thermistor(ml3_measurement_ctx_t* ctx) {
  ml3_measurement_error_t err = ML3_MEASUREMENT_OK;
  if (ctx == NULL) {
    return ML3_MEASUREMENT_STEP_ERROR;
  }

  if (ctx->discharge_timed_out) {
    if (!ctx->port.set_thermistor_excitation(ctx->port.context, false)) {
      ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_CALLBACK, 0U);
      return ML3_MEASUREMENT_STEP_ERROR;
    }
    ml3_measurement_set_state(ctx, ML3_STATE_PROCESS);
    return ML3_MEASUREMENT_STEP_BUSY;
  }

  if (ctx->abba_substep == ML3_MEASUREMENT_THERM_PHASE_OFF) {
    if (!ctx->port.set_thermistor_excitation(ctx->port.context, true)) {
      ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_CALLBACK, 0U);
      return ML3_MEASUREMENT_STEP_ERROR;
    }
    ctx->state_enter_ms = ctx->port.now_ms(ctx->port.context);
    ctx->abba_substep = ML3_MEASUREMENT_THERM_PHASE_WAIT_SETTLE;
    return ML3_MEASUREMENT_STEP_BUSY;
  }

  if (ctx->abba_substep == ML3_MEASUREMENT_THERM_PHASE_WAIT_SETTLE) {
    if (!ml3_measurement_timed_out(ctx, ctx->state_enter_ms, ctx->config.therm_settle_ms)) {
      return ML3_MEASUREMENT_STEP_BUSY;
    }
    ctx->abba_substep = ML3_MEASUREMENT_THERM_PHASE_READ;
  }

  if (ctx->abba_substep == ML3_MEASUREMENT_THERM_PHASE_READ) {
    err = ml3_measurement_read_raw(ctx, ctx->config.channel_thermistor, &ctx->last_result.thermistor_raw);
    if (err != ML3_MEASUREMENT_OK) {
      ml3_measurement_set_adc_fault_and_continue(ctx, err);
      return ML3_MEASUREMENT_STEP_BUSY;
    }
    ctx->last_result.has_thermistor_raw = true;
    ctx->abba_substep = ML3_MEASUREMENT_THERM_PHASE_OFF;
    if (!ctx->port.set_thermistor_excitation(ctx->port.context, false)) {
      ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_CALLBACK, 0U);
      return ML3_MEASUREMENT_STEP_ERROR;
    }
    ml3_measurement_set_state(ctx, ML3_STATE_PROCESS);
  }
  return ML3_MEASUREMENT_STEP_BUSY;
}

static ml3_measurement_step_t ml3_measurement_step_process(ml3_measurement_ctx_t* ctx) {
  if (ctx == NULL) {
    return ML3_MEASUREMENT_STEP_ERROR;
  }
  if ((ctx->port.on_process == NULL) ||
      !ctx->port.on_process(ctx->port.context, &ctx->last_result)) {
    ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_CALLBACK, 0U);
    return ML3_MEASUREMENT_STEP_ERROR;
  }
  ml3_measurement_set_state(ctx, ML3_STATE_BUILD_PAYLOAD);
  return ML3_MEASUREMENT_STEP_BUSY;
}

static ml3_measurement_step_t ml3_measurement_step_build_payload(ml3_measurement_ctx_t* ctx) {
  if (ctx == NULL) {
    return ML3_MEASUREMENT_STEP_ERROR;
  }
  if ((ctx->port.on_build_payload == NULL) ||
      !ctx->port.on_build_payload(ctx->port.context, &ctx->last_result)) {
    ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_CALLBACK, 0U);
    return ML3_MEASUREMENT_STEP_ERROR;
  }
  ml3_measurement_set_state(ctx, ML3_STATE_QUEUE_TX);
  return ML3_MEASUREMENT_STEP_BUSY;
}

static ml3_measurement_step_t ml3_measurement_step_queue_tx(ml3_measurement_ctx_t* ctx) {
  if (ctx == NULL) {
    return ML3_MEASUREMENT_STEP_ERROR;
  }
  if (ctx->port.on_queue == NULL) {
    ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_CALLBACK, 0U);
    return ML3_MEASUREMENT_STEP_ERROR;
  }
  if (!ctx->port.watchdog_refresh(ctx->port.context)) {
    ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_CALLBACK, 0U);
    return ML3_MEASUREMENT_STEP_ERROR;
  }
  if (!ctx->port.on_queue(ctx->port.context, &ctx->last_result)) {
    ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_CALLBACK, 0U);
    return ML3_MEASUREMENT_STEP_ERROR;
  }
  ctx->active = false;
  ml3_measurement_set_state(ctx, ML3_STATE_IDLE);
  return ML3_MEASUREMENT_STEP_DONE;
}

ml3_measurement_error_t ml3_measurement_init(
  ml3_measurement_ctx_t* ctx,
  const ml3_measurement_config_t* config,
  const ml3_measurement_port_t* port,
  adc_precision_context_t* adc_ctx,
  const adc_precision_timeouts_t* adc_timeouts) {
  size_t i;
  if ((ctx == NULL) || (config == NULL) || (port == NULL) || (adc_ctx == NULL) ||
    (adc_timeouts == NULL)) {
    return ML3_MEASUREMENT_ERR_INVALID_ARGUMENT;
  }
  if (!ml3_measurement_validate_config(config)) {
    return ML3_MEASUREMENT_ERR_INVALID_ARGUMENT;
  }
  if (!ml3_measurement_validate_timeouts(adc_timeouts)) {
    return ML3_MEASUREMENT_ERR_INVALID_ARGUMENT;
  }
  if (!ml3_measurement_validate_port(port)) {
    return ML3_MEASUREMENT_ERR_INVALID_ARGUMENT;
  }

  memset(ctx, 0, sizeof(*ctx));
  ctx->config = *config;
  ctx->port = *port;
  ctx->adc_ctx = adc_ctx;
  ctx->adc_timeouts = *adc_timeouts;
  ctx->initialized = true;
  ctx->state = ML3_STATE_IDLE;
  ctx->state_enter_ms = ctx->port.now_ms(ctx->port.context);
  ctx->last_error = ML3_MEASUREMENT_OK;
  ctx->sequence = 0U;
  ctx->last_result.sequence = 0U;
  ctx->last_result.has_sequence = true;
  ctx->last_result.has_faults = false;
  ctx->fault_flags = 0U;
  ctx->last_result.fault_flags = 0U;

  for (i = 0U; i < ML3_MEASUREMENT_MAX_ABBA_CYCLES; ++i) {
    ctx->cycle_hi_uv[i] = 0;
    ctx->cycle_lo_uv[i] = 0;
    ctx->cycle_valid[i] = false;
  }
  return ML3_MEASUREMENT_OK;
}

ml3_measurement_error_t ml3_measurement_start(ml3_measurement_ctx_t* ctx) {
  size_t i;
  if (ctx == NULL) {
    return ML3_MEASUREMENT_ERR_INVALID_ARGUMENT;
  }
  if (!ctx->initialized) {
    return ML3_MEASUREMENT_ERR_INVALID_ARGUMENT;
  }
  if ((ctx->active) || (ctx->state != ML3_STATE_IDLE)) {
    return ML3_MEASUREMENT_ERR_BUSY;
  }
  if (ctx->port.read_reset_cause == NULL) {
    return ML3_MEASUREMENT_ERR_INVALID_ARGUMENT;
  }

  ml3_measurement_clear_result(&ctx->last_result);
  ctx->last_result.reset_cause =
    ctx->port.read_reset_cause(ctx->port.context);
  ctx->last_result.has_reset_cause = true;
  ++ctx->sequence;
  ctx->last_error = ML3_MEASUREMENT_OK;
  ctx->fault_flags = 0U;
  ctx->last_result.fault_flags = 0U;
  ctx->last_result.has_faults = false;
  ctx->last_result.has_sequence = true;
  ctx->last_result.sequence = ctx->sequence;
  ctx->state = ML3_STATE_PREPARE;
  ctx->state_enter_ms = ctx->port.now_ms(ctx->port.context);
  ctx->active = true;
  ctx->current_abba_cycle = 0U;
  ctx->abba_substep = ML3_MEASUREMENT_ABBA_PHASE_H1;
  ctx->discharge_timed_out = false;
  ctx->abba_h1_uv = 0;
  ctx->abba_l1_uv = 0;
  ctx->abba_l2_uv = 0;
  ctx->abba_h2_uv = 0;
  ctx->abba_h1_raw = 0U;
  ctx->abba_l1_raw = 0U;
  ctx->abba_l2_raw = 0U;
  ctx->abba_h2_raw = 0U;

  for (i = 0U; i < ML3_MEASUREMENT_MAX_ABBA_CYCLES; ++i) {
    ctx->cycle_hi_uv[i] = 0;
    ctx->cycle_lo_uv[i] = 0;
    ctx->cycle_valid[i] = false;
  }
  return ML3_MEASUREMENT_OK;
}

ml3_measurement_step_t ml3_measurement_step(ml3_measurement_ctx_t* ctx) {
  if (ctx == NULL) {
    return ML3_MEASUREMENT_STEP_ERROR;
  }
  if (!ctx->initialized) {
    return ML3_MEASUREMENT_STEP_ERROR;
  }

  switch (ctx->state) {
    case ML3_STATE_IDLE:
      return ML3_MEASUREMENT_STEP_DONE;
    case ML3_STATE_PREPARE:
      return ml3_measurement_step_prepare(ctx);
    case ML3_STATE_POWER_ON:
      return ml3_measurement_step_power_on(ctx);
    case ML3_STATE_WARMUP:
      return ml3_measurement_step_warmup(ctx);
    case ML3_STATE_ADC_CONFIGURE:
      return ml3_measurement_step_adc_configure(ctx);
    case ML3_STATE_ADC_CALIBRATE:
      return ml3_measurement_step_adc_calibrate(ctx);
    case ML3_STATE_REFERENCE_PRE:
      return ml3_measurement_step_reference_pre(ctx);
    case ML3_STATE_SAMPLE_ABBA:
      return ml3_measurement_step_sample_abba(ctx);
    case ML3_STATE_REFERENCE_POST:
      return ml3_measurement_step_reference_post(ctx);
    case ML3_STATE_POWER_OFF:
      return ml3_measurement_step_power_off(ctx);
    case ML3_STATE_VERIFY_DISCHARGE:
      return ml3_measurement_step_verify_discharge(ctx);
    case ML3_STATE_SAMPLE_THERMISTOR:
      return ml3_measurement_step_sample_thermistor(ctx);
    case ML3_STATE_PROCESS:
      return ml3_measurement_step_process(ctx);
    case ML3_STATE_BUILD_PAYLOAD:
      return ml3_measurement_step_build_payload(ctx);
    case ML3_STATE_QUEUE_TX:
      return ml3_measurement_step_queue_tx(ctx);
    case ML3_STATE_ERROR:
      ctx->active = false;
      (void)ml3_measurement_cleanup_controls(ctx);
      return ML3_MEASUREMENT_STEP_ERROR;
    default:
      ml3_measurement_set_error(ctx, ML3_MEASUREMENT_ERR_STATE, 0U);
      return ML3_MEASUREMENT_STEP_ERROR;
  }
}

void ml3_measurement_abort(ml3_measurement_ctx_t* ctx) {
  if (ctx == NULL) {
    return;
  }
  ctx->active = false;
  ctx->last_error = ML3_MEASUREMENT_ERR_ABORTED;

  if (!ctx->initialized) {
    ctx->state = ML3_STATE_IDLE;
    return;
  }

  if (ml3_measurement_cleanup_controls(ctx)) {
    ctx->state = ML3_STATE_IDLE;
    return;
  }

  ctx->state = ML3_STATE_ERROR;
}

ml3_state_t ml3_measurement_state(const ml3_measurement_ctx_t* ctx) {
  if (ctx == NULL) {
    return ML3_STATE_IDLE;
  }
  return ctx->state;
}

const ml3_measurement_result_t* ml3_measurement_last_result(const ml3_measurement_ctx_t* ctx) {
  if (ctx == NULL) {
    return NULL;
  }
  return &ctx->last_result;
}

ml3_measurement_error_t ml3_measurement_last_error(const ml3_measurement_ctx_t* ctx) {
  if (ctx == NULL) {
    return ML3_MEASUREMENT_ERR_INVALID_ARGUMENT;
  }
  return ctx->last_error;
}

static void ml3_measurement_sort_int64(int64_t* values, size_t size) {
  size_t i = 0U;
  size_t j = 0U;
  int64_t value = 0LL;

  for (i = 1U; i < size; ++i) {
    value = values[i];
    j = i;
    while ((j > 0U) && (values[j - 1U] > value)) {
      values[j] = values[j - 1U];
      --j;
    }
    values[j] = value;
  }
}

void ml3_measurement_compute_uv_stats(
  const int64_t* cycle_hi_uv,
  const int64_t* cycle_lo_uv,
  const bool* cycle_valid,
  size_t cycle_count,
  ml3_measurement_result_t* out) {
  size_t i = 0U;
  size_t valid_count = 0U;
  size_t first_valid = 0U;
  size_t last_valid = 0U;
  size_t usable_count = cycle_count;
  int64_t first_diff = 0LL;
  int64_t last_diff = 0LL;
  int64_t sum_hi = 0LL;
  int64_t sum_lo = 0LL;
  int64_t sum_diff = 0LL;
  int64_t diffs[ML3_MEASUREMENT_MAX_ABBA_CYCLES];
  int64_t mad_values[ML3_MEASUREMENT_MAX_ABBA_CYCLES];
  int64_t mean_hi = 0LL;
  int64_t mean_lo = 0LL;
  int64_t mean_diff = 0LL;
  uint64_t centered_square_quotient = 0ULL;
  uint64_t centered_square_remainder_sum = 0ULL;
  uint64_t variance = 0ULL;
  uint64_t deviation_square_remainder = 0ULL;
  uint64_t mean_remainder_square = 0ULL;
  uint64_t mean_offset_square = 0ULL;
  size_t diff_index = 0U;
  int64_t center = 0LL;
  int64_t mean_offset = 0LL;
  int64_t mean_remainder = 0LL;
  int64_t centered_correction_numerator = 0LL;
  int64_t variance_correction = 0LL;
  const int64_t max_uv = (int64_t)UINT32_MAX;

  if (out == NULL) {
    return;
  }
  ml3_measurement_clear_result(out);
  out->has_valid_cycle_count = false;
  out->valid_cycle_count = 0U;

  if (cycle_count == 0U || cycle_hi_uv == NULL || cycle_lo_uv == NULL ||
      cycle_valid == NULL) {
    return;
  }
  if (usable_count > (size_t)ML3_MEASUREMENT_MAX_ABBA_CYCLES) {
    return;
  }

  for (i = 0U; i < usable_count; ++i) {
    if (cycle_valid[i]) {
      if ((cycle_hi_uv[i] < 0LL) || (cycle_hi_uv[i] > max_uv) ||
          (cycle_lo_uv[i] < 0LL) || (cycle_lo_uv[i] > max_uv)) {
        return;
      }
      int64_t diff = cycle_hi_uv[i] - cycle_lo_uv[i];
      if (valid_count == 0U) {
        first_valid = i;
      }
      last_valid = i;
      sum_hi += cycle_hi_uv[i];
      sum_lo += cycle_lo_uv[i];
      sum_diff += diff;
      ++valid_count;
    }
  }

  out->valid_cycle_count = (uint16_t)valid_count;
  out->has_valid_cycle_count = true;
  if (valid_count == 0U) {
    return;
  }

  if (valid_count < (size_t)ML3_MEASUREMENT_FIXED_MIN_VALID_CYCLES) {
    return;
  }

  mean_hi = sum_hi / (int64_t)valid_count;
  mean_lo = sum_lo / (int64_t)valid_count;
  mean_diff = sum_diff / (int64_t)valid_count;
  out->mean_hi_uv = mean_hi;
  out->mean_lo_uv = mean_lo;
  out->common_mode_uv = mean_lo;
  out->mean_diff_uv = mean_diff;
  out->has_mean_hi_uv = true;
  out->has_mean_lo_uv = true;
  out->has_common_mode_uv = true;
  out->has_mean_diff_uv = true;

  first_diff = cycle_hi_uv[first_valid] - cycle_lo_uv[first_valid];
  last_diff = cycle_hi_uv[last_valid] - cycle_lo_uv[last_valid];
  out->drift_uv = last_diff - first_diff;
  out->has_drift_uv = true;

  for (i = 0U; i < usable_count; ++i) {
    if (cycle_valid[i]) {
      diffs[diff_index] = cycle_hi_uv[i] - cycle_lo_uv[i];
      ++diff_index;
    }
  }

  ml3_measurement_sort_int64(diffs, diff_index);
  if ((diff_index & 1U) == 0U) {
    out->median_diff_uv = ml3_measurement_safe_average_int64(
      diffs[(diff_index / 2U) - 1U],
      diffs[diff_index / 2U]);
  } else {
    out->median_diff_uv = diffs[diff_index / 2U];
  }
  out->has_median_diff_uv = true;
  out->min_diff_uv = diffs[0U];
  out->max_diff_uv = diffs[diff_index - 1U];
  out->has_min_diff_uv = true;
  out->has_max_diff_uv = true;

  center = ml3_measurement_safe_average_int64(
    out->min_diff_uv,
    out->max_diff_uv);
  mean_offset = mean_diff - center;
  mean_remainder = sum_diff - ((int64_t)diff_index * mean_diff);
  /* The loop accumulates floor(sum(z^2)/n) and sum(z^2) mod n for
   * z=x-center. With d=mean-center and r=sum-n*mean, the later floor-div
   * correction derives floor(A/n) and A mod n for A=sum((x-mean)^2).
   * Exact floor variance is floor(A/n)-1 iff (A mod n)*n<r^2. */
  for (i = 0U; i < diff_index; ++i) {
    const uint64_t centered =
      (uint64_t)ml3_measurement_abs_i64(diffs[i] - center);
    const uint64_t centered_square = centered * centered;
    centered_square_quotient += centered_square / (uint64_t)diff_index;
    centered_square_remainder_sum += centered_square % (uint64_t)diff_index;
  }
  centered_square_quotient +=
    centered_square_remainder_sum / (uint64_t)diff_index;
  mean_offset_square =
    (uint64_t)ml3_measurement_abs_i64(mean_offset) *
    (uint64_t)ml3_measurement_abs_i64(mean_offset);
  centered_correction_numerator =
    (int64_t)(centered_square_remainder_sum % (uint64_t)diff_index) -
    (2LL * mean_offset * mean_remainder);
  variance_correction = ml3_measurement_floor_div_i64(
    centered_correction_numerator,
    (int64_t)diff_index);
  deviation_square_remainder = (uint64_t)(
    centered_correction_numerator -
    (variance_correction * (int64_t)diff_index));

  if (centered_square_quotient >= mean_offset_square) {
    variance = centered_square_quotient - mean_offset_square;
  } else {
    const uint64_t deficit = mean_offset_square - centered_square_quotient;
    if ((variance_correction < 0LL) ||
        ((uint64_t)variance_correction < deficit)) {
      ml3_measurement_clear_result(out);
      return;
    }
    variance = (uint64_t)variance_correction - deficit;
    variance_correction = 0LL;
  }
  if (variance_correction < 0LL) {
    const uint64_t correction_magnitude =
      (uint64_t)ml3_measurement_abs_i64(variance_correction);
    if (variance < correction_magnitude) {
      ml3_measurement_clear_result(out);
      return;
    }
    variance -= correction_magnitude;
  } else if ((UINT64_MAX - variance) < (uint64_t)variance_correction) {
    ml3_measurement_clear_result(out);
    return;
  } else {
    variance += (uint64_t)variance_correction;
  }
  mean_remainder_square =
    (uint64_t)ml3_measurement_abs_i64(mean_remainder) *
    (uint64_t)ml3_measurement_abs_i64(mean_remainder);
  if ((deviation_square_remainder * (uint64_t)diff_index) <
      mean_remainder_square) {
    if (variance == 0ULL) {
      ml3_measurement_clear_result(out);
      return;
    }
    --variance;
  }
  out->sd_uv = (int64_t)ml3_measurement_u64_floor_sqrt(variance);
  out->has_sd_uv = true;

  for (i = 0U; i < diff_index; ++i) {
    mad_values[i] = ml3_measurement_abs_i64(diffs[i] - out->median_diff_uv);
  }
  ml3_measurement_sort_int64(mad_values, diff_index);
  if ((diff_index & 1U) == 0U) {
    out->mad_uv = ml3_measurement_safe_average_int64(
      mad_values[(diff_index / 2U) - 1U],
      mad_values[diff_index / 2U]);
  } else {
    out->mad_uv = mad_values[diff_index / 2U];
  }
  out->has_mad_uv = true;
}
