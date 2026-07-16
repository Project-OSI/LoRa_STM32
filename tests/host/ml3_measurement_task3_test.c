#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "adc_precision.h"
#include "ml3_measurement.h"

static uint32_t failure_count = 0U;

#define EXPECT(condition, message) do { \
  if (!(condition)) { \
    ++failure_count; \
    printf("FAIL: %s\n", message); \
    return; \
  } \
} while (0)

#define EXPECT_U32(expected, actual, message) do { \
  if ((uint32_t)(expected) != (uint32_t)(actual)) { \
    ++failure_count; \
    printf("FAIL: %s: expected=%u actual=%u\n", message, \
      (unsigned)(uint32_t)(expected), (unsigned)(uint32_t)(actual)); \
    return; \
  } \
} while (0)

#define EXPECT_I64(expected, actual, message) do { \
  if ((int64_t)(expected) != (int64_t)(actual)) { \
    ++failure_count; \
    printf("FAIL: %s: expected=%lld actual=%lld\n", message, \
      (long long)(int64_t)(expected), (long long)(int64_t)(actual)); \
    return; \
  } \
} while (0)

#define EXPECT_ERR(expected, actual, message) EXPECT_U32((uint32_t)(expected), (uint32_t)(actual), message)
#define EXPECT_TRUE(condition, message) EXPECT((condition), message)
#define EXPECT_FALSE(condition, message) EXPECT(!(condition), message)

enum {
  TEST_EVENT_RESET_CAUSE = 1U,
  TEST_EVENT_RADIO_SLEEP,
  TEST_EVENT_ANALOG_PREP,
  TEST_EVENT_STOP_CONVERSION,
  TEST_EVENT_DISABLE_ADC,
  TEST_EVENT_ADC_CONFIGURE,
  TEST_EVENT_ADC_CAL,
  TEST_EVENT_SELECT_CHANNEL,
  TEST_EVENT_START_CONVERSION,
  TEST_EVENT_RAW_CONSUMED,
  TEST_EVENT_WATCHDOG,
  TEST_EVENT_SET_POWER,
  TEST_EVENT_SET_THERM,
  TEST_EVENT_PROCESS,
  TEST_EVENT_BUILD,
  TEST_EVENT_QUEUE,
  TEST_EVENT_ADC_TIMEOUT_INJECTION,
  TEST_EVENT_ADC_OVERRUN_INJECTION,
  TEST_ML3_FIXED_VREF_CHANNEL = 17U,
  TEST_ML3_FIXED_DIE_TEMP_CHANNEL = 18U,
  TEST_ML3_WARMUP_TIME_STEP_MS = 500U
};

typedef struct {
  uint16_t kind;
  uint16_t arg;
  ml3_state_t state;
  bool applied;
} test_event_t;

typedef struct {
  uint16_t kind;
  size_t call_index;
  size_t event_index;
  bool power_state_after;
  bool therm_state_after;
} test_callback_failure_t;

typedef struct {
  uint32_t now_ms;
  uint32_t now_step;
  bool auto_advance_warmup;

  size_t event_count;
  test_event_t events[512];
  size_t callback_failure_count;
  test_callback_failure_t callback_failures[16];

  const ml3_measurement_ctx_t* meas_ctx;
  ml3_state_t entered_states[256];
  size_t entered_state_count;
  size_t total_steps;
  ml3_state_t last_state;

  bool conversion_stopped;
  bool conversion_unread;

  size_t stop_polls;
  size_t disable_polls;
  size_t calibration_polls;
  size_t ready_polls;
  size_t vref_ready_polls;
  size_t temp_ready_polls;
  size_t vref_buffer_ready_polls;
  size_t temp_buffer_ready_polls;
  size_t settle_polls;
  size_t conversion_polls;
  size_t default_conversion_polls;
  size_t timeout_conversion_polls;

  size_t sample_count;
  size_t sample_index;
  uint16_t samples[512];
  bool sample_overrun[512];
  uint16_t discard_raw_code;
  size_t verify_discharge_vref_count;
  size_t verify_discharge_vref_index;
  uint16_t verify_discharge_vref_samples[64];

  size_t selected_channel_count;
  uint16_t selected_channels[512];

  bool adc_enabled;
  bool vref_int_enabled;
  bool temp_enabled;
  bool vref_buffer_enabled;
  bool temp_buffer_enabled;

  size_t configure_analog_calls;
  size_t set_radio_calls;
  size_t set_power_calls;
  size_t set_power_true_calls;
  size_t set_power_false_calls;
  size_t set_therm_calls;
  size_t set_therm_true_calls;
  size_t set_therm_false_calls;
  size_t set_therm_on_retained_read_index;
  size_t set_therm_on_sample_index;
  size_t watchdog_calls;
  size_t process_calls;
  size_t build_calls;
  size_t queue_calls;
  size_t reset_cause_calls;
  uint32_t reset_cause_value;
  uint16_t sequence_at_reset_cause;
  bool process_result_seen;
  ml3_measurement_result_t process_result;
  bool build_result_seen;
  ml3_measurement_result_t build_result;
  bool queued_result_seen;
  ml3_measurement_result_t queued_result;
  size_t adc_configure_calls;
  size_t adc_self_cal_calls;

  bool last_power_state;
  bool last_therm_state;
  bool power_therm_overlap;
  bool queue_after_lows;

  size_t request_radio_sleep_fail_at;
  size_t configure_analog_fail_at;
  size_t set_power_fail_at;
  size_t set_therm_fail_at;
  size_t watchdog_fail_at;
  size_t process_fail_at;
  size_t build_fail_at;
  size_t queue_fail_at;

  ml3_state_t inject_overrun_state;
  size_t inject_overrun_conversion_index;
  size_t inject_overrun_retained_index;
  uint16_t inject_overrun_channel;
  bool inject_overrun_target_discard;
  bool inject_overrun_seen;
  size_t inject_overrun_hit_event_index;
  ml3_state_t inject_overrun_hit_state;
  size_t inject_overrun_hit_conversion_index;
  size_t inject_overrun_hit_retained_index;
  uint16_t inject_overrun_hit_channel;
  bool inject_overrun_hit_discard;

  ml3_state_t inject_timeout_state;
  size_t inject_timeout_index;
  bool inject_timeout_seen;
  size_t inject_timeout_hit_event_index;
  ml3_state_t inject_timeout_hit_state;
  size_t inject_timeout_hit_index;
  size_t inject_timeout_hit_retained_index;
  uint16_t inject_timeout_hit_channel;
  bool inject_timeout_hit_discard;

  ml3_state_t last_select_state;
  size_t state_select_index;
  uint16_t last_select_channel;
  bool discard_next_read;
  ml3_state_t last_conversion_state;
  size_t state_conversion_index;
  ml3_state_t last_read_state;
  size_t state_read_index;
  size_t state_retained_read_index;
} test_state_t;

static test_state_t test_state;
static void test_set_timeout_injection(
  ml3_state_t timeout_state,
  size_t timeout_index);

static size_t test_push_event(uint16_t kind, uint16_t arg) {
  size_t event_index = test_state.event_count;
  if (test_state.event_count < 512U) {
    test_state.events[test_state.event_count].kind = kind;
    test_state.events[test_state.event_count].arg = arg;
    test_state.events[test_state.event_count].state =
      (test_state.meas_ctx == NULL) ? ML3_STATE_IDLE : ml3_measurement_state(test_state.meas_ctx);
    test_state.events[test_state.event_count].applied = false;
    ++test_state.event_count;
    return event_index;
  }
  return SIZE_MAX;
}

static void test_record_callback_failure(
  uint16_t kind,
  size_t call_index,
  size_t event_index) {
  if (test_state.callback_failure_count < 16U) {
    test_callback_failure_t* failure =
      &test_state.callback_failures[test_state.callback_failure_count];
    failure->kind = kind;
    failure->call_index = call_index;
    failure->event_index = event_index;
    failure->power_state_after = test_state.last_power_state;
    failure->therm_state_after = test_state.last_therm_state;
    ++test_state.callback_failure_count;
  }
}

static void test_record_entered_state(ml3_state_t state) {
  if ((test_state.entered_state_count < 256U) &&
      ((test_state.entered_state_count == 0U) ||
       (test_state.entered_states[test_state.entered_state_count - 1U] != state))) {
    test_state.entered_states[test_state.entered_state_count] = state;
    ++test_state.entered_state_count;
  }
}

static void test_reset_measurement_state(void) {
  memset(&test_state, 0, sizeof(test_state));
  test_state.now_step = 1U;
  test_state.auto_advance_warmup = true;
  test_state.default_conversion_polls = 1U;
  test_state.timeout_conversion_polls = 2U;
  test_state.stop_polls = 0U;
  test_state.disable_polls = 0U;
  test_state.calibration_polls = 0U;
  test_state.ready_polls = 0U;
  test_state.vref_ready_polls = 0U;
  test_state.temp_ready_polls = 0U;
  test_state.vref_buffer_ready_polls = 0U;
  test_state.temp_buffer_ready_polls = 0U;
  test_state.settle_polls = 0U;
  test_state.set_radio_calls = 0U;
  test_state.configure_analog_calls = 0U;
  test_state.set_power_calls = 0U;
  test_state.set_therm_calls = 0U;
  test_state.watchdog_calls = 0U;
  test_state.adc_configure_calls = 0U;
  test_state.adc_self_cal_calls = 0U;
  test_state.request_radio_sleep_fail_at = 0U;
  test_state.configure_analog_fail_at = 0U;
  test_state.set_power_fail_at = 0U;
  test_state.set_therm_fail_at = 0U;
  test_state.watchdog_fail_at = 0U;
  test_state.process_fail_at = 0U;
  test_state.build_fail_at = 0U;
  test_state.queue_fail_at = 0U;
  test_state.inject_overrun_state = ML3_STATE_IDLE;
  test_state.inject_overrun_conversion_index = SIZE_MAX;
  test_state.inject_overrun_retained_index = SIZE_MAX;
  test_state.inject_overrun_channel = UINT16_MAX;
  test_state.inject_overrun_target_discard = false;
  test_state.inject_overrun_seen = false;
  test_state.inject_overrun_hit_event_index = SIZE_MAX;
  test_state.inject_overrun_hit_state = ML3_STATE_IDLE;
  test_state.inject_overrun_hit_conversion_index = 0U;
  test_state.inject_overrun_hit_retained_index = 0U;
  test_state.inject_overrun_hit_channel = UINT16_MAX;
  test_state.inject_overrun_hit_discard = false;
  test_state.inject_timeout_state = ML3_STATE_IDLE;
  test_state.inject_timeout_index = SIZE_MAX;
  test_state.inject_timeout_seen = false;
  test_state.inject_timeout_hit_event_index = SIZE_MAX;
  test_state.inject_timeout_hit_state = ML3_STATE_IDLE;
  test_state.inject_timeout_hit_index = 0U;
  test_state.inject_timeout_hit_retained_index = 0U;
  test_state.inject_timeout_hit_channel = UINT16_MAX;
  test_state.inject_timeout_hit_discard = false;
  test_state.last_select_state = ML3_STATE_IDLE;
  test_state.state_select_index = 0U;
  test_state.last_select_channel = UINT16_MAX;
  test_state.last_conversion_state = ML3_STATE_IDLE;
  test_state.state_conversion_index = 0U;
  test_state.last_read_state = ML3_STATE_IDLE;
  test_state.state_read_index = 0U;
  test_state.state_retained_read_index = 0U;
  test_state.discard_next_read = false;
  test_state.conversion_stopped = true;
}

static uint32_t test_now_ms(void* user_ctx) {
  uint32_t step = test_state.now_step;
  (void)user_ctx;
  if ((test_state.meas_ctx != NULL) &&
      (ml3_measurement_state(test_state.meas_ctx) == ML3_STATE_WARMUP) &&
      test_state.auto_advance_warmup &&
      (step < TEST_ML3_WARMUP_TIME_STEP_MS)) {
    step = TEST_ML3_WARMUP_TIME_STEP_MS;
  }
  test_state.now_ms += step;
  return test_state.now_ms;
}

static uint32_t test_read_reset_cause(void* user_ctx) {
  (void)user_ctx;
  ++test_state.reset_cause_calls;
  test_state.sequence_at_reset_cause = test_state.meas_ctx == NULL
    ? UINT16_MAX
    : test_state.meas_ctx->sequence;
  test_push_event(TEST_EVENT_RESET_CAUSE, 0U);
  return test_state.reset_cause_value;
}

static void test_set_now_ms(uint32_t now_ms) {
  test_state.now_ms = now_ms;
}

static void test_set_now_step(uint32_t now_step) {
  test_state.now_step = now_step;
}

static void test_request_stop_conversion(void* user_ctx) {
  (void)user_ctx;
  test_state.conversion_stopped = false;
  test_state.conversion_unread = false;
  test_push_event(TEST_EVENT_STOP_CONVERSION, 0U);
}

static bool test_is_conversion_stopped(void* user_ctx) {
  (void)user_ctx;
  if (test_state.stop_polls > 0U) {
    --test_state.stop_polls;
    return false;
  }
  return true;
}

static void test_request_disable_adc(void* user_ctx) {
  (void)user_ctx;
  test_push_event(TEST_EVENT_DISABLE_ADC, 0U);
  test_state.adc_enabled = false;
}

static bool test_is_adc_disabled(void* user_ctx) {
  (void)user_ctx;
  if (test_state.disable_polls > 0U) {
    --test_state.disable_polls;
    return false;
  }
  return true;
}

static void test_configure(void* user_ctx, const adc_precision_config_t* config) {
  (void)user_ctx;
  (void)config;
  ++test_state.adc_configure_calls;
  test_push_event(TEST_EVENT_ADC_CONFIGURE, 0U);
}

static void test_request_self_calibration(void* user_ctx) {
  (void)user_ctx;
  ++test_state.adc_self_cal_calls;
  test_push_event(TEST_EVENT_ADC_CAL, 0U);
}

static bool test_is_calibration_complete(void* user_ctx) {
  (void)user_ctx;
  if (test_state.calibration_polls > 0U) {
    --test_state.calibration_polls;
    return false;
  }
  return true;
}

static void test_request_enable_adc(void* user_ctx) {
  (void)user_ctx;
  test_state.adc_enabled = true;
}

static bool test_is_adc_ready(void* user_ctx) {
  (void)user_ctx;
  if (test_state.ready_polls > 0U) {
    --test_state.ready_polls;
    return false;
  }
  return true;
}

static void test_enable_vrefint_gate(void* user_ctx) {
  (void)user_ctx;
  test_state.vref_int_enabled = true;
}

static void test_enable_temp_gate(void* user_ctx) {
  (void)user_ctx;
  test_state.temp_enabled = true;
}

static void test_enable_vrefint_buffer_gate(void* user_ctx) {
  (void)user_ctx;
  test_state.vref_buffer_enabled = true;
}

static void test_enable_temperature_buffer_gate(void* user_ctx) {
  (void)user_ctx;
  test_state.temp_buffer_enabled = true;
}

static bool test_is_vrefint_ready(void* user_ctx) {
  (void)user_ctx;
  if (test_state.vref_ready_polls > 0U) {
    --test_state.vref_ready_polls;
    return false;
  }
  return true;
}

static bool test_is_temperature_ready(void* user_ctx) {
  (void)user_ctx;
  if (test_state.temp_ready_polls > 0U) {
    --test_state.temp_ready_polls;
    return false;
  }
  return true;
}

static bool test_is_vrefint_buffer_ready(void* user_ctx) {
  (void)user_ctx;
  if (test_state.vref_buffer_ready_polls > 0U) {
    --test_state.vref_buffer_ready_polls;
    return false;
  }
  return true;
}

static bool test_is_temperature_buffer_ready(void* user_ctx) {
  (void)user_ctx;
  if (test_state.temp_buffer_ready_polls > 0U) {
    --test_state.temp_buffer_ready_polls;
    return false;
  }
  return true;
}

static bool test_is_reference_settled(void* user_ctx) {
  (void)user_ctx;
  if (test_state.settle_polls > 0U) {
    --test_state.settle_polls;
    return false;
  }
  return true;
}

static void test_select_channel(void* user_ctx, uint16_t channel) {
  (void)user_ctx;
  test_state.discard_next_read = (test_state.last_select_channel != channel);
  if (test_state.selected_channel_count < 512U) {
    test_state.selected_channels[test_state.selected_channel_count] = channel;
    ++test_state.selected_channel_count;
  }
  test_state.last_select_channel = channel;

  if (test_state.meas_ctx != NULL) {
    const ml3_state_t current_state = ml3_measurement_state(test_state.meas_ctx);
    if (current_state == test_state.last_select_state) {
      ++test_state.state_select_index;
    } else {
      test_state.last_select_state = current_state;
      test_state.state_select_index = 0U;
    }
  } else {
    test_state.last_select_state = ML3_STATE_IDLE;
    test_state.state_select_index = 0U;
  }
  test_push_event(TEST_EVENT_SELECT_CHANNEL, channel);
}

static void test_start_conversion(void* user_ctx) {
  (void)user_ctx;
  if ((test_state.meas_ctx != NULL) &&
      (ml3_measurement_state(test_state.meas_ctx) != test_state.last_read_state)) {
    test_state.last_read_state = ml3_measurement_state(test_state.meas_ctx);
    test_state.state_read_index = 0U;
    test_state.state_retained_read_index = 0U;
  }
  test_state.conversion_unread = true;
  if (test_state.meas_ctx != NULL) {
    const ml3_state_t current_state = ml3_measurement_state(test_state.meas_ctx);
    if (current_state == test_state.last_conversion_state) {
      ++test_state.state_conversion_index;
    } else {
      test_state.last_conversion_state = current_state;
      test_state.state_conversion_index = 0U;
    }
  }
  test_push_event(TEST_EVENT_START_CONVERSION, test_state.last_select_channel);

  if ((test_state.meas_ctx != NULL) &&
      (test_state.inject_timeout_state != ML3_STATE_IDLE) &&
      (!test_state.inject_timeout_seen) &&
      (ml3_measurement_state(test_state.meas_ctx) == test_state.inject_timeout_state) &&
      ((test_state.inject_timeout_index == SIZE_MAX) ||
       (test_state.state_conversion_index == test_state.inject_timeout_index))) {
    test_state.inject_timeout_hit_state = ml3_measurement_state(test_state.meas_ctx);
    test_state.inject_timeout_hit_event_index = test_push_event(
      TEST_EVENT_ADC_TIMEOUT_INJECTION,
      test_state.last_select_channel);
    test_state.inject_timeout_hit_index = test_state.state_conversion_index;
    test_state.inject_timeout_hit_retained_index = test_state.state_retained_read_index;
    test_state.inject_timeout_hit_discard = test_state.discard_next_read;
    test_state.inject_timeout_hit_channel = test_state.last_select_channel;
    test_state.conversion_polls = test_state.timeout_conversion_polls;
    test_state.inject_timeout_seen = true;
  } else {
    test_state.conversion_polls = test_state.default_conversion_polls;
  }
}

static bool test_is_conversion_complete(void* user_ctx) {
  (void)user_ctx;
  if (test_state.conversion_polls > 0U) {
    --test_state.conversion_polls;
    return false;
  }
  return true;
}

static bool test_read_raw(void* user_ctx, uint16_t* raw_code, bool* overrun) {
  (void)user_ctx;
  size_t retained_read_index = 0U;
  size_t conversion_index = 0U;
  bool use_verify_discharge_vref = false;
  bool inject_overrun = false;

  if (raw_code == NULL) {
    return false;
  }

  if ((test_state.meas_ctx != NULL) && (ml3_measurement_state(test_state.meas_ctx) != test_state.last_read_state)) {
    test_state.last_read_state = ml3_measurement_state(test_state.meas_ctx);
    test_state.state_read_index = 0U;
    test_state.state_retained_read_index = 0U;
  }
  retained_read_index = test_state.state_retained_read_index;
  conversion_index = test_state.state_conversion_index;
  use_verify_discharge_vref =
    (test_state.meas_ctx != NULL) &&
    (ml3_measurement_state(test_state.meas_ctx) == ML3_STATE_VERIFY_DISCHARGE) &&
    (test_state.last_select_channel == ML3_MEASUREMENT_CHANNEL_VREFINT);

  inject_overrun =
      !test_state.inject_overrun_seen &&
      (test_state.inject_overrun_state != ML3_STATE_IDLE) &&
      (test_state.meas_ctx != NULL) &&
      (ml3_measurement_state(test_state.meas_ctx) == test_state.inject_overrun_state) &&
      (conversion_index == test_state.inject_overrun_conversion_index) &&
      (retained_read_index == test_state.inject_overrun_retained_index) &&
      (test_state.last_select_channel == test_state.inject_overrun_channel) &&
      (test_state.discard_next_read == test_state.inject_overrun_target_discard);

  if (inject_overrun) {
    if (overrun != NULL) {
      *overrun = true;
    }
    test_state.inject_overrun_hit_state = ml3_measurement_state(test_state.meas_ctx);
    test_state.inject_overrun_hit_event_index = test_push_event(
      TEST_EVENT_ADC_OVERRUN_INJECTION,
      test_state.last_select_channel);
    test_state.inject_overrun_hit_conversion_index = conversion_index;
    test_state.inject_overrun_hit_retained_index = retained_read_index;
    test_state.inject_overrun_hit_channel = test_state.last_select_channel;
    test_state.inject_overrun_hit_discard = test_state.discard_next_read;
    test_state.inject_overrun_seen = true;
  }

  if (test_state.discard_next_read) {
    test_state.discard_next_read = false;
    *raw_code = test_state.discard_raw_code;
    if ((overrun != NULL) && !inject_overrun) {
      *overrun = false;
    }
    test_state.conversion_unread = false;
    test_push_event(TEST_EVENT_RAW_CONSUMED, test_state.last_select_channel);
    return true;
  }
  if ((test_state.meas_ctx == NULL) ||
      (!use_verify_discharge_vref &&
       (test_state.sample_index >= test_state.sample_count))) {
    return false;
  }

  if ((overrun != NULL) && !inject_overrun) {
    *overrun = test_state.sample_overrun[test_state.sample_index];
  }

  if (use_verify_discharge_vref) {
    if (test_state.verify_discharge_vref_index < test_state.verify_discharge_vref_count) {
      *raw_code = test_state.verify_discharge_vref_samples[test_state.verify_discharge_vref_index];
      ++test_state.verify_discharge_vref_index;
    } else {
      *raw_code = test_state.meas_ctx->last_result.post_reference_raw;
    }
  } else {
    *raw_code = test_state.samples[test_state.sample_index];
    ++test_state.sample_index;
  }
  ++test_state.state_read_index;
  ++test_state.state_retained_read_index;
  test_state.conversion_unread = false;
  test_push_event(TEST_EVENT_RAW_CONSUMED, test_state.last_select_channel);
  return true;
}

