#include "via_chameleon.h"
#include "mock_chameleon_i2c.h"
#include <stdio.h>
#include <stdlib.h>

#define ASSERT_EQ_U32(actual, expected, label) do {                             \
    if ((uint32_t)(actual) != (uint32_t)(expected)) {                           \
        fprintf(stderr, "FAIL %s: expected %u got %u (%s:%d)\n",                \
                (label), (unsigned)(expected), (unsigned)(actual),              \
                __FILE__, __LINE__);                                            \
        exit(1);                                                                \
    }                                                                           \
} while (0)

#define ASSERT_TRUE(cond, label) do {                                           \
    if (!(cond)) {                                                              \
        fprintf(stderr, "FAIL %s (%s:%d)\n", (label), __FILE__, __LINE__);      \
        exit(1);                                                                \
    }                                                                           \
} while (0)

static void test_happy_path(void) {
    mock_chameleon_reset();
    chameleon_sample_t s;
    int ok = via_chameleon_acquire(&s, CHAMELEON_DEFAULT_TIMEOUT_MS);
    ASSERT_TRUE(ok, "acquire ok");
    ASSERT_EQ_U32(s.status_flags, 0,        "no flags");
    ASSERT_EQ_U32(s.soil_temp_c_x100, 1987, "temp");
    ASSERT_EQ_U32(s.r1_ohm_comp, 1100,      "r1");
    ASSERT_EQ_U32(s.r2_ohm_comp, 10100,     "r2");
    ASSERT_EQ_U32(s.r3_ohm_comp, 101200,    "r3");
    ASSERT_EQ_U32(s.r1_ohm_raw,  1200,      "raw r1");
    ASSERT_EQ_U32(s.r2_ohm_raw,  10200,     "raw r2");
    ASSERT_EQ_U32(s.r3_ohm_raw,  102200,    "raw r3");
    ASSERT_EQ_U32(s.battery_mv, 3300,       "battery");
    ASSERT_EQ_U32(s.array_id[0], 0x28,      "id 0");
    ASSERT_EQ_U32(s.array_id[7], 0xF1,      "id 7");
    ASSERT_EQ_U32(mock_chameleon_trigger_count(), 1, "one trigger");
}

static void test_device_missing(void) {
    mock_chameleon_reset();
    mock_chameleon_set_present(0);
    chameleon_sample_t s;
    int ok = via_chameleon_acquire(&s, CHAMELEON_DEFAULT_TIMEOUT_MS);
    ASSERT_EQ_U32(ok, 0, "acquire reports missing");
    ASSERT_TRUE(s.status_flags & CHAMELEON_FLAG_I2C_MISSING, "i2c missing flag");
    ASSERT_EQ_U32(s.r1_ohm_comp, 0, "r1 zeroed");
    ASSERT_EQ_U32(s.r1_ohm_raw, 0, "raw r1 zeroed");
}

static void test_status_polled_until_ready(void) {
    mock_chameleon_reset();
    mock_chameleon_set_status_ready_after_polls(3);
    chameleon_sample_t s;
    int ok = via_chameleon_acquire(&s, CHAMELEON_DEFAULT_TIMEOUT_MS);
    ASSERT_TRUE(ok, "acquire ok");
    ASSERT_EQ_U32(s.status_flags, 0, "no flags");
    ASSERT_EQ_U32(mock_chameleon_status_poll_count(), 4, "4 polls");
    ASSERT_TRUE(mock_chameleon_total_delay_ms() >= 150, "delay >= 150 ms");
}

static void test_status_timeout(void) {
    mock_chameleon_reset();
    mock_chameleon_set_status_after_trigger(0x00);
    chameleon_sample_t s;
    int ok = via_chameleon_acquire(&s, 200);
    ASSERT_EQ_U32(ok, 1, "device present, partial data ok");
    ASSERT_TRUE(s.status_flags & CHAMELEON_FLAG_TIMEOUT, "timeout flag");
    ASSERT_EQ_U32(mock_chameleon_total_delay_ms(), 200U, "absolute deadline");
}

