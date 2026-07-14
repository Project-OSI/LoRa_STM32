#include "adc_precision.h"

#include <stddef.h>

#ifdef ADC_PRECISION_DEBUG
#include <stdio.h>
#define ADC_PRECISION_TRACE(message) fprintf(stderr, "ADC_PRECISION_TRACE: %s\n", message)
#else
#define ADC_PRECISION_TRACE(message) (void)0
#endif

#define ADC_PRECISION_DEFAULT_TIMEOUT_MS 5U

static const adc_precision_config_t k_adc_precision_config = {
  ADC_PRECISION_NATIVE_RESOLUTION,
  ADC_PRECISION_SAMPLE_CYCLES_X2,
  ADC_PRECISION_OVERSAMPLING,
  ADC_PRECISION_OVERSAMPLING_SHIFT,
  ADC_PRECISION_ADC_CLOCK_HZ,
  true,
  false,
  false,
};

static bool validate_port(const adc_precision_port_t* port);
static void invalidate_prepared_cycle(adc_precision_context_t* context);
static adc_precision_error_t validate_timeouts(
  const adc_precision_timeouts_t* timeouts,
  adc_precision_timeouts_t* resolved);
static adc_precision_error_t wait_for(
  const adc_precision_port_t* port,
  void* port_ctx,
  uint32_t timeout_ms,
  bool (*predicate)(void*));
static adc_precision_error_t do_single_conversion(
  const adc_precision_port_t* port,
  void* port_ctx,
  uint16_t channel,
  const adc_precision_timeouts_t* timeouts,
  uint16_t* out_code);

static adc_precision_error_t set_adcs(
  const adc_precision_port_t* port,
  void* port_ctx,
  const adc_precision_timeouts_t* timeouts);

void adc_precision_init(adc_precision_context_t* context, const adc_precision_port_t* port, void* port_ctx) {
  if ((context == NULL) || (port == NULL)) {
    return;
  }
  context->port = port;
  context->port_ctx = port_ctx;
  context->acquisition_prepared = false;
  context->has_retained_channel = false;
  context->retained_channel = 0U;
  context->resolved_timeouts.stop_ms = 0U;
  context->resolved_timeouts.disable_ms = 0U;
  context->resolved_timeouts.calibration_ms = 0U;
  context->resolved_timeouts.ready_ms = 0U;
  context->resolved_timeouts.vref_ready_ms = 0U;
  context->resolved_timeouts.sensor_ready_ms = 0U;
  context->resolved_timeouts.settle_ms = 0U;
  context->resolved_timeouts.conversion_ms = 0U;
}

adc_precision_error_t adc_precision_default_timeouts(adc_precision_timeouts_t* timeouts) {
  if (timeouts == NULL) {
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }
  timeouts->stop_ms = ADC_PRECISION_DEFAULT_TIMEOUT_MS;
  timeouts->disable_ms = ADC_PRECISION_DEFAULT_TIMEOUT_MS;
  timeouts->calibration_ms = ADC_PRECISION_DEFAULT_TIMEOUT_MS;
  timeouts->ready_ms = ADC_PRECISION_DEFAULT_TIMEOUT_MS;
  timeouts->vref_ready_ms = ADC_PRECISION_DEFAULT_TIMEOUT_MS;
  timeouts->sensor_ready_ms = ADC_PRECISION_DEFAULT_TIMEOUT_MS;
  timeouts->settle_ms = ADC_PRECISION_DEFAULT_TIMEOUT_MS;
  timeouts->conversion_ms = ADC_PRECISION_DEFAULT_TIMEOUT_MS;
  return ADC_PRECISION_OK;
}

adc_precision_error_t adc_precision_prepare(
  adc_precision_context_t* context,
  const adc_precision_timeouts_t* timeouts) {
  adc_precision_error_t err = ADC_PRECISION_OK;

  if ((context == NULL) || (context->port == NULL)) {
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }
  if (!validate_port(context->port)) {
    ADC_PRECISION_TRACE("prepare: invalid port callbacks");
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }

  context->acquisition_prepared = false;
  if (validate_timeouts(timeouts, &context->resolved_timeouts) != ADC_PRECISION_OK) {
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }

  err = set_adcs(context->port, context->port_ctx, &context->resolved_timeouts);
  if (err != ADC_PRECISION_OK) {
    return err;
  }

  context->has_retained_channel = false;
  context->acquisition_prepared = true;
  return ADC_PRECISION_OK;
}

