#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "adc_precision.h"

#define EXPECT(condition, message) do { \
  if (!(condition)) { \
    ++failure_count; \
    printf("FAIL: %s\n", message); \
    return; \
  } \
} while (0)

#define EXPECT_EQ_U32(expected, actual, message) do { \
  if ((uint32_t)(expected) != (uint32_t)(actual)) { \
    ++failure_count; \
    printf("FAIL: %s: expected=%u actual=%u\n", message, \
      (unsigned)(uint32_t)(expected), (unsigned)(uint32_t)(actual)); \
    return; \
  } \
} while (0)

#define EXPECT_EQ_ERR(expected, actual, message) do { \
  if ((expected) != (actual)) { \
    ++failure_count; \
    printf("FAIL: %s: expected=%d actual=%d\n", message, \
      (int)(expected), (int)(actual)); \
    return; \
  } \
} while (0)

enum {
  EVENT_STOP,
  EVENT_DISABLE,
  EVENT_CONFIGURE,
  EVENT_CALIBRATE,
  EVENT_ENABLE,
  EVENT_ENABLE_VREF,
  EVENT_ENABLE_TEMP,
  EVENT_ENABLE_VREF_BUFFER,
  EVENT_ENABLE_TEMP_BUFFER,
  EVENT_SELECT,
  EVENT_START_CONVERSION,
  EVENT_READ
};

struct test_port_event {
  uint16_t kind;
  uint16_t arg;
};

struct test_port_state {
  uint32_t now_ms;
  bool adc_disable_requested;
  bool conversion_stopped;
  bool adc_enabled;
  bool conversion_unread;
  bool vref_int_enabled;
  bool temp_enabled;
  bool vref_buffer_enabled;
  bool temp_buffer_enabled;
  bool vref_ready;
  bool temp_ready;
  bool vref_buffer_ready;
  bool temp_buffer_ready;
  bool settled;
  bool sequence_violation;
  size_t overrun_sample_index;
  bool config_seen;

  uint32_t stop_polls;
  uint32_t disable_polls;
  uint32_t calibration_polls;
  uint32_t ready_polls;
  uint32_t vref_ready_polls;
  uint32_t temp_ready_polls;
  uint32_t vref_buffer_ready_polls;
  uint32_t temp_buffer_ready_polls;
  uint32_t settle_polls;
  uint32_t conversion_polls;

  uint32_t event_count;
  struct test_port_event events[256];
  adc_precision_config_t observed_config;

  uint16_t samples[16];
  size_t sample_count;
  size_t sample_index;
  uint16_t current_channel;
};

static struct test_port_state test_state;

static void push_event(uint16_t kind, uint16_t arg) {
  if (test_state.event_count < 256U) {
    test_state.events[test_state.event_count].kind = kind;
    test_state.events[test_state.event_count].arg = arg;
    ++test_state.event_count;
  }
}

static uint32_t test_now_ms(void* user_ctx) {
  (void)user_ctx;
  ++test_state.now_ms;
  return test_state.now_ms;
}

static void maybe_tick_counter(uint32_t* countdown, bool* ready_state) {
  if (*countdown > 0U) {
    --(*countdown);
    *ready_state = false;
  } else {
    *ready_state = true;
  }
}

static void test_request_stop_conversion(void* user_ctx) {
  (void)user_ctx;
  test_state.conversion_stopped = false;
  push_event(EVENT_STOP, 0U);
}

static bool test_is_conversion_stopped(void* user_ctx) {
  (void)user_ctx;
  if (test_state.conversion_stopped) {
    test_state.conversion_unread = false;
    return true;
  }
  maybe_tick_counter(&test_state.stop_polls, &test_state.conversion_stopped);
  if (test_state.conversion_stopped) {
    test_state.conversion_unread = false;
  }
  return test_state.conversion_stopped;
}

static void test_request_disable_adc(void* user_ctx) {
  (void)user_ctx;
  test_state.adc_disable_requested = true;
  push_event(EVENT_DISABLE, 0U);
}

static bool test_is_adc_disabled(void* user_ctx) {
  (void)user_ctx;
  if (!test_state.adc_enabled) {
    return true;
  }
  if (!test_state.adc_disable_requested) {
    return false;
  }
  maybe_tick_counter(&test_state.disable_polls, &(bool){ false });
  if (test_state.disable_polls == 0U) {
    test_state.adc_enabled = false;
  }
  return !test_state.adc_enabled;
}

static void test_configure(void* user_ctx, const adc_precision_config_t* config) {
  (void)user_ctx;
  if (test_state.adc_enabled) {
    test_state.sequence_violation = true;
  }
  if (config == NULL) {
    memset(&test_state.observed_config, 0, sizeof(test_state.observed_config));
    test_state.config_seen = false;
  } else {
    test_state.observed_config = *config;
    test_state.config_seen = true;
  }
  push_event(EVENT_CONFIGURE, 0U);
}

static void test_request_self_calibration(void* user_ctx) {
  (void)user_ctx;
  push_event(EVENT_CALIBRATE, 0U);
}

static bool test_is_calibration_complete(void* user_ctx) {
  (void)user_ctx;
  maybe_tick_counter(&test_state.calibration_polls, &(bool){ true });
  return test_state.calibration_polls == 0U;
}

static void test_request_enable_adc(void* user_ctx) {
  (void)user_ctx;
  test_state.adc_enabled = true;
  push_event(EVENT_ENABLE, 0U);
}