static void test_status_transport_failure_is_not_busy_timeout(void) {
    mock_chameleon_reset();
    mock_chameleon_fail_command(CHAMELEON_CMD_STATUS);
    ASSERT_EQ_U32(via_chameleon_trigger(), CHAMELEON_RESULT_OK, "trigger ok");
    ASSERT_EQ_U32(via_chameleon_wait_ready(200U),
                  CHAMELEON_RESULT_STATUS_IO_FAILED,
                  "status transport result");
    ASSERT_EQ_U32(mock_chameleon_total_delay_ms(), 0U, "transport fails promptly");
}

static void test_register_read_failure_sets_fault_flag(void) {
    mock_chameleon_reset();
    mock_chameleon_fail_command(CHAMELEON_CMD_RES_RAW2);
    chameleon_sample_t s;
    int ok = via_chameleon_acquire(&s, CHAMELEON_DEFAULT_TIMEOUT_MS);
    ASSERT_EQ_U32(ok, 1, "device present, partial read failure ok");
    ASSERT_TRUE(s.status_flags & CHAMELEON_FLAG_I2C_MISSING, "read failure flag");
}

static void test_short_read_is_partial_sample(void) {
    mock_chameleon_reset();
    mock_chameleon_short_command(CHAMELEON_CMD_RES_RAW2);
    chameleon_sample_t s;
    int ok = via_chameleon_acquire(&s, CHAMELEON_DEFAULT_TIMEOUT_MS);
    ASSERT_EQ_U32(ok, 1, "device present after short read");
    ASSERT_TRUE(s.status_flags & CHAMELEON_FLAG_I2C_MISSING,
                "short read invalidates sample");
    ASSERT_EQ_U32(s.r2_ohm_raw, 0U, "short field zeroed");
}

static void test_sentinel_temperature(void) {
    mock_chameleon_reset();
    mock_chameleon_set_temp_x100(CHAMELEON_TEMP_SENTINEL_X100);
    chameleon_sample_t s;
    via_chameleon_acquire(&s, CHAMELEON_DEFAULT_TIMEOUT_MS);
    ASSERT_TRUE(s.status_flags & CHAMELEON_FLAG_TEMP_FAULT, "temp fault flag");
}

static void test_sentinel_id(void) {
    mock_chameleon_reset();
    uint8_t ff[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    mock_chameleon_set_id(ff);
    chameleon_sample_t s;
    via_chameleon_acquire(&s, CHAMELEON_DEFAULT_TIMEOUT_MS);
    ASSERT_TRUE(s.status_flags & CHAMELEON_FLAG_ID_FAULT, "id fault flag");
}

static void test_sentinel_open_channel(void) {
    mock_chameleon_reset();
    mock_chameleon_set_resistance(0, CHAMELEON_RES_OPEN_OHMS);
    mock_chameleon_set_resistance(2, CHAMELEON_RES_OPEN_OHMS);
    chameleon_sample_t s;
    via_chameleon_acquire(&s, CHAMELEON_DEFAULT_TIMEOUT_MS);
    ASSERT_TRUE(s.status_flags & CHAMELEON_FLAG_CH1_OPEN, "ch1 open");
    ASSERT_TRUE(!(s.status_flags & CHAMELEON_FLAG_CH2_OPEN), "ch2 ok");
    ASSERT_TRUE(s.status_flags & CHAMELEON_FLAG_CH3_OPEN, "ch3 open");
}

int main(void) {
    test_happy_path();
    test_device_missing();
    test_status_polled_until_ready();
    test_status_timeout();
    test_status_transport_failure_is_not_busy_timeout();
    test_register_read_failure_sets_fault_flag();
    test_short_read_is_partial_sample();
    test_sentinel_temperature();
    test_sentinel_id();
    test_sentinel_open_channel();
    printf("test_chameleon_driver OK\n");
    return 0;
}