adc_precision_error_t adc_precision_read_raw(
  adc_precision_context_t* context,
  uint16_t channel,
  const adc_precision_timeouts_t* timeouts,
  uint16_t* out_code) {
  uint16_t raw_sample = 0U;
  uint16_t discarded_sample = 0U;
  bool needs_discard = false;

  if ((context == NULL) || (context->port == NULL) || (out_code == NULL)) {
    ADC_PRECISION_TRACE("read_raw: invalid argument (context/port/output)");
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }
  if ((context->port->read_raw == NULL) || !context->acquisition_prepared) {
    ADC_PRECISION_TRACE("read_raw: operation requires prepared acquisition cycle");
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }
  if (validate_port(context->port) != true) {
    ADC_PRECISION_TRACE("read_raw: invalid port callbacks");
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }
  if (validate_timeouts(timeouts, &context->resolved_timeouts) != ADC_PRECISION_OK) {
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }
  if (channel > ADC_PRECISION_CHANNEL_MAX) {
    ADC_PRECISION_TRACE("read_raw: invalid channel");
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }

  needs_discard = !context->has_retained_channel || (context->retained_channel != channel);
  if (needs_discard) {
    adc_precision_error_t err = do_single_conversion(
      context->port,
      context->port_ctx,
      channel,
      &context->resolved_timeouts,
      &discarded_sample);
    if (err != ADC_PRECISION_OK) {
      invalidate_prepared_cycle(context);
      return err;
    }
  }

  {
    adc_precision_error_t err = do_single_conversion(
        context->port,
        context->port_ctx,
        channel,
        &context->resolved_timeouts,
        &raw_sample);
    if (err != ADC_PRECISION_OK) {
      invalidate_prepared_cycle(context);
      return err;
    }
  }

  context->has_retained_channel = true;
  context->retained_channel = channel;
  *out_code = raw_sample;
  (void)discarded_sample;
  return ADC_PRECISION_OK;
}

adc_precision_error_t adc_precision_read_uV(
  adc_precision_context_t* context,
  uint16_t channel,
  uint32_t vrefint_cal,
  uint32_t vrefint_os,
  const adc_precision_timeouts_t* timeouts,
  uint32_t* out_uv) {
  uint16_t raw_code = 0U;
  uint32_t vdda_uv = 0U;
  adc_precision_error_t err = ADC_PRECISION_OK;

  if (context == NULL) {
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }
  if (out_uv == NULL) {
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }

  err = adc_precision_compute_vdda_uv(vrefint_cal, vrefint_os, &vdda_uv);
  if (err != ADC_PRECISION_OK) {
    return err;
  }

  err = adc_precision_read_raw(context, channel, timeouts, &raw_code);
  if (err != ADC_PRECISION_OK) {
    return err;
  }

  err = adc_precision_compute_channel_uv(raw_code, vdda_uv, out_uv);
  if (err != ADC_PRECISION_OK) {
    return err;
  }
  return ADC_PRECISION_OK;
}

adc_precision_error_t adc_precision_compute_vdda_uv(
  uint32_t vrefint_cal,
  uint32_t vrefint_os,
  uint32_t* out_vdda_uv) {
  uint64_t vdda_64 = 0ULL;

  if (out_vdda_uv == NULL) {
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }
  if (vrefint_cal == 0U) {
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }
  if (vrefint_cal > ADC_PRECISION_VREFINT_CAL_MAX) {
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }
  if ((vrefint_os == 0U) || (vrefint_os > ADC_PRECISION_OS_CODE_FULL_SCALE)) {
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }

  vdda_64 = 3000000ULL * (uint64_t)vrefint_cal;
  vdda_64 = (vdda_64 * (uint64_t)ADC_PRECISION_OVERSAMPLING_SCALE) / (uint64_t)vrefint_os;
  if (vdda_64 > (uint64_t)UINT32_MAX) {
    return ADC_PRECISION_ERROR_OVERFLOW;
  }

  *out_vdda_uv = (uint32_t)vdda_64;
  return ADC_PRECISION_OK;
}

adc_precision_error_t adc_precision_compute_channel_uv(
  uint32_t oversampled_code,
  uint32_t vdda_uv,
  uint32_t* out_uv) {
  uint64_t value = 0ULL;
  if (out_uv == NULL) {
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }
  if (vdda_uv == 0U) {
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }
  if (oversampled_code > ADC_PRECISION_OS_CODE_FULL_SCALE) {
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }

  value = (uint64_t)oversampled_code * (uint64_t)vdda_uv;
  if (value > ((uint64_t)UINT32_MAX * (uint64_t)ADC_PRECISION_OS_CODE_FULL_SCALE)) {
    return ADC_PRECISION_ERROR_OVERFLOW;
  }

  value /= (uint64_t)ADC_PRECISION_OS_CODE_FULL_SCALE;
  if (value > (uint64_t)UINT32_MAX) {
    return ADC_PRECISION_ERROR_OVERFLOW;
  }
  *out_uv = (uint32_t)value;
  return ADC_PRECISION_OK;
}