static bool test_is_adc_ready(void* user_ctx) {
  (void)user_ctx;
  maybe_tick_counter(&test_state.ready_polls, &(bool){ true });
  return test_state.ready_polls == 0U;
}

static void test_enable_vrefint_gate(void* user_ctx) {
  (void)user_ctx;
  test_state.vref_int_enabled = true;
  push_event(EVENT_ENABLE_VREF, 0U);
}

static void test_enable_temperature_gate(void* user_ctx) {
  (void)user_ctx;
  test_state.temp_enabled = true;
  push_event(EVENT_ENABLE_TEMP, 0U);
}

static void test_enable_vrefint_buffer_gate(void* user_ctx) {
  (void)user_ctx;
  test_state.vref_buffer_enabled = true;
  push_event(EVENT_ENABLE_VREF_BUFFER, 0U);
}

static void test_enable_temperature_buffer_gate(void* user_ctx) {
  (void)user_ctx;
  test_state.temp_buffer_enabled = true;
  push_event(EVENT_ENABLE_TEMP_BUFFER, 0U);
}

static bool test_is_vrefint_ready(void* user_ctx) {
  (void)user_ctx;
  maybe_tick_counter(&test_state.vref_ready_polls, &test_state.vref_ready);
  return test_state.vref_ready;
}

static bool test_is_temperature_ready(void* user_ctx) {
  (void)user_ctx;
  maybe_tick_counter(&test_state.temp_ready_polls, &test_state.temp_ready);
  return test_state.temp_ready;
}

static bool test_is_vrefint_buffer_ready(void* user_ctx) {
  (void)user_ctx;
  maybe_tick_counter(&test_state.vref_buffer_ready_polls, &test_state.vref_buffer_ready);
  return test_state.vref_buffer_ready;
}

static bool test_is_temperature_buffer_ready(void* user_ctx) {
  (void)user_ctx;
  maybe_tick_counter(&test_state.temp_buffer_ready_polls, &test_state.temp_buffer_ready);
  return test_state.temp_buffer_ready;
}

static bool test_is_reference_settled(void* user_ctx) {
  (void)user_ctx;
  maybe_tick_counter(&test_state.settle_polls, &test_state.settled);
  return test_state.settled;
}

static void test_select_channel(void* user_ctx, uint16_t channel) {
  (void)user_ctx;
  test_state.current_channel = channel;
  push_event(EVENT_SELECT, channel);
}

static void test_start_conversion(void* user_ctx) {
  (void)user_ctx;
  if (!(test_state.vref_int_enabled && test_state.temp_enabled
    && test_state.vref_buffer_enabled && test_state.temp_buffer_enabled
    && test_state.vref_ready && test_state.temp_ready
    && test_state.vref_buffer_ready && test_state.temp_buffer_ready
    && test_state.settled)) {
    test_state.sequence_violation = true;
  }
  if (test_state.conversion_unread) {
    test_state.sequence_violation = true;
  }
  test_state.conversion_stopped = false;
  test_state.conversion_unread = true;
  push_event(EVENT_START_CONVERSION, test_state.current_channel);
}

static bool test_is_conversion_complete(void* user_ctx) {
  (void)user_ctx;
  maybe_tick_counter(&test_state.conversion_polls, &(bool){ true });
  return test_state.conversion_polls == 0U;
}

static bool test_read_raw(void* user_ctx, uint16_t* raw_code, bool* overrun) {
  size_t index;
  (void)user_ctx;
  if (raw_code == NULL || test_state.sample_index >= test_state.sample_count) {
    return false;
  }
  index = test_state.sample_index;
  *raw_code = test_state.samples[index];
  ++test_state.sample_index;
  if (overrun != NULL) {
    *overrun = (test_state.overrun_sample_index == index);
    if (test_state.overrun_sample_index == index) {
      test_state.overrun_sample_index = (size_t)-1U;
    }
  }
  test_state.conversion_unread = false;
  test_state.conversion_stopped = true;
  push_event(EVENT_READ, *raw_code);
  return true;
}

static const adc_precision_port_t test_port = {
  test_now_ms,
  test_request_stop_conversion,
  test_is_conversion_stopped,
  test_request_disable_adc,
  test_is_adc_disabled,
  test_configure,
  test_request_self_calibration,
  test_is_calibration_complete,
  test_request_enable_adc,
  test_is_adc_ready,
  test_enable_vrefint_gate,
  test_enable_temperature_gate,
  test_enable_vrefint_buffer_gate,
  test_enable_temperature_buffer_gate,
  test_is_vrefint_ready,
  test_is_temperature_ready,
  test_is_vrefint_buffer_ready,
  test_is_temperature_buffer_ready,
  test_is_reference_settled,
  test_select_channel,
  test_start_conversion,
  test_is_conversion_complete,
  test_read_raw
};

static void reset_port_state(void) {
  memset(&test_state, 0, sizeof(test_state));
  test_state.conversion_stopped = true;
  test_state.adc_enabled = false;
  test_state.adc_disable_requested = false;
  test_state.overrun_sample_index = (size_t)-1U;
}

static void set_default_timeouts(adc_precision_timeouts_t* timeouts) {
  timeouts->stop_ms = 5U;
  timeouts->disable_ms = 5U;
  timeouts->calibration_ms = 5U;
  timeouts->ready_ms = 5U;
  timeouts->vref_ready_ms = 5U;
  timeouts->sensor_ready_ms = 5U;
  timeouts->settle_ms = 5U;
  timeouts->conversion_ms = 5U;
}