static const adc_precision_port_t task3_adc_port = {
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
  test_enable_temp_gate,
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

static bool test_request_radio_sleep(void* user_ctx) {
  size_t event_index = SIZE_MAX;
  (void)user_ctx;
  ++test_state.set_radio_calls;
  event_index = test_push_event(TEST_EVENT_RADIO_SLEEP, 1U);
  if ((test_state.request_radio_sleep_fail_at != 0U) &&
      (test_state.set_radio_calls == test_state.request_radio_sleep_fail_at)) {
    test_record_callback_failure(
      TEST_EVENT_RADIO_SLEEP,
      test_state.set_radio_calls,
      event_index);
    return false;
  }
  return true;
}

static bool test_configure_analog_pins(void* user_ctx) {
  size_t event_index = SIZE_MAX;
  (void)user_ctx;
  ++test_state.configure_analog_calls;
  event_index = test_push_event(TEST_EVENT_ANALOG_PREP, 1U);
  if ((test_state.configure_analog_fail_at != 0U) &&
      (test_state.configure_analog_calls == test_state.configure_analog_fail_at)) {
    test_record_callback_failure(
      TEST_EVENT_ANALOG_PREP,
      test_state.configure_analog_calls,
      event_index);
    return false;
  }
  return true;
}

static bool test_set_power_5v(void* user_ctx, bool on) {
  size_t event_index = SIZE_MAX;
  (void)user_ctx;
  ++test_state.set_power_calls;
  if (on) {
    ++test_state.set_power_true_calls;
  } else {
    ++test_state.set_power_false_calls;
  }
  event_index = test_push_event(TEST_EVENT_SET_POWER, (uint16_t)(on ? 1U : 0U));
  if ((test_state.set_power_fail_at != 0U) &&
      (test_state.set_power_calls == test_state.set_power_fail_at)) {
    test_record_callback_failure(
      TEST_EVENT_SET_POWER,
      test_state.set_power_calls,
      event_index);
    return false;
  }
  test_state.last_power_state = on;
  if (event_index != SIZE_MAX) {
    test_state.events[event_index].applied = true;
  }
  if (test_state.last_therm_state) {
    test_state.power_therm_overlap = true;
  }
  return true;
}

static bool test_set_thermistor_excitation(void* user_ctx, bool on) {
  size_t event_index = SIZE_MAX;
  (void)user_ctx;
  ++test_state.set_therm_calls;
  if (on) {
    ++test_state.set_therm_true_calls;
    test_state.set_therm_on_retained_read_index = test_state.state_retained_read_index;
    test_state.set_therm_on_sample_index = test_state.sample_index;
  } else {
    ++test_state.set_therm_false_calls;
  }
  event_index = test_push_event(TEST_EVENT_SET_THERM, (uint16_t)(on ? 1U : 0U));
  if ((test_state.set_therm_fail_at != 0U) &&
      (test_state.set_therm_calls == test_state.set_therm_fail_at)) {
    test_record_callback_failure(
      TEST_EVENT_SET_THERM,
      test_state.set_therm_calls,
      event_index);
    return false;
  }
  test_state.last_therm_state = on;
  if (event_index != SIZE_MAX) {
    test_state.events[event_index].applied = true;
  }
  if (test_state.last_power_state) {
    test_state.power_therm_overlap = true;
  }
  return true;
}

static bool test_watchdog_refresh(void* user_ctx) {
  size_t event_index = SIZE_MAX;
  (void)user_ctx;
  ++test_state.watchdog_calls;
  event_index = test_push_event(TEST_EVENT_WATCHDOG, 1U);
  if ((test_state.watchdog_fail_at != 0U) &&
      (test_state.watchdog_calls == test_state.watchdog_fail_at)) {
    test_record_callback_failure(
      TEST_EVENT_WATCHDOG,
      test_state.watchdog_calls,
      event_index);
    return false;
  }
  return true;
}

static bool test_on_process(void* user_ctx, const ml3_measurement_result_t* result) {
  size_t event_index = SIZE_MAX;
  (void)user_ctx;
  if (result != NULL) {
    test_state.process_result = *result;
    test_state.process_result_seen = true;
  }
  ++test_state.process_calls;
  event_index = test_push_event(TEST_EVENT_PROCESS, 1U);
  test_state.queue_after_lows = (!test_state.last_power_state && !test_state.last_therm_state);
  if ((test_state.process_fail_at != 0U) &&
      (test_state.process_calls == test_state.process_fail_at)) {
    test_record_callback_failure(
      TEST_EVENT_PROCESS,
      test_state.process_calls,
      event_index);
    return false;
  }
  return true;
}

static bool test_on_build_payload(void* user_ctx, const ml3_measurement_result_t* result) {
  size_t event_index = SIZE_MAX;
  (void)user_ctx;
  if (result != NULL) {
    test_state.build_result = *result;
    test_state.build_result_seen = true;
  }
  ++test_state.build_calls;
  event_index = test_push_event(TEST_EVENT_BUILD, 1U);
  if ((test_state.build_fail_at != 0U) &&
      (test_state.build_calls == test_state.build_fail_at)) {
    test_record_callback_failure(
      TEST_EVENT_BUILD,
      test_state.build_calls,
      event_index);
    return false;
  }
  return true;
}

static bool test_on_queue(void* user_ctx, const ml3_measurement_result_t* result) {
  size_t event_index = SIZE_MAX;
  (void)user_ctx;
  if (result != NULL) {
    test_state.queued_result = *result;
    test_state.queued_result_seen = true;
  }
  ++test_state.queue_calls;
  event_index = test_push_event(TEST_EVENT_QUEUE, 1U);
  if ((test_state.queue_fail_at != 0U) &&
      (test_state.queue_calls == test_state.queue_fail_at)) {
    test_record_callback_failure(
      TEST_EVENT_QUEUE,
      test_state.queue_calls,
      event_index);
    return false;
  }
  return true;
}

static const ml3_measurement_port_t task3_measurement_port = {
  &test_state,
  test_now_ms,
  test_read_reset_cause,
  test_request_radio_sleep,
  test_configure_analog_pins,
  test_set_power_5v,
  test_set_thermistor_excitation,
  test_watchdog_refresh,
  test_on_process,
  test_on_build_payload,
  test_on_queue
};

static void test_set_samples(
  const uint16_t* samples,
  const bool* overrun,
  size_t count) {
  if (count > 512U) {
    count = 512U;
  }
  test_state.sample_count = count;
  memset(test_state.samples, 0, sizeof(test_state.samples));
  memset(test_state.sample_overrun, 0, sizeof(test_state.sample_overrun));
  for (size_t i = 0U; i < count; ++i) {
    test_state.samples[i] = samples[i];
    if (overrun != NULL) {
      test_state.sample_overrun[i] = overrun[i];
    }
  }
  test_state.sample_index = 0U;
}

static void test_set_verify_discharge_vref_samples(
  const uint16_t* samples,
  size_t count) {
  if (count > 64U) {
    count = 64U;
  }
  test_state.verify_discharge_vref_count = count;
  test_state.verify_discharge_vref_index = 0U;
  memset(test_state.verify_discharge_vref_samples, 0, sizeof(test_state.verify_discharge_vref_samples));
  for (size_t i = 0U; i < count; ++i) {
    test_state.verify_discharge_vref_samples[i] = samples[i];
  }
}

static size_t test_fill_nominal_samples(
  uint16_t* destination,
  size_t destination_capacity,
  size_t abba_cycles,
  uint16_t pre_vref,
  uint16_t pre_v5,
  uint16_t post_vref,
  uint16_t post_v5,
  uint16_t die_temp,
  uint16_t discharge,
  uint16_t thermistor,
  uint16_t h1,
  uint16_t l1,
  uint16_t l2,
  uint16_t h2,
  size_t extra_discharge_samples) {
  size_t index = 0U;
  size_t cycle = 0U;
  size_t d = 0U;

  if (destination_capacity == 0U) {
    return 0U;
  }
  if (abba_cycles > ML3_MEASUREMENT_MAX_ABBA_CYCLES) {
    abba_cycles = ML3_MEASUREMENT_MAX_ABBA_CYCLES;
  }

  if (index < destination_capacity) {
    destination[index++] = pre_vref;
  }
  if (index < destination_capacity) {
    destination[index++] = pre_v5;
  }
  for (cycle = 0U; cycle < abba_cycles; ++cycle) {
    if (index < destination_capacity) {
      destination[index++] = h1;
    }
    if (index < destination_capacity) {
      destination[index++] = l1;
    }
    if (index < destination_capacity) {
      destination[index++] = l2;
    }
    if (index < destination_capacity) {
      destination[index++] = h2;
    }
  }
  if (index < destination_capacity) {
    destination[index++] = post_vref;
  }
  if (index < destination_capacity) {
    destination[index++] = post_v5;
  }
  if (index < destination_capacity) {
    destination[index++] = die_temp;
  }
  for (d = 0U; d < extra_discharge_samples; ++d) {
    if (index < destination_capacity) {
      destination[index++] = discharge;
    }
  }
  if (index < destination_capacity) {
    destination[index++] = thermistor;
  }
  return index;
}

static size_t test_discharge_sample_start_index(size_t abba_cycles) {
  return 2U + (4U * abba_cycles) + 3U;
}

static size_t test_make_expected_selected_physical(
  uint16_t* destination,
  size_t destination_capacity,
  const uint16_t* requested_channels,
  size_t requested_count) {
  size_t index = 0U;
  size_t i = 0U;
  uint16_t last_channel = UINT16_MAX;

  if ((destination == NULL) || (requested_channels == NULL)) {
    return 0U;
  }

  for (i = 0U; i < requested_count; ++i) {
    const uint16_t channel = requested_channels[i];
    if (destination_capacity == 0U) {
      return 0U;
    }
    if (last_channel != channel) {
      if ((index + 2U) <= destination_capacity) {
        destination[index++] = channel;
        destination[index++] = channel;
      } else if (index < destination_capacity) {
        destination[index++] = channel;
      }
    } else {
      if (index < destination_capacity) {
        destination[index++] = channel;
      }
    }
    last_channel = channel;
  }

  return index;
}

static void test_prepare_adc_ctx(adc_precision_context_t* adc_ctx) {
  memset(adc_ctx, 0, sizeof(*adc_ctx));
  adc_ctx->port = &task3_adc_port;
  adc_ctx->port_ctx = &test_state;
}

static ml3_measurement_config_t test_default_config(uint16_t cycles) {
  ml3_measurement_config_t config = {
    .channel_hi = 2U,
    .channel_lo = 3U,
    .channel_v5 = 5U,
    .channel_thermistor = 7U,
    .abba_cycles = cycles,
    .vrefint_calibration_word = 3000U,
    .warmup_ms = 500U,
    .discharge_threshold_mv = 500U,
    .discharge_timeout_ms = 4U,
    .therm_settle_ms = 2U
  };
  return config;
}

static void test_reset_timeouts(adc_precision_timeouts_t* timeouts) {
  timeouts->stop_ms = 1U;
  timeouts->disable_ms = 1U;
  timeouts->calibration_ms = 1U;
  timeouts->ready_ms = 1U;
  timeouts->vref_ready_ms = 1U;
  timeouts->sensor_ready_ms = 1U;
  timeouts->settle_ms = 1U;
  timeouts->conversion_ms = 2U;
}

static void test_clear_overrun_injection(void) {
  test_state.inject_overrun_state = ML3_STATE_IDLE;
  test_state.inject_overrun_conversion_index = SIZE_MAX;
  test_state.inject_overrun_retained_index = SIZE_MAX;
  test_state.inject_overrun_channel = UINT16_MAX;
  test_state.inject_overrun_target_discard = false;
  test_state.inject_overrun_seen = false;
  test_state.inject_overrun_hit_event_index = SIZE_MAX;
  test_state.inject_overrun_hit_state = ML3_STATE_IDLE;
  test_state.inject_overrun_hit_conversion_index = 0U;
  test_state.inject_overrun_hit_retained_index = 0U;
  test_state.inject_overrun_hit_channel = UINT16_MAX;
  test_state.inject_overrun_hit_discard = false;
}

static void test_set_overrun_injection(
  ml3_state_t state,
  size_t conversion_index,
  size_t retained_index,
  uint16_t channel,
  bool discard) {
  test_clear_overrun_injection();
  test_state.inject_overrun_state = state;
  test_state.inject_overrun_conversion_index = conversion_index;
  test_state.inject_overrun_retained_index = retained_index;
  test_state.inject_overrun_channel = channel;
  test_state.inject_overrun_target_discard = discard;
}

static void test_set_timeout_injection(
  ml3_state_t timeout_state,
  size_t timeout_index) {
  test_state.inject_timeout_state = timeout_state;
  test_state.inject_timeout_index = timeout_state == ML3_STATE_IDLE ? SIZE_MAX : timeout_index;
  test_state.inject_timeout_seen = false;
  test_state.inject_timeout_hit_event_index = SIZE_MAX;
  test_state.inject_timeout_hit_state = ML3_STATE_IDLE;
  test_state.inject_timeout_hit_index = 0U;
  test_state.inject_timeout_hit_retained_index = 0U;
  test_state.inject_timeout_hit_channel = UINT16_MAX;
  test_state.inject_timeout_hit_discard = false;
}

static void test_init_ctx(
  ml3_measurement_ctx_t* ctx,
  adc_precision_context_t* adc_ctx,
  const ml3_measurement_config_t* config,
  const adc_precision_timeouts_t* timeouts) {
  ml3_measurement_error_t err = ML3_MEASUREMENT_OK;
  test_state.meas_ctx = ctx;
  test_state.entered_state_count = 0U;
  err = ml3_measurement_init(
    ctx,
    config,
    &task3_measurement_port,
    adc_ctx,
    timeouts);
  EXPECT_ERR(ML3_MEASUREMENT_OK, err, "measurement init");
}

static void test_prepare_measurement(
  ml3_measurement_ctx_t* ctx,
  adc_precision_context_t* adc_ctx,
  const ml3_measurement_config_t* config,
  const adc_precision_timeouts_t* timeouts,
  const uint16_t* samples,
  size_t sample_count) {
  test_reset_measurement_state();
  test_prepare_adc_ctx(adc_ctx);
  test_set_samples(samples, NULL, sample_count);
  test_init_ctx(ctx, adc_ctx, config, timeouts);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_OK, ml3_measurement_start(ctx), "measurement start");
}

static void test_record_entered_state_once(ml3_state_t state) {
  test_record_entered_state(state);
}

static size_t test_event_count_for_kind(uint16_t kind) {
  size_t count = 0U;
  for (size_t i = 0U; i < test_state.event_count; ++i) {
    if (test_state.events[i].kind == kind) {
      ++count;
    }
  }
  return count;
}

static size_t test_event_count_for_kind_state(
  uint16_t kind,
  ml3_state_t state) {
  size_t count = 0U;
  for (size_t i = 0U; i < test_state.event_count; ++i) {
    if ((test_state.events[i].kind == kind) && (test_state.events[i].state == state)) {
      ++count;
    }
  }
  return count;
}

static size_t test_event_count_for_kind_state_arg(
  uint16_t kind,
  ml3_state_t state,
  uint16_t arg) {
  size_t count = 0U;
  for (size_t i = 0U; i < test_state.event_count; ++i) {
    if ((test_state.events[i].kind == kind) &&
        (test_state.events[i].state == state) &&
        (test_state.events[i].arg == arg)) {
      ++count;
    }
  }
  return count;
}

static size_t test_event_nth_index_for_kind_state(
  uint16_t kind,
  ml3_state_t state,
  size_t nth) {
  size_t i = 0U;
  size_t count = 0U;
  for (i = 0U; i < test_state.event_count; ++i) {
    if ((test_state.events[i].kind == kind) && (test_state.events[i].state == state)) {
      ++count;
      if (count == nth) {
        return i;
      }
    }
  }
  return (size_t)-1;
}

static size_t test_event_nth_index_for_kind_state_arg(
  uint16_t kind,
  ml3_state_t state,
  uint16_t arg,
  size_t nth) {
  size_t i = 0U;
  size_t count = 0U;
  for (i = 0U; i < test_state.event_count; ++i) {
    if ((test_state.events[i].kind == kind) &&
        (test_state.events[i].state == state) &&
        (test_state.events[i].arg == arg)) {
      ++count;
      if (count == nth) {
        return i;
      }
    }
  }
  return (size_t)-1;
}

static size_t test_callback_failure_event_index(
  uint16_t kind,
  size_t call_index) {
  for (size_t i = 0U; i < test_state.callback_failure_count; ++i) {
    if ((test_state.callback_failures[i].kind == kind) &&
        (test_state.callback_failures[i].call_index == call_index)) {
      return test_state.callback_failures[i].event_index;
    }
  }
  return SIZE_MAX;
}

static void test_expect_callback_failure_contract(
  const ml3_measurement_ctx_t* ctx,
  const char* error_label,
  size_t expected_power_false_calls,
  size_t expected_therm_false_calls) {
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_CALLBACK,
    (uint32_t)ml3_measurement_last_error(ctx),
    error_label);
  EXPECT_TRUE(test_state.set_power_false_calls >= expected_power_false_calls,
    "callback failure cleanup attempts power low");
  EXPECT_TRUE(test_state.set_therm_false_calls >= expected_therm_false_calls,
    "callback failure cleanup attempts therm low");
}

static void test_expect_controls_low_before_event(
  size_t callback_event_index,
  const char* label) {
  bool seen_power = false;
  bool seen_power_high = false;
  bool seen_therm = false;
  bool seen_therm_high = false;
  bool power_low_after_last_high = false;
  bool therm_low_after_last_high = false;

  if (callback_event_index == ((size_t)-1U)) {
    EXPECT_TRUE(false, label);
    return;
  }

  for (size_t i = 0U; i < callback_event_index; ++i) {
    if (test_state.events[i].kind == TEST_EVENT_SET_POWER) {
      seen_power = true;
      if (test_state.events[i].arg == 1U) {
        seen_power_high = true;
        power_low_after_last_high = false;
      } else {
        if (seen_power_high) {
          power_low_after_last_high = true;
        }
      }
    } else if (test_state.events[i].kind == TEST_EVENT_SET_THERM) {
      seen_therm = true;
      if (test_state.events[i].arg == 1U) {
        seen_therm_high = true;
        therm_low_after_last_high = false;
      } else {
        if (seen_therm_high) {
          therm_low_after_last_high = true;
        }
      }
    }
  }

  EXPECT_TRUE(seen_power, "fault cleanup path attempted power off");
  EXPECT_TRUE(seen_therm, "fault cleanup path attempted therm off");
  if (seen_power_high) {
    EXPECT_TRUE(power_low_after_last_high, "fault cleanup drives power low before report");
  }
  if (seen_therm_high) {
    EXPECT_TRUE(therm_low_after_last_high, "fault cleanup drives therm low before report");
  }
}

static void test_expect_low_callbacks_after_event(
  size_t callback_event_index,
  const char* label) {
  bool saw_power_low_after = false;
  bool saw_therm_low_after = false;

  if (callback_event_index == ((size_t)-1U)) {
    EXPECT_TRUE(false, label);
    return;
  }

  for (size_t i = callback_event_index + 1U; i < test_state.event_count; ++i) {
    if ((test_state.events[i].kind == TEST_EVENT_SET_POWER) &&
        (test_state.events[i].arg == 0U)) {
      saw_power_low_after = true;
    }
    if ((test_state.events[i].kind == TEST_EVENT_SET_THERM) &&
        (test_state.events[i].arg == 0U)) {
      saw_therm_low_after = true;
    }
  }

  EXPECT_TRUE(saw_power_low_after, "callback failure triggers power-low attempt after failure");
  EXPECT_TRUE(saw_therm_low_after, "callback failure triggers therm-low attempt after failure");
}

static void test_expect_applied_low_callbacks_after_event(
  size_t event_index,
  const char* label) {
  bool saw_applied_power_low = false;
  bool saw_applied_therm_low = false;

  if ((event_index == SIZE_MAX) ||
      (event_index >= test_state.event_count)) {
    EXPECT_TRUE(false, label);
    return;
  }

  for (size_t i = event_index + 1U; i < test_state.event_count; ++i) {
    if ((test_state.events[i].kind == TEST_EVENT_SET_POWER) &&
        (test_state.events[i].arg == 0U) &&
        test_state.events[i].applied) {
      saw_applied_power_low = true;
    }
    if ((test_state.events[i].kind == TEST_EVENT_SET_THERM) &&
        (test_state.events[i].arg == 0U) &&
        test_state.events[i].applied) {
      saw_applied_therm_low = true;
    }
  }

  EXPECT_TRUE(saw_applied_power_low, "ADC failure causally applies power low");
  EXPECT_TRUE(saw_applied_therm_low, "ADC failure causally applies thermistor low");
}

static void test_expect_invalid_measurement_result(
  const ml3_measurement_result_t* result,
  const char* label) {
  EXPECT_TRUE(result != NULL, label);
  EXPECT_TRUE(result->has_reset_cause, "invalid report retains reset-cause presence");
  EXPECT_TRUE(result->has_sequence, "invalid report retains sequence presence");
  EXPECT_TRUE(result->has_faults, "invalid report retains fault presence");
  EXPECT_FALSE(result->has_pre_reference_raw, "invalid report clears pre reference");
  EXPECT_FALSE(result->has_post_reference_raw, "invalid report clears post reference");
  EXPECT_FALSE(result->has_pre_v5_raw, "invalid report clears pre pa4");
  EXPECT_FALSE(result->has_post_v5_raw, "invalid report clears post pa4");
  EXPECT_FALSE(result->has_die_temp_raw, "invalid report clears die temperature");
  EXPECT_FALSE(result->has_thermistor_raw, "invalid report clears thermistor");
  EXPECT_FALSE(result->has_vdda_pre_uv, "invalid report clears pre vdda");
  EXPECT_FALSE(result->has_vdda_post_uv, "invalid report clears post vdda");
  EXPECT_FALSE(result->has_mean_hi_uv, "invalid report clears mean hi");
  EXPECT_FALSE(result->has_mean_lo_uv, "invalid report clears mean lo");
  EXPECT_FALSE(result->has_common_mode_uv, "invalid report clears common mode");
  EXPECT_FALSE(result->has_mean_diff_uv, "invalid report clears mean differential");
  EXPECT_FALSE(result->has_median_diff_uv, "invalid report clears median differential");
  EXPECT_FALSE(result->has_drift_uv, "invalid report clears drift");
  EXPECT_FALSE(result->has_sd_uv, "invalid report clears standard deviation");
  EXPECT_FALSE(result->has_mad_uv, "invalid report clears MAD");
  EXPECT_FALSE(result->has_min_diff_uv, "invalid report clears minimum differential");
  EXPECT_FALSE(result->has_max_diff_uv, "invalid report clears maximum differential");
  EXPECT_FALSE(result->has_abba_raw, "invalid report clears ABBA raw presence");
  EXPECT_U32(0U, result->abba_raw_cycle_count, "invalid report clears ABBA raw cycle count");
  for (size_t i = 0U; i < ML3_MEASUREMENT_MAX_ABBA_CYCLES; ++i) {
    EXPECT_U32(0U, result->abba_h1_raw[i], "invalid report clears H1 raw evidence");
    EXPECT_U32(0U, result->abba_l1_raw[i], "invalid report clears L1 raw evidence");
    EXPECT_U32(0U, result->abba_l2_raw[i], "invalid report clears L2 raw evidence");
    EXPECT_U32(0U, result->abba_h2_raw[i], "invalid report clears H2 raw evidence");
  }
  EXPECT_TRUE(result->has_valid_cycle_count, "invalid report retains valid-cycle presence");
  EXPECT_U32(0U, result->valid_cycle_count, "invalid report sets valid-cycle count to zero");
}

static ml3_measurement_step_t test_run_to_completion(
  ml3_measurement_ctx_t* ctx,
  size_t max_steps) {
  ml3_measurement_step_t step = ML3_MEASUREMENT_STEP_BUSY;
  size_t i = 0U;
  test_state.total_steps = 0U;
  test_state.entered_state_count = 0U;
  if (ctx != NULL) {
    test_record_entered_state_once(ctx->state);
    test_state.last_state = ctx->state;
  }

  while ((i < max_steps) && (step == ML3_MEASUREMENT_STEP_BUSY)) {
    step = ml3_measurement_step(ctx);
    ++test_state.total_steps;
    test_state.last_state = ml3_measurement_state(ctx);
    test_record_entered_state_once(test_state.last_state);
    ++i;
  }
  return step;
}

static void test_expect_state_exact(const ml3_state_t* expected, size_t expected_count) {
  EXPECT_U32((uint32_t)expected_count, (uint32_t)test_state.entered_state_count, "state count");
  for (size_t i = 0U; i < expected_count; ++i) {
    EXPECT(expected[i] == test_state.entered_states[i], "state sequence");
  }
}

static void test_expect_selected_channels(
  const uint16_t* expected,
  size_t expected_count) {
  EXPECT_U32((uint32_t)expected_count, (uint32_t)test_state.selected_channel_count, "selected count");
  for (size_t i = 0U; i < expected_count; ++i) {
    EXPECT_U32(expected[i], test_state.selected_channels[i], "selected channel sequence");
  }
}

static size_t test_count_value_in_selected(uint16_t value) {
  size_t count = 0U;
  for (size_t i = 0U; i < test_state.selected_channel_count; ++i) {
    if (test_state.selected_channels[i] == value) {
      ++count;
    }
  }
  return count;
}

