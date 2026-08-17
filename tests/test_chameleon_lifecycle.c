#include "chameleon_lsn50_hw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_EQ(actual, expected, label) do {                                \
    if ((actual) != (expected)) {                                              \
        fprintf(stderr, "FAIL %s: expected %lu got %lu (%s:%d)\n",           \
                (label), (unsigned long)(expected), (unsigned long)(actual),  \
                __FILE__, __LINE__);                                          \
        exit(1);                                                               \
    }                                                                          \
} while (0)

#define ASSERT_TRUE(value, label) do {                                        \
    if (!(value)) {                                                            \
        fprintf(stderr, "FAIL %s (%s:%d)\n", (label), __FILE__, __LINE__);   \
        exit(1);                                                               \
    }                                                                          \
} while (0)

#define ASSERT_STR(actual, expected, label) do {                               \
    if (strcmp((actual), (expected)) != 0) {                                   \
        fprintf(stderr, "FAIL %s:\nexpected %s\nactual   %s\n",            \
                (label), (expected), (actual));                               \
        exit(1);                                                               \
    }                                                                          \
} while (0)

typedef struct {
    char trace[4096];
    size_t length;
    uint32_t now_ms;
    uint32_t last_watchdog_ms;
    uint32_t max_watchdog_gap_ms;
    uint32_t probe_cost_ms;
    uint32_t ready_cost_ms;
    uint32_t measure_cost_ms;
    uint32_t extra_after_probe_ms;
    uint32_t extra_after_ready_ms;
    uint32_t extra_after_first_measure_ms;
    uint32_t ready_timeout_seen;
    uint32_t measure_timeout_seen;
    unsigned probe_calls;
    unsigned wait_ready_calls;
    unsigned measure_calls;
    unsigned bus_clear_calls;
    unsigned watchdog_calls;
    unsigned fail_probe_calls;
    int init_ok;
    chameleon_result_t first_probe;
    chameleon_result_t second_probe;
    chameleon_result_t first_ready;
    chameleon_result_t second_ready;
    chameleon_result_t first_measure;
    chameleon_result_t second_measure;
    uint8_t measure_flags;
    int probe_always_fails;
} fake_hw_t;

static void event(fake_hw_t *fake, const char *name)
{
    int written = snprintf(fake->trace + fake->length,
                           sizeof(fake->trace) - fake->length,
                           "%s%s", fake->length == 0U ? "" : ",", name);
    if (written > 0) {
        fake->length += (size_t)written;
    }
}

static void add_cost(fake_hw_t *fake, uint32_t cost_ms, uint32_t budget_ms)
{
    fake->now_ms += cost_ms > budget_ms ? budget_ms : cost_ms;
}

static void rail_off(void *context) { event(context, "off"); }
static void bus_isolate(void *context) { event(context, "isolate"); }
static void rail_on(void *context) { event(context, "on"); }
static int i2c_init(void *context)
{
    fake_hw_t *fake = context;
    event(fake, "init");
    return fake->init_ok;
}
static void i2c_deinit(void *context) { event(context, "deinit"); }
static void delay_ms(void *context, uint32_t ms)
{
    fake_hw_t *fake = context;
    event(fake, ms == CHAMELEON_POWER_STABILIZE_MS ? "stabilize" : "wait");
    fake->now_ms += ms;
}
static uint32_t millis(void *context) { return ((fake_hw_t *)context)->now_ms; }
static void watchdog_refresh(void *context)
{
    fake_hw_t *fake = context;
    uint32_t gap = fake->now_ms - fake->last_watchdog_ms;
    if (gap > fake->max_watchdog_gap_ms) {
        fake->max_watchdog_gap_ms = gap;
    }
    fake->last_watchdog_ms = fake->now_ms;
    fake->watchdog_calls++;
}
static chameleon_i2c_status_t bus_clear(void *context)
{
    fake_hw_t *fake = context;
    fake->bus_clear_calls++;
    event(fake, "clear");
    return CHAMELEON_I2C_OK;
}
static chameleon_result_t probe(void *context)
{
    fake_hw_t *fake = context;
    chameleon_result_t result = fake->probe_always_fails
        ? CHAMELEON_RESULT_NO_DEVICE
        : (fake->probe_calls < fake->fail_probe_calls
            ? CHAMELEON_RESULT_NO_DEVICE
            : (fake->probe_calls == 0U ? fake->first_probe : fake->second_probe));
    fake->probe_calls++;
    add_cost(fake, fake->probe_cost_ms, 50U);
    fake->now_ms += fake->extra_after_probe_ms;
    event(fake, "probe");
    return result;
}
static chameleon_result_t measure(void *context, chameleon_sample_t *sample,
                                  uint32_t timeout_ms)
{
    fake_hw_t *fake = context;
    chameleon_result_t result = fake->measure_calls == 0U
        ? fake->first_measure : fake->second_measure;
    fake->measure_timeout_seen = timeout_ms;
    fake->measure_calls++;
    add_cost(fake, fake->measure_cost_ms, timeout_ms);
    if (fake->measure_calls == 1U) {
        fake->now_ms += fake->extra_after_first_measure_ms;
    }
    sample->status_flags = fake->measure_flags;
    event(fake, "measure");
    return result;
}
static chameleon_result_t wait_ready(void *context, uint32_t timeout_ms)
{
    fake_hw_t *fake = context;
    chameleon_result_t result = fake->wait_ready_calls == 0U
        ? fake->first_ready : fake->second_ready;
    fake->ready_timeout_seen = timeout_ms;
    fake->wait_ready_calls++;
    add_cost(fake, fake->ready_cost_ms, timeout_ms);
    fake->now_ms += fake->extra_after_ready_ms;
    event(fake, "boot-ready");
    return result;
}
static uint16_t battery_mv(void *context)
{
    (void)context;
    return 3210U;
}