static void set_default_samples(
  uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e, uint16_t f) {
  test_state.samples[0] = a;
  test_state.samples[1] = b;
  test_state.samples[2] = c;
  test_state.samples[3] = d;
  test_state.samples[4] = e;
  test_state.samples[5] = f;
  test_state.sample_count = 6U;
  test_state.sample_index = 0U;
}

static size_t event_count_by_kind(uint16_t kind) {
  size_t i;
  size_t result = 0U;
  for (i = 0U; i < test_state.event_count; ++i) {
    if (test_state.events[i].kind == kind) {
      ++result;
    }
  }
  return result;
}

static size_t event_index_by_kind(uint16_t kind) {
  size_t i;
  for (i = 0U; i < test_state.event_count; ++i) {
    if (test_state.events[i].kind == kind) {
      return i;
    }
  }
  return (size_t)-1;
}

static size_t nth_event_index(uint16_t kind, size_t nth) {
  size_t i;
  size_t count = 0U;
  for (i = 0U; i < test_state.event_count; ++i) {
    if (test_state.events[i].kind == kind) {
      ++count;
      if (count == nth) {
        return i;
      }
    }
  }
  return (size_t)-1;
}

static bool port_event_order_is(uint16_t first, uint16_t second) {
  size_t first_index = event_index_by_kind(first);
  size_t second_index = event_index_by_kind(second);
  return (first_index < second_index);
}

static bool port_event_nth_order_is(uint16_t first, uint16_t second, size_t first_n, size_t second_n) {
  return nth_event_index(first, first_n) < nth_event_index(second, second_n);
}

static uint32_t failure_count = 0U;

static void test_default_timeouts(void) {
  adc_precision_timeouts_t timeouts;
  adc_precision_error_t err = adc_precision_default_timeouts(&timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "default timeouts");
  EXPECT(timeouts.stop_ms != 0U, "default timeout stop_ms");
  EXPECT(timeouts.disable_ms != 0U, "default timeout disable_ms");
  EXPECT(timeouts.calibration_ms != 0U, "default timeout calibration_ms");
  EXPECT(timeouts.ready_ms != 0U, "default timeout ready_ms");
  EXPECT(timeouts.vref_ready_ms != 0U, "default timeout vref_ready_ms");
  EXPECT(timeouts.sensor_ready_ms != 0U, "default timeout sensor_ready_ms");
  EXPECT(timeouts.settle_ms != 0U, "default timeout settle_ms");
  EXPECT(timeouts.conversion_ms != 0U, "default timeout conversion_ms");
}

static void test_prepare_sequence_and_config(void) {
  adc_precision_context_t context;
  adc_precision_timeouts_t timeouts;
  adc_precision_error_t err = ADC_PRECISION_OK;
  reset_port_state();
  set_default_timeouts(&timeouts);
  test_state.conversion_stopped = false;
  test_state.adc_enabled = true;
  test_state.stop_polls = 2U;
  test_state.disable_polls = 2U;
  test_state.calibration_polls = 2U;
  test_state.ready_polls = 2U;
  test_state.vref_ready_polls = 2U;
  test_state.temp_ready_polls = 2U;
  test_state.vref_buffer_ready_polls = 2U;
  test_state.temp_buffer_ready_polls = 2U;
  test_state.settle_polls = 2U;

  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "prepare succeeds");
  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_STOP), "stop requested");
  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_DISABLE), "disable requested");
  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_CONFIGURE), "configure called");
  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_CALIBRATE), "calibration requested");
  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_ENABLE), "enable requested");
  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_ENABLE_VREF), "vrefint gate enabled");
  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_ENABLE_TEMP), "temp gate enabled");
  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_ENABLE_VREF_BUFFER), "vrefint buffer enabled");
  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_ENABLE_TEMP_BUFFER), "temp buffer enabled");
  EXPECT(test_state.config_seen, "configuration observed");
  EXPECT_EQ_U32(ADC_PRECISION_NATIVE_RESOLUTION, test_state.observed_config.native_resolution_bits,
    "config captures native resolution");
  EXPECT_EQ_U32(ADC_PRECISION_SAMPLE_CYCLES_X2, test_state.observed_config.sample_cycles_x2,
    "config captures sample cycles");
  EXPECT_EQ_U32(ADC_PRECISION_OVERSAMPLING, test_state.observed_config.oversampling_ratio,
    "config captures oversampling ratio");
  EXPECT_EQ_U32(ADC_PRECISION_ADC_CLOCK_HZ, test_state.observed_config.adc_clock_hz,
    "config captures ADC clock");
  EXPECT_EQ_U32(ADC_PRECISION_OVERSAMPLING_SHIFT, test_state.observed_config.oversampling_right_shift,
    "config captures oversampling shift");
  EXPECT(test_state.observed_config.single_software_trigger, "config captures software trigger mode");
  EXPECT(!test_state.observed_config.continuous_mode, "config captures single channel mode");
  EXPECT(!test_state.observed_config.dma_enabled, "config captures DMA off");
  EXPECT(port_event_nth_order_is(EVENT_STOP, EVENT_DISABLE, 1U, 1U), "stop before disable");
  EXPECT(port_event_order_is(EVENT_DISABLE, EVENT_CONFIGURE), "disable before configure");
  EXPECT(port_event_order_is(EVENT_CONFIGURE, EVENT_CALIBRATE), "configure before calibration");
  EXPECT(port_event_order_is(EVENT_CALIBRATE, EVENT_ENABLE), "calibration before enable");
  EXPECT(port_event_order_is(EVENT_ENABLE, EVENT_ENABLE_VREF), "enable before enable gates");
  EXPECT(port_event_order_is(EVENT_ENABLE_VREF, EVENT_START_CONVERSION), "no conversion during prepare");
}