static void test_measurement_init_and_bounds(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_timeouts_t timeouts;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(4U);
  ml3_measurement_port_t invalid_port = task3_measurement_port;
  test_prepare_adc_ctx(&adc_ctx);
  test_reset_timeouts(&timeouts);

  EXPECT_ERR(ML3_MEASUREMENT_ERR_INVALID_ARGUMENT,
    ml3_measurement_init(NULL, &config, &task3_measurement_port, &adc_ctx, &timeouts),
    "null ctx");
  EXPECT_ERR(ML3_MEASUREMENT_ERR_INVALID_ARGUMENT,
    ml3_measurement_init(&ctx, NULL, &task3_measurement_port, &adc_ctx, &timeouts),
    "null config");
  EXPECT_ERR(ML3_MEASUREMENT_ERR_INVALID_ARGUMENT,
    ml3_measurement_init(&ctx, &config, NULL, &adc_ctx, &timeouts),
    "null port");
  EXPECT_ERR(ML3_MEASUREMENT_ERR_INVALID_ARGUMENT,
    ml3_measurement_init(&ctx, &config, &task3_measurement_port, NULL, &timeouts),
    "null adc ctx");
  EXPECT_ERR(ML3_MEASUREMENT_ERR_INVALID_ARGUMENT,
    ml3_measurement_init(&ctx, &config, &task3_measurement_port, &adc_ctx, NULL),
    "null timeouts");

  invalid_port.on_process = NULL;
  EXPECT_ERR(ML3_MEASUREMENT_ERR_INVALID_ARGUMENT,
    ml3_measurement_init(&ctx, &config, &invalid_port, &adc_ctx, &timeouts),
    "null process callback");
  invalid_port = task3_measurement_port;
  invalid_port.on_build_payload = NULL;
  EXPECT_ERR(ML3_MEASUREMENT_ERR_INVALID_ARGUMENT,
    ml3_measurement_init(&ctx, &config, &invalid_port, &adc_ctx, &timeouts),
    "null build callback");
  invalid_port = task3_measurement_port;
  invalid_port.on_queue = NULL;
  EXPECT_ERR(ML3_MEASUREMENT_ERR_INVALID_ARGUMENT,
    ml3_measurement_init(&ctx, &config, &invalid_port, &adc_ctx, &timeouts),
    "null queue callback");
  invalid_port = task3_measurement_port;
  invalid_port.read_reset_cause = NULL;
  EXPECT_ERR(ML3_MEASUREMENT_ERR_INVALID_ARGUMENT,
    ml3_measurement_init(&ctx, &config, &invalid_port, &adc_ctx, &timeouts),
    "null reset-cause callback");

  config.warmup_ms = 499U;
  EXPECT_ERR(ML3_MEASUREMENT_ERR_INVALID_ARGUMENT,
    ml3_measurement_init(&ctx, &config, &task3_measurement_port, &adc_ctx, &timeouts),
    "warmup below 500 ms");
  config.warmup_ms = 500U;
  EXPECT_ERR(ML3_MEASUREMENT_OK,
    ml3_measurement_init(&ctx, &config, &task3_measurement_port, &adc_ctx, &timeouts),
    "warmup accepts 500 ms");
  config.warmup_ms = 3000U;
  EXPECT_ERR(ML3_MEASUREMENT_OK,
    ml3_measurement_init(&ctx, &config, &task3_measurement_port, &adc_ctx, &timeouts),
    "warmup accepts 3000 ms");
  config.warmup_ms = 3001U;
  EXPECT_ERR(ML3_MEASUREMENT_ERR_INVALID_ARGUMENT,
    ml3_measurement_init(&ctx, &config, &task3_measurement_port, &adc_ctx, &timeouts),
    "warmup above 3000 ms");
  config.warmup_ms = 500U;

  config.discharge_threshold_mv = 0U;
  EXPECT_ERR(ML3_MEASUREMENT_ERR_INVALID_ARGUMENT,
    ml3_measurement_init(&ctx, &config, &task3_measurement_port, &adc_ctx, &timeouts),
    "zero discharge threshold");
  config.discharge_threshold_mv = 500U;

  config.therm_settle_ms = 0U;
  EXPECT_ERR(ML3_MEASUREMENT_ERR_INVALID_ARGUMENT,
    ml3_measurement_init(&ctx, &config, &task3_measurement_port, &adc_ctx, &timeouts),
    "zero therm settle");
  config.therm_settle_ms = 2U;

  config.abba_cycles = 1U;
  EXPECT_ERR(ML3_MEASUREMENT_ERR_INVALID_ARGUMENT,
    ml3_measurement_init(&ctx, &config, &task3_measurement_port, &adc_ctx, &timeouts),
    "abba below 3");
  config.abba_cycles = 2U;
  EXPECT_ERR(ML3_MEASUREMENT_ERR_INVALID_ARGUMENT,
    ml3_measurement_init(&ctx, &config, &task3_measurement_port, &adc_ctx, &timeouts),
    "abba at former floor now rejected (min raised to 3; a valid reading "
    "needs ML3_MEASUREMENT_FIXED_MIN_VALID_CYCLES=3)");
  config.abba_cycles = 3U;
  EXPECT_ERR(ML3_MEASUREMENT_OK,
    ml3_measurement_init(&ctx, &config, &task3_measurement_port, &adc_ctx, &timeouts),
    "abba accepts new floor of 3");
  config.abba_cycles = 8U;
  EXPECT_ERR(ML3_MEASUREMENT_OK,
    ml3_measurement_init(&ctx, &config, &task3_measurement_port, &adc_ctx, &timeouts),
    "abba accepts upper bound of 8");
  config.abba_cycles = 9U;
  EXPECT_ERR(ML3_MEASUREMENT_ERR_INVALID_ARGUMENT,
    ml3_measurement_init(&ctx, &config, &task3_measurement_port, &adc_ctx, &timeouts),
    "abba above 8");
}

static void test_warmup_transitions_only_at_deadline(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[64U];
  size_t sample_count = 0U;
  uint32_t warmup_start = 0U;

  config.warmup_ms = 3000U;
  test_reset_timeouts(&timeouts);
  sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    8000U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_BUSY,
    (uint32_t)ml3_measurement_step(&ctx),
    "warmup timing reaches power-on state");
  test_state.auto_advance_warmup = false;
  test_set_now_step(0U);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_BUSY,
    (uint32_t)ml3_measurement_step(&ctx),
    "warmup timing applies power before waiting");
  EXPECT_U32((uint32_t)ML3_STATE_WARMUP, (uint32_t)ctx.state, "warmup timing enters warmup");
  warmup_start = ctx.state_enter_ms;

  test_set_now_ms(warmup_start + 2999U);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_BUSY,
    (uint32_t)ml3_measurement_step(&ctx),
    "warmup remains busy one millisecond before deadline");
  EXPECT_U32((uint32_t)ML3_STATE_WARMUP,
    (uint32_t)ctx.state,
    "warmup remains in state one millisecond before deadline");

  test_set_now_ms(warmup_start + 3000U);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_BUSY,
    (uint32_t)ml3_measurement_step(&ctx),
    "warmup deadline transition remains nonblocking");
  EXPECT_U32((uint32_t)ML3_STATE_ADC_CONFIGURE,
    (uint32_t)ctx.state,
    "warmup transitions at exact deadline");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_BUSY,
    (uint32_t)ml3_measurement_step(&ctx),
    "wrap warmup reaches power-on state");
  test_state.auto_advance_warmup = false;
  test_set_now_step(0U);
  test_set_now_ms(UINT32_MAX - 1000U);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_BUSY,
    (uint32_t)ml3_measurement_step(&ctx),
    "wrap warmup applies power before waiting");
  EXPECT_U32((uint32_t)ML3_STATE_WARMUP, (uint32_t)ctx.state, "wrap warmup enters warmup");
  warmup_start = ctx.state_enter_ms;

  test_set_now_ms(warmup_start + 2999U);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_BUSY,
    (uint32_t)ml3_measurement_step(&ctx),
    "wrap warmup remains busy before deadline");
  EXPECT_U32((uint32_t)ML3_STATE_WARMUP,
    (uint32_t)ctx.state,
    "wrap warmup remains in state before deadline");
  test_set_now_ms(warmup_start + 3000U);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_BUSY,
    (uint32_t)ml3_measurement_step(&ctx),
    "wrap warmup deadline transition remains nonblocking");
  EXPECT_U32((uint32_t)ML3_STATE_ADC_CONFIGURE,
    (uint32_t)ctx.state,
    "wrap warmup transitions at exact deadline");
}

static void test_corrupted_report_callbacks_fail_safe(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[64U];
  const ml3_state_t states[] = {
    ML3_STATE_PROCESS,
    ML3_STATE_BUILD_PAYLOAD,
    ML3_STATE_QUEUE_TX
  };
  size_t sample_count = 0U;

  test_reset_timeouts(&timeouts);
  sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    8000U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);

  for (size_t i = 0U; i < sizeof(states) / sizeof(states[0U]); ++i) {
    size_t event_start = 0U;
    test_prepare_measurement(
      &ctx,
      &adc_ctx,
      &config,
      &timeouts,
      samples,
      sample_count);
    ctx.state = states[i];
    ctx.active = true;
    if (states[i] == ML3_STATE_PROCESS) {
      ctx.port.on_process = NULL;
    } else if (states[i] == ML3_STATE_BUILD_PAYLOAD) {
      ctx.port.on_build_payload = NULL;
    } else {
      ctx.port.on_queue = NULL;
    }
    test_state.last_power_state = true;
    test_state.last_therm_state = true;
    event_start = test_state.event_count;

    EXPECT_U32(
      (uint32_t)ML3_MEASUREMENT_STEP_ERROR,
      (uint32_t)ml3_measurement_step(&ctx),
      "corrupted report callback fails immediately");
    EXPECT_U32(
      (uint32_t)ML3_STATE_ERROR,
      (uint32_t)ml3_measurement_state(&ctx),
      "corrupted report callback enters error state");
    EXPECT_U32(
      (uint32_t)ML3_MEASUREMENT_ERR_CALLBACK,
      (uint32_t)ml3_measurement_last_error(&ctx),
      "corrupted report callback reports callback error");
    EXPECT_U32(0U, test_state.process_calls, "corrupted report callback skips process hook");
    EXPECT_U32(0U, test_state.build_calls, "corrupted report callback skips build hook");
    EXPECT_U32(0U, test_state.queue_calls, "corrupted report callback skips queue hook");
    EXPECT_U32(
      TEST_EVENT_SET_POWER,
      test_state.events[event_start].kind,
      "corrupted report callback cleanup requests power low first");
    EXPECT_U32(0U, test_state.events[event_start].arg, "corrupted report callback requests power low");
    EXPECT_U32(
      TEST_EVENT_SET_THERM,
      test_state.events[event_start + 1U].kind,
      "corrupted report callback cleanup requests thermistor low second");
    EXPECT_U32(0U, test_state.events[event_start + 1U].arg, "corrupted report callback requests thermistor low");
    EXPECT_TRUE(test_state.events[event_start].applied, "corrupted report callback applies power low");
    EXPECT_TRUE(test_state.events[event_start + 1U].applied, "corrupted report callback applies thermistor low");
    EXPECT_FALSE(test_state.last_power_state, "corrupted report callback leaves power low");
    EXPECT_FALSE(test_state.last_therm_state, "corrupted report callback leaves thermistor low");
  }
}

static void test_reset_cause_capture_order_and_replacement(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(4U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[128U];
  size_t sample_count = 0U;
  size_t reset_event = SIZE_MAX;
  size_t radio_event = SIZE_MAX;

  test_reset_timeouts(&timeouts);
  sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    8000U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);

  test_reset_measurement_state();
  test_state.reset_cause_value = 0x89ABCDEFU;
  test_prepare_adc_ctx(&adc_ctx);
  test_set_samples(samples, NULL, sample_count);
  test_init_ctx(&ctx, &adc_ctx, &config, &timeouts);
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_OK,
    (uint32_t)ml3_measurement_start(&ctx),
    "reset-cause first acquisition starts");
  EXPECT_U32(1U, test_state.reset_cause_calls, "reset cause captured once at first start");
  EXPECT_U32(0U, test_state.sequence_at_reset_cause, "reset cause captured before first sequence increment");
  EXPECT_TRUE(ctx.last_result.has_reset_cause, "first reset cause presence set at start");
  EXPECT_U32(0x89ABCDEFU, ctx.last_result.reset_cause, "first reset cause stored at start");
  EXPECT_U32(1U, ctx.last_result.sequence, "first sequence increments after reset cause");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_STEP_DONE,
    (uint32_t)test_run_to_completion(&ctx, 800U),
    "reset-cause first acquisition completes");
  reset_event = test_event_nth_index_for_kind_state(
    TEST_EVENT_RESET_CAUSE,
    ML3_STATE_IDLE,
    1U);
  radio_event = test_event_nth_index_for_kind_state(
    TEST_EVENT_RADIO_SLEEP,
    ML3_STATE_PREPARE,
    1U);
  EXPECT_TRUE(reset_event != SIZE_MAX, "reset-cause event recorded");
  EXPECT_TRUE(radio_event != SIZE_MAX, "radio-sleep event recorded");
  EXPECT_TRUE(reset_event < radio_event, "reset cause captured before radio-sleep preparation");
  EXPECT_TRUE(test_state.process_result_seen, "process receives reset cause");
  EXPECT_TRUE(test_state.build_result_seen, "build receives reset cause");
  EXPECT_TRUE(test_state.queued_result_seen, "queue receives reset cause");
  EXPECT_TRUE(test_state.process_result.has_reset_cause, "process reset-cause presence survives");
  EXPECT_TRUE(test_state.build_result.has_reset_cause, "build reset-cause presence survives");
  EXPECT_TRUE(test_state.queued_result.has_reset_cause, "queue reset-cause presence survives");
  EXPECT_U32(0x89ABCDEFU, test_state.process_result.reset_cause, "process receives exact reset cause");
  EXPECT_U32(0x89ABCDEFU, test_state.build_result.reset_cause, "build receives exact reset cause");
  EXPECT_U32(0x89ABCDEFU, test_state.queued_result.reset_cause, "queue receives exact reset cause");

  test_set_samples(samples, NULL, sample_count);
  test_state.reset_cause_value = 0U;
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_OK,
    (uint32_t)ml3_measurement_start(&ctx),
    "reset-cause second acquisition starts");
  EXPECT_U32(2U, test_state.reset_cause_calls, "reset cause captured once per acquisition");
  EXPECT_U32(1U, test_state.sequence_at_reset_cause, "second reset cause captured before sequence increment");
  EXPECT_TRUE(ctx.last_result.has_reset_cause, "opaque zero reset cause remains present");
  EXPECT_U32(0U, ctx.last_result.reset_cause, "second reset cause replaces first with valid zero");
  EXPECT_U32(2U, ctx.last_result.sequence, "second sequence increments after reset cause");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_STEP_DONE,
    (uint32_t)test_run_to_completion(&ctx, 800U),
    "reset-cause second acquisition completes");
  EXPECT_TRUE(test_state.queued_result.has_reset_cause, "queued second reset cause remains present");
  EXPECT_U32(0U, test_state.queued_result.reset_cause, "queued second reset cause replaces prior value");
}

static void test_adc_invalid_report_retains_exact_reset_cause(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[64U];
  size_t sample_count = 0U;
  const uint32_t reset_cause = 0xC35A91E7U;

  test_reset_timeouts(&timeouts);
  sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    8000U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);

  test_reset_measurement_state();
  test_state.reset_cause_value = reset_cause;
  test_prepare_adc_ctx(&adc_ctx);
  test_set_samples(samples, NULL, sample_count);
  test_init_ctx(&ctx, &adc_ctx, &config, &timeouts);
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_OK,
    (uint32_t)ml3_measurement_start(&ctx),
    "reset-cause invalid acquisition starts");
  test_set_timeout_injection(ML3_STATE_IDLE, SIZE_MAX);
  test_set_overrun_injection(
    ML3_STATE_SAMPLE_ABBA,
    7U,
    4U,
    config.channel_hi,
    false);

  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_STEP_DONE,
    (uint32_t)test_run_to_completion(&ctx, 800U),
    "reset-cause invalid acquisition reports");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_ERR_ADC_OVERRUN,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "reset-cause invalid acquisition retains ADC taxonomy");
  EXPECT_TRUE(test_state.inject_overrun_seen, "reset-cause invalid acquisition injects overrun");
  EXPECT_U32(1U, ctx.current_abba_cycle, "reset-cause invalidation follows one completed ABBA cycle");
  EXPECT_U32(1U, test_state.process_calls, "reset-cause invalid acquisition processes once");
  EXPECT_U32(1U, test_state.build_calls, "reset-cause invalid acquisition builds once");
  EXPECT_U32(1U, test_state.queue_calls, "reset-cause invalid acquisition queues once");
  EXPECT_TRUE(test_state.process_result_seen, "reset-cause invalid process snapshot exists");
  EXPECT_TRUE(test_state.build_result_seen, "reset-cause invalid build snapshot exists");
  EXPECT_TRUE(test_state.queued_result_seen, "reset-cause invalid queue snapshot exists");
  test_expect_invalid_measurement_result(
    &test_state.process_result,
    "reset-cause invalid process snapshot is fully invalidated");
  test_expect_invalid_measurement_result(
    &test_state.build_result,
    "reset-cause invalid build snapshot is fully invalidated");
  test_expect_invalid_measurement_result(
    &test_state.queued_result,
    "reset-cause invalid queue snapshot is fully invalidated");
  EXPECT_TRUE(test_state.process_result.has_reset_cause, "invalid process retains reset-cause presence");
  EXPECT_TRUE(test_state.build_result.has_reset_cause, "invalid build retains reset-cause presence");
  EXPECT_TRUE(test_state.queued_result.has_reset_cause, "invalid queue retains reset-cause presence");
  EXPECT_U32(reset_cause, test_state.process_result.reset_cause, "invalid process retains exact reset cause");
  EXPECT_U32(reset_cause, test_state.build_result.reset_cause, "invalid build retains exact reset cause");
  EXPECT_U32(reset_cause, test_state.queued_result.reset_cause, "invalid queue retains exact reset cause");
}

static void test_start_only_from_idle(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(4U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[64U];

  test_reset_timeouts(&timeouts);
  test_reset_measurement_state();
  test_prepare_adc_ctx(&adc_ctx);
  size_t sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5000U,
    23000U,
    4800U,
    500U,
    4200U,
    6100U,
    1000U,
    2000U,
    3000U,
    4000U,
    1U);
  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  EXPECT_ERR(ML3_MEASUREMENT_ERR_BUSY, ml3_measurement_start(&ctx), "start from active");

  test_run_to_completion(&ctx, 400U);
  EXPECT_U32((uint32_t)ML3_STATE_IDLE, (uint32_t)test_state.last_state, "finish once");
  EXPECT_U32((uint32_t)ML3_STATE_IDLE, (uint32_t)ml3_measurement_state(&ctx), "idle after done");
  EXPECT_ERR(ML3_MEASUREMENT_ERR_INVALID_ARGUMENT, ml3_measurement_start(NULL), "start null ctx");
  EXPECT_ERR(ML3_MEASUREMENT_OK, ml3_measurement_start(&ctx), "restart from idle");
}

static void test_error_and_unknown_states_force_safe_low(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[64U];
  size_t sample_count = 0U;
  size_t event_start = 0U;
  size_t power_attempts = 0U;
  size_t therm_attempts = 0U;
  const ml3_state_t corrupt_state = (ml3_state_t)99;

  test_reset_timeouts(&timeouts);
  sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    8000U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  ctx.state = ML3_STATE_ERROR;
  ctx.active = true;
  ctx.last_error = ML3_MEASUREMENT_ERR_CALLBACK;
  test_state.last_power_state = true;
  test_state.last_therm_state = true;
  event_start = test_state.event_count;
  power_attempts = test_state.set_power_false_calls;
  therm_attempts = test_state.set_therm_false_calls;

  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_ERROR,
    (uint32_t)ml3_measurement_step(&ctx),
    "error state reports error after cleanup attempt");
  EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ctx.state, "error state remains error");
  EXPECT_FALSE(ctx.active, "error state becomes inactive");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_CALLBACK,
    (uint32_t)ctx.last_error,
    "error state preserves originating error");
  EXPECT_U32((uint32_t)(power_attempts + 1U), (uint32_t)test_state.set_power_false_calls, "error state attempts power low");
  EXPECT_U32((uint32_t)(therm_attempts + 1U), (uint32_t)test_state.set_therm_false_calls, "error state attempts therm low");
  EXPECT_U32(TEST_EVENT_SET_POWER, test_state.events[event_start].kind, "error cleanup power request first");
  EXPECT_U32(0U, test_state.events[event_start].arg, "error cleanup requests power low");
  EXPECT_U32(TEST_EVENT_SET_THERM, test_state.events[event_start + 1U].kind, "error cleanup therm request second");
  EXPECT_U32(0U, test_state.events[event_start + 1U].arg, "error cleanup requests therm low");
  EXPECT_FALSE(test_state.last_power_state, "successful error cleanup applies power low");
  EXPECT_FALSE(test_state.last_therm_state, "successful error cleanup applies therm low");

  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_ERROR,
    (uint32_t)ml3_measurement_step(&ctx),
    "error cleanup retry returns without looping");
  EXPECT_U32((uint32_t)(power_attempts + 2U), (uint32_t)test_state.set_power_false_calls, "error retry attempts power low once");
  EXPECT_U32((uint32_t)(therm_attempts + 2U), (uint32_t)test_state.set_therm_false_calls, "error retry attempts therm low once");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  ctx.state = corrupt_state;
  ctx.active = true;
  ctx.last_error = ML3_MEASUREMENT_OK;
  test_state.last_power_state = true;
  test_state.last_therm_state = true;
  event_start = test_state.event_count;

  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_ERROR,
    (uint32_t)ml3_measurement_step(&ctx),
    "unknown state reports error");
  EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ctx.state, "unknown state enters error");
  EXPECT_FALSE(ctx.active, "unknown state becomes inactive");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_STATE,
    (uint32_t)ctx.last_error,
    "unknown state reports state error");
  EXPECT_U32(TEST_EVENT_SET_POWER, test_state.events[event_start].kind, "unknown cleanup power request first");
  EXPECT_U32(0U, test_state.events[event_start].arg, "unknown cleanup requests power low");
  EXPECT_U32((uint32_t)corrupt_state, (uint32_t)test_state.events[event_start].state, "unknown cleanup records corrupt state");
  EXPECT_U32(TEST_EVENT_SET_THERM, test_state.events[event_start + 1U].kind, "unknown cleanup therm request second");
  EXPECT_U32(0U, test_state.events[event_start + 1U].arg, "unknown cleanup requests therm low");
  EXPECT_U32(0U, (uint32_t)test_state.process_calls, "unknown state skips process");
  EXPECT_U32(0U, (uint32_t)test_state.build_calls, "unknown state skips build");
  EXPECT_U32(0U, (uint32_t)test_state.queue_calls, "unknown state skips queue");
  EXPECT_FALSE(test_state.last_power_state, "unknown cleanup applies power low");
  EXPECT_FALSE(test_state.last_therm_state, "unknown cleanup applies therm low");
}

static void test_nominal_sequence_and_controls(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(4U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[128U];

  test_reset_timeouts(&timeouts);
  size_t sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    1200U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);
  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);

  ml3_measurement_step_t step = test_run_to_completion(&ctx, 512U);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_DONE, (uint32_t)step, "nominal done");

  static const ml3_state_t expected_states[] = {
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
    ML3_STATE_IDLE
  };
  test_expect_state_exact(expected_states, sizeof(expected_states) / sizeof(expected_states[0U]));

  EXPECT_TRUE(ctx.last_result.has_sequence, "sequence present");
  EXPECT_TRUE(ctx.last_result.has_pre_reference_raw, "pre vref present");
  EXPECT_TRUE(ctx.last_result.has_post_reference_raw, "post vref present");
  EXPECT_TRUE(ctx.last_result.has_pre_v5_raw, "pre v5 present");
  EXPECT_TRUE(ctx.last_result.has_post_v5_raw, "post v5 present");
  EXPECT_TRUE(ctx.last_result.has_die_temp_raw, "die present");
  EXPECT_TRUE(ctx.last_result.has_thermistor_raw, "therm present");
  EXPECT_TRUE(ctx.last_result.has_vdda_pre_uv, "vdda pre present");
  EXPECT_TRUE(ctx.last_result.has_vdda_post_uv, "vdda post present");
  EXPECT_TRUE(test_state.queue_after_lows, "queue after lows");
  EXPECT_FALSE(test_state.power_therm_overlap, "no overlap");
}

static void test_internal_channels_are_fixed(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[64U];

  test_reset_timeouts(&timeouts);
  size_t sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    1200U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);
  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);

  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_DONE, (uint32_t)test_run_to_completion(&ctx, 512U), "fixed internal channels run");
  EXPECT_U32(6U, (uint32_t)test_count_value_in_selected(TEST_ML3_FIXED_VREF_CHANNEL), "vref uses fixed channel 17 for pre/post and discharge reads");
  EXPECT_U32(2U, (uint32_t)test_count_value_in_selected(TEST_ML3_FIXED_DIE_TEMP_CHANNEL), "die temp uses fixed channel 18");
  EXPECT_U32(0U, (uint32_t)test_count_value_in_selected(4U), "caller vref mapping ignored");
  EXPECT_U32(0U, (uint32_t)test_count_value_in_selected(6U), "caller die-temp mapping ignored");
}