static bool validate_port(const adc_precision_port_t* port) {
  if (port == NULL) {
    ADC_PRECISION_TRACE("validate_port: null port");
    return false;
  }
  if (port->now_ms == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing now_ms");
    return false;
  }
  if (port->request_stop_conversion == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing request_stop_conversion");
    return false;
  }
  if (port->is_conversion_stopped == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing is_conversion_stopped");
    return false;
  }
  if (port->request_disable_adc == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing request_disable_adc");
    return false;
  }
  if (port->is_adc_disabled == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing is_adc_disabled");
    return false;
  }
  if (port->configure == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing configure");
    return false;
  }
  if (port->request_self_calibration == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing request_self_calibration");
    return false;
  }
  if (port->is_calibration_complete == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing is_calibration_complete");
    return false;
  }
  if (port->request_enable_adc == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing request_enable_adc");
    return false;
  }
  if (port->is_adc_ready == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing is_adc_ready");
    return false;
  }
  if (port->enable_vrefint_gate == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing enable_vrefint_gate");
    return false;
  }
  if (port->enable_temperature_gate == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing enable_temperature_gate");
    return false;
  }
  if (port->enable_vrefint_buffer_gate == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing enable_vrefint_buffer_gate");
    return false;
  }
  if (port->enable_temperature_buffer_gate == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing enable_temperature_buffer_gate");
    return false;
  }
  if (port->is_vrefint_ready == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing is_vrefint_ready");
    return false;
  }
  if (port->is_temperature_ready == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing is_temperature_ready");
    return false;
  }
  if (port->is_vrefint_buffer_ready == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing is_vrefint_buffer_ready");
    return false;
  }
  if (port->is_temperature_buffer_ready == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing is_temperature_buffer_ready");
    return false;
  }
  if (port->is_reference_settled == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing is_reference_settled");
    return false;
  }
  if (port->select_channel == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing select_channel");
    return false;
  }
  if (port->start_conversion == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing start_conversion");
    return false;
  }
  if (port->is_conversion_complete == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing is_conversion_complete");
    return false;
  }
  if (port->read_raw == NULL) {
    ADC_PRECISION_TRACE("validate_port: missing read_raw");
    return false;
  }
  ADC_PRECISION_TRACE("validate_port: ok");
  return true;
}

static void invalidate_prepared_cycle(adc_precision_context_t* context) {
  if (context == NULL) {
    return;
  }
  context->acquisition_prepared = false;
  context->has_retained_channel = false;
  context->retained_channel = 0U;
}

static adc_precision_error_t validate_timeouts(
  const adc_precision_timeouts_t* timeouts,
  adc_precision_timeouts_t* resolved) {
  adc_precision_timeouts_t defaults;
  if ((resolved == NULL) || (timeouts == NULL)) {
    if (resolved != NULL) {
      if (timeouts == NULL) {
        return adc_precision_default_timeouts(resolved);
      }
      adc_precision_default_timeouts(resolved);
    }
    return ADC_PRECISION_OK;
  }

  adc_precision_default_timeouts(&defaults);
  resolved->stop_ms = timeouts->stop_ms == 0U ? defaults.stop_ms : timeouts->stop_ms;
  resolved->disable_ms = timeouts->disable_ms == 0U ? defaults.disable_ms : timeouts->disable_ms;
  resolved->calibration_ms =
    timeouts->calibration_ms == 0U ? defaults.calibration_ms : timeouts->calibration_ms;
  resolved->ready_ms = timeouts->ready_ms == 0U ? defaults.ready_ms : timeouts->ready_ms;
  resolved->vref_ready_ms =
    timeouts->vref_ready_ms == 0U ? defaults.vref_ready_ms : timeouts->vref_ready_ms;
  resolved->sensor_ready_ms =
    timeouts->sensor_ready_ms == 0U ? defaults.sensor_ready_ms : timeouts->sensor_ready_ms;
  resolved->settle_ms = timeouts->settle_ms == 0U ? defaults.settle_ms : timeouts->settle_ms;
  resolved->conversion_ms =
    timeouts->conversion_ms == 0U ? defaults.conversion_ms : timeouts->conversion_ms;
  return ADC_PRECISION_OK;
}

static adc_precision_error_t wait_for(
  const adc_precision_port_t* port,
  void* port_ctx,
  uint32_t timeout_ms,
  bool (*predicate)(void*)) {
  uint32_t start_ms = 0U;
  uint32_t now_ms = 0U;
  if ((port == NULL) || (predicate == NULL) || (port->now_ms == NULL)) {
    return ADC_PRECISION_ERROR_INVALID_ARGUMENT;
  }
  start_ms = port->now_ms(port_ctx);
  while (1) {
    if (predicate(port_ctx)) {
      return ADC_PRECISION_OK;
    }
    now_ms = port->now_ms(port_ctx);
    if ((now_ms - start_ms) >= timeout_ms) {
      return ADC_PRECISION_ERROR_TIMEOUT;
    }
  }
}

