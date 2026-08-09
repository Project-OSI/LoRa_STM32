#include "chameleon_lsn50_hw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_EQ(actual, expected, label) do {                                 \
    if ((actual) != (expected)) {                                               \
        fprintf(stderr, "FAIL %s: expected %d got %d (%s:%d)\n",              \
                (label), (int)(expected), (int)(actual), __FILE__, __LINE__);   \
        exit(1);                                                                \
    }                                                                           \
} while (0)

#define ASSERT_STR(actual, expected, label) do {                                \
    if (strcmp((actual), (expected)) != 0) {                                    \
        fprintf(stderr, "FAIL %s:\nexpected %s\nactual   %s\n",              \
                (label), (expected), (actual));                                 \
        exit(1);                                                                \
    }                                                                           \
} while (0)

typedef struct {
    char trace[2048];
    size_t length;
    uint32_t now_ms;
    unsigned probe_calls;
    unsigned measure_calls;
    int init_ok;
    chameleon_result_t first_probe;
    chameleon_result_t second_probe;
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
static uint32_t millis(void *context)
{
    fake_hw_t *fake = context;
    return fake->now_ms;
}
static chameleon_result_t probe(void *context)
{
    fake_hw_t *fake = context;
    chameleon_result_t result = fake->probe_always_fails
        ? CHAMELEON_RESULT_NO_DEVICE
        : (fake->probe_calls == 0U ? fake->first_probe : fake->second_probe);
    fake->probe_calls++;
    event(fake, "probe");
    return result;
}
static chameleon_result_t measure(void *context, chameleon_sample_t *sample,
                                  uint32_t timeout_ms)
{
    fake_hw_t *fake = context;
    chameleon_result_t result = fake->measure_calls == 0U
        ? fake->first_measure : fake->second_measure;
    (void)sample;
    (void)timeout_ms;
    fake->measure_calls++;
    sample->status_flags = fake->measure_flags;
    event(fake, "measure");
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
    ops.measure = measure;
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
    fake.first_measure = CHAMELEON_RESULT_OK;
    fake.second_measure = CHAMELEON_RESULT_OK;
    return fake;
}

static void test_success_has_one_session_and_cleanup(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_OK, "success result");
    ASSERT_STR(fake.trace,
               "off,isolate,on,stabilize,init,probe,measure,deinit,isolate,off",
               "success lifecycle");
    ASSERT_EQ(fake.measure_calls, 1U, "one measurement");
    ASSERT_EQ(chameleon_lsn50_last_attempts(), 1U, "one attempt reported");
    ASSERT_EQ(sample.battery_mv, 3210U, "battery retained");
}

static void test_transport_failure_gets_one_cold_retry(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.first_measure = CHAMELEON_RESULT_STATUS_IO_FAILED;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_OK, "retry succeeds");
    ASSERT_STR(fake.trace,
               "off,isolate,on,stabilize,init,probe,measure,deinit,isolate,off,wait,off,isolate,on,stabilize,init,probe,measure,deinit,isolate,off",
               "cold retry lifecycle");
    ASSERT_EQ(fake.measure_calls, 2U, "at most two measurements");
    ASSERT_EQ(chameleon_lsn50_last_attempts(), 2U, "retry count reported");
}

static void test_startup_probe_waits_for_delayed_ack(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.first_probe = CHAMELEON_RESULT_NO_DEVICE;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_OK, "delayed ACK succeeds");
    ASSERT_STR(fake.trace,
               "off,isolate,on,stabilize,init,probe,wait,probe,measure,deinit,isolate,off",
               "bounded startup probing");
    ASSERT_EQ(fake.probe_calls, 2U, "probe repeated after interval");
}

static void test_second_failure_still_cleans_up(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.first_measure = CHAMELEON_RESULT_READ_FAILED;
    fake.second_measure = CHAMELEON_RESULT_READ_FAILED;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_READ_FAILED, "second failure returned");
    ASSERT_EQ(fake.measure_calls, 2U, "retry bounded");
    ASSERT_STR(fake.trace + fake.length - strlen("deinit,isolate,off"),
               "deinit,isolate,off", "failure cleanup suffix");
}

