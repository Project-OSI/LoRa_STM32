#ifndef ADC_PRECISION_H
#define ADC_PRECISION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { ADC_PRECISION_SAMPLE_CYCLES_X2 = 321U };
enum { ADC_PRECISION_OVERSAMPLING = 256U };
enum { ADC_PRECISION_OVERSAMPLING_SCALE = 16U };
enum { ADC_PRECISION_OVERSAMPLING_SHIFT = 4U };
enum { ADC_PRECISION_OS_CODE_FULL_SCALE = 65520U };
enum { ADC_PRECISION_ADC_CLOCK_HZ = 8000000U };
enum { ADC_PRECISION_NATIVE_RESOLUTION = 12U };
enum { ADC_PRECISION_VREFINT_CAL_MAX = 4095U };

enum { ADC_PRECISION_CHANNEL_MIN = 0U };
enum { ADC_PRECISION_CHANNEL_MAX = 18U };

typedef struct {
  uint32_t native_resolution_bits;
  uint32_t sample_cycles_x2;
  uint32_t oversampling_ratio;
  uint32_t oversampling_right_shift;
  uint32_t adc_clock_hz;
  bool single_software_trigger;
  bool continuous_mode;
  bool dma_enabled;
} adc_precision_config_t;

typedef enum {
  ADC_PRECISION_OK = 0,
  ADC_PRECISION_ERROR_INVALID_ARGUMENT = 1,
  ADC_PRECISION_ERROR_TIMEOUT = 2,
  ADC_PRECISION_ERROR_OVERFLOW = 3,
  ADC_PRECISION_ERROR_OVERRUN = 4,
  ADC_PRECISION_ERROR_INTERNAL = 5,
  ADC_PRECISION_ERROR_CALIBRATION = 6
} adc_precision_error_t;

typedef struct {
  uint32_t stop_ms;
  uint32_t disable_ms;
  uint32_t calibration_ms;
  uint32_t ready_ms;
  uint32_t vref_ready_ms;
  uint32_t sensor_ready_ms;
  uint32_t settle_ms;
  uint32_t conversion_ms;
} adc_precision_timeouts_t;

typedef struct {
  uint32_t (*now_ms)(void* port_ctx);
  void (*request_stop_conversion)(void* port_ctx);
  bool (*is_conversion_stopped)(void* port_ctx);
  void (*request_disable_adc)(void* port_ctx);
  bool (*is_adc_disabled)(void* port_ctx);
  void (*configure)(void* port_ctx, const adc_precision_config_t* config);
  void (*request_self_calibration)(void* port_ctx);
  bool (*is_calibration_complete)(void* port_ctx);
  void (*request_enable_adc)(void* port_ctx);
  bool (*is_adc_ready)(void* port_ctx);
  void (*enable_vrefint_gate)(void* port_ctx);
  void (*enable_temperature_gate)(void* port_ctx);
  void (*enable_vrefint_buffer_gate)(void* port_ctx);
  void (*enable_temperature_buffer_gate)(void* port_ctx);
  bool (*is_vrefint_ready)(void* port_ctx);
  bool (*is_temperature_ready)(void* port_ctx);
  bool (*is_vrefint_buffer_ready)(void* port_ctx);
  bool (*is_temperature_buffer_ready)(void* port_ctx);
  bool (*is_reference_settled)(void* port_ctx);
  void (*select_channel)(void* port_ctx, uint16_t channel);
  void (*start_conversion)(void* port_ctx);
  bool (*is_conversion_complete)(void* port_ctx);
  bool (*read_raw)(void* port_ctx, uint16_t* raw_code, bool* overrun);
} adc_precision_port_t;

typedef struct {
  const adc_precision_port_t* port;
  void* port_ctx;
  bool acquisition_prepared;
  bool has_retained_channel;
  uint16_t retained_channel;
  adc_precision_timeouts_t resolved_timeouts;
} adc_precision_context_t;

void adc_precision_init(adc_precision_context_t* context, const adc_precision_port_t* port, void* port_ctx);

adc_precision_error_t adc_precision_default_timeouts(adc_precision_timeouts_t* timeouts);
adc_precision_error_t adc_precision_prepare(
  adc_precision_context_t* context,
  const adc_precision_timeouts_t* timeouts);
/*
 * Single oversampled read of one channel inside a prepared acquisition.
 *
 * Failure semantics: a conversion-class failure (ADC_PRECISION_ERROR_TIMEOUT
 * or ADC_PRECISION_ERROR_OVERRUN) is recoverable - the ADC is stopped within
 * the bounded stop window, only the channel-retention cache is dropped, and
 * the next read re-establishes a known state with a forced discard
 * conversion; no re-prepare is needed. If that stop fails, or on any other
 * failure class, the prepared session is invalidated and reads are rejected
 * until adc_precision_prepare() succeeds again.
 */
adc_precision_error_t adc_precision_read_raw(
  adc_precision_context_t* context,
  uint16_t channel,
  const adc_precision_timeouts_t* timeouts,
  uint16_t* out_code);
adc_precision_error_t adc_precision_read_uV(
  adc_precision_context_t* context,
  uint16_t channel,
  uint32_t vrefint_cal,
  uint32_t vrefint_os,
  const adc_precision_timeouts_t* timeouts,
  uint32_t* out_uv);
adc_precision_error_t adc_precision_compute_vdda_uv(
  uint32_t vrefint_cal,
  uint32_t vrefint_os,
  uint32_t* out_vdda_uv);
adc_precision_error_t adc_precision_compute_channel_uv(
  uint32_t oversampled_code,
  uint32_t vdda_uv,
  uint32_t* out_uv);

#ifdef __cplusplus
}
#endif

#endif /* ADC_PRECISION_H */