static adc_precision_error_t set_adcs(
  const adc_precision_port_t* port,
  void* port_ctx,
  const adc_precision_timeouts_t* timeouts) {
  if (!port->is_conversion_stopped(port_ctx)) {
    port->request_stop_conversion(port_ctx);
    if (wait_for(port, port_ctx, timeouts->stop_ms, port->is_conversion_stopped) != ADC_PRECISION_OK) {
      ADC_PRECISION_TRACE("set_adcs: stop timeout");
      return ADC_PRECISION_ERROR_TIMEOUT;
    }
  }

  if (!port->is_adc_disabled(port_ctx)) {
    port->request_disable_adc(port_ctx);
    if (wait_for(port, port_ctx, timeouts->disable_ms, port->is_adc_disabled) != ADC_PRECISION_OK) {
      ADC_PRECISION_TRACE("set_adcs: disable timeout");
      return ADC_PRECISION_ERROR_TIMEOUT;
    }
  }

  /* VERIFY-RM0376: configure and calibrate while ADC is known disabled. */
  port->configure(port_ctx, &k_adc_precision_config);
  port->request_self_calibration(port_ctx);
  if (wait_for(port, port_ctx, timeouts->calibration_ms, port->is_calibration_complete)
      != ADC_PRECISION_OK) {
    ADC_PRECISION_TRACE("set_adcs: calibration timeout");
    return ADC_PRECISION_ERROR_CALIBRATION;
  }

  port->request_enable_adc(port_ctx);
  if (wait_for(port, port_ctx, timeouts->ready_ms, port->is_adc_ready) != ADC_PRECISION_OK) {
    ADC_PRECISION_TRACE("set_adcs: ready timeout");
    return ADC_PRECISION_ERROR_TIMEOUT;
  }

  port->enable_vrefint_gate(port_ctx);
  port->enable_temperature_gate(port_ctx);
  port->enable_vrefint_buffer_gate(port_ctx);
  port->enable_temperature_buffer_gate(port_ctx);
  if (wait_for(port, port_ctx, timeouts->vref_ready_ms, port->is_vrefint_ready)
      != ADC_PRECISION_OK) {
    ADC_PRECISION_TRACE("set_adcs: vref ready timeout");
    return ADC_PRECISION_ERROR_TIMEOUT;
  }
  if (wait_for(port, port_ctx, timeouts->sensor_ready_ms, port->is_temperature_ready)
      != ADC_PRECISION_OK) {
    ADC_PRECISION_TRACE("set_adcs: temp ready timeout");
    return ADC_PRECISION_ERROR_TIMEOUT;
  }
  if (wait_for(port, port_ctx, timeouts->vref_ready_ms, port->is_vrefint_buffer_ready)
      != ADC_PRECISION_OK) {
    ADC_PRECISION_TRACE("set_adcs: vref buffer ready timeout");
    return ADC_PRECISION_ERROR_TIMEOUT;
  }
  if (wait_for(port, port_ctx, timeouts->sensor_ready_ms, port->is_temperature_buffer_ready)
      != ADC_PRECISION_OK) {
    ADC_PRECISION_TRACE("set_adcs: temp buffer ready timeout");
    return ADC_PRECISION_ERROR_TIMEOUT;
  }
  if (wait_for(port, port_ctx, timeouts->settle_ms, port->is_reference_settled)
      != ADC_PRECISION_OK) {
    ADC_PRECISION_TRACE("set_adcs: settle timeout");
    return ADC_PRECISION_ERROR_TIMEOUT;
  }

  return ADC_PRECISION_OK;
}

static adc_precision_error_t do_single_conversion(
  const adc_precision_port_t* port,
  void* port_ctx,
  uint16_t channel,
  const adc_precision_timeouts_t* timeouts,
  uint16_t* out_code) {
  bool overrun = false;

  port->select_channel(port_ctx, channel);
  port->start_conversion(port_ctx);
  if (wait_for(port, port_ctx, timeouts->conversion_ms, port->is_conversion_complete)
      != ADC_PRECISION_OK) {
    ADC_PRECISION_TRACE("do_single_conversion: conversion timeout");
    return ADC_PRECISION_ERROR_TIMEOUT;
  }

  if (!port->read_raw(port_ctx, out_code, &overrun)) {
    ADC_PRECISION_TRACE("do_single_conversion: read_raw failed");
    return ADC_PRECISION_ERROR_INTERNAL;
  }
  if (overrun) {
    ADC_PRECISION_TRACE("do_single_conversion: overrun");
    return ADC_PRECISION_ERROR_OVERRUN;
  }
  ADC_PRECISION_TRACE("do_single_conversion: success");
  return ADC_PRECISION_OK;
}