static void test_prepare_noop_when_already_safe(void) {
  adc_precision_context_t context;
  adc_precision_timeouts_t timeouts;
  adc_precision_error_t err = ADC_PRECISION_OK;
  reset_port_state();
  set_default_timeouts(&timeouts);
  test_state.conversion_stopped = true;
  test_state.adc_enabled = false;
  test_state.stop_polls = 1U;
  test_state.disable_polls = 1U;
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "prepare succeeds when already safe");
  EXPECT_EQ_U32(0U, event_count_by_kind(EVENT_STOP), "stop not requested when already stopped");
  EXPECT_EQ_U32(0U, event_count_by_kind(EVENT_DISABLE), "disable not requested when already disabled");
}

static void test_prepare_timeout_steps_and_no_conversion(void) {
  adc_precision_context_t context;
  adc_precision_timeouts_t timeouts;
  adc_precision_error_t err = ADC_PRECISION_OK;
  uint16_t sample = 1234U;

  reset_port_state();
  set_default_timeouts(&timeouts);
  test_state.conversion_stopped = false;
  test_state.stop_polls = 100U;
  test_state.adc_enabled = true;
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_TIMEOUT, err, "stop timeout");
  err = adc_precision_read_raw(&context, 7U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_INVALID_ARGUMENT, err, "read not allowed after prepare failure");
  EXPECT_EQ_U32(1234U, sample, "no sample after prepare timeout");

  reset_port_state();
  set_default_timeouts(&timeouts);
  test_state.conversion_stopped = true;
  test_state.adc_enabled = true;
  test_state.disable_polls = 100U;
  sample = 4321U;
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_TIMEOUT, err, "disable timeout");
  EXPECT_EQ_U32(4321U, sample, "no sample on disable timeout");
  EXPECT_EQ_U32(0U, event_count_by_kind(EVENT_START_CONVERSION), "no conversion on disable timeout");

  reset_port_state();
  set_default_timeouts(&timeouts);
  test_state.conversion_stopped = true;
  test_state.adc_enabled = false;
  test_state.calibration_polls = 100U;
  sample = 8765U;
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_CALIBRATION, err, "calibration timeout");
  EXPECT_EQ_U32(8765U, sample, "no sample on calibration timeout");
  EXPECT_EQ_U32(0U, event_count_by_kind(EVENT_START_CONVERSION), "no conversion on calibration timeout");

  reset_port_state();
  set_default_timeouts(&timeouts);
  test_state.conversion_stopped = true;
  test_state.adc_enabled = false;
  test_state.ready_polls = 100U;
  sample = 7890U;
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_TIMEOUT, err, "ready timeout");
  EXPECT_EQ_U32(7890U, sample, "no sample on ready timeout");
  EXPECT_EQ_U32(0U, event_count_by_kind(EVENT_START_CONVERSION), "no conversion on ready timeout");

  reset_port_state();
  set_default_timeouts(&timeouts);
  test_state.conversion_stopped = true;
  test_state.adc_enabled = false;
  test_state.calibration_polls = 0U;
  test_state.ready_polls = 0U;
  test_state.vref_ready_polls = 100U;
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_TIMEOUT, err, "vref ready timeout");
  EXPECT_EQ_U32(0U, event_count_by_kind(EVENT_START_CONVERSION), "no conversion on prepare timeout");
}

static void test_prepare_readiness_timeout_paths_do_not_start_conversion(void) {
  adc_precision_context_t context;
  adc_precision_timeouts_t timeouts;
  adc_precision_error_t err = ADC_PRECISION_OK;

  reset_port_state();
  set_default_timeouts(&timeouts);
  test_state.conversion_stopped = true;
  test_state.adc_enabled = false;
  test_state.vref_ready_polls = 0U;
  test_state.temp_ready_polls = 0U;
  test_state.vref_buffer_ready_polls = 100U;
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_TIMEOUT, err, "vref buffer ready timeout");
  EXPECT_EQ_U32(0U, event_count_by_kind(EVENT_START_CONVERSION), "no conversion on buffer timeout");

  reset_port_state();
  set_default_timeouts(&timeouts);
  test_state.conversion_stopped = true;
  test_state.adc_enabled = false;
  test_state.vref_ready_polls = 0U;
  test_state.temp_ready_polls = 0U;
  test_state.vref_buffer_ready_polls = 0U;
  test_state.temp_buffer_ready_polls = 100U;
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_TIMEOUT, err, "temp buffer ready timeout");
  EXPECT_EQ_U32(0U, event_count_by_kind(EVENT_START_CONVERSION), "no conversion on buffer timeout");

  reset_port_state();
  set_default_timeouts(&timeouts);
  test_state.conversion_stopped = true;
  test_state.adc_enabled = false;
  test_state.vref_ready_polls = 0U;
  test_state.temp_ready_polls = 0U;
  test_state.vref_buffer_ready_polls = 0U;
  test_state.temp_buffer_ready_polls = 0U;
  test_state.settle_polls = 100U;
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_TIMEOUT, err, "settle timeout");
  EXPECT_EQ_U32(0U, event_count_by_kind(EVENT_START_CONVERSION), "no conversion on settle timeout");

  reset_port_state();
  set_default_timeouts(&timeouts);
  test_state.conversion_stopped = true;
  test_state.adc_enabled = false;
  test_state.vref_ready_polls = 0U;
  test_state.temp_ready_polls = 100U;
  test_state.vref_buffer_ready_polls = 0U;
  test_state.temp_buffer_ready_polls = 0U;
  test_state.settle_polls = 0U;
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_TIMEOUT, err, "temperature ready timeout");
  EXPECT_EQ_U32(0U, event_count_by_kind(EVENT_START_CONVERSION), "no conversion on temperature timeout");
}