static void test_startup_probe_timeout_is_bounded_and_retried_once(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.probe_always_fails = 1;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_NO_DEVICE, "startup timeout result");
    ASSERT_EQ(fake.probe_calls, 60U, "30 bounded probes per session");
    ASSERT_EQ(fake.measure_calls, 0U, "missing reader is not measured");
    ASSERT_EQ(fake.now_ms, 3250U, "two 1.5 second probe windows");
    ASSERT_EQ(chameleon_lsn50_last_attempts(), 2U,
              "startup timeout retry count");
    ASSERT_STR(fake.trace + fake.length - strlen("deinit,isolate,off"),
               "deinit,isolate,off", "startup timeout cleanup suffix");
}

static void test_every_measurement_failure_gets_one_cold_retry(void)
{
    static const chameleon_result_t failures[] = {
        CHAMELEON_RESULT_TRIGGER_FAILED,
        CHAMELEON_RESULT_STATUS_IO_FAILED,
        CHAMELEON_RESULT_MEASUREMENT_TIMEOUT,
        CHAMELEON_RESULT_READ_FAILED,
        CHAMELEON_RESULT_PARTIAL_SAMPLE
    };
    size_t i;

    for (i = 0U; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        fake_hw_t fake = make_fake();
        chameleon_lsn50_ops_t ops = make_ops(&fake);
        chameleon_sample_t sample;
        fake.first_measure = failures[i];

        ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
                  CHAMELEON_RESULT_OK, "recoverable failure retries");
        ASSERT_EQ(fake.measure_calls, 2U, "failure uses two sessions");
        ASSERT_EQ(chameleon_lsn50_last_attempts(), 2U,
                  "failure attempt count reported");
    }
}

static void test_result_names_are_exact(void)
{
    ASSERT_STR(chameleon_result_name(CHAMELEON_RESULT_OK), "ok", "ok name");
    ASSERT_STR(chameleon_result_name(CHAMELEON_RESULT_I2C_INIT_FAILED),
               "i2c_init_failed", "init name");
    ASSERT_STR(chameleon_result_name(CHAMELEON_RESULT_NO_DEVICE),
               "no_device", "missing name");
    ASSERT_STR(chameleon_result_name(CHAMELEON_RESULT_TRIGGER_FAILED),
               "trigger_failed", "trigger name");
    ASSERT_STR(chameleon_result_name(CHAMELEON_RESULT_STATUS_IO_FAILED),
               "status_io_failed", "status name");
    ASSERT_STR(chameleon_result_name(CHAMELEON_RESULT_MEASUREMENT_TIMEOUT),
               "measurement_timeout", "timeout name");
    ASSERT_STR(chameleon_result_name(CHAMELEON_RESULT_READ_FAILED),
               "read_failed", "read name");
    ASSERT_STR(chameleon_result_name(CHAMELEON_RESULT_PARTIAL_SAMPLE),
               "partial_sample", "partial name");
}

static void test_sentinel_flags_do_not_retry(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.measure_flags = CHAMELEON_FLAG_CH2_OPEN;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_OK, "sentinel sample valid");
    ASSERT_EQ(fake.measure_calls, 1U, "sentinel does not retry");
    ASSERT_EQ(chameleon_lsn50_last_attempts(), 1U,
              "sentinel attempt count reported");
}

static void test_i2c_init_failure_cleans_and_retries_once(void)
{
    fake_hw_t fake = make_fake();
    chameleon_lsn50_ops_t ops = make_ops(&fake);
    chameleon_sample_t sample;
    fake.init_ok = 0;

    ASSERT_EQ(chameleon_lsn50_run(&ops, &sample, 2000U),
              CHAMELEON_RESULT_I2C_INIT_FAILED, "init failure result");
    ASSERT_STR(fake.trace,
               "off,isolate,on,stabilize,init,isolate,off,wait,off,isolate,on,stabilize,init,isolate,off",
               "init failure cleanup");
}

int main(void)
{
    test_success_has_one_session_and_cleanup();
    test_transport_failure_gets_one_cold_retry();
    test_startup_probe_waits_for_delayed_ack();
    test_second_failure_still_cleans_up();
    test_startup_probe_timeout_is_bounded_and_retried_once();
    test_every_measurement_failure_gets_one_cold_retry();
    test_result_names_are_exact();
    test_sentinel_flags_do_not_retry();
    test_i2c_init_failure_cleans_and_retries_once();
    printf("test_chameleon_lifecycle OK\n");
    return 0;
}