static void test_channel_sequences_for_cycles(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  adc_precision_timeouts_t timeouts;
  uint16_t samples[256U];
  uint16_t expected[256U];
  uint16_t requested_channels[256U];
  const size_t cycle_values[] = {3U, 4U, 8U};

  test_reset_timeouts(&timeouts);
  for (size_t i = 0U; i < 3U; ++i) {
    ml3_measurement_config_t config = test_default_config((uint16_t)cycle_values[i]);
    size_t requested_count = 0U;
    size_t requested_index = 0U;

    size_t sample_count = test_fill_nominal_samples(
      samples,
      sizeof(samples) / sizeof(samples[0U]),
      config.abba_cycles,
      24000U,
      5400U,
      23200U,
      5200U,
      5000U,
      4200U,
      7000U,
      1000U,
      2000U,
      2500U,
      3000U,
      1U);
    test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
    EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_DONE, (uint32_t)test_run_to_completion(&ctx, 800U), "cycle run done");
    EXPECT_U32((uint32_t)ML3_MEASUREMENT_OK, (uint32_t)ml3_measurement_last_error(&ctx), "cycle no error");

    requested_channels[requested_index++] = ML3_MEASUREMENT_CHANNEL_VREFINT;
    requested_channels[requested_index++] = config.channel_v5;
    for (size_t cycle = 0U; cycle < config.abba_cycles; ++cycle) {
      requested_channels[requested_index++] = config.channel_hi;
      requested_channels[requested_index++] = config.channel_lo;
      requested_channels[requested_index++] = config.channel_lo;
      requested_channels[requested_index++] = config.channel_hi;
    }
    requested_channels[requested_index++] = ML3_MEASUREMENT_CHANNEL_VREFINT;
    requested_channels[requested_index++] = config.channel_v5;
    requested_channels[requested_index++] = ML3_MEASUREMENT_CHANNEL_DIE_TEMP;
    requested_channels[requested_index++] = ML3_MEASUREMENT_CHANNEL_VREFINT;
    requested_channels[requested_index++] = config.channel_v5;
    requested_channels[requested_index++] = config.channel_thermistor;

    requested_count = requested_index;

    size_t expected_count = test_make_expected_selected_physical(
      expected,
      sizeof(expected) / sizeof(expected[0U]),
      requested_channels,
      requested_count);
    test_expect_selected_channels(expected, expected_count);
    EXPECT_U32((uint32_t)expected_count,
      (uint32_t)test_state.selected_channel_count,
      "selected count");

    EXPECT_U32((uint32_t)config.channel_hi, (uint32_t)expected[4U], "pre->abba h1 sample");
    EXPECT_U32((uint32_t)config.channel_hi, (uint32_t)expected[5U], "pre->abba h1 retained");
    EXPECT_U32((uint32_t)config.channel_lo, (uint32_t)expected[6U], "abba l1 sample");
    EXPECT_U32((uint32_t)config.channel_lo, (uint32_t)expected[7U], "abba l1 retained");
    EXPECT_U32((uint32_t)config.channel_lo, (uint32_t)expected[8U], "abba l2 retained");
    EXPECT_U32((uint32_t)config.channel_hi, (uint32_t)expected[9U], "abba h2 sample");
    EXPECT_U32((uint32_t)config.channel_hi, (uint32_t)expected[10U], "abba h2 retained");

    if (config.abba_cycles > 1U) {
      size_t h1_second_cycle_index = 11U;
      if (h1_second_cycle_index < expected_count) {
        EXPECT_U32(
          (uint32_t)config.channel_hi,
          (uint32_t)expected[h1_second_cycle_index],
          "next cycle h1 retained");
      }
    }
  }
}

static void test_single_vref_pairing_and_order(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[64U];
  uint32_t expected_vdda_pre;
  uint32_t expected_vdda_post;

  test_reset_timeouts(&timeouts);
  size_t sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    28000U,
    5200U,
    31000U,
    4600U,
    5000U,
    1000U,
    7000U,
    1000U,
    1200U,
    1500U,
    2000U,
    1U);
  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_DONE, (uint32_t)test_run_to_completion(&ctx, 512U), "pairing done");

  EXPECT_U32(28000U, (uint32_t)ctx.last_result.pre_reference_raw, "pre raw paired");
  EXPECT_U32(31000U, (uint32_t)ctx.last_result.post_reference_raw, "post raw paired");

  EXPECT_U32(0U,
    (uint32_t)adc_precision_compute_vdda_uv(
      config.vrefint_calibration_word,
      ctx.last_result.pre_reference_raw,
      &expected_vdda_pre),
    "vdda pre compute ok");
  EXPECT_U32(expected_vdda_pre, ctx.last_result.vdda_pre_uv, "vdda pre exact");

  EXPECT_U32(0U,
    (uint32_t)adc_precision_compute_vdda_uv(
      config.vrefint_calibration_word,
      ctx.last_result.post_reference_raw,
      &expected_vdda_post),
    "vdda post compute ok");
  EXPECT_U32(expected_vdda_post, ctx.last_result.vdda_post_uv, "vdda post exact");

  EXPECT_U32((uint32_t)6U, (uint32_t)test_count_value_in_selected(ML3_MEASUREMENT_CHANNEL_VREFINT), "pre/post and discharge vref reads");
  EXPECT_U32((uint32_t)1U, (uint32_t)test_event_count_for_kind(TEST_EVENT_ADC_CONFIGURE), "configure called");
  EXPECT_U32((uint32_t)1U, (uint32_t)test_state.adc_self_cal_calls, "cal called");
}

static void test_watchdog_points(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(4U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[128U];

  test_reset_timeouts(&timeouts);
  size_t sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    4U,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    800U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);
  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_DONE, (uint32_t)test_run_to_completion(&ctx, 600U), "watchdog run done");
  EXPECT_U32(4U, test_event_count_for_kind(TEST_EVENT_WATCHDOG), "watchdog count");

  size_t first = test_event_nth_index_for_kind_state(
    TEST_EVENT_WATCHDOG,
    ML3_STATE_POWER_ON,
    1U);
  size_t second = test_event_nth_index_for_kind_state(
    TEST_EVENT_WATCHDOG,
    ML3_STATE_SAMPLE_ABBA,
    1U);
  size_t third = test_event_nth_index_for_kind_state(
    TEST_EVENT_WATCHDOG,
    ML3_STATE_SAMPLE_ABBA,
    2U);
  size_t fourth = test_event_nth_index_for_kind_state(
    TEST_EVENT_WATCHDOG,
    ML3_STATE_QUEUE_TX,
    1U);
  size_t power_high = test_event_nth_index_for_kind_state_arg(
    TEST_EVENT_SET_POWER,
    ML3_STATE_POWER_ON,
    1U,
    1U);
  size_t first_abba_conversion = test_event_nth_index_for_kind_state(
    TEST_EVENT_START_CONVERSION,
    ML3_STATE_SAMPLE_ABBA,
    1U);
  size_t abba_conversion_count = test_event_count_for_kind_state(
    TEST_EVENT_START_CONVERSION,
    ML3_STATE_SAMPLE_ABBA);
  size_t last_abba_conversion = test_event_nth_index_for_kind_state(
    TEST_EVENT_START_CONVERSION,
    ML3_STATE_SAMPLE_ABBA,
    abba_conversion_count);
  size_t abba_raw_consumed_count = test_event_count_for_kind_state(
    TEST_EVENT_RAW_CONSUMED,
    ML3_STATE_SAMPLE_ABBA);
  size_t last_abba_raw_consumed = test_event_nth_index_for_kind_state(
    TEST_EVENT_RAW_CONSUMED,
    ML3_STATE_SAMPLE_ABBA,
    abba_raw_consumed_count);
  size_t build = test_event_nth_index_for_kind_state(
    TEST_EVENT_BUILD,
    ML3_STATE_BUILD_PAYLOAD,
    1U);
  size_t queue = test_event_nth_index_for_kind_state(
    TEST_EVENT_QUEUE,
    ML3_STATE_QUEUE_TX,
    1U);
  EXPECT_U32(
    2U,
    (uint32_t)test_event_count_for_kind_state(
      TEST_EVENT_WATCHDOG,
      ML3_STATE_SAMPLE_ABBA),
    "watchdog count in ABBA");
  EXPECT_TRUE(first != (size_t)-1U, "watchdog in power-on");
  EXPECT_TRUE(second != (size_t)-1U, "watchdog in sample-abba start");
  EXPECT_TRUE(third != (size_t)-1U, "watchdog in sample-abba end");
  EXPECT_TRUE(fourth != (size_t)-1U, "watchdog in queue");
  EXPECT_TRUE(power_high != SIZE_MAX, "power-high callback event recorded");
  EXPECT_TRUE(first_abba_conversion != SIZE_MAX, "first physical ABBA conversion recorded");
  EXPECT_TRUE(last_abba_conversion != SIZE_MAX, "last physical ABBA conversion recorded");
  EXPECT_TRUE(last_abba_raw_consumed != SIZE_MAX, "final successful ABBA raw-consumption event recorded");
  EXPECT_TRUE(build != SIZE_MAX, "payload build event recorded");
  EXPECT_TRUE(queue != SIZE_MAX, "queue callback event recorded");
  EXPECT_TRUE(first < second, "watchdog order 1");
  EXPECT_TRUE(second < third, "watchdog order 2");
  EXPECT_TRUE(third < fourth, "watchdog order 3");
  EXPECT_TRUE(first < power_high, "first watchdog precedes power-high side effect");
  EXPECT_TRUE(second < first_abba_conversion, "second watchdog precedes first physical ABBA conversion");
  EXPECT_TRUE(last_abba_conversion < last_abba_raw_consumed, "final ABBA conversion start precedes raw consumption");
  EXPECT_TRUE(last_abba_raw_consumed < third, "third watchdog follows final successful ABBA raw consumption");
  EXPECT_TRUE(build < fourth, "fourth watchdog follows payload build");
  EXPECT_TRUE(fourth < queue, "fourth watchdog precedes queue callback");
}

static void test_wrap_and_discharge_timer_behaviour(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;

  config.warmup_ms = 500U;
  config.discharge_timeout_ms = 3U;
  test_reset_timeouts(&timeouts);

  uint16_t samples[64U];
  size_t sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    1200U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  test_set_now_ms(UINT32_MAX - 5U);
  test_set_now_step(1U);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_DONE,
    (uint32_t)test_run_to_completion(&ctx, 600U),
    "wrap warmup complete");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_OK, (uint32_t)ml3_measurement_last_error(&ctx), "wrap no error");

  uint16_t timeout_samples[80U];
  config.discharge_threshold_mv = 1U;
  size_t timeout_count = test_fill_nominal_samples(
    timeout_samples,
    sizeof(timeout_samples) / sizeof(timeout_samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    9000U,
    9200U,
    5000U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    10U);
  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, timeout_samples, timeout_count);
  test_set_now_ms(UINT32_MAX - 5U);
  test_set_now_step(1U);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_DONE,
    (uint32_t)test_run_to_completion(&ctx, 700U),
    "discharge timeout complete");
  EXPECT_FALSE(ctx.last_result.has_thermistor_raw, "discharge timeout skips therm");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_FAULT_THERM_FAULT, (uint32_t)ctx.last_result.fault_flags, "timeout path flags therm only");
  EXPECT_TRUE(ctx.last_result.has_pre_reference_raw, "timeout preserves pre");
  EXPECT_TRUE(ctx.last_result.has_post_reference_raw, "timeout preserves post");
  EXPECT_TRUE(ctx.last_result.has_pre_v5_raw, "timeout preserves pre v5");
  EXPECT_TRUE(ctx.last_result.has_post_v5_raw, "timeout preserves post v5");
  EXPECT_TRUE(ctx.last_result.has_die_temp_raw, "timeout preserves die");
}

static void test_reprepare_each_cycle(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[64U];

  test_reset_timeouts(&timeouts);
  size_t sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    1000U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_DONE, (uint32_t)test_run_to_completion(&ctx, 600U), "cycle one done");
  EXPECT_U32(1U, (uint32_t)test_state.adc_configure_calls, "configure once cycle one");
  EXPECT_U32(1U, (uint32_t)test_state.adc_self_cal_calls, "cal once cycle one");

  test_set_samples(samples, NULL, sample_count);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_OK, (uint32_t)ml3_measurement_start(&ctx), "cycle two start");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_DONE, (uint32_t)test_run_to_completion(&ctx, 600U), "cycle two done");
  EXPECT_U32(2U, (uint32_t)test_state.adc_configure_calls, "configure once cycle two");
  EXPECT_U32(2U, (uint32_t)test_state.adc_self_cal_calls, "cal once cycle two");
  EXPECT_U32(2U, (uint32_t)ctx.sequence, "sequence increments");
}

static void test_run_timeout_injection_with_expected_phase(
  ml3_measurement_ctx_t* ctx,
  adc_precision_context_t* adc_ctx,
  ml3_measurement_config_t* config,
  adc_precision_timeouts_t* timeouts,
  const uint16_t* samples,
  size_t sample_count,
  ml3_state_t state,
  size_t index,
  size_t retained_index,
  uint16_t expected_channel,
  bool expected_discard,
  const char* label) {
  const size_t saved_default_conversion_polls = test_state.default_conversion_polls;
  const size_t saved_timeout_conversion_polls = test_state.timeout_conversion_polls;

  test_prepare_measurement(
    ctx,
    adc_ctx,
    config,
    timeouts,
    samples,
    sample_count);
  if (saved_default_conversion_polls != 0U) {
    test_state.default_conversion_polls = saved_default_conversion_polls;
  }
  if (saved_timeout_conversion_polls != 0U) {
    test_state.timeout_conversion_polls = saved_timeout_conversion_polls;
  }
  test_clear_overrun_injection();
  test_set_timeout_injection(state, index);
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_STEP_DONE,
    (uint32_t)test_run_to_completion(ctx, 800U),
    label);
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_ERR_ADC_TIMEOUT,
    (uint32_t)ml3_measurement_last_error(ctx),
    label);
  EXPECT_TRUE(test_state.inject_timeout_seen, "timeout injection observed");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_FAULT_ADC_TIMEOUT,
    (uint32_t)ctx->last_result.fault_flags,
    label);
  EXPECT_U32((uint32_t)state, (uint32_t)test_state.inject_timeout_hit_state, "timeout observed in target state");
  EXPECT_U32((uint32_t)index, (uint32_t)test_state.inject_timeout_hit_index, "timeout observed at target conversion index");
  EXPECT_U32((uint32_t)retained_index, (uint32_t)test_state.inject_timeout_hit_retained_index, "timeout observed at target retained index");
  EXPECT_U32((uint32_t)expected_channel, (uint32_t)test_state.inject_timeout_hit_channel, "timeout observed on target channel");
  EXPECT_TRUE(expected_discard == test_state.inject_timeout_hit_discard, "timeout phase discarded or retained");
  EXPECT_TRUE(
    test_state.inject_timeout_hit_event_index < test_state.event_count,
    "timeout injection records an explicit causal event");
  EXPECT_U32(
    TEST_EVENT_ADC_TIMEOUT_INJECTION,
    test_state.events[test_state.inject_timeout_hit_event_index].kind,
    "timeout injection event has exact taxonomy");
  test_expect_low_callbacks_after_event(
    test_state.inject_timeout_hit_event_index,
    "timeout injection is followed by paired low requests");
  test_expect_applied_low_callbacks_after_event(
    test_state.inject_timeout_hit_event_index,
    "timeout injection is followed by applied paired lows");
  EXPECT_FALSE(test_state.last_power_state, "timeout cleanup leaves power low");
  EXPECT_FALSE(test_state.last_therm_state, "timeout cleanup leaves thermistor low");
  EXPECT_U32(1U, test_state.process_calls, "timeout path reaches process");
  EXPECT_U32(1U, test_state.build_calls, "timeout path reaches build");
  EXPECT_U32(1U, test_state.queue_calls, "timeout path reaches queue");
  EXPECT_TRUE(test_state.queued_result_seen, "timeout path snapshots one queued report");
  test_expect_invalid_measurement_result(&test_state.queued_result, label);
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_FAULT_ADC_TIMEOUT,
    (uint32_t)test_state.queued_result.fault_flags,
    "queued timeout report carries exact fault taxonomy");
}

static void test_run_overrun_injection_with_expected_phase(
  ml3_measurement_ctx_t* ctx,
  adc_precision_context_t* adc_ctx,
  ml3_measurement_config_t* config,
  adc_precision_timeouts_t* timeouts,
  const uint16_t* samples,
  size_t sample_count,
  ml3_state_t state,
  bool expected_discard,
  size_t expected_conversion_index,
  size_t expected_retained_index,
  uint16_t expected_channel,
  const char* label) {
  const size_t saved_default_conversion_polls = test_state.default_conversion_polls;
  const size_t saved_timeout_conversion_polls = test_state.timeout_conversion_polls;

  test_prepare_measurement(
    ctx,
    adc_ctx,
    config,
    timeouts,
    samples,
    sample_count);
  if (saved_default_conversion_polls != 0U) {
    test_state.default_conversion_polls = saved_default_conversion_polls;
  }
  if (saved_timeout_conversion_polls != 0U) {
    test_state.timeout_conversion_polls = saved_timeout_conversion_polls;
  }
  test_set_timeout_injection(ML3_STATE_IDLE, SIZE_MAX);
  test_set_overrun_injection(
    state,
    expected_conversion_index,
    expected_retained_index,
    expected_channel,
    expected_discard);
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_STEP_DONE,
    (uint32_t)test_run_to_completion(ctx, 800U),
    label);
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_ERR_ADC_OVERRUN,
    (uint32_t)ml3_measurement_last_error(ctx),
    label);
  EXPECT_TRUE(test_state.inject_overrun_seen, "overrun injection observed");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_FAULT_ADC_OVERRUN,
    (uint32_t)ctx->last_result.fault_flags,
    label);
  EXPECT_U32((uint32_t)state, (uint32_t)test_state.inject_overrun_hit_state, "overrun observed in target state");
  EXPECT_U32((uint32_t)expected_conversion_index,
    (uint32_t)test_state.inject_overrun_hit_conversion_index,
    "overrun observed at target physical conversion index");
  EXPECT_U32((uint32_t)expected_retained_index,
    (uint32_t)test_state.inject_overrun_hit_retained_index,
    "overrun observed at target retained ordinal");
  EXPECT_U32((uint32_t)expected_channel, (uint32_t)test_state.inject_overrun_hit_channel, "overrun observed on target channel");
  EXPECT_TRUE(expected_discard == test_state.inject_overrun_hit_discard, "overrun observed on expected retained/read discard semantics");
  EXPECT_FALSE(test_state.last_power_state, "overrun cleanup leaves power low");
  EXPECT_FALSE(test_state.last_therm_state, "overrun cleanup leaves therm low");
  EXPECT_TRUE(test_state.set_power_false_calls >= 1U, "overrun cleanup attempts power low");
  EXPECT_TRUE(test_state.set_therm_false_calls >= 1U, "overrun cleanup attempts therm low");
  EXPECT_U32(1U, test_state.process_calls, "overrun path reaches process");
  EXPECT_U32(1U, test_state.build_calls, "overrun path reaches build");
  EXPECT_U32(1U, test_state.queue_calls, "overrun path reaches queue");
  EXPECT_TRUE(test_state.queued_result_seen, "overrun path snapshots queued invalid report");
  test_expect_invalid_measurement_result(&test_state.queued_result, label);
}

static void test_adc_fault_indexing_matrix(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[256U];
  uint16_t post_discharge_samples[256U];
  size_t sample_count = 0U;
  size_t post_sample_count = 0U;

  test_reset_timeouts(&timeouts);
  test_state.default_conversion_polls = 1U;
  test_state.timeout_conversion_polls = 10U;

  sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    8000U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    4U);

  post_sample_count = test_fill_nominal_samples(
    post_discharge_samples,
    sizeof(post_discharge_samples) / sizeof(post_discharge_samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    7000U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    4U);

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_REFERENCE_PRE,
    0U,
    0U,
    ML3_MEASUREMENT_CHANNEL_VREFINT,
    true,
    "pre vref timeout on discard");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_REFERENCE_PRE,
    1U,
    0U,
    ML3_MEASUREMENT_CHANNEL_VREFINT,
    false,
    "pre vref timeout on retained");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_REFERENCE_PRE,
    2U,
    1U,
    config.channel_v5,
    true,
    "pre pa4 timeout on discard");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_REFERENCE_PRE,
    3U,
    1U,
    config.channel_v5,
    false,
    "pre pa4 timeout on retained");

  test_run_overrun_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_REFERENCE_PRE,
    false,
    1U,
    0U,
    ML3_MEASUREMENT_CHANNEL_VREFINT,
    "pre vref overrun on retained");

  test_run_overrun_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_REFERENCE_PRE,
    false,
    3U,
    1U,
    config.channel_v5,
    "pre pa4 overrun on retained");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_SAMPLE_ABBA,
    0U,
    0U,
    config.channel_hi,
    true,
    "abba h1 timeout on discard");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_SAMPLE_ABBA,
    1U,
    0U,
    config.channel_hi,
    false,
    "abba h1 timeout on retained");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_SAMPLE_ABBA,
    7U,
    4U,
    config.channel_hi,
    false,
    "abba h1 second-cycle timeout on retained");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_SAMPLE_ABBA,
    2U,
    1U,
    config.channel_lo,
    true,
    "abba l1 timeout on discard");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_SAMPLE_ABBA,
    3U,
    1U,
    config.channel_lo,
    false,
    "abba l1 timeout on retained");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_SAMPLE_ABBA,
    4U,
    2U,
    config.channel_lo,
    false,
    "abba l2 timeout on retained");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_SAMPLE_ABBA,
    5U,
    3U,
    config.channel_hi,
    true,
    "abba h2 timeout on discard");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_SAMPLE_ABBA,
    6U,
    3U,
    config.channel_hi,
    false,
    "abba h2 timeout on retained");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    post_discharge_samples,
    post_sample_count,
    ML3_STATE_REFERENCE_POST,
    0U,
    0U,
    ML3_MEASUREMENT_CHANNEL_VREFINT,
    true,
    "post vref timeout on discard");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    post_discharge_samples,
    post_sample_count,
    ML3_STATE_REFERENCE_POST,
    1U,
    0U,
    ML3_MEASUREMENT_CHANNEL_VREFINT,
    false,
    "post vref timeout on retained");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    post_discharge_samples,
    post_sample_count,
    ML3_STATE_REFERENCE_POST,
    2U,
    1U,
    config.channel_v5,
    true,
    "post pa4 timeout on discard");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    post_discharge_samples,
    post_sample_count,
    ML3_STATE_REFERENCE_POST,
    3U,
    1U,
    config.channel_v5,
    false,
    "post pa4 timeout on retained");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    post_discharge_samples,
    post_sample_count,
    ML3_STATE_REFERENCE_POST,
    4U,
    2U,
    ML3_MEASUREMENT_CHANNEL_DIE_TEMP,
    true,
    "post die timeout on discard");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    post_discharge_samples,
    post_sample_count,
    ML3_STATE_REFERENCE_POST,
    5U,
    2U,
    ML3_MEASUREMENT_CHANNEL_DIE_TEMP,
    false,
    "post die timeout on retained");

  test_run_overrun_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_SAMPLE_ABBA,
    false,
    1U,
    0U,
    config.channel_hi,
    "abba h1 overrun on retained");

  test_run_overrun_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_SAMPLE_ABBA,
    false,
    3U,
    1U,
    config.channel_lo,
    "abba l1 overrun on retained");

  test_run_overrun_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_SAMPLE_ABBA,
    false,
    4U,
    2U,
    config.channel_lo,
    "abba l2 overrun on retained");

  test_run_overrun_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_SAMPLE_ABBA,
    false,
    6U,
    3U,
    config.channel_hi,
    "abba h2 overrun on retained");

  test_run_overrun_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    post_discharge_samples,
    post_sample_count,
    ML3_STATE_REFERENCE_POST,
    false,
    1U,
    0U,
    ML3_MEASUREMENT_CHANNEL_VREFINT,
    "post vref retained overrun");

  test_run_overrun_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    post_discharge_samples,
    post_sample_count,
    ML3_STATE_REFERENCE_POST,
    false,
    3U,
    1U,
    config.channel_v5,
    "post pa4 retained overrun");

  test_run_overrun_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    post_discharge_samples,
    post_sample_count,
    ML3_STATE_REFERENCE_POST,
    false,
    5U,
    2U,
    ML3_MEASUREMENT_CHANNEL_DIE_TEMP,
    "post die retained overrun");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_VERIFY_DISCHARGE,
    0U,
    0U,
    ML3_MEASUREMENT_CHANNEL_VREFINT,
    true,
    "verify discharge timeout on discard");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_VERIFY_DISCHARGE,
    1U,
    0U,
    ML3_MEASUREMENT_CHANNEL_VREFINT,
    false,
    "verify discharge timeout on retained");

  test_run_overrun_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_VERIFY_DISCHARGE,
    false,
    1U,
    0U,
    ML3_MEASUREMENT_CHANNEL_VREFINT,
    "verify discharge overrun on retained");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_VERIFY_DISCHARGE,
    2U,
    1U,
    config.channel_v5,
    true,
    "verify discharge pa4 timeout on discard");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_VERIFY_DISCHARGE,
    3U,
    1U,
    config.channel_v5,
    false,
    "verify discharge pa4 timeout on retained");

  test_run_overrun_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_VERIFY_DISCHARGE,
    true,
    2U,
    1U,
    config.channel_v5,
    "verify discharge pa4 overrun on discard");

  test_run_overrun_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_VERIFY_DISCHARGE,
    false,
    3U,
    1U,
    config.channel_v5,
    "verify discharge pa4 overrun on retained");

  config.discharge_threshold_mv = 10000U;

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    post_discharge_samples,
    post_sample_count,
    ML3_STATE_SAMPLE_THERMISTOR,
    0U,
    0U,
    config.channel_thermistor,
    true,
    "therm timeout on discard");

  test_run_timeout_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    post_discharge_samples,
    post_sample_count,
    ML3_STATE_SAMPLE_THERMISTOR,
    1U,
    0U,
    config.channel_thermistor,
    false,
    "therm timeout on retained");

  test_run_overrun_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    post_discharge_samples,
    post_sample_count,
    ML3_STATE_SAMPLE_THERMISTOR,
    false,
    1U,
    0U,
    config.channel_thermistor,
    "therm overrun on retained");
}