static void test_prepare_readiness_timeout_split_uses_distinct_timeouts(void) {
  adc_precision_context_t context;
  adc_precision_timeouts_t timeouts;
  adc_precision_error_t err = ADC_PRECISION_OK;

  reset_port_state();
  set_default_timeouts(&timeouts);
  test_state.conversion_stopped = true;
  test_state.adc_enabled = false;
  timeouts.vref_ready_ms = 1U;
  timeouts.sensor_ready_ms = 5U;
  test_state.vref_ready_polls = 0U;
  test_state.temp_ready_polls = 2U;
  test_state.vref_buffer_ready_polls = 0U;
  test_state.temp_buffer_ready_polls = 2U;
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "readiness uses distinct vref/sensor timeouts");
}

static void test_discard_and_retained_sample_sequence(void) {
  adc_precision_context_t context;
  adc_precision_timeouts_t timeouts;
  uint16_t sample = 0U;
  adc_precision_error_t err = ADC_PRECISION_OK;
  reset_port_state();
  set_default_timeouts(&timeouts);
  set_default_samples(111U, 122U, 133U, 144U, 155U, 166U);
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "prepare for discard behavior");

  err = adc_precision_read_raw(&context, 7U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "first read ok");
  EXPECT_EQ_U32(122U, sample, "first read returns retained sample after discard");

  err = adc_precision_read_raw(&context, 7U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "same-channel read ok");
  EXPECT_EQ_U32(133U, sample, "same channel read keeps retained");

  err = adc_precision_read_raw(&context, 8U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "channel switch read ok");
  EXPECT_EQ_U32(155U, sample, "channel switch returns retained of second channel");

  EXPECT_EQ_U32(5U, event_count_by_kind(EVENT_START_CONVERSION),
    "discard+retain then retain then discard+retain");
  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_CONFIGURE), "setup once");
  EXPECT(port_event_nth_order_is(EVENT_START_CONVERSION, EVENT_START_CONVERSION, 1U, 2U),
    "start conversions are sequential");
}

static void test_setup_only_once_per_acquisition(void) {
  adc_precision_context_t context;
  adc_precision_timeouts_t timeouts;
  uint16_t sample = 0U;
  adc_precision_error_t err = ADC_PRECISION_OK;

  reset_port_state();
  set_default_timeouts(&timeouts);
  set_default_samples(10U, 11U, 12U, 13U, 14U, 15U);
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "prepare for single-cycle setup check");

  err = adc_precision_read_raw(&context, 1U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "first read ok");
  err = adc_precision_read_raw(&context, 2U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "switch-channel read ok");
  err = adc_precision_read_raw(&context, 2U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "same-channel read ok");

  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_CONFIGURE), "configure once");
  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_CALIBRATE), "calibrate once");
  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_ENABLE), "enable once");
  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_ENABLE_VREF), "internal path called once per acquisition");
  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_ENABLE_TEMP), "internal path called once per acquisition");
  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_ENABLE_VREF_BUFFER), "internal buffer path called once per acquisition");
  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_ENABLE_TEMP_BUFFER), "internal buffer path called once per acquisition");
}

static void test_internal_paths_before_conversion_and_no_premature_conversion(void) {
  adc_precision_context_t context;
  adc_precision_timeouts_t timeouts;
  uint16_t sample = 0U;
  adc_precision_error_t err = ADC_PRECISION_OK;

  reset_port_state();
  set_default_timeouts(&timeouts);
  set_default_samples(1U, 2U, 3U, 4U, 5U, 6U);
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "prepare before conversion checks");
  err = adc_precision_read_raw(&context, 5U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "read ok");
  EXPECT(port_event_order_is(EVENT_ENABLE_VREF, EVENT_SELECT), "vref path before select");
  EXPECT(port_event_order_is(EVENT_ENABLE_TEMP, EVENT_SELECT), "temp path before select");
  EXPECT(port_event_order_is(EVENT_ENABLE_VREF_BUFFER, EVENT_SELECT), "vref buffer path before select");
  EXPECT(port_event_order_is(EVENT_ENABLE_TEMP_BUFFER, EVENT_SELECT), "temp buffer path before select");
  EXPECT(port_event_order_is(EVENT_ENABLE_VREF, EVENT_START_CONVERSION), "enable vref before conversion");
  EXPECT(port_event_order_is(EVENT_ENABLE_TEMP, EVENT_START_CONVERSION), "enable temp before conversion");
  EXPECT(port_event_order_is(EVENT_ENABLE_VREF_BUFFER, EVENT_START_CONVERSION),
    "enable vref buffer before conversion");
  EXPECT(port_event_order_is(EVENT_ENABLE_TEMP_BUFFER, EVENT_START_CONVERSION),
    "enable temp buffer before conversion");
  EXPECT(!test_state.sequence_violation, "no sequencing violation");
}