static chameleon_lsn50_ops_t make_ops(fake_hw_t *fake)
{
    chameleon_lsn50_ops_t ops;
    memset(&ops, 0, sizeof(ops));
    ops.context = fake;
    ops.rail_off = rail_off;
    ops.bus_isolate = bus_isolate;
    ops.rail_on = rail_on;
    ops.i2c_init = i2c_init;
    ops.i2c_deinit = i2c_deinit;
    ops.delay_ms = delay_ms;
    ops.millis = millis;
    ops.probe = probe;
    ops.wait_ready = wait_ready;
    ops.measure = measure;
    ops.bus_clear = bus_clear;
    ops.watchdog_refresh = watchdog_refresh;
    ops.battery_mv = battery_mv;
    return ops;
}

static fake_hw_t make_fake(void)
{
    fake_hw_t fake;
    memset(&fake, 0, sizeof(fake));
    fake.init_ok = 1;
    fake.first_probe = CHAMELEON_RESULT_OK;
    fake.second_probe = CHAMELEON_RESULT_OK;
    fake.first_ready = CHAMELEON_RESULT_OK;
    fake.second_ready = CHAMELEON_RESULT_OK;
    fake.first_measure = CHAMELEON_RESULT_OK;
    fake.second_measure = CHAMELEON_RESULT_OK;
    return fake;
}

static void test_success_starts_and_ends_with_idempotent_cleanup(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_OK, "success result");
    ASSERT_TRUE(strncmp(fake.trace, "deinit,isolate,off,on,stabilize,init",
                        strlen("deinit,isolate,off,on,stabilize,init")) == 0,
                "success begins with cleanup");
    ASSERT_TRUE(strcmp(fake.trace + fake.length - strlen("deinit,isolate,off"),
                       "deinit,isolate,off") == 0,
                "success ends with cleanup");
    ASSERT_EQ(fake.measure_calls, 1U, "one measurement");
    ASSERT_EQ(fake.bus_clear_calls, 0U, "success does not clear bus");
    ASSERT_EQ(chameleon_lsn50_last_attempts(), 1U, "one attempt reported");
    ASSERT_EQ(sample.battery_mv, 3210U, "battery retained");
}

static void test_init_failure_uses_complete_cleanup(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.init_ok = 0;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_I2C_INIT_FAILED, "init failure result");
    ASSERT_TRUE(strstr(fake.trace, "init,deinit,isolate,off") != 0,
                "init failure complete cleanup");
}

static void test_transport_failures_clear_once_before_cleanup(void)
{
    static const chameleon_result_t failures[] = {
        CHAMELEON_RESULT_NO_DEVICE,
        CHAMELEON_RESULT_TRIGGER_FAILED,
        CHAMELEON_RESULT_STATUS_IO_FAILED,
        CHAMELEON_RESULT_READ_FAILED,
        CHAMELEON_RESULT_PARTIAL_SAMPLE
    };
    size_t i;

    for (i = 0U; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        fake_hw_t fake = make_fake();
        chameleon_lsn50_ops_t ops = make_ops(&fake);
        chameleon_sample_t sample;
        if (failures[i] == CHAMELEON_RESULT_NO_DEVICE) {
            fake.fail_probe_calls = 9U;
        } else {
            fake.first_measure = failures[i];
        }
        ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
                  CHAMELEON_RESULT_OK, "transport failure retry succeeds");
        ASSERT_EQ(fake.bus_clear_calls, 1U, "one first-session bus clear");
        ASSERT_TRUE(strstr(fake.trace, "clear,deinit,isolate,off") != 0,
                    "clear before cleanup");
    }
}

static void test_readiness_timeout_does_not_clear_bus(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.first_ready = CHAMELEON_RESULT_MEASUREMENT_TIMEOUT;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_OK, "readiness timeout retry succeeds");
    ASSERT_EQ(fake.bus_clear_calls, 0U, "readiness timeout no bus clear");
}

static void test_probe_window_is_four_hundred_ms_with_fifty_ms_intervals(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.probe_always_fails = 1;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_NO_DEVICE, "missing reader result");
    ASSERT_EQ(fake.probe_calls, 16U, "eight probes per session");
    ASSERT_EQ(fake.now_ms, 2000U, "two 400ms windows plus cold off");
}