static void test_verify_discharge_internal_read_failure_is_clean_continue(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[64U];
  size_t sample_count = 0U;
  config.discharge_threshold_mv = 10000U;

  test_reset_timeouts(&timeouts);
  sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    65535U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  test_clear_overrun_injection();
  test_set_timeout_injection(ML3_STATE_IDLE, SIZE_MAX);
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_STEP_DONE,
    (uint32_t)test_run_to_completion(&ctx, 700U),
    "verify discharge internal read conversion failure continues");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_ERR_ADC_INTERNAL,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "verify discharge internal read conversion mapped to internal init fault");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_FAULT_ADC_INIT,
    (uint32_t)ctx.last_result.fault_flags,
    "verify discharge internal read conversion maps to ADC_INIT");
  EXPECT_U32(1U, test_state.process_calls, "verify discharge internal read conversion reaches process");
  EXPECT_U32(1U, test_state.build_calls, "verify discharge internal read conversion reaches build");
  EXPECT_U32(1U, test_state.queue_calls, "verify discharge internal read conversion reaches queue");
  EXPECT_TRUE(test_state.set_power_false_calls >= 1U, "verify discharge internal read conversion attempts power off cleanup");
  EXPECT_TRUE(test_state.set_therm_false_calls >= 1U, "verify discharge internal read conversion attempts therm off cleanup");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_FAULT_ADC_INIT,
    (uint32_t)ctx.last_result.fault_flags,
    "verify discharge internal read conversion keeps init fault only");
  EXPECT_U32(1U, (uint32_t)ctx.last_result.has_sequence, "verify discharge internal read conversion keeps sequence");
}

static void test_verify_pa4_high_high_low_discharge_gating(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[64U];
  uint16_t discharge_vref_samples[] = {16000U, 16000U, 16000U};
  size_t sample_count = 0U;
  size_t discharge_start = 0U;
  size_t discharge_vref_select_count = 0U;
  size_t discharge_pa4_select_count = 0U;
  size_t therm_off_event = 0U;
  size_t first_therm_on_event = 0U;
  size_t power_off_event = 0U;
  size_t first_discharge_vref_event = 0U;
  size_t first_discharge_pa4_event = 0U;

  test_reset_timeouts(&timeouts);
  config.discharge_threshold_mv = 500U;
  config.discharge_timeout_ms = 24U;
  sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    1000U,
    23200U,
    5200U,
    5000U,
    7000U,
    900U,
    700U,
    1000U,
    2000U,
    2500U,
    3U);
  discharge_start = test_discharge_sample_start_index(config.abba_cycles);
  samples[discharge_start + 0U] = 7000U;
  samples[discharge_start + 1U] = 7000U;
  samples[discharge_start + 2U] = 1000U;

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  test_set_verify_discharge_vref_samples(
    discharge_vref_samples,
    sizeof(discharge_vref_samples) / sizeof(discharge_vref_samples[0U]));
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_STEP_DONE,
    (uint32_t)test_run_to_completion(&ctx, 800U),
    "pa4 high-high-low path reaches read completion");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_OK, (uint32_t)ml3_measurement_last_error(&ctx), "pa4 high-high-low has no fault");
  EXPECT_U32(1U, (uint32_t)ctx.last_result.has_sequence, "pa4 high-high-low preserves sequence");
  EXPECT_U32(0U, (uint32_t)ctx.last_result.fault_flags, "pa4 high-high-low has no fault flags");
  EXPECT_U32((uint32_t)config.abba_cycles, (uint32_t)ctx.last_result.valid_cycle_count, "pa4 high-high-low retains moisture cycle count");
  EXPECT_U32(1U, (uint32_t)ctx.last_result.has_thermistor_raw, "pa4 high-high-low reads thermistor after discharge low sample");

  first_therm_on_event = test_event_nth_index_for_kind_state_arg(
    TEST_EVENT_SET_THERM,
    ML3_STATE_SAMPLE_THERMISTOR,
    1U,
    1U);
  therm_off_event = test_event_nth_index_for_kind_state_arg(
    TEST_EVENT_SET_THERM,
    ML3_STATE_SAMPLE_THERMISTOR,
    0U,
    1U);
  EXPECT_TRUE(first_therm_on_event != (size_t)-1U, "pa4 high-high-low asserts therm after discharge loop");
  EXPECT_TRUE(therm_off_event != (size_t)-1U, "pa4 high-high-low deasserts therm after sample read");
  power_off_event = test_event_nth_index_for_kind_state(TEST_EVENT_SET_POWER, ML3_STATE_POWER_OFF, 1U);
  EXPECT_TRUE(power_off_event != (size_t)-1U, "pa4 high-high-low waits for power-off before therm");
  first_discharge_vref_event = test_event_nth_index_for_kind_state_arg(
    TEST_EVENT_SELECT_CHANNEL,
    ML3_STATE_VERIFY_DISCHARGE,
    ML3_MEASUREMENT_CHANNEL_VREFINT,
    1U);
  first_discharge_pa4_event = test_event_nth_index_for_kind_state_arg(
    TEST_EVENT_SELECT_CHANNEL,
    ML3_STATE_VERIFY_DISCHARGE,
    config.channel_v5,
    1U);
  EXPECT_TRUE(first_discharge_vref_event != (size_t)-1U, "pa4 high-high-low refreshes vref before pa4");
  EXPECT_TRUE(first_discharge_pa4_event != (size_t)-1U, "pa4 high-high-low samples pa4 after fresh vref");
  EXPECT_TRUE(first_discharge_vref_event > power_off_event, "pa4 high-high-low discharge vref after power off");
  EXPECT_TRUE(first_discharge_pa4_event > first_discharge_vref_event, "pa4 high-high-low pa4 after discharge vref");
  EXPECT_TRUE(first_therm_on_event > power_off_event, "pa4 high-high-low therm asserted after power off");
  EXPECT_TRUE(therm_off_event > first_therm_on_event, "pa4 high-high-low therm assert occurs before therm-off");
  EXPECT_U32(1U, test_state.set_therm_true_calls, "pa4 high-high-low asserts therm once");
  EXPECT_U32(2U, test_state.set_therm_false_calls, "pa4 high-high-low deasserts therm and resets preparation low");
  EXPECT_U32((uint32_t)6U, (uint32_t)test_state.set_therm_on_retained_read_index, "pa4 high-high-low therm only after third discharge pa4 low sample");
  EXPECT_U32((uint32_t)(discharge_start + 3U), (uint32_t)test_state.set_therm_on_sample_index, "pa4 high-high-low therm on sample index just after low");
  EXPECT_U32(
    0U,
    (uint32_t)test_event_count_for_kind_state_arg(
      TEST_EVENT_SET_THERM,
      ML3_STATE_VERIFY_DISCHARGE,
      1U),
    "pa4 high-high-low never asserts therm before low");
  discharge_vref_select_count = test_event_count_for_kind_state_arg(
    TEST_EVENT_SELECT_CHANNEL,
    ML3_STATE_VERIFY_DISCHARGE,
    ML3_MEASUREMENT_CHANNEL_VREFINT);
  discharge_pa4_select_count = test_event_count_for_kind_state_arg(
    TEST_EVENT_SELECT_CHANNEL,
    ML3_STATE_VERIFY_DISCHARGE,
    config.channel_v5);
  EXPECT_U32((uint32_t)6U, (uint32_t)discharge_vref_select_count, "pa4 high-high-low rechecks vref on each discharge attempt");
  EXPECT_U32((uint32_t)6U, (uint32_t)discharge_pa4_select_count, "pa4 high-high-low samples pa4 on each discharge attempt");
  EXPECT_TRUE(test_state.total_steps <= 48U, "pa4 high-high-low bounded completion steps");
  EXPECT_U32((uint32_t)ML3_STATE_IDLE, (uint32_t)ml3_measurement_state(&ctx), "pa4 high-high-low exits idle");
}

static void test_verify_discharge_threshold_straddle_uses_fresh_vdda(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[64U];
  uint16_t discharge_vref_samples[] = {16000U, 16000U};
  size_t sample_count = 0U;
  size_t discharge_start = 0U;

  test_reset_timeouts(&timeouts);
  config.discharge_threshold_mv = 500U;
  config.discharge_timeout_ms = 24U;
  sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    1000U,
    23200U,
    5200U,
    5000U,
    5000U,
    900U,
    1000U,
    2000U,
    2500U,
    3000U,
    2U);
  discharge_start = test_discharge_sample_start_index(config.abba_cycles);
  samples[discharge_start + 0U] = 5000U;
  samples[discharge_start + 1U] = 1000U;

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  test_set_verify_discharge_vref_samples(
    discharge_vref_samples,
    sizeof(discharge_vref_samples) / sizeof(discharge_vref_samples[0U]));
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_STEP_DONE,
    (uint32_t)test_run_to_completion(&ctx, 800U),
    "threshold straddle discharge run");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_OK, (uint32_t)ml3_measurement_last_error(&ctx), "threshold straddle stays nominal");
  EXPECT_U32(1U, (uint32_t)ctx.last_result.has_thermistor_raw, "threshold straddle still reads thermistor");
  EXPECT_U32(4U,
    (uint32_t)test_event_count_for_kind_state_arg(
      TEST_EVENT_SELECT_CHANNEL,
      ML3_STATE_VERIFY_DISCHARGE,
      ML3_MEASUREMENT_CHANNEL_VREFINT),
    "threshold straddle rechecks vref on each attempt");
  EXPECT_U32((uint32_t)(discharge_start + 2U), (uint32_t)test_state.set_therm_on_sample_index, "threshold straddle keeps therm low until second pa4 sample");
}

static void test_verify_discharge_threshold_straddle_keeps_higher_retained_vdda(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[64U];
  uint16_t discharge_vref_samples[] = {23200U, 23200U};
  size_t sample_count = 0U;
  size_t discharge_start = 0U;
  size_t power_off_event = SIZE_MAX;
  size_t first_vref_event = SIZE_MAX;
  size_t first_pa4_event = SIZE_MAX;
  size_t second_vref_event = SIZE_MAX;
  size_t second_pa4_event = SIZE_MAX;
  size_t therm_on_event = SIZE_MAX;
  uint32_t retained_vdda_uv = 0U;
  uint32_t fresh_vdda_uv = 0U;
  uint32_t pa4_retained_uv = 0U;
  uint32_t pa4_fresh_uv = 0U;

  test_reset_timeouts(&timeouts);
  config.discharge_threshold_mv = 500U;
  config.discharge_timeout_ms = 24U;
  sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    1000U,
    16000U,
    5200U,
    5000U,
    5000U,
    900U,
    1000U,
    2000U,
    2500U,
    3000U,
    2U);
  discharge_start = test_discharge_sample_start_index(config.abba_cycles);
  samples[discharge_start + 0U] = 5000U;
  samples[discharge_start + 1U] = 1000U;

  EXPECT_U32((uint32_t)ADC_PRECISION_OK,
    (uint32_t)adc_precision_compute_vdda_uv(config.vrefint_calibration_word, 16000U, &retained_vdda_uv),
    "retained discharge vdda fixture computes");
  EXPECT_U32((uint32_t)ADC_PRECISION_OK,
    (uint32_t)adc_precision_compute_vdda_uv(config.vrefint_calibration_word, 23200U, &fresh_vdda_uv),
    "fresh discharge vdda fixture computes");
  EXPECT_TRUE(retained_vdda_uv > fresh_vdda_uv, "retained under-load vdda is conservative maximum");
  EXPECT_U32((uint32_t)ADC_PRECISION_OK,
    (uint32_t)adc_precision_compute_channel_uv(5000U, retained_vdda_uv, &pa4_retained_uv),
    "retained-vdda pa4 fixture computes");
  EXPECT_U32((uint32_t)ADC_PRECISION_OK,
    (uint32_t)adc_precision_compute_channel_uv(5000U, fresh_vdda_uv, &pa4_fresh_uv),
    "fresh-vdda pa4 fixture computes");
  EXPECT_TRUE(pa4_fresh_uv <= 500000U, "fresh vdda alone would allow premature thermistor excitation");
  EXPECT_TRUE(pa4_retained_uv > 500000U, "retained vdda keeps first pa4 result above threshold");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  test_set_verify_discharge_vref_samples(
    discharge_vref_samples,
    sizeof(discharge_vref_samples) / sizeof(discharge_vref_samples[0U]));
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_DONE,
    (uint32_t)test_run_to_completion(&ctx, 800U),
    "retained-vdda threshold straddle discharge run");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_OK,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "retained-vdda threshold straddle stays nominal");
  EXPECT_TRUE(ctx.last_result.has_thermistor_raw, "retained-vdda threshold straddle reads thermistor after second pa4 sample");
  EXPECT_U32(16000U, ctx.last_result.post_reference_raw, "discharge checks preserve post reference diagnostic");
  EXPECT_U32(retained_vdda_uv, ctx.last_result.vdda_post_uv, "discharge checks preserve post vdda diagnostic");
  EXPECT_U32(4U,
    (uint32_t)test_event_count_for_kind_state_arg(
      TEST_EVENT_SELECT_CHANNEL,
      ML3_STATE_VERIFY_DISCHARGE,
      ML3_MEASUREMENT_CHANNEL_VREFINT),
    "retained-vdda threshold straddle rechecks vref on each attempt");
  EXPECT_U32(4U,
    (uint32_t)test_event_count_for_kind_state_arg(
      TEST_EVENT_SELECT_CHANNEL,
      ML3_STATE_VERIFY_DISCHARGE,
      config.channel_v5),
    "retained-vdda threshold straddle samples pa4 on each attempt");
  EXPECT_U32((uint32_t)(discharge_start + 2U), (uint32_t)test_state.set_therm_on_sample_index, "retained vdda blocks thermistor until second pa4 sample");

  power_off_event = test_event_nth_index_for_kind_state(TEST_EVENT_SET_POWER, ML3_STATE_POWER_OFF, 1U);
  first_vref_event = test_event_nth_index_for_kind_state_arg(
    TEST_EVENT_SELECT_CHANNEL,
    ML3_STATE_VERIFY_DISCHARGE,
    ML3_MEASUREMENT_CHANNEL_VREFINT,
    1U);
  first_pa4_event = test_event_nth_index_for_kind_state_arg(
    TEST_EVENT_SELECT_CHANNEL,
    ML3_STATE_VERIFY_DISCHARGE,
    config.channel_v5,
    1U);
  second_vref_event = test_event_nth_index_for_kind_state_arg(
    TEST_EVENT_SELECT_CHANNEL,
    ML3_STATE_VERIFY_DISCHARGE,
    ML3_MEASUREMENT_CHANNEL_VREFINT,
    3U);
  second_pa4_event = test_event_nth_index_for_kind_state_arg(
    TEST_EVENT_SELECT_CHANNEL,
    ML3_STATE_VERIFY_DISCHARGE,
    config.channel_v5,
    3U);
  therm_on_event = test_event_nth_index_for_kind_state_arg(
    TEST_EVENT_SET_THERM,
    ML3_STATE_SAMPLE_THERMISTOR,
    1U,
    1U);
  EXPECT_TRUE(power_off_event < first_vref_event, "retained-vdda first vref follows power off");
  EXPECT_TRUE(first_vref_event < first_pa4_event, "retained-vdda first pa4 follows fresh vref");
  EXPECT_TRUE(first_pa4_event < second_vref_event, "retained-vdda second attempt follows first high pa4");
  EXPECT_TRUE(second_vref_event < second_pa4_event, "retained-vdda second pa4 follows refreshed vref");
  EXPECT_TRUE(second_pa4_event < therm_on_event, "retained-vdda thermistor follows verified low pa4");
}

static void test_verify_discharge_vref_timeout_continues_queue(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[64U];
  size_t sample_count = 0U;

  test_reset_timeouts(&timeouts);
  test_state.default_conversion_polls = 1U;
  test_state.timeout_conversion_polls = 10U;
  sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    7000U,
    900U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  test_clear_overrun_injection();
  test_set_timeout_injection(ML3_STATE_VERIFY_DISCHARGE, 0U);
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_STEP_DONE,
    (uint32_t)test_run_to_completion(&ctx, 800U),
    "discharge vref timeout continues");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_ERR_ADC_TIMEOUT,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "discharge vref timeout error");
  EXPECT_TRUE(test_state.inject_timeout_seen, "discharge vref timeout observed");
  EXPECT_U32((uint32_t)ML3_STATE_VERIFY_DISCHARGE, (uint32_t)test_state.inject_timeout_hit_state, "discharge vref timeout in verify discharge");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_CHANNEL_VREFINT, (uint32_t)test_state.inject_timeout_hit_channel, "discharge vref timeout on fresh vref");
  EXPECT_TRUE(test_state.inject_timeout_hit_discard, "discharge vref timeout occurs on discard conversion");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_FAULT_ADC_TIMEOUT, (uint32_t)ctx.last_result.fault_flags, "discharge vref timeout sets adc timeout fault");
  EXPECT_U32(1U, test_state.process_calls, "discharge vref timeout reaches process");
  EXPECT_U32(1U, test_state.build_calls, "discharge vref timeout reaches build");
  EXPECT_U32(1U, test_state.queue_calls, "discharge vref timeout reaches queue");
  EXPECT_FALSE(test_state.last_power_state, "discharge vref timeout leaves power low");
  EXPECT_FALSE(test_state.last_therm_state, "discharge vref timeout leaves therm low");
}

static void test_abort_and_cleanup(void) {
  ml3_measurement_ctx_t ctx;
  ml3_measurement_ctx_t uninitialized_ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[64U];
  uint16_t therm_samples[64U];
  ml3_measurement_step_t step;
  size_t sample_count = 0U;
  size_t therm_sample_count = 0U;
  size_t i = 0U;
  size_t discharge_start = 0U;

  test_reset_timeouts(&timeouts);
  test_reset_measurement_state();

  memset(&uninitialized_ctx, 0, sizeof(uninitialized_ctx));
  ml3_measurement_abort(&uninitialized_ctx);
  EXPECT_U32(0U, test_state.set_power_calls, "uninitialized abort skips power callback");
  EXPECT_U32(0U, test_state.set_therm_calls, "uninitialized abort skips therm callback");
  EXPECT_U32((uint32_t)ML3_STATE_IDLE, (uint32_t)ml3_measurement_state(&uninitialized_ctx), "uninitialized abort stays idle");

  sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    1000U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  step = ml3_measurement_step(&ctx);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_BUSY, (uint32_t)step, "power-high abort reaches power-on state");
  EXPECT_U32((uint32_t)ML3_STATE_POWER_ON, (uint32_t)ml3_measurement_state(&ctx), "power-high abort waiting for power-on callback");
  step = ml3_measurement_step(&ctx);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_BUSY, (uint32_t)step, "power-high abort executes power-on callback");
  EXPECT_U32((uint32_t)ML3_STATE_WARMUP, (uint32_t)ml3_measurement_state(&ctx), "power-high abort reaches warmup");
  EXPECT_TRUE(test_state.last_power_state, "power is high before power-high abort");
  ml3_measurement_abort(&ctx);
  EXPECT_U32((uint32_t)ML3_STATE_IDLE, (uint32_t)ml3_measurement_state(&ctx), "power-high abort returns idle");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_ABORTED, (uint32_t)ml3_measurement_last_error(&ctx), "power-high abort sets abort error");
  EXPECT_FALSE(test_state.last_power_state, "power-high abort drives power low");
  EXPECT_FALSE(test_state.last_therm_state, "power-high abort drives therm low");
  EXPECT_TRUE(test_state.set_power_false_calls >= 2U, "power-high abort attempts power low");
  EXPECT_TRUE(test_state.set_therm_false_calls >= 2U, "power-high abort attempts therm low");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_OK, (uint32_t)ml3_measurement_start(&ctx), "power-high aborted context restarts");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_DONE, (uint32_t)test_run_to_completion(&ctx, 700U), "power-high abort resumes");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  step = ml3_measurement_step(&ctx);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_BUSY, (uint32_t)step, "power cleanup failure reaches power-on state");
  step = ml3_measurement_step(&ctx);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_BUSY, (uint32_t)step, "power cleanup failure executes power-on callback");
  test_state.set_power_fail_at = 3U;
  ml3_measurement_abort(&ctx);
  EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ml3_measurement_state(&ctx), "power cleanup failure leaves error");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_ABORTED, (uint32_t)ml3_measurement_last_error(&ctx), "power cleanup failure keeps abort error");
  EXPECT_TRUE(test_state.last_power_state, "failed power-low request leaves applied power high");
  EXPECT_FALSE(test_state.last_therm_state, "power cleanup failure still applies therm low");
  EXPECT_TRUE(test_state.set_power_false_calls >= 2U, "power cleanup failure still attempts power low");
  EXPECT_TRUE(test_state.set_therm_false_calls >= 2U, "power cleanup failure still attempts therm low");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_ERROR,
    (uint32_t)ml3_measurement_step(&ctx),
    "error state retries failed abort cleanup once per step");
  EXPECT_FALSE(test_state.last_power_state, "error-state retry applies power low");
  EXPECT_FALSE(test_state.last_therm_state, "error-state retry applies therm low");
  test_expect_low_callbacks_after_event(
    test_callback_failure_event_index(TEST_EVENT_SET_POWER, 3U),
    "error-state retry follows failed abort cleanup");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  step = ml3_measurement_step(&ctx);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_BUSY, (uint32_t)step, "therm cleanup failure reaches power-on state");
  step = ml3_measurement_step(&ctx);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_BUSY, (uint32_t)step, "therm cleanup failure executes power-on callback");
  test_state.set_therm_fail_at = 2U;
  ml3_measurement_abort(&ctx);
  EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ml3_measurement_state(&ctx), "therm cleanup failure leaves error");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_ABORTED, (uint32_t)ml3_measurement_last_error(&ctx), "therm cleanup failure keeps abort error");
  EXPECT_FALSE(test_state.last_power_state, "therm cleanup failure still drives power low");
  EXPECT_FALSE(test_state.last_therm_state, "failed therm-low request leaves prior applied low unchanged");
  EXPECT_TRUE(test_state.set_power_false_calls >= 2U, "therm cleanup failure still attempts power low");
  EXPECT_TRUE(test_state.set_therm_false_calls >= 2U, "therm cleanup failure still attempts therm low");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  therm_sample_count = test_fill_nominal_samples(
    therm_samples,
    sizeof(therm_samples) / sizeof(therm_samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    7000U,
    100U,
    100U,
    1000U,
    2000U,
    3000U,
    1U);
  discharge_start = test_discharge_sample_start_index(config.abba_cycles);
  therm_samples[discharge_start] = 100U;
  test_reset_measurement_state();
  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, therm_samples, therm_sample_count);
  for (i = 0U; i < 64U; ++i) {
    step = ml3_measurement_step(&ctx);
    if (step != ML3_MEASUREMENT_STEP_BUSY) {
      break;
    }
    if (ml3_measurement_state(&ctx) == ML3_STATE_SAMPLE_THERMISTOR) {
      break;
    }
  }
  EXPECT_U32((uint32_t)ML3_STATE_SAMPLE_THERMISTOR, (uint32_t)ml3_measurement_state(&ctx), "abort path reaches sample thermistor");
  step = ml3_measurement_step(&ctx);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_BUSY, (uint32_t)step, "sample thermistor enables excitation");
  EXPECT_TRUE(test_state.last_therm_state, "thermistor excitation asserted");
  ml3_measurement_abort(&ctx);
  EXPECT_U32((uint32_t)ML3_STATE_IDLE, (uint32_t)ml3_measurement_state(&ctx), "abort during thermistor-high returns idle");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_ABORTED, (uint32_t)ml3_measurement_last_error(&ctx), "thermistor-high abort sets abort error");
  EXPECT_FALSE(test_state.last_power_state, "thermistor-high abort drives power low");
  EXPECT_FALSE(test_state.last_therm_state, "thermistor-high abort drives therm low");
  EXPECT_TRUE(test_state.set_power_false_calls >= 3U, "thermistor-high abort attempts power low");
  EXPECT_TRUE(test_state.set_therm_false_calls >= 2U, "thermistor-high abort attempts therm low");
  test_set_samples(therm_samples, NULL, therm_sample_count);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_OK, ml3_measurement_start(&ctx), "thermistor-high abort reusable");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_DONE, (uint32_t)test_run_to_completion(&ctx, 700U), "post thermistor-high abort run completes");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, therm_samples, therm_sample_count);
  for (i = 0U; i < 64U; ++i) {
    step = ml3_measurement_step(&ctx);
    if (step != ML3_MEASUREMENT_STEP_BUSY) {
      break;
    }
    if (ml3_measurement_state(&ctx) == ML3_STATE_SAMPLE_THERMISTOR) {
      break;
    }
  }
  EXPECT_U32((uint32_t)ML3_STATE_SAMPLE_THERMISTOR, (uint32_t)ml3_measurement_state(&ctx), "abort power cleanup failure reaches thermistor phase");
  step = ml3_measurement_step(&ctx);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_BUSY, (uint32_t)step, "abort power cleanup failure enables thermistor");
  test_state.set_power_fail_at = 4U;
  ml3_measurement_abort(&ctx);
  EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ml3_measurement_state(&ctx), "power cleanup failure on abort enters error");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_ABORTED, (uint32_t)ml3_measurement_last_error(&ctx), "power cleanup failure keeps abort error");
  EXPECT_FALSE(test_state.last_power_state, "failed power-low request leaves prior applied low unchanged");
  EXPECT_FALSE(test_state.last_therm_state, "abort power cleanup failure still drives therm low");
  EXPECT_TRUE(test_state.set_power_false_calls >= 3U, "abort power cleanup failure still attempts power off");
  EXPECT_TRUE(test_state.set_therm_false_calls >= 2U, "abort power cleanup failure still attempts therm off");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, therm_samples, therm_sample_count);
  for (i = 0U; i < 64U; ++i) {
    step = ml3_measurement_step(&ctx);
    if (step != ML3_MEASUREMENT_STEP_BUSY) {
      break;
    }
    if (ml3_measurement_state(&ctx) == ML3_STATE_SAMPLE_THERMISTOR) {
      break;
    }
  }
  EXPECT_U32((uint32_t)ML3_STATE_SAMPLE_THERMISTOR, (uint32_t)ml3_measurement_state(&ctx), "abort therm cleanup failure reaches thermistor phase");
  step = ml3_measurement_step(&ctx);
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_BUSY, (uint32_t)step, "thermistor-excitation-on before abort");
  test_state.set_therm_fail_at = 3U;
  ml3_measurement_abort(&ctx);
  EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ml3_measurement_state(&ctx), "therm cleanup failure on abort enters error");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_ABORTED, (uint32_t)ml3_measurement_last_error(&ctx), "therm cleanup failure keeps abort error");
  EXPECT_FALSE(test_state.last_power_state, "abort therm cleanup failure still drives power low");
  EXPECT_TRUE(test_state.last_therm_state, "failed therm-low request leaves applied excitation high");
  EXPECT_TRUE(test_state.set_power_false_calls >= 3U, "abort therm cleanup failure still attempts power off");
  EXPECT_TRUE(test_state.set_therm_false_calls >= 2U, "abort therm cleanup failure still attempts therm off");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_ERROR,
    (uint32_t)ml3_measurement_step(&ctx),
    "error state retries failed therm cleanup once per step");
  EXPECT_FALSE(test_state.last_power_state, "error-state retry keeps power low");
  EXPECT_FALSE(test_state.last_therm_state, "error-state retry applies therm low");
  test_expect_low_callbacks_after_event(
    test_callback_failure_event_index(TEST_EVENT_SET_THERM, 3U),
    "error-state retry follows failed therm cleanup");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  for (i = 0U; i < 8U; ++i) {
    step = ml3_measurement_step(&ctx);
    if (step != ML3_MEASUREMENT_STEP_BUSY) {
      break;
    }
  }
  ml3_measurement_abort(&ctx);
  EXPECT_U32((uint32_t)ML3_STATE_IDLE, (uint32_t)ml3_measurement_state(&ctx), "double abort on active path is safe");
  ml3_measurement_abort(&ctx);
  EXPECT_U32((uint32_t)ML3_STATE_IDLE, (uint32_t)ml3_measurement_state(&ctx), "double abort on idle path is safe");
}