static void test_conversion_timeout_and_overrun_preserve_output(void) {
  adc_precision_context_t context;
  adc_precision_timeouts_t timeouts;
  uint16_t sample = 1234U;
  adc_precision_error_t err = ADC_PRECISION_OK;

  reset_port_state();
  set_default_timeouts(&timeouts);
  set_default_samples(9U, 10U, 11U, 12U, 13U, 14U);
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "prepare for conversion timeout");
  sample = 1234U;
  timeouts.conversion_ms = 1U;
  test_state.conversion_polls = 100U;
  err = adc_precision_read_raw(&context, 6U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_TIMEOUT, err, "timeout while waiting conversion");
  EXPECT_EQ_U32(1234U, sample, "output preserved on conversion timeout");
  EXPECT_EQ_U32(0U, event_count_by_kind(EVENT_READ), "read not observed on timeout");

  reset_port_state();
  set_default_timeouts(&timeouts);
  set_default_samples(9U, 10U, 11U, 12U, 13U, 14U);
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "prepare for overrun path");
  sample = 7777U;
  err = adc_precision_read_raw(&context, 6U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "first retained read");
  sample = 5555U;
  test_state.overrun_sample_index = 2U;
  err = adc_precision_read_raw(&context, 6U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_OVERRUN, err, "overrun returned");
  EXPECT_EQ_U32(5555U, sample, "output preserved on overrun");
}

static void test_channel_switch_discard_retained_overrun_reprepare_required(void) {
  adc_precision_context_t context;
  adc_precision_timeouts_t timeouts;
  uint16_t sample = 0U;
  adc_precision_error_t err = ADC_PRECISION_OK;
  size_t conversions_before = 0U;

  reset_port_state();
  set_default_timeouts(&timeouts);
  test_state.samples[0] = 0U;
  test_state.samples[1] = 1U;
  test_state.samples[2] = 2U;
  test_state.samples[3] = 3U;
  test_state.samples[4] = 4U;
  test_state.samples[5] = 302U;
  test_state.sample_count = 6U;
  test_state.sample_index = 0U;
  test_state.overrun_sample_index = 3U;
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "prepare before retained-overrun invalidation");

  err = adc_precision_read_raw(&context, 7U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "channel 7 establishes retention");
  EXPECT_EQ_U32(1U, sample, "channel 7 retained sample");

  sample = 9999U;
  err = adc_precision_read_raw(&context, 8U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_OVERRUN, err, "channel 8 retained conversion can overrun");
  EXPECT_EQ_U32(9999U, sample, "overrun preserves caller output");
  EXPECT(!context.acquisition_prepared, "prepared flag cleared on retained failure");
  EXPECT(!context.has_retained_channel, "retained channel state cleared on retained failure");

  sample = 4321U;
  err = adc_precision_read_raw(&context, 8U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_INVALID_ARGUMENT, err, "read rejected until prepare after failure");
  EXPECT_EQ_U32(4321U, sample, "rejection preserves caller value");
  EXPECT_EQ_U32(4U, event_count_by_kind(EVENT_START_CONVERSION), "no conversion during immediate retry");

  test_state.stop_polls = 2U;
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "re-prepare required and succeeds");
  EXPECT_EQ_U32(1U, event_count_by_kind(EVENT_DISABLE), "reprepare disables ADC");
  EXPECT_EQ_U32(2U, event_count_by_kind(EVENT_CONFIGURE), "reprepare reconfigures once");
  EXPECT_EQ_U32(2U, event_count_by_kind(EVENT_CALIBRATE), "reprepare recalibrates once");
  EXPECT(port_event_nth_order_is(EVENT_DISABLE, EVENT_CONFIGURE, 1U, 2U),
    "disable happens before reprepare configure");
  EXPECT(port_event_nth_order_is(EVENT_CONFIGURE, EVENT_CALIBRATE, 2U, 2U),
    "configure happens before reprepare calibration");

  conversions_before = event_count_by_kind(EVENT_START_CONVERSION);
  EXPECT_EQ_U32(4U, conversions_before, "reprepare performs no conversion");
  sample = 8888U;
  err = adc_precision_read_raw(&context, 8U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "recovered channel 8 read succeeds");
  EXPECT_EQ_U32(302U, sample, "recovered read returns retained sample");
  EXPECT_EQ_U32(2U, (uint32_t)(event_count_by_kind(EVENT_START_CONVERSION) - conversions_before),
    "recovered read performs one discard and one retained");
}