static void test_cold_retry_is_exactly_one_second_when_budget_allows(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.first_measure = CHAMELEON_RESULT_STATUS_IO_FAILED;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_OK, "retry succeeds");
    ASSERT_TRUE(strstr(fake.trace, "off,wait,deinit,isolate,off,on") != 0,
                "cold off delay sequence");
    ASSERT_EQ(fake.now_ms, 1200U, "two startups plus exact cold off");
    ASSERT_EQ(chameleon_lsn50_last_attempts(), 2U, "one retry only");
}

static void test_retry_requires_six_point_one_five_seconds_remaining(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.first_measure = CHAMELEON_RESULT_STATUS_IO_FAILED;
    fake.ready_cost_ms = 6000U;
    fake.measure_cost_ms = 6000U;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 6000U),
              CHAMELEON_RESULT_STATUS_IO_FAILED, "retry skipped at reserve boundary");
    ASSERT_EQ(chameleon_lsn50_last_attempts(), 1U, "no second session");
    ASSERT_EQ(fake.now_ms, 11500U, "first session consumes bounded costs");
}

static void test_ready_and_measure_reserves_prevent_late_calls(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.probe_cost_ms = 50U;
    fake.ready_cost_ms = 2000U;
    fake.measure_cost_ms = 2000U;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_OK, "bounded normal operation");
    ASSERT_TRUE(fake.ready_timeout_seen <= 2000U, "ready timeout bounded");
    ASSERT_TRUE(fake.measure_timeout_seen <= 2000U, "measure timeout bounded");
    ASSERT_TRUE(fake.now_ms < CHAMELEON_ACQUIRE_TIMEOUT_MS,
                "normal operation below global cap");
}

static void test_ready_reserve_skips_call_with_fifty_ms_or_less_remaining(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.extra_after_probe_ms = 11850U;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_MEASUREMENT_TIMEOUT, "ready reserve timeout");
    ASSERT_EQ(fake.wait_ready_calls, 0U, "ready not started without reserve");
    ASSERT_EQ(fake.measure_calls, 0U, "measure not started after ready reserve");
}

static void test_measure_reserve_skips_call_with_five_hundred_ms_or_less_remaining(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.extra_after_ready_ms = 11400U;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_MEASUREMENT_TIMEOUT, "measure reserve timeout");
    ASSERT_EQ(fake.wait_ready_calls, 1U, "ready completed before reserve");
    ASSERT_EQ(fake.measure_calls, 0U, "measure not started without reserve");
}

static void test_retry_runs_at_exact_six_point_one_five_second_reserve(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.first_measure = CHAMELEON_RESULT_STATUS_IO_FAILED;
    fake.extra_after_first_measure_ms = 5750U;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_OK, "retry allowed at exact reserve");
    ASSERT_EQ(chameleon_lsn50_last_attempts(), 2U, "second session at boundary");
}

static void test_watchdog_delay_slices_are_no_more_than_one_second(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.first_measure = CHAMELEON_RESULT_STATUS_IO_FAILED;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_OK, "retry after bounded delay");
    ASSERT_TRUE(fake.max_watchdog_gap_ms <= CHAMELEON_WATCHDOG_SLICE_MS,
                "watchdog gap bounded by delay slice");
    ASSERT_TRUE(fake.watchdog_calls > 0U, "watchdog serviced");
}

static void test_watchdog_is_refreshed_around_bounded_opaque_calls(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.probe_cost_ms = 50U;
    fake.ready_cost_ms = 2000U;
    fake.measure_cost_ms = 2000U;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_OK, "bounded opaque calls succeed");
    ASSERT_TRUE(fake.max_watchdog_gap_ms <= 5000U,
                "opaque calls remain inside watchdog limit");
}

static void test_valid_sentinel_sample_does_not_retry(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.measure_flags = CHAMELEON_FLAG_CH2_OPEN;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_OK, "sentinel sample valid");
    ASSERT_EQ(chameleon_lsn50_last_attempts(), 1U, "sentinel no retry");
}

int main(void)
{
    test_success_starts_and_ends_with_idempotent_cleanup();
    test_init_failure_uses_complete_cleanup();
    test_transport_failures_clear_once_before_cleanup();
    test_readiness_timeout_does_not_clear_bus();
    test_probe_window_is_four_hundred_ms_with_fifty_ms_intervals();
    test_cold_retry_is_exactly_one_second_when_budget_allows();
    test_retry_requires_six_point_one_five_seconds_remaining();
    test_ready_and_measure_reserves_prevent_late_calls();
    test_ready_reserve_skips_call_with_fifty_ms_or_less_remaining();
    test_measure_reserve_skips_call_with_five_hundred_ms_or_less_remaining();
    test_retry_runs_at_exact_six_point_one_five_second_reserve();
    test_watchdog_delay_slices_are_no_more_than_one_second();
    test_watchdog_is_refreshed_around_bounded_opaque_calls();
    test_valid_sentinel_sample_does_not_retry();
    puts("test_chameleon_lifecycle OK");
    return 0;
}