static void test_discard_and_retained_overrun_paths_are_distinct(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[256U];
  size_t sample_count = 0U;

  test_reset_timeouts(&timeouts);
  sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    8000U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);

  test_run_overrun_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_REFERENCE_PRE,
    true,
    0U,
    0U,
    ML3_MEASUREMENT_CHANNEL_VREFINT,
    "discard overrun on pre vref");
  EXPECT_U32(0U, (uint32_t)test_state.sample_index, "discard overrun does not consume retained sample");

  test_run_overrun_injection_with_expected_phase(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count,
    ML3_STATE_REFERENCE_PRE,
    false,
    1U,
    0U,
    ML3_MEASUREMENT_CHANNEL_VREFINT,
    "retained overrun on pre vref");
}

static void test_adc_fault_continues_queue(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[256U];
  uint16_t therm_fault_samples[256U];
  const struct {
    ml3_state_t state;
    size_t timeout_index;
    size_t overrun_conversion_index;
    size_t overrun_retained_index;
    uint16_t overrun_channel;
    bool discharge_to_therm;
    const char* label;
  } fault_injections[] = {
    {ML3_STATE_ADC_CALIBRATE, SIZE_MAX, SIZE_MAX, SIZE_MAX, UINT16_MAX, false, "prepare adc fault path"},
    {ML3_STATE_REFERENCE_PRE, 0U, 1U, 0U, ML3_MEASUREMENT_CHANNEL_VREFINT, false, "pre vref adc fault path"},
    {ML3_STATE_REFERENCE_PRE, 2U, 3U, 1U, config.channel_v5, false, "pre pa4 adc fault path"},
    {ML3_STATE_SAMPLE_ABBA, 0U, 1U, 0U, config.channel_hi, false, "abba h1 adc fault path"},
    {ML3_STATE_SAMPLE_ABBA, 2U, 3U, 1U, config.channel_lo, false, "abba l1 adc fault path"},
    {ML3_STATE_SAMPLE_ABBA, 4U, 4U, 2U, config.channel_lo, false, "abba l2 adc fault path"},
    {ML3_STATE_SAMPLE_ABBA, 5U, 6U, 3U, config.channel_hi, false, "abba h2 adc fault path"},
    {ML3_STATE_REFERENCE_POST, 0U, 1U, 0U, ML3_MEASUREMENT_CHANNEL_VREFINT, false, "post vref adc fault path"},
    {ML3_STATE_REFERENCE_POST, 2U, 3U, 1U, config.channel_v5, false, "post pa4 adc fault path"},
    {ML3_STATE_REFERENCE_POST, 4U, 5U, 2U, ML3_MEASUREMENT_CHANNEL_DIE_TEMP, false, "post die adc fault path"},
    {ML3_STATE_VERIFY_DISCHARGE, 0U, 1U, 0U, ML3_MEASUREMENT_CHANNEL_VREFINT, false, "discharge adc fault path"},
    {ML3_STATE_SAMPLE_THERMISTOR, 0U, 1U, 0U, config.channel_thermistor, true, "thermistor adc fault path"}
  };
  uint32_t original_discharge_threshold = config.discharge_threshold_mv;
  const size_t fault_count = sizeof(fault_injections) / sizeof(fault_injections[0U]);

  test_reset_timeouts(&timeouts);
  test_state.default_conversion_polls = 1U;
  test_state.timeout_conversion_polls = 10U;

  size_t count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    8000U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    4U);

  size_t count_for_therm_fault = test_fill_nominal_samples(
    therm_fault_samples,
    sizeof(therm_fault_samples) / sizeof(therm_fault_samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    0U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    4U);

  for (size_t i = 0U; i < fault_count; ++i) {
    size_t error_variants = (fault_injections[i].state == ML3_STATE_ADC_CALIBRATE) ? 1U : 2U;
    for (size_t error_variant = 0U; error_variant < error_variants; ++error_variant) {
      ml3_measurement_error_t expected_error =
        (fault_injections[i].state == ML3_STATE_ADC_CALIBRATE) ?
          ML3_MEASUREMENT_ERR_ADC_PREPARE :
          ((error_variant == 0U) ? ML3_MEASUREMENT_ERR_ADC_TIMEOUT : ML3_MEASUREMENT_ERR_ADC_OVERRUN);
      uint16_t expected_fault = (fault_injections[i].state == ML3_STATE_ADC_CALIBRATE) ?
        ML3_MEASUREMENT_FAULT_ADC_CAL :
        ((error_variant == 0U) ? ML3_MEASUREMENT_FAULT_ADC_TIMEOUT : ML3_MEASUREMENT_FAULT_ADC_OVERRUN);
      const size_t injection_index = fault_injections[i].timeout_index;

      if (fault_injections[i].discharge_to_therm) {
        config.discharge_threshold_mv = 1U;
      } else {
        config.discharge_threshold_mv = original_discharge_threshold;
      }

      const uint16_t* active_samples = (fault_injections[i].state == ML3_STATE_SAMPLE_THERMISTOR) ? therm_fault_samples : samples;
      const size_t active_count = (fault_injections[i].state == ML3_STATE_SAMPLE_THERMISTOR) ? count_for_therm_fault : count;

      test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, active_samples, active_count);
      test_state.default_conversion_polls = 1U;
      test_state.timeout_conversion_polls = 10U;
      test_state.calibration_polls = 0U;
      if (error_variant == 0U) {
        if (fault_injections[i].state == ML3_STATE_ADC_CALIBRATE) {
          test_state.calibration_polls = 1U;
        }
        test_clear_overrun_injection();
        test_set_timeout_injection(fault_injections[i].state, injection_index);
      } else {
        test_set_timeout_injection(ML3_STATE_IDLE, SIZE_MAX);
        test_set_overrun_injection(
          fault_injections[i].state,
          fault_injections[i].overrun_conversion_index,
          fault_injections[i].overrun_retained_index,
          fault_injections[i].overrun_channel,
          false);
      }

      EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_DONE,
        (uint32_t)test_run_to_completion(&ctx, 700U),
        fault_injections[i].label);
      EXPECT_U32((uint32_t)expected_error, (uint32_t)ctx.last_error, fault_injections[i].label);
      EXPECT_U32((uint32_t)expected_fault, (uint32_t)ctx.last_result.fault_flags, "fault bit set");
      EXPECT_TRUE(ctx.last_result.fault_flags == expected_fault, "fault bits are exact adc classification");
      if (error_variant == 0U) {
        if (fault_injections[i].state == ML3_STATE_ADC_CALIBRATE) {
          EXPECT_U32(0U, (uint32_t)test_state.selected_channel_count, "prepare timeout avoids conversions");
        } else {
          EXPECT_TRUE(test_state.inject_timeout_seen, "timeout injection observed");
          EXPECT_U32((uint32_t)fault_injections[i].state, (uint32_t)test_state.inject_timeout_hit_state, "timeout hit state");
          if (fault_injections[i].timeout_index != SIZE_MAX) {
            EXPECT_U32((uint32_t)fault_injections[i].timeout_index, (uint32_t)test_state.inject_timeout_hit_index, "timeout hit index");
          }
        }
      } else {
        EXPECT_TRUE(test_state.inject_overrun_seen, "overrun injection observed");
        EXPECT_U32((uint32_t)fault_injections[i].state, (uint32_t)test_state.inject_overrun_hit_state, "overrun hit state");
        EXPECT_U32((uint32_t)fault_injections[i].overrun_conversion_index, (uint32_t)test_state.inject_overrun_hit_conversion_index, "overrun hit physical conversion index");
        EXPECT_U32((uint32_t)fault_injections[i].overrun_retained_index, (uint32_t)test_state.inject_overrun_hit_retained_index, "overrun hit retained ordinal");
        EXPECT_U32((uint32_t)fault_injections[i].overrun_channel, (uint32_t)test_state.inject_overrun_hit_channel, "overrun hit channel");
        EXPECT_FALSE(test_state.inject_overrun_hit_discard, "overrun hit retained conversion");
      }
      EXPECT_FALSE(test_state.last_power_state, "power back low before callbacks");
      EXPECT_FALSE(test_state.last_therm_state, "therm back low before callbacks");
      EXPECT_U32(1U, test_state.process_calls, "fault path reaches process");
      EXPECT_U32(1U, test_state.build_calls, "fault path reaches build");
      EXPECT_U32(1U, test_state.queue_calls, "fault path reaches queue");
      test_expect_controls_low_before_event(
        test_event_nth_index_for_kind_state(TEST_EVENT_PROCESS, ML3_STATE_PROCESS, 1U),
        "fault cleanup before process");
      test_expect_controls_low_before_event(
        test_event_nth_index_for_kind_state(TEST_EVENT_BUILD, ML3_STATE_BUILD_PAYLOAD, 1U),
        "fault cleanup before build");
      test_expect_controls_low_before_event(
        test_event_nth_index_for_kind_state(TEST_EVENT_QUEUE, ML3_STATE_QUEUE_TX, 1U),
        "fault cleanup before queue");
      EXPECT_TRUE(test_state.queue_after_lows, "queue after lows on fault");
      if (fault_injections[i].state == ML3_STATE_SAMPLE_THERMISTOR) {
        EXPECT_TRUE(test_state.set_therm_true_calls > 0U, "therm excursion enters high before therm fault");
      }
      if ((fault_injections[i].state == ML3_STATE_REFERENCE_PRE) ||
          (fault_injections[i].state == ML3_STATE_SAMPLE_ABBA) ||
          (fault_injections[i].state == ML3_STATE_REFERENCE_POST)) {
        EXPECT_TRUE(test_state.set_power_true_calls > 0U, "adc fault while power was on");
      }
      EXPECT_TRUE(test_state.set_power_false_calls >= 1U, "fault path attempts power-off cleanup");
      EXPECT_TRUE(test_state.set_therm_false_calls >= 1U, "fault path attempts therm-off cleanup");
      EXPECT_FALSE(ctx.last_result.has_pre_reference_raw, "fault invalidates pre raw");
      EXPECT_FALSE(ctx.last_result.has_post_reference_raw, "fault invalidates post raw");
      EXPECT_FALSE(ctx.last_result.has_pre_v5_raw, "fault invalidates pre v5");
      EXPECT_FALSE(ctx.last_result.has_post_v5_raw, "fault invalidates post v5");
      EXPECT_FALSE(ctx.last_result.has_die_temp_raw, "fault invalidates die");
      EXPECT_FALSE(ctx.last_result.has_thermistor_raw, "fault invalidates therm");
      EXPECT_U32(0U, ctx.last_result.valid_cycle_count, "fault invalidates cycle count");
      EXPECT_U32((uint32_t)ML3_STATE_IDLE, (uint32_t)ml3_measurement_state(&ctx), "idle after fault continue");
      test_clear_overrun_injection();
      test_set_timeout_injection(ML3_STATE_IDLE, SIZE_MAX);
    }
  }
  config.discharge_threshold_mv = original_discharge_threshold;
}

static void test_adc_fault_cleanup_failure_blocks_reporting(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[256U];
  uint16_t therm_fault_samples[256U];
  size_t sample_count = 0U;
  size_t therm_count = 0U;
  size_t callback_failure_event = SIZE_MAX;

  test_reset_timeouts(&timeouts);
  test_set_now_ms(1U);

  sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    8000U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    4U);

  therm_count = test_fill_nominal_samples(
    therm_fault_samples,
    sizeof(therm_fault_samples) / sizeof(therm_fault_samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    0U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    4U);

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  test_state.set_power_fail_at = 3U;
  test_clear_overrun_injection();
  test_set_timeout_injection(ML3_STATE_REFERENCE_PRE, 0U);
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_STEP_ERROR,
    (uint32_t)test_run_to_completion(&ctx, 400U),
    "fault cleanup power fail stays error");
  EXPECT_U32(
    (uint32_t)ML3_STATE_ERROR,
    (uint32_t)ml3_measurement_state(&ctx),
    "state error after fault cleanup power fail");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_ERR_ADC_TIMEOUT,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "nested power cleanup failure preserves ADC timeout error");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_FAULT_ADC_TIMEOUT,
    (uint32_t)ctx.last_result.fault_flags,
    "nested power cleanup failure preserves exact timeout fault");
  EXPECT_TRUE(test_state.inject_timeout_seen, "nested timeout injection occurred");
  EXPECT_U32(
    (uint32_t)ML3_STATE_REFERENCE_PRE,
    (uint32_t)test_state.inject_timeout_hit_state,
    "nested timeout occurs during pre reference");
  EXPECT_U32(0U, test_state.inject_timeout_hit_index, "nested timeout physical conversion zero");
  EXPECT_U32(0U, test_state.inject_timeout_hit_retained_index, "nested timeout retained ordinal zero");
  EXPECT_U32(
    TEST_ML3_FIXED_VREF_CHANNEL,
    test_state.inject_timeout_hit_channel,
    "nested timeout targets fixed VREFINT channel");
  EXPECT_TRUE(test_state.inject_timeout_hit_discard, "nested timeout targets discard conversion");
  EXPECT_TRUE(
    test_state.inject_timeout_hit_event_index < test_state.event_count,
    "nested timeout records explicit injection event");
  EXPECT_U32(
    TEST_EVENT_ADC_TIMEOUT_INJECTION,
    test_state.events[test_state.inject_timeout_hit_event_index].kind,
    "nested timeout records exact injection taxonomy");
  callback_failure_event = test_callback_failure_event_index(TEST_EVENT_SET_POWER, 3U);
  EXPECT_TRUE(callback_failure_event != SIZE_MAX, "nested timeout records power-low callback failure");
  EXPECT_TRUE(
    test_state.inject_timeout_hit_event_index < callback_failure_event,
    "nested timeout injection precedes power-low callback failure");
  test_expect_low_callbacks_after_event(
    test_state.inject_timeout_hit_event_index,
    "nested timeout causally triggers paired low requests");
  test_expect_applied_low_callbacks_after_event(
    test_state.inject_timeout_hit_event_index,
    "nested timeout error-state retry applies paired lows");
  EXPECT_FALSE(test_state.last_power_state, "nested timeout final power state is low");
  EXPECT_FALSE(test_state.last_therm_state, "nested timeout final thermistor state is low");
  EXPECT_U32(0U, (uint32_t)test_state.process_calls, "fault cleanup failure skips process");
  EXPECT_U32(0U, (uint32_t)test_state.build_calls, "fault cleanup failure skips build");
  EXPECT_U32(0U, (uint32_t)test_state.queue_calls, "fault cleanup failure skips queue");
  EXPECT_TRUE(test_state.set_power_false_calls >= 2U, "cleanup still attempted power-off");
  EXPECT_TRUE(test_state.set_therm_false_calls >= 2U, "cleanup still attempted therm-off");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, therm_fault_samples, therm_count);
  test_state.set_power_fail_at = 0U;
  test_state.set_therm_fail_at = 3U;
  test_set_timeout_injection(ML3_STATE_IDLE, SIZE_MAX);
  test_set_overrun_injection(
    ML3_STATE_SAMPLE_THERMISTOR,
    1U,
    0U,
    config.channel_thermistor,
    false);
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_STEP_ERROR,
    (uint32_t)test_run_to_completion(&ctx, 500U),
    "fault cleanup therm fail stays error");
  EXPECT_U32(
    (uint32_t)ML3_STATE_ERROR,
    (uint32_t)ml3_measurement_state(&ctx),
    "state error after fault cleanup therm fail");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_ERR_ADC_OVERRUN,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "nested therm cleanup failure preserves ADC overrun error");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_FAULT_ADC_OVERRUN,
    (uint32_t)ctx.last_result.fault_flags,
    "nested therm cleanup failure preserves exact overrun fault");
  EXPECT_TRUE(test_state.inject_overrun_seen, "nested thermistor overrun injection occurred");
  EXPECT_U32(
    (uint32_t)ML3_STATE_SAMPLE_THERMISTOR,
    (uint32_t)test_state.inject_overrun_hit_state,
    "nested overrun occurs during thermistor sample");
  EXPECT_U32(
    1U,
    test_state.inject_overrun_hit_conversion_index,
    "nested overrun physical conversion one");
  EXPECT_U32(
    0U,
    test_state.inject_overrun_hit_retained_index,
    "nested overrun retained ordinal zero");
  EXPECT_U32(
    config.channel_thermistor,
    test_state.inject_overrun_hit_channel,
    "nested overrun targets thermistor channel");
  EXPECT_FALSE(test_state.inject_overrun_hit_discard, "nested overrun targets retained conversion");
  EXPECT_TRUE(
    test_state.inject_overrun_hit_event_index < test_state.event_count,
    "nested overrun records explicit injection event");
  EXPECT_U32(
    TEST_EVENT_ADC_OVERRUN_INJECTION,
    test_state.events[test_state.inject_overrun_hit_event_index].kind,
    "nested overrun records exact injection taxonomy");
  callback_failure_event = test_callback_failure_event_index(TEST_EVENT_SET_THERM, 3U);
  EXPECT_TRUE(callback_failure_event != SIZE_MAX, "nested overrun records therm-low callback failure");
  EXPECT_TRUE(
    test_state.inject_overrun_hit_event_index < callback_failure_event,
    "nested overrun injection precedes therm-low callback failure");
  test_expect_low_callbacks_after_event(
    test_state.inject_overrun_hit_event_index,
    "nested overrun causally triggers paired low requests");
  test_expect_applied_low_callbacks_after_event(
    test_state.inject_overrun_hit_event_index,
    "nested overrun error-state retry applies paired lows");
  EXPECT_FALSE(test_state.last_power_state, "nested overrun final power state is low");
  EXPECT_FALSE(test_state.last_therm_state, "nested overrun final thermistor state is low");
  EXPECT_U32(0U, (uint32_t)test_state.process_calls, "fault therm cleanup failure skips process");
  EXPECT_U32(0U, (uint32_t)test_state.build_calls, "fault therm cleanup failure skips build");
  EXPECT_U32(0U, (uint32_t)test_state.queue_calls, "fault therm cleanup failure skips queue");
  EXPECT_TRUE(test_state.set_therm_true_calls > 0U, "therm fault path turns thermistor high");
  EXPECT_TRUE(test_state.set_power_false_calls >= 3U, "fault cleanup still attempts power-off on therm failure");
  EXPECT_TRUE(test_state.set_therm_false_calls >= 2U, "fault cleanup still attempts therm-off");
}

static void test_adc_prepare_calibration_timeout_continues_with_cleanup(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[256U];

  test_reset_timeouts(&timeouts);
  test_set_now_ms(1U);

  size_t sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    8000U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);
  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);

  test_state.calibration_polls = 1U;
  test_state.set_power_fail_at = 0U;
  test_state.set_therm_fail_at = 0U;
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_STEP_DONE,
    (uint32_t)test_run_to_completion(&ctx, 200U),
    "prepare calibration timeout continues");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_ERR_ADC_PREPARE,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "prepare calibration timeout error");
  EXPECT_U32(
    0U,
    (uint32_t)test_state.selected_channel_count,
    "prepare calibration timeout avoids sample reads");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_FAULT_ADC_CAL,
    (uint32_t)ctx.last_result.fault_flags,
    "prepare calibration timeout sets adc cal fault");
  EXPECT_U32(2U, test_state.set_power_false_calls, "prepare path cleanup power low");
  EXPECT_U32(2U, test_state.set_therm_false_calls, "prepare path cleanup therm low");
  EXPECT_U32(1U, test_state.process_calls, "prepare calibration timeout reaches process");
  EXPECT_U32(1U, test_state.build_calls, "prepare calibration timeout reaches build");
  EXPECT_U32(1U, test_state.queue_calls, "prepare calibration timeout reaches queue");
  EXPECT_FALSE(ctx.last_result.has_pre_reference_raw, "prepare calibration timeout invalidates pre raw");
  EXPECT_FALSE(ctx.last_result.has_post_reference_raw, "prepare calibration timeout invalidates post raw");
  EXPECT_FALSE(ctx.last_result.has_pre_v5_raw, "prepare calibration timeout invalidates pre v5");
  EXPECT_FALSE(ctx.last_result.has_post_v5_raw, "prepare calibration timeout invalidates post v5");
  EXPECT_FALSE(ctx.last_result.has_die_temp_raw, "prepare calibration timeout invalidates die");
  EXPECT_FALSE(ctx.last_result.has_thermistor_raw, "prepare calibration timeout invalidates therm");
  EXPECT_U32(0U, ctx.last_result.valid_cycle_count, "prepare calibration timeout invalidates cycle count");
  EXPECT_U32((uint32_t)ML3_STATE_IDLE, (uint32_t)ml3_measurement_state(&ctx), "idle after prepare calibration timeout");
}

static void test_adc_prepare_ready_timeout_maps_to_init_fault(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[256U];

  test_reset_timeouts(&timeouts);
  test_set_now_ms(1U);

  size_t sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    8000U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);
  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);

  test_state.ready_polls = 1U;
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_STEP_DONE,
    (uint32_t)test_run_to_completion(&ctx, 200U),
    "prepare ready timeout continues");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_ERR_ADC_PREPARE,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "prepare ready timeout maps to adc prepare");
  EXPECT_U32(0U, (uint32_t)test_state.selected_channel_count, "prepare ready timeout avoids sample reads");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_FAULT_ADC_INIT,
    (uint32_t)ctx.last_result.fault_flags,
    "prepare ready timeout maps to adc init");
  EXPECT_U32(1U, test_state.process_calls, "prepare ready timeout reaches process");
  EXPECT_U32(1U, test_state.build_calls, "prepare ready timeout reaches build");
  EXPECT_U32(1U, test_state.queue_calls, "prepare ready timeout reaches queue");
  EXPECT_FALSE(ctx.last_result.has_pre_reference_raw, "prepare ready timeout invalidates pre raw");
  EXPECT_FALSE(ctx.last_result.has_post_reference_raw, "prepare ready timeout invalidates post raw");
  EXPECT_FALSE(ctx.last_result.has_pre_v5_raw, "prepare ready timeout invalidates pre v5");
  EXPECT_FALSE(ctx.last_result.has_post_v5_raw, "prepare ready timeout invalidates post v5");
  EXPECT_FALSE(ctx.last_result.has_die_temp_raw, "prepare ready timeout invalidates die");
  EXPECT_FALSE(ctx.last_result.has_thermistor_raw, "prepare ready timeout invalidates therm");
  EXPECT_U32(0U, ctx.last_result.valid_cycle_count, "prepare ready timeout invalidates cycle count");
  EXPECT_U32((uint32_t)ML3_STATE_IDLE, (uint32_t)ml3_measurement_state(&ctx), "idle after prepare ready timeout");
}