static void test_retained_timeout_recovery_and_reprepare(void) {
  adc_precision_context_t context;
  adc_precision_timeouts_t timeouts;
  uint16_t sample = 0U;
  adc_precision_error_t err = ADC_PRECISION_OK;
  size_t conversions_before = 0U;
  size_t conversions_after = 0U;

  reset_port_state();
  set_default_timeouts(&timeouts);
  set_default_samples(10U, 11U, 12U, 13U, 14U, 15U);
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "prepare for retained-timeout recovery");
  err = adc_precision_read_raw(&context, 7U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "seed retained sample");

  test_state.conversion_stopped = false;
  sample = 9876U;
  timeouts.conversion_ms = 1U;
  test_state.conversion_polls = 100U;
  err = adc_precision_read_raw(&context, 7U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_TIMEOUT, err, "same-channel retained read can timeout");
  EXPECT_EQ_U32(9876U, sample, "retained timeout preserves caller sentinel");
  EXPECT(!context.acquisition_prepared, "prepared flag cleared on retained timeout");
  EXPECT(!context.has_retained_channel, "retained state cleared on retained timeout");

  sample = 8765U;
  err = adc_precision_read_raw(&context, 7U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_INVALID_ARGUMENT, err, "read still rejected after retained timeout");
  EXPECT_EQ_U32(8765U, sample, "rejection preserves caller sentinel");
  size_t stop_count_before = event_count_by_kind(EVENT_STOP);
  test_state.stop_polls = 1U;
  test_state.conversion_stopped = false;

  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "re-prepare after retained timeout");
  EXPECT_EQ_U32(stop_count_before + 1U, event_count_by_kind(EVENT_STOP),
    "timeout recovery prepare issues one stop");
  set_default_timeouts(&timeouts);
  test_state.conversion_polls = 0U;
  conversions_before = event_count_by_kind(EVENT_START_CONVERSION);
  err = adc_precision_read_raw(&context, 7U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "read after re-prepare succeeds");
  conversions_after = event_count_by_kind(EVENT_START_CONVERSION);
  EXPECT_EQ_U32(2U, (uint32_t)(conversions_after - conversions_before),
    "recovered read again performs one discard and one retained");
  EXPECT(!test_state.sequence_violation, "recovered conversion has no sequence_violation");
}

static void test_channel_bounds_and_setup_preconditions(void) {
  adc_precision_context_t context;
  adc_precision_timeouts_t timeouts;
  uint16_t sample = 0U;
  adc_precision_error_t err = ADC_PRECISION_OK;

  reset_port_state();
  set_default_timeouts(&timeouts);
  set_default_samples(1U, 2U, 3U, 4U, 5U, 6U);
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "prepare for bounds checks");
  err = adc_precision_read_raw(&context, ADC_PRECISION_CHANNEL_MIN, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "channel 0 accepted");
  err = adc_precision_read_raw(&context, ADC_PRECISION_CHANNEL_MAX, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "channel 18 accepted");
  err = adc_precision_read_raw(&context, ADC_PRECISION_CHANNEL_MAX + 1U, &timeouts, &sample);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_INVALID_ARGUMENT, err, "channel 19 rejected");
}

static void test_compute_vdda_and_channel_uv_bounds(void) {
  uint32_t out_vdda = 0U;
  uint32_t out_uv = 0U;
  adc_precision_error_t err = ADC_PRECISION_OK;

  err = adc_precision_compute_vdda_uv(1500U, 4095U, &out_vdda);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "vdda valid cal/os accepted");
  EXPECT_EQ_U32(17582417U, out_vdda, "vdda exact for valid inputs");

  out_vdda = 12345U;
  err = adc_precision_compute_vdda_uv(0U, 4095U, &out_vdda);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_INVALID_ARGUMENT, err, "vdda rejects zero cal");
  EXPECT_EQ_U32(12345U, out_vdda, "vdda sentinel preserved on zero cal");

  out_vdda = 12345U;
  err = adc_precision_compute_vdda_uv(4096U, 4095U, &out_vdda);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_INVALID_ARGUMENT, err, "vdda rejects cal above 4095");
  EXPECT_EQ_U32(12345U, out_vdda, "vdda sentinel preserved on cal above max");

  out_vdda = 12345U;
  err = adc_precision_compute_vdda_uv(1500U, 65521U, &out_vdda);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_INVALID_ARGUMENT, err, "vdda rejects os above full scale");
  EXPECT_EQ_U32(12345U, out_vdda, "vdda sentinel preserved on os above max scale");

  out_vdda = 12345U;
  err = adc_precision_compute_vdda_uv(4095U, 0U, &out_vdda);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_INVALID_ARGUMENT, err, "vdda rejects zero os");
  EXPECT_EQ_U32(12345U, out_vdda, "vdda sentinel preserved on zero os");

  err = adc_precision_compute_vdda_uv(4095U, 1U, &out_vdda);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_OVERFLOW, err, "vdda overflow is reported");
  EXPECT_EQ_U32(12345U, out_vdda, "vdda sentinel preserved on overflow");

  err = adc_precision_compute_channel_uv(65520U, 18000000U, &out_uv);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "channel max code accepted");
  EXPECT_EQ_U32(18000000U, out_uv, "channel max maps to vdd");

  out_uv = 54321U;
  err = adc_precision_compute_channel_uv(1U, 18000000U, &out_uv);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "channel code 1 accepted");
  EXPECT_EQ_U32(274U, out_uv, "channel floor");

  out_uv = 54321U;
  err = adc_precision_compute_channel_uv(0U, 18000000U, &out_uv);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "code 0 accepted with non-zero vdda");
  EXPECT_EQ_U32(0U, out_uv, "code 0 maps to zero");

  out_uv = 54321U;
  err = adc_precision_compute_channel_uv(0U, 0U, &out_uv);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_INVALID_ARGUMENT, err, "code 0 invalid with zero vdda");
  EXPECT_EQ_U32(54321U, out_uv, "channel uv sentinel preserved on zero vdda");

  out_uv = 54321U;
  err = adc_precision_compute_channel_uv(65520U, UINT32_MAX, &out_uv);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "channel max code with max vdda accepted");
  EXPECT_EQ_U32(UINT32_MAX, out_uv, "channel max code with max vdda maps to UINT32_MAX");

  out_uv = 54321U;
  err = adc_precision_compute_channel_uv(65521U, 18000000U, &out_uv);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_INVALID_ARGUMENT, err, "channel code overflow rejected");
  EXPECT_EQ_U32(54321U, out_uv, "channel uv sentinel preserved on overflow");
}