static void test_callback_failures_fail_fast(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[64U];
  uint16_t therm_samples[64U];
  size_t sample_count = 0U;
  size_t therm_count = 0U;
  const size_t watchdog_fail_points[] = {1U, 2U, 3U, 4U};
  size_t i = 0U;

  test_reset_timeouts(&timeouts);

  sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    8000U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);
  therm_count = test_fill_nominal_samples(
    therm_samples,
    sizeof(therm_samples) / sizeof(therm_samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    0U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);

  config.discharge_threshold_mv = 1U;

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  test_state.request_radio_sleep_fail_at = 1U;
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_ERROR, (uint32_t)test_run_to_completion(&ctx, 20U), "radio fail state error");
  EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ml3_measurement_state(&ctx), "state error radio");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_CALLBACK,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "radio fail error callback");
  EXPECT_U32(0U, (uint32_t)test_state.process_calls, "radio fail skips process");
  EXPECT_U32(0U, (uint32_t)test_state.build_calls, "radio fail skips build");
  EXPECT_U32(0U, (uint32_t)test_state.queue_calls, "radio fail skips queue");
  test_expect_callback_failure_contract(&ctx, "radio fail callback path", 1U, 1U);
  test_expect_low_callbacks_after_event(
    test_callback_failure_event_index(TEST_EVENT_RADIO_SLEEP, 1U),
    "radio callback cleanup after failure");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  test_state.configure_analog_fail_at = 1U;
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_ERROR, (uint32_t)test_run_to_completion(&ctx, 20U), "analog prep failure");
  EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ml3_measurement_state(&ctx), "state error analog");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_CALLBACK,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "analog prep error callback");
  EXPECT_U32(0U, (uint32_t)test_state.process_calls, "analog prep skips process");
  test_expect_callback_failure_contract(&ctx, "analog prep callback path", 1U, 1U);
  test_expect_low_callbacks_after_event(
    test_callback_failure_event_index(TEST_EVENT_ANALOG_PREP, 1U),
    "analog callback cleanup after failure");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  test_state.set_power_fail_at = 1U;
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_ERROR, (uint32_t)test_run_to_completion(&ctx, 20U), "prepare power-low callback fail");
  EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ml3_measurement_state(&ctx), "state error prepare power-off callback");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_CALLBACK,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "prepare power-off callback error callback");
  test_expect_callback_failure_contract(&ctx, "prepare power-off callback path", 1U, 1U);
  test_expect_low_callbacks_after_event(
    test_callback_failure_event_index(TEST_EVENT_SET_POWER, 1U),
    "prepare power-low callback cleanup after failure");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  test_state.set_power_fail_at = 2U;
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_ERROR, (uint32_t)test_run_to_completion(&ctx, 20U), "power-on callback fail");
  EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ml3_measurement_state(&ctx), "state error power-on callback");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_CALLBACK,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "power-on callback error callback");
  test_expect_callback_failure_contract(&ctx, "power-on callback path", 1U, 1U);
  test_expect_low_callbacks_after_event(
    test_callback_failure_event_index(TEST_EVENT_SET_POWER, 2U),
    "power-on callback cleanup after failure");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  test_state.set_power_fail_at = 3U;
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_ERROR, (uint32_t)test_run_to_completion(&ctx, 20U), "power-off callback fail");
  EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ml3_measurement_state(&ctx), "state error power-off callback");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_CALLBACK,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "power-off callback error callback");
  test_expect_callback_failure_contract(&ctx, "power-off callback path", 1U, 1U);
  test_expect_low_callbacks_after_event(
    test_callback_failure_event_index(TEST_EVENT_SET_POWER, 3U),
    "power-off callback cleanup after failure");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, therm_samples, therm_count);
  test_state.set_therm_fail_at = 2U;
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_ERROR, (uint32_t)test_run_to_completion(&ctx, 300U), "therm callback fail");
  EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ml3_measurement_state(&ctx), "state error therm callback");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_CALLBACK,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "therm callback error callback");
  test_expect_callback_failure_contract(&ctx, "therm fail callback path", 1U, 1U);
  test_expect_low_callbacks_after_event(
    test_callback_failure_event_index(TEST_EVENT_SET_THERM, 2U),
    "therm assert callback cleanup after failure");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, therm_samples, therm_count);
  test_state.set_therm_fail_at = 3U;
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_ERROR, (uint32_t)test_run_to_completion(&ctx, 300U), "therm callback deassert fail");
  EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ml3_measurement_state(&ctx), "state error therm deassert callback");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_CALLBACK,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "therm deassert callback error callback");
  test_expect_callback_failure_contract(&ctx, "therm deassert callback path", 1U, 1U);
  test_expect_low_callbacks_after_event(
    test_callback_failure_event_index(TEST_EVENT_SET_THERM, 3U),
    "therm deassert callback cleanup after failure");

  for (i = 0U; i < 4U; ++i) {
    test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
    test_state.watchdog_fail_at = watchdog_fail_points[i];
    ml3_measurement_step_t watchdog_step = test_run_to_completion(&ctx, 500U);
    const char* watchdog_point_label =
      (i == 0U) ? "watchdog point 1" :
      (i == 1U) ? "watchdog point 2" :
      (i == 2U) ? "watchdog point 3" :
      "watchdog point 4";
    EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_ERROR, (uint32_t)watchdog_step, watchdog_point_label);
    EXPECT_U32((uint32_t)watchdog_step, (uint32_t)ML3_MEASUREMENT_STEP_ERROR, "watchdog fail callback");
    EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_CALLBACK,
      (uint32_t)ml3_measurement_last_error(&ctx),
      "watchdog fail error callback");
    EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ml3_measurement_state(&ctx), "state error watchdog");
    test_expect_callback_failure_contract(&ctx, watchdog_point_label, 1U, 1U);
    test_expect_low_callbacks_after_event(
      test_callback_failure_event_index(TEST_EVENT_WATCHDOG, watchdog_fail_points[i]),
      watchdog_point_label);
    if (i < 3U) {
      EXPECT_U32(0U, (uint32_t)test_state.build_calls, "watchdog fail before queue/build skips build");
    } else {
      EXPECT_U32(1U, (uint32_t)test_state.build_calls, "watchdog in queue occurs after build");
    }
    EXPECT_U32(0U, (uint32_t)test_state.queue_calls, "watchdog fail skips queue");
  }

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  test_state.process_fail_at = 1U;
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_ERROR, (uint32_t)test_run_to_completion(&ctx, 300U), "process fail");
  EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ml3_measurement_state(&ctx), "state error process");
  EXPECT_U32(1U, test_state.process_calls, "process callback attempted");
  EXPECT_U32(0U, (uint32_t)test_state.build_calls, "process failure skips build");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_CALLBACK,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "process fail callback error");
  test_expect_callback_failure_contract(&ctx, "process fail callback path", 1U, 1U);
  test_expect_low_callbacks_after_event(
    test_callback_failure_event_index(TEST_EVENT_PROCESS, 1U),
    "process callback cleanup after failure");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  test_state.build_fail_at = 1U;
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_ERROR, (uint32_t)test_run_to_completion(&ctx, 300U), "build fail");
  EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ml3_measurement_state(&ctx), "state error build");
  EXPECT_U32(1U, (uint32_t)test_state.process_calls, "build failure reached process");
  EXPECT_U32(1U, (uint32_t)test_state.build_calls, "build callback attempted");
  EXPECT_U32(0U, (uint32_t)test_state.queue_calls, "build failure skips queue");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_CALLBACK,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "build fail callback error");
  test_expect_callback_failure_contract(&ctx, "build fail callback path", 1U, 1U);
  test_expect_low_callbacks_after_event(
    test_callback_failure_event_index(TEST_EVENT_BUILD, 1U),
    "build callback cleanup after failure");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  test_state.queue_fail_at = 1U;
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_ERROR, (uint32_t)test_run_to_completion(&ctx, 300U), "queue fail");
  EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ml3_measurement_state(&ctx), "state error queue");
  EXPECT_U32(1U, (uint32_t)test_state.process_calls, "queue failure reached process");
  EXPECT_U32(1U, (uint32_t)test_state.build_calls, "queue failure reached build");
  EXPECT_U32(1U, (uint32_t)test_state.queue_calls, "queue callback attempted");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_CALLBACK,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "queue fail callback error");
  test_expect_callback_failure_contract(&ctx, "queue fail callback path", 1U, 1U);
  test_expect_low_callbacks_after_event(
    test_callback_failure_event_index(TEST_EVENT_QUEUE, 1U),
    "queue callback cleanup after failure");

  test_prepare_measurement(&ctx, &adc_ctx, &config, &timeouts, samples, sample_count);
  test_state.set_power_fail_at = 4U;
  test_state.queue_fail_at = 1U;
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_STEP_ERROR, (uint32_t)test_run_to_completion(&ctx, 300U), "queue fail plus cleanup leg fail");
  EXPECT_U32((uint32_t)ML3_STATE_ERROR, (uint32_t)ml3_measurement_state(&ctx), "state error on cleanup fail");
  EXPECT_U32((uint32_t)ML3_MEASUREMENT_ERR_CALLBACK,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "queue fail cleanup callback error");
  test_expect_callback_failure_contract(&ctx, "queue fail plus cleanup leg fail callback path", 1U, 1U);
  EXPECT_TRUE(
    test_callback_failure_event_index(TEST_EVENT_QUEUE, 1U) != SIZE_MAX,
    "queue failure injection fired");
  EXPECT_TRUE(
    test_callback_failure_event_index(TEST_EVENT_SET_POWER, 4U) != SIZE_MAX,
    "queue cleanup power-low failure injection fired");
  test_expect_low_callbacks_after_event(
    test_callback_failure_event_index(TEST_EVENT_QUEUE, 1U),
    "queue cleanup-leg failure still attempts lows after failure");

  config.discharge_threshold_mv = 500U;

}

static void test_prepare_initial_thermistor_low_failure_retries_safe_low(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(3U);
  adc_precision_timeouts_t timeouts;
  uint16_t samples[64U];
  size_t sample_count = 0U;
  size_t failure_event = SIZE_MAX;
  size_t retry_event = SIZE_MAX;
  size_t power_false_before_retry = 0U;
  size_t therm_false_before_retry = 0U;

  test_reset_timeouts(&timeouts);
  sample_count = test_fill_nominal_samples(
    samples,
    sizeof(samples) / sizeof(samples[0U]),
    config.abba_cycles,
    24000U,
    5400U,
    23200U,
    5200U,
    5000U,
    8000U,
    7000U,
    1000U,
    2000U,
    2500U,
    3000U,
    1U);

  test_prepare_measurement(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sample_count);
  test_state.last_power_state = true;
  test_state.last_therm_state = true;
  test_state.set_therm_fail_at = 1U;

  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_STEP_ERROR,
    (uint32_t)ml3_measurement_step(&ctx),
    "initial PREPARE thermistor-low failure returns error");
  EXPECT_U32(
    (uint32_t)ML3_STATE_ERROR,
    (uint32_t)ml3_measurement_state(&ctx),
    "initial PREPARE thermistor-low failure enters error state");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_ERR_CALLBACK,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "initial PREPARE thermistor-low failure reports callback error");
  EXPECT_U32(1U, test_state.callback_failure_count, "initial PREPARE thermistor failure recorded once");
  EXPECT_U32(TEST_EVENT_SET_THERM, test_state.callback_failures[0U].kind, "initial PREPARE failure is thermistor callback");
  EXPECT_U32(1U, test_state.callback_failures[0U].call_index, "initial PREPARE failure occurs on thermistor call one");
  failure_event = test_callback_failure_event_index(TEST_EVENT_SET_THERM, 1U);
  EXPECT_TRUE(failure_event != SIZE_MAX, "initial PREPARE failure event exists");
  EXPECT_U32(TEST_EVENT_SET_THERM, test_state.events[failure_event].kind, "initial PREPARE failure event taxonomy");
  EXPECT_U32(0U, test_state.events[failure_event].arg, "initial PREPARE failure requests thermistor low");
  EXPECT_U32(
    (uint32_t)ML3_STATE_PREPARE,
    (uint32_t)test_state.events[failure_event].state,
    "initial thermistor-low failure occurs in PREPARE");
  EXPECT_FALSE(test_state.events[failure_event].applied, "failed initial thermistor-low event is not applied");
  EXPECT_FALSE(test_state.callback_failures[0U].power_state_after, "power low applied before thermistor failure");
  EXPECT_TRUE(test_state.callback_failures[0U].therm_state_after, "failed thermistor-low write leaves applied excitation high");
  EXPECT_U32(0U, test_state.process_calls, "initial PREPARE failure skips process");
  EXPECT_U32(0U, test_state.build_calls, "initial PREPARE failure skips build");
  EXPECT_U32(0U, test_state.queue_calls, "initial PREPARE failure skips queue");
  test_expect_low_callbacks_after_event(
    failure_event,
    "initial PREPARE thermistor failure triggers causal paired lows");
  test_expect_applied_low_callbacks_after_event(
    failure_event,
    "initial PREPARE thermistor failure cleanup applies paired lows");
  EXPECT_FALSE(test_state.last_power_state, "initial PREPARE failure cleanup leaves power low");
  EXPECT_FALSE(test_state.last_therm_state, "initial PREPARE failure cleanup leaves thermistor low");

  power_false_before_retry = test_state.set_power_false_calls;
  therm_false_before_retry = test_state.set_therm_false_calls;
  retry_event = test_state.event_count;
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_STEP_ERROR,
    (uint32_t)ml3_measurement_step(&ctx),
    "ERROR state retries safe-low after initial PREPARE failure");
  EXPECT_U32(
    (uint32_t)(power_false_before_retry + 1U),
    (uint32_t)test_state.set_power_false_calls,
    "ERROR retry requests power low once");
  EXPECT_U32(
    (uint32_t)(therm_false_before_retry + 1U),
    (uint32_t)test_state.set_therm_false_calls,
    "ERROR retry requests thermistor low once");
  EXPECT_U32(TEST_EVENT_SET_POWER, test_state.events[retry_event].kind, "ERROR retry power-low event first");
  EXPECT_U32(TEST_EVENT_SET_THERM, test_state.events[retry_event + 1U].kind, "ERROR retry thermistor-low event second");
  EXPECT_TRUE(test_state.events[retry_event].applied, "ERROR retry applies power low");
  EXPECT_TRUE(test_state.events[retry_event + 1U].applied, "ERROR retry applies thermistor low");
  EXPECT_FALSE(test_state.last_power_state, "ERROR retry leaves power low");
  EXPECT_FALSE(test_state.last_therm_state, "ERROR retry leaves thermistor low");
}

static void test_full_state_machine_abba_oracle(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  ml3_measurement_config_t config = test_default_config(4U);
  adc_precision_timeouts_t timeouts;
  const uint16_t samples[] = {
    48000U,
    59000U,
    10920U, 11739U, 11193U, 11466U,
    12012U, 11466U, 10920U, 12558U,
    13104U, 12012U, 12558U, 12558U,
    13650U, 13104U, 12558U, 14196U,
    45000U,
    59000U,
    30000U,
    8190U,
    32000U
  };
  const uint16_t discharge_vref_samples[] = {48000U};
  const int64_t expected_hi_uv[] = {512500LL, 562500LL, 587500LL, 637500LL};
  const int64_t expected_lo_uv[] = {525000LL, 512500LL, 562500LL, 587500LL};
  const int64_t expected_diff_uv[] = {-12500LL, 50000LL, 25000LL, 50000LL};
  const uint16_t expected_h1_raw[] = {10920U, 12012U, 13104U, 13650U};
  const uint16_t expected_l1_raw[] = {11739U, 11466U, 12012U, 13104U};
  const uint16_t expected_l2_raw[] = {11193U, 10920U, 12558U, 12558U};
  const uint16_t expected_h2_raw[] = {11466U, 12558U, 12558U, 14196U};
  const ml3_state_t expected_states[] = {
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
    ML3_STATE_IDLE
  };
  const ml3_measurement_result_t* result = NULL;
  uint64_t variance_sum = 0ULL;
  size_t h1_retained_event = SIZE_MAX;
  size_t l1_retained_event = SIZE_MAX;
  size_t l2_retained_event = SIZE_MAX;
  size_t h2_retained_event = SIZE_MAX;

  test_reset_timeouts(&timeouts);
  test_prepare_measurement(
    &ctx,
    &adc_ctx,
    &config,
    &timeouts,
    samples,
    sizeof(samples) / sizeof(samples[0U]));
  test_set_verify_discharge_vref_samples(
    discharge_vref_samples,
    sizeof(discharge_vref_samples) / sizeof(discharge_vref_samples[0U]));

  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_STEP_DONE,
    (uint32_t)test_run_to_completion(&ctx, 800U),
    "full ABBA oracle completes");
  EXPECT_U32(
    (uint32_t)ML3_MEASUREMENT_OK,
    (uint32_t)ml3_measurement_last_error(&ctx),
    "full ABBA oracle has no measurement error");
  test_expect_state_exact(
    expected_states,
    sizeof(expected_states) / sizeof(expected_states[0U]));
  EXPECT_U32(1U, test_state.process_calls, "full ABBA oracle processes once");
  EXPECT_U32(1U, test_state.build_calls, "full ABBA oracle builds once");
  EXPECT_U32(1U, test_state.queue_calls, "full ABBA oracle queues once");
  EXPECT_TRUE(test_state.queued_result_seen, "full ABBA oracle snapshots queued result");

  h1_retained_event = test_event_nth_index_for_kind_state_arg(
    TEST_EVENT_SELECT_CHANNEL,
    ML3_STATE_SAMPLE_ABBA,
    config.channel_hi,
    2U);
  l1_retained_event = test_event_nth_index_for_kind_state_arg(
    TEST_EVENT_SELECT_CHANNEL,
    ML3_STATE_SAMPLE_ABBA,
    config.channel_lo,
    2U);
  l2_retained_event = test_event_nth_index_for_kind_state_arg(
    TEST_EVENT_SELECT_CHANNEL,
    ML3_STATE_SAMPLE_ABBA,
    config.channel_lo,
    3U);
  h2_retained_event = test_event_nth_index_for_kind_state_arg(
    TEST_EVENT_SELECT_CHANNEL,
    ML3_STATE_SAMPLE_ABBA,
    config.channel_hi,
    4U);
  EXPECT_TRUE(
    (h1_retained_event != SIZE_MAX) &&
    (l1_retained_event != SIZE_MAX) &&
    (l2_retained_event != SIZE_MAX) &&
    (h2_retained_event != SIZE_MAX),
    "full ABBA oracle observes all first-cycle retained phases");
  EXPECT_TRUE(
    (h1_retained_event < l1_retained_event) &&
    (l1_retained_event < l2_retained_event) &&
    (l2_retained_event < h2_retained_event),
    "full ABBA oracle retains H1-L1-L2-H2 in order");

  for (size_t i = 0U; i < config.abba_cycles; ++i) {
    const int64_t diff_uv = ctx.cycle_hi_uv[i] - ctx.cycle_lo_uv[i];
    const int64_t variance_delta = diff_uv - 28125LL;
    EXPECT_TRUE(ctx.cycle_valid[i], "full ABBA oracle marks each cycle valid");
    EXPECT_I64(expected_hi_uv[i], ctx.cycle_hi_uv[i], "full ABBA oracle cycle high basis");
    EXPECT_I64(expected_lo_uv[i], ctx.cycle_lo_uv[i], "full ABBA oracle cycle low basis");
    EXPECT_I64(expected_diff_uv[i], diff_uv, "full ABBA oracle cycle differential basis");
    variance_sum += (uint64_t)(variance_delta * variance_delta);
  }
  EXPECT_U32(4U, ctx.current_abba_cycle, "full ABBA oracle retains four cycles");
  EXPECT_I64(625000LL, ctx.abba_h1_uv, "full ABBA oracle final H1 phase scaling");
  EXPECT_I64(600000LL, ctx.abba_l1_uv, "full ABBA oracle final L1 phase scaling");
  EXPECT_I64(575000LL, ctx.abba_l2_uv, "full ABBA oracle final L2 phase scaling");
  EXPECT_I64(650000LL, ctx.abba_h2_uv, "full ABBA oracle final H2 phase scaling");
  EXPECT_I64(
    654296875LL,
    (int64_t)(variance_sum / (uint64_t)config.abba_cycles),
    "full ABBA oracle population variance basis");

  result = &test_state.queued_result;
  EXPECT_TRUE(result->has_abba_raw, "full ABBA queued raw evidence present");
  EXPECT_U32(4U, result->abba_raw_cycle_count, "full ABBA queued raw evidence count");
  for (size_t i = 0U; i < config.abba_cycles; ++i) {
    EXPECT_U32(expected_h1_raw[i], result->abba_h1_raw[i], "full ABBA queued H1 raw evidence");
    EXPECT_U32(expected_l1_raw[i], result->abba_l1_raw[i], "full ABBA queued L1 raw evidence");
    EXPECT_U32(expected_l2_raw[i], result->abba_l2_raw[i], "full ABBA queued L2 raw evidence");
    EXPECT_U32(expected_h2_raw[i], result->abba_h2_raw[i], "full ABBA queued H2 raw evidence");
  }
  EXPECT_TRUE(result->has_sequence, "full ABBA queued sequence present");
  EXPECT_U32(1U, result->sequence, "full ABBA queued sequence value");
  EXPECT_TRUE(result->has_pre_reference_raw, "full ABBA pre reference present");
  EXPECT_TRUE(result->has_post_reference_raw, "full ABBA post reference present");
  EXPECT_TRUE(result->has_pre_v5_raw, "full ABBA pre PA4 present");
  EXPECT_TRUE(result->has_post_v5_raw, "full ABBA post PA4 present");
  EXPECT_TRUE(result->has_die_temp_raw, "full ABBA die temperature present");
  EXPECT_TRUE(result->has_thermistor_raw, "full ABBA thermistor present");
  EXPECT_TRUE(result->has_vdda_pre_uv, "full ABBA pre VDDA present");
  EXPECT_TRUE(result->has_vdda_post_uv, "full ABBA post VDDA present");
  EXPECT_FALSE(result->has_faults, "full ABBA omits absent faults");
  EXPECT_U32(48000U, result->pre_reference_raw, "full ABBA pre reference raw");
  EXPECT_U32(45000U, result->post_reference_raw, "full ABBA post reference raw");
  EXPECT_U32(59000U, result->pre_v5_raw, "full ABBA pre PA4 raw");
  EXPECT_U32(59000U, result->post_v5_raw, "full ABBA post PA4 raw");
  EXPECT_U32(30000U, result->die_temp_raw, "full ABBA die temperature raw");
  EXPECT_U32(32000U, result->thermistor_raw, "full ABBA thermistor raw");
  EXPECT_U32(3000000U, result->vdda_pre_uv, "full ABBA pre VDDA scaling");
  EXPECT_U32(3200000U, result->vdda_post_uv, "full ABBA post VDDA scaling");
  EXPECT_U32(0U, result->fault_flags, "full ABBA queued fault flags clear");

  EXPECT_TRUE(result->has_mean_hi_uv, "full ABBA mean high present");
  EXPECT_TRUE(result->has_mean_lo_uv, "full ABBA mean low present");
  EXPECT_TRUE(result->has_common_mode_uv, "full ABBA common mode present");
  EXPECT_TRUE(result->has_mean_diff_uv, "full ABBA mean differential present");
  EXPECT_TRUE(result->has_median_diff_uv, "full ABBA median differential present");
  EXPECT_TRUE(result->has_drift_uv, "full ABBA drift present");
  EXPECT_TRUE(result->has_sd_uv, "full ABBA standard deviation present");
  EXPECT_TRUE(result->has_mad_uv, "full ABBA MAD present");
  EXPECT_TRUE(result->has_min_diff_uv, "full ABBA minimum differential present");
  EXPECT_TRUE(result->has_max_diff_uv, "full ABBA maximum differential present");
  EXPECT_TRUE(result->has_valid_cycle_count, "full ABBA valid-cycle count present");
  EXPECT_I64(575000LL, result->mean_hi_uv, "full ABBA mean high");
  EXPECT_I64(546875LL, result->mean_lo_uv, "full ABBA mean low");
  EXPECT_I64(546875LL, result->common_mode_uv, "full ABBA common mode");
  EXPECT_I64(28125LL, result->mean_diff_uv, "full ABBA mean differential");
  EXPECT_I64(37500LL, result->median_diff_uv, "full ABBA median differential");
  EXPECT_I64(62500LL, result->drift_uv, "full ABBA drift");
  EXPECT_I64(25579LL, result->sd_uv, "full ABBA population standard deviation");
  EXPECT_I64(12500LL, result->mad_uv, "full ABBA median absolute deviation");
  EXPECT_I64(-12500LL, result->min_diff_uv, "full ABBA minimum differential");
  EXPECT_I64(50000LL, result->max_diff_uv, "full ABBA maximum differential");
  EXPECT_U32(4U, result->valid_cycle_count, "full ABBA valid-cycle count");
}