static void test_read_uV_preserves_sentinel_on_error(void) {
  adc_precision_context_t context;
  adc_precision_timeouts_t timeouts;
  uint32_t out_uv = 2020U;
  adc_precision_error_t err = ADC_PRECISION_OK;

  reset_port_state();
  set_default_timeouts(&timeouts);
  set_default_samples(5U, 6U, 7U, 8U, 9U, 10U);
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "prepare before read_uV");
  timeouts.conversion_ms = 1U;
  test_state.conversion_polls = 100U;
  err = adc_precision_read_uV(&context, 7U, 1500U, 4095U, &timeouts, &out_uv);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_TIMEOUT, err, "read_uV timeout error");
  EXPECT_EQ_U32(2020U, out_uv, "sentinel preserved through timeout");

  out_uv = 2020U;
  err = adc_precision_read_uV(&context, ADC_PRECISION_CHANNEL_MAX + 1U, 1500U, 4095U, &timeouts, &out_uv);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_INVALID_ARGUMENT, err, "read_uV rejects invalid channel");
  EXPECT_EQ_U32(2020U, out_uv, "sentinel preserved through channel error");

  out_uv = 2020U;
  err = adc_precision_read_uV(&context, 7U, 4096U, 1U, &timeouts, &out_uv);
  EXPECT_EQ_ERR(ADC_PRECISION_ERROR_INVALID_ARGUMENT, err, "read_uV rejects invalid cal");
  EXPECT_EQ_U32(2020U, out_uv, "sentinel preserved through helper error");
}

static void test_read_uV_end_to_end(void) {
  adc_precision_context_t context;
  adc_precision_timeouts_t timeouts;
  uint32_t out_uv = 0U;
  adc_precision_error_t err = ADC_PRECISION_OK;

  reset_port_state();
  set_default_timeouts(&timeouts);
  set_default_samples(65520U, 65520U, 1U, 2U, 3U, 4U);
  adc_precision_init(&context, &test_port, &test_state);
  err = adc_precision_prepare(&context, &timeouts);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "prepare read_uV");
  err = adc_precision_read_uV(&context, 7U, 1500U, 4095U, &timeouts, &out_uv);
  EXPECT_EQ_ERR(ADC_PRECISION_OK, err, "read_uV success");
  EXPECT_EQ_U32(17582417U, out_uv, "read_uV computed value");
}

static void run_tests(void (*fn)(void), const char* name, uint32_t* passed, uint32_t* failed) {
  const uint32_t before = failure_count;
  fn();
  if (failure_count == before) {
    ++(*passed);
    printf("PASS: %s\n", name);
  } else {
    ++(*failed);
    printf("FAILURES in %s\n", name);
  }
}

int main(void) {
  uint32_t passed = 0U;
  uint32_t failed = 0U;

  run_tests(test_default_timeouts, "default timeouts", &passed, &failed);
  run_tests(test_prepare_sequence_and_config, "prepare sequence and config", &passed, &failed);
  run_tests(test_prepare_noop_when_already_safe, "prepare already-safe no-ops", &passed, &failed);
  run_tests(test_prepare_timeout_steps_and_no_conversion, "prepare timeout steps", &passed, &failed);
  run_tests(test_prepare_readiness_timeout_paths_do_not_start_conversion, "prepare readiness timeout safety", &passed, &failed);
  run_tests(test_prepare_readiness_timeout_split_uses_distinct_timeouts,
    "prepare readiness timeout split", &passed, &failed);
  run_tests(test_discard_and_retained_sample_sequence, "discard/retain sequencing", &passed, &failed);
  run_tests(test_setup_only_once_per_acquisition, "single-cycle setup once", &passed, &failed);
  run_tests(test_internal_paths_before_conversion_and_no_premature_conversion, "internal path readiness order", &passed, &failed);
  run_tests(test_conversion_timeout_and_overrun_preserve_output, "conversion timeout and overrun", &passed, &failed);
  run_tests(test_channel_switch_discard_retained_overrun_reprepare_required,
    "channel switch overrun invalidates acquisition", &passed, &failed);
  run_tests(test_retained_timeout_recovery_and_reprepare, "retained timeout recovery", &passed, &failed);
  run_tests(test_channel_bounds_and_setup_preconditions, "channel bounds", &passed, &failed);
  run_tests(test_compute_vdda_and_channel_uv_bounds, "math helper boundaries", &passed, &failed);
  run_tests(test_read_uV_preserves_sentinel_on_error, "read_uV error preservation", &passed, &failed);
  run_tests(test_read_uV_end_to_end, "read_uV end-to-end", &passed, &failed);

  printf("adc precision tests: %u passed, %u failed\n", passed, failed);
  return failed == 0U ? 0 : 1;
}