static void test_abba_raw_evidence_counts_and_excludes_discards(void) {
  ml3_measurement_ctx_t ctx;
  adc_precision_context_t adc_ctx;
  adc_precision_timeouts_t timeouts;
  uint16_t samples[128U];
  const uint16_t cycle_counts[] = {3U, 8U};

  test_reset_timeouts(&timeouts);
  for (size_t case_index = 0U;
       case_index < sizeof(cycle_counts) / sizeof(cycle_counts[0U]);
       ++case_index) {
    ml3_measurement_config_t config = test_default_config(cycle_counts[case_index]);
    const size_t sample_count = test_fill_nominal_samples(
      samples,
      sizeof(samples) / sizeof(samples[0U]),
      config.abba_cycles,
      24000U,
      5400U,
      23200U,
      5200U,
      5000U,
      8000U,
      7000U,
      1000U,
      2000U,
      2500U,
      3000U,
      1U);

    test_prepare_measurement(
      &ctx,
      &adc_ctx,
      &config,
      &timeouts,
      samples,
      sample_count);
    test_state.discard_raw_code = 0xA5A5U;
    EXPECT_U32(
      (uint32_t)ML3_MEASUREMENT_STEP_DONE,
      (uint32_t)test_run_to_completion(&ctx, 1000U),
      "ABBA raw evidence acquisition completes");
    EXPECT_TRUE(ctx.last_result.has_abba_raw, "ABBA raw evidence is present after full burst");
    EXPECT_U32(
      config.abba_cycles,
      ctx.last_result.abba_raw_cycle_count,
      "ABBA raw evidence exposes exact configured cycle count");
    for (size_t cycle = 0U; cycle < config.abba_cycles; ++cycle) {
      EXPECT_U32(1000U, ctx.last_result.abba_h1_raw[cycle], "ABBA raw H1 retains sample, not discard");
      EXPECT_U32(2000U, ctx.last_result.abba_l1_raw[cycle], "ABBA raw L1 retains sample, not discard");
      EXPECT_U32(2500U, ctx.last_result.abba_l2_raw[cycle], "ABBA raw L2 retains sample, not discard");
      EXPECT_U32(3000U, ctx.last_result.abba_h2_raw[cycle], "ABBA raw H2 retains sample, not discard");
      EXPECT_TRUE(ctx.last_result.abba_h1_raw[cycle] != 0xA5A5U, "ABBA H1 excludes discard poison");
      EXPECT_TRUE(ctx.last_result.abba_l1_raw[cycle] != 0xA5A5U, "ABBA L1 excludes discard poison");
      EXPECT_TRUE(ctx.last_result.abba_l2_raw[cycle] != 0xA5A5U, "ABBA L2 excludes discard poison");
      EXPECT_TRUE(ctx.last_result.abba_h2_raw[cycle] != 0xA5A5U, "ABBA H2 excludes discard poison");
    }
    for (size_t cycle = config.abba_cycles;
         cycle < ML3_MEASUREMENT_MAX_ABBA_CYCLES;
         ++cycle) {
      EXPECT_U32(0U, ctx.last_result.abba_h1_raw[cycle], "unused H1 raw slot remains clear");
      EXPECT_U32(0U, ctx.last_result.abba_l1_raw[cycle], "unused L1 raw slot remains clear");
      EXPECT_U32(0U, ctx.last_result.abba_l2_raw[cycle], "unused L2 raw slot remains clear");
      EXPECT_U32(0U, ctx.last_result.abba_h2_raw[cycle], "unused H2 raw slot remains clear");
    }
  }
}

static void test_compute_stats_oracles(void) {
  ml3_measurement_result_t stats;
  int64_t hi[8];
  int64_t lo[8];
  bool valid[8];
  size_t i = 0U;
  const int64_t adc_derived_max_uv = (int64_t)UINT32_MAX;
  int64_t mask_4cycle_hi[4];
  int64_t mask_4cycle_lo[4];

  memset(valid, true, sizeof(valid));
  memset(hi, 0, sizeof(hi));
  memset(lo, 0, sizeof(lo));
  ml3_measurement_compute_uv_stats(NULL, lo, valid, 1U, &stats);
  EXPECT_FALSE(stats.has_valid_cycle_count, "reject null hi");
  ml3_measurement_compute_uv_stats(hi, NULL, valid, 1U, &stats);
  EXPECT_FALSE(stats.has_valid_cycle_count, "reject null lo");
  ml3_measurement_compute_uv_stats(hi, lo, NULL, 1U, &stats);
  EXPECT_FALSE(stats.has_valid_cycle_count, "reject null valid mask");

  memset(valid, true, sizeof(valid));
  hi[0] = 1000000LL;
  hi[1] = 1020000LL;
  lo[0] = 1200000LL;
  lo[1] = 1190000LL;
  ml3_measurement_compute_uv_stats(hi, lo, valid, 2U, &stats);
  EXPECT_TRUE(stats.has_valid_cycle_count, "2-cycle has valid count");
  EXPECT_U32(2U, (uint32_t)stats.valid_cycle_count, "2-cycle count");
  EXPECT_FALSE(stats.has_mean_hi_uv, "2-cycle insufficient has mean");
  EXPECT_FALSE(stats.has_mean_lo_uv, "2-cycle insufficient has mean lo");
  EXPECT_FALSE(stats.has_common_mode_uv, "2-cycle insufficient has cm");
  EXPECT_FALSE(stats.has_sd_uv, "2-cycle insufficient has sd");

  hi[0] = 500000LL; hi[1] = 505000LL; hi[2] = 510000LL; hi[3] = 515000LL;
  lo[0] = 530000LL; lo[1] = 507000LL; lo[2] = 498000LL; lo[3] = 492000LL;
  ml3_measurement_compute_uv_stats(hi, lo, valid, 4U, &stats);
  EXPECT_TRUE(stats.has_mean_hi_uv, "4-cycle mean computed");
  EXPECT_I64(507500LL, stats.mean_hi_uv, "4-cycle mean hi");
  EXPECT_I64(506750LL, stats.mean_lo_uv, "4-cycle mean lo");
  EXPECT_I64(506750LL, stats.common_mode_uv, "4-cycle cm");
  EXPECT_I64(750LL, stats.mean_diff_uv, "4-cycle mean diff");
  EXPECT_I64(5000LL, stats.median_diff_uv, "4-cycle median");
  EXPECT_I64(53000LL, stats.drift_uv, "4-cycle drift");
  EXPECT_I64(19841LL, stats.sd_uv, "4-cycle sd");
  EXPECT_I64(12500LL, stats.mad_uv, "4-cycle mad");
  EXPECT_I64(-30000LL, stats.min_diff_uv, "4-cycle min");
  EXPECT_I64(23000LL, stats.max_diff_uv, "4-cycle max");
  EXPECT_U32(4U, (uint32_t)stats.valid_cycle_count, "4-cycle valid");

  memset(valid, true, sizeof(valid));
  hi[0] = 510000LL; hi[1] = 514000LL; hi[2] = 530000LL; hi[3] = 500000LL; hi[4] = 496000LL;
  lo[0] = 530000LL; lo[1] = 509000LL; lo[2] = 500000LL; lo[3] = 490000LL; lo[4] = 484000LL;
  ml3_measurement_compute_uv_stats(hi, lo, valid, 5U, &stats);
  EXPECT_I64(510000LL, stats.mean_hi_uv, "5-cycle mean hi");
  EXPECT_I64(502600LL, stats.mean_lo_uv, "5-cycle mean lo");
  EXPECT_I64(502600LL, stats.common_mode_uv, "5-cycle cm");
  EXPECT_I64(7400LL, stats.mean_diff_uv, "5-cycle mean diff");
  EXPECT_I64(10000LL, stats.median_diff_uv, "5-cycle median diff");
  EXPECT_I64(32000LL, stats.drift_uv, "5-cycle drift");
  EXPECT_I64(16094LL, stats.sd_uv, "5-cycle sd");
  EXPECT_I64(5000LL, stats.mad_uv, "5-cycle mad");
  EXPECT_I64(-20000LL, stats.min_diff_uv, "5-cycle min");
  EXPECT_I64(30000LL, stats.max_diff_uv, "5-cycle max");
  EXPECT_U32(5U, (uint32_t)stats.valid_cycle_count, "5-cycle valid");

  memset(valid, true, sizeof(valid));
  hi[0] = 500000LL;
  hi[1] = 505000LL;
  hi[2] = 510000LL;
  hi[3] = 515000LL;
  lo[0] = 530000LL;
  lo[1] = 507000LL;
  lo[2] = 498000LL;
  lo[3] = 492000LL;
  for (i = 0U; i < 4U; ++i) {
    hi[4U + i] = hi[i];
    lo[4U + i] = lo[i];
  }
  ml3_measurement_compute_uv_stats(hi, lo, valid, 8U, &stats);
  EXPECT_I64(507500LL, stats.mean_hi_uv, "8-cycle mean hi");
  EXPECT_I64(506750LL, stats.mean_lo_uv, "8-cycle mean lo");
  EXPECT_I64(750LL, stats.mean_diff_uv, "8-cycle mean diff");
  EXPECT_I64(53000LL, stats.drift_uv, "8-cycle drift");
  EXPECT_I64(19841LL, stats.sd_uv, "8-cycle sd");
  EXPECT_I64(12500LL, stats.mad_uv, "8-cycle mad");
  EXPECT_U32(8U, (uint32_t)stats.valid_cycle_count, "8-cycle valid");

  memset(valid, true, sizeof(valid));
  valid[0] = true;
  valid[1] = true;
  valid[2] = false;
  valid[3] = true;
  hi[0] = 500000LL; hi[1] = 505000LL; hi[2] = 510000LL; hi[3] = 515000LL;
  lo[0] = 530000LL; lo[1] = 507000LL; lo[2] = 498000LL; lo[3] = 492000LL;
  ml3_measurement_compute_uv_stats(hi, lo, valid, 4U, &stats);
  EXPECT_I64(506666LL, stats.mean_hi_uv, "mask mean hi");
  EXPECT_I64(509666LL, stats.mean_lo_uv, "mask mean lo");
  EXPECT_I64(-3000LL, stats.mean_diff_uv, "mask mean diff");
  EXPECT_I64(-2000LL, stats.median_diff_uv, "mask median");
  EXPECT_I64(53000LL, stats.drift_uv, "mask drift");
  EXPECT_I64(21648LL, stats.sd_uv, "mask sd");
  EXPECT_I64(25000LL, stats.mad_uv, "mask mad");
  EXPECT_U32(3U, (uint32_t)stats.valid_cycle_count, "mask valid");

  memset(valid, true, sizeof(valid));
  memset(hi, 0, sizeof(hi));
  memset(lo, 0, sizeof(lo));
  hi[3] = 2LL;
  ml3_measurement_compute_uv_stats(hi, lo, valid, 4U, &stats);
  EXPECT_TRUE(stats.has_sd_uv, "fractional-mean population variance reports SD");
  EXPECT_I64(0LL, stats.mean_diff_uv, "fractional-mean population mean truncates to zero");
  EXPECT_I64(0LL, stats.sd_uv, "fractional-mean exact floor variance produces zero SD");

  memset(valid, true, sizeof(valid));
  memset(hi, 0, sizeof(hi));
  memset(lo, 0, sizeof(lo));
  lo[0] = 5LL;
  lo[1] = 5LL;
  lo[2] = 3LL;
  ml3_measurement_compute_uv_stats(hi, lo, valid, 3U, &stats);
  EXPECT_TRUE(stats.has_sd_uv, "negative fractional-mean population variance reports SD");
  EXPECT_I64(-4LL, stats.mean_diff_uv, "negative fractional mean truncates toward zero");
  EXPECT_I64(0LL, stats.sd_uv, "negative fractional-mean correction uses mathematical floor");

  memset(valid, true, sizeof(valid));
  memset(hi, 0, sizeof(hi));
  memset(lo, 0, sizeof(lo));
  for (i = 0U; i < 7U; ++i) {
    lo[i] = 2LL;
  }
  hi[7] = 1LL;
  ml3_measurement_compute_uv_stats(hi, lo, valid, 8U, &stats);
  EXPECT_TRUE(stats.has_sd_uv, "negative centered-correction floor reports SD");
  EXPECT_I64(-1LL, stats.mean_diff_uv, "negative centered-correction mean truncates toward zero");
  EXPECT_I64(-2LL, stats.median_diff_uv, "negative centered-correction median");
  EXPECT_I64(0LL, stats.sd_uv, "negative centered-correction uses mathematical floor division");

  memset(valid, true, sizeof(valid));
  memset(hi, 0, sizeof(hi));
  memset(lo, 0, sizeof(lo));
  lo[0] = 5LL;
  lo[1] = 1LL;
  hi[2] = 2LL;
  hi[3] = 8LL;
  ml3_measurement_compute_uv_stats(hi, lo, valid, 4U, &stats);
  EXPECT_TRUE(stats.has_median_diff_uv, "mixed-sign even median is reported");
  EXPECT_I64(0LL, stats.median_diff_uv, "mixed-sign middle pair average truncates toward zero");
  EXPECT_I64(3LL, stats.mad_uv, "mixed-sign even MAD uses the corrected median helper");
  EXPECT_I64(4LL, stats.sd_uv, "mixed-sign even oracle keeps exact population SD");

  memset(valid, true, sizeof(valid));
  hi[0] = 0LL;
  hi[1] = 1LL;
  hi[2] = 2LL;
  hi[3] = 3LL;
  lo[0] = 3LL;
  lo[1] = 3LL;
  lo[2] = 3LL;
  lo[3] = 4LL;
  ml3_measurement_compute_uv_stats(hi, lo, valid, 4U, &stats);
  EXPECT_TRUE(stats.has_median_diff_uv, "negative-even median odd truncation");
  EXPECT_I64(-1LL,
    stats.median_diff_uv,
    "negative-even median");

  memset(valid, true, sizeof(valid));
  hi[0] = 998LL;
  hi[1] = 999LL;
  hi[2] = 900LL;
  hi[3] = 1005LL;
  lo[0] = 1000LL;
  lo[1] = 1000LL;
  lo[2] = 1000LL;
  lo[3] = 1000LL;
  ml3_measurement_compute_uv_stats(hi, lo, valid, 4U, &stats);
  EXPECT_TRUE(stats.has_median_diff_uv, "negative-even median contract uses trunc toward zero");
  EXPECT_I64(-1LL,
    stats.median_diff_uv,
    "negative-even median from odd negative midpoint");

  memset(valid, true, sizeof(valid));
  hi[0] = 2LL;
  hi[1] = 1LL;
  hi[2] = 0LL;
  hi[3] = 3LL;
  lo[0] = 4LL;
  lo[1] = 2LL;
  lo[2] = 2LL;
  lo[3] = 1LL;
  ml3_measurement_compute_uv_stats(hi, lo, valid, 4U, &stats);
  EXPECT_I64(-1LL, stats.median_diff_uv, "negative-even median odd trunc");

  mask_4cycle_hi[0] = 500000LL;
  mask_4cycle_hi[1] = 505000LL;
  mask_4cycle_hi[2] = 510000LL;
  mask_4cycle_hi[3] = 515000LL;
  mask_4cycle_lo[0] = 530000LL;
  mask_4cycle_lo[1] = 507000LL;
  mask_4cycle_lo[2] = 498000LL;
  mask_4cycle_lo[3] = 492000LL;
  for (i = 0U; i < 8U; ++i) {
    hi[i] = mask_4cycle_hi[i & 3U];
    lo[i] = mask_4cycle_lo[i & 3U];
    valid[i] = true;
  }
  ml3_measurement_compute_uv_stats(hi, lo, valid, 8U, &stats);
  EXPECT_I64(19841LL, stats.sd_uv, "8-cycle boundary sd");
  EXPECT_I64(12500LL, stats.mad_uv, "8-cycle boundary mad");

  memset(valid, true, sizeof(valid));
  for (i = 0U; i < 8U; ++i) {
    if ((i & 1U) == 0U) {
      hi[i] = adc_derived_max_uv;
      lo[i] = 0LL;
    } else {
      hi[i] = 0LL;
      lo[i] = adc_derived_max_uv;
    }
  }
  ml3_measurement_compute_uv_stats(hi, lo, valid, 8U, &stats);
  EXPECT_TRUE(stats.has_valid_cycle_count, "extreme-domain has valid cycles");
  EXPECT_U32(8U, (uint32_t)stats.valid_cycle_count, "extreme-domain count");
  EXPECT_TRUE(stats.has_mean_hi_uv, "extreme-domain mean high computed");
  EXPECT_TRUE(stats.has_mean_lo_uv, "extreme-domain mean low computed");
  EXPECT_TRUE(stats.has_common_mode_uv, "extreme-domain common mode computed");
  EXPECT_TRUE(stats.has_mean_diff_uv, "extreme-domain mean differential computed");
  EXPECT_TRUE(stats.has_median_diff_uv, "extreme-domain median computed");
  EXPECT_TRUE(stats.has_drift_uv, "extreme-domain drift computed");
  EXPECT_TRUE(stats.has_sd_uv, "extreme-domain sd computed");
  EXPECT_TRUE(stats.has_mad_uv, "extreme-domain mad computed");
  EXPECT_TRUE(stats.has_min_diff_uv, "extreme-domain minimum computed");
  EXPECT_TRUE(stats.has_max_diff_uv, "extreme-domain maximum computed");
  EXPECT_I64(2147483647LL, stats.mean_hi_uv, "extreme-domain mean high");
  EXPECT_I64(2147483647LL, stats.mean_lo_uv, "extreme-domain mean low");
  EXPECT_I64(2147483647LL, stats.common_mode_uv, "extreme-domain common mode");
  EXPECT_I64(0LL, stats.mean_diff_uv, "extreme-domain mean differential");
  EXPECT_I64(0LL, stats.median_diff_uv, "extreme-domain median differential");
  EXPECT_I64(-8589934590LL, stats.drift_uv, "extreme-domain drift");
  EXPECT_I64(adc_derived_max_uv, stats.sd_uv, "extreme-domain floor population sd");
  EXPECT_I64(adc_derived_max_uv, stats.mad_uv, "extreme-domain median absolute deviation");
  EXPECT_I64(-adc_derived_max_uv, stats.min_diff_uv, "extreme-domain minimum differential");
  EXPECT_I64(adc_derived_max_uv, stats.max_diff_uv, "extreme-domain maximum differential");

  hi[0] = adc_derived_max_uv;
  lo[0] = 0LL;
  hi[1] = 0LL;
  lo[1] = adc_derived_max_uv;
  hi[2] = 0LL;
  lo[2] = adc_derived_max_uv;
  ml3_measurement_compute_uv_stats(hi, lo, valid, 3U, &stats);
  EXPECT_TRUE(stats.has_sd_uv, "three-cycle individual-square overflow computes sd");
  EXPECT_I64(-1431655765LL, stats.mean_diff_uv, "three-cycle extreme mean differential");
  EXPECT_I64(-adc_derived_max_uv, stats.median_diff_uv, "three-cycle extreme median");
  EXPECT_I64(-8589934590LL, stats.drift_uv, "three-cycle extreme drift");
  EXPECT_I64(4049333999LL, stats.sd_uv, "three-cycle individual-square overflow floor sd");
  EXPECT_I64(0LL, stats.mad_uv, "three-cycle extreme MAD");

  for (i = 0U; i < 4U; ++i) {
    hi[i] = adc_derived_max_uv;
    lo[i] = 0LL;
  }
  for (i = 4U; i < 7U; ++i) {
    hi[i] = 0LL;
    lo[i] = adc_derived_max_uv;
  }
  hi[7] = 0LL;
  lo[7] = adc_derived_max_uv - 1LL;
  ml3_measurement_compute_uv_stats(hi, lo, valid, 8U, &stats);
  EXPECT_TRUE(stats.has_sd_uv, "nonzero-division-remainder extreme computes sd");
  EXPECT_I64(0LL, stats.mean_diff_uv, "nonzero-division-remainder mean truncates to zero");
  EXPECT_I64(4294967294LL, stats.sd_uv, "nonzero-division-remainder floor sd");
  EXPECT_I64(adc_derived_max_uv, stats.mad_uv, "nonzero-division-remainder MAD");

  memset(valid, true, sizeof(valid));
  memset(hi, 0, sizeof(hi));
  memset(lo, 0, sizeof(lo));
  ml3_measurement_compute_uv_stats(hi, lo, valid, 0U, &stats);
  EXPECT_FALSE(stats.has_mean_hi_uv, "zero count no mean");
  EXPECT_FALSE(stats.has_valid_cycle_count, "zero count invalid");
  EXPECT_U32(0U, (uint32_t)stats.valid_cycle_count, "zero count valid_cycle_count");

  memset(valid, true, sizeof(valid));
  memset(hi, 0, sizeof(hi));
  memset(lo, 0, sizeof(lo));
  ml3_measurement_compute_uv_stats(hi, lo, valid, 9U, &stats);
  EXPECT_FALSE(stats.has_mean_hi_uv, "reject count above 8");
  EXPECT_FALSE(stats.has_valid_cycle_count, "count above max invalid");
  EXPECT_U32(0U, (uint32_t)stats.valid_cycle_count, "count above max valid_cycle_count");

  hi[0] = -1LL;
  lo[0] = 1000LL;
  ml3_measurement_compute_uv_stats(hi, lo, valid, 1U, &stats);
  EXPECT_FALSE(stats.has_mean_hi_uv, "reject negative domain");
  EXPECT_FALSE(stats.has_valid_cycle_count, "negative domain invalid");
  EXPECT_U32(0U, (uint32_t)stats.valid_cycle_count, "negative domain valid_cycle_count");

  hi[0] = 1000LL;
  lo[0] = adc_derived_max_uv + 1LL;
  ml3_measurement_compute_uv_stats(hi, lo, valid, 1U, &stats);
  EXPECT_FALSE(stats.has_mean_hi_uv, "reject positive out-of-range domain");
  EXPECT_FALSE(stats.has_valid_cycle_count, "positive out-of-range domain invalid");
  EXPECT_U32(0U, (uint32_t)stats.valid_cycle_count, "positive out-of-range valid_cycle_count");

  hi[0] = -1LL;
  lo[0] = -1LL;
  ml3_measurement_compute_uv_stats(hi, lo, valid, 1U, &stats);
  EXPECT_FALSE(stats.has_valid_cycle_count, "dual negative domain invalid");
}

int main(void) {
  test_measurement_init_and_bounds();
  test_warmup_transitions_only_at_deadline();
  test_corrupted_report_callbacks_fail_safe();
  test_reset_cause_capture_order_and_replacement();
  test_adc_invalid_report_retains_exact_reset_cause();
  test_start_only_from_idle();
  test_error_and_unknown_states_force_safe_low();
  test_nominal_sequence_and_controls();
  test_internal_channels_are_fixed();
  test_channel_sequences_for_cycles();
  test_single_vref_pairing_and_order();
  test_watchdog_points();
  test_wrap_and_discharge_timer_behaviour();
  test_reprepare_each_cycle();
  test_abort_and_cleanup();
  test_adc_fault_indexing_matrix();
  test_discard_and_retained_overrun_paths_are_distinct();
  test_verify_discharge_internal_read_failure_is_clean_continue();
  test_adc_fault_continues_queue();
  test_adc_fault_cleanup_failure_blocks_reporting();
  test_adc_prepare_calibration_timeout_continues_with_cleanup();
  test_adc_prepare_ready_timeout_maps_to_init_fault();
  test_verify_pa4_high_high_low_discharge_gating();
  test_verify_discharge_threshold_straddle_uses_fresh_vdda();
  test_verify_discharge_threshold_straddle_keeps_higher_retained_vdda();
  test_verify_discharge_vref_timeout_continues_queue();
  test_callback_failures_fail_fast();
  test_prepare_initial_thermistor_low_failure_retries_safe_low();
  test_full_state_machine_abba_oracle();
  test_abba_raw_evidence_counts_and_excludes_discards();
  test_compute_stats_oracles();

  if (failure_count != 0U) {
    printf("FAILURES: %u\n", (unsigned)failure_count);
    return 1;
  }
  printf("OK\n");
  return 0;
}
