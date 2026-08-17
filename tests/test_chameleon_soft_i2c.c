#include "chameleon_soft_i2c.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EDGES 4096U
#define MAX_SAMPLES 512U

#define ASSERT_TRUE(value, label) do {                                         \
    if (!(value)) {                                                             \
        fprintf(stderr, "FAIL %s (%s:%d)\n", (label), __FILE__, __LINE__);   \
        exit(1);                                                                \
    }                                                                           \
} while (0)

#define ASSERT_EQ(actual, expected, label) do {                                \
    if ((actual) != (expected)) {                                               \
        fprintf(stderr, "FAIL %s: expected %lu got %lu (%s:%d)\n",          \
                (label), (unsigned long)(expected), (unsigned long)(actual),   \
                __FILE__, __LINE__);                                           \
        exit(1);                                                                \
    }                                                                           \
} while (0)

typedef struct {
    uint32_t now_us;
    uint32_t delay_scale;
    uint32_t scl_stretch_until;
    int scl_stretch_active;
    int master_scl_low;
    int master_sda_low;
    int slave_sda_low;
    int scl_stuck_low;
    int previous_scl;
    int previous_sda;
    size_t sda_low_calls;
    size_t sda_release_calls;
    uint8_t samples[MAX_SAMPLES];
    size_t sample_count;
    size_t sample_index;
    uint8_t sda_on_rise[MAX_EDGES];
    uint32_t scl_rise_at[MAX_EDGES];
    uint32_t scl_fall_at[MAX_EDGES];
    size_t scl_rise_count;
    size_t scl_fall_count;
    uint32_t start_at[MAX_EDGES];
    uint32_t stop_at[MAX_EDGES];
    size_t start_count;
    size_t stop_count;
} fake_bus_t;

static int physical_scl(const fake_bus_t *fake)
{
    return !fake->master_scl_low && !fake->scl_stuck_low
        && (!fake->scl_stretch_active
            || (int32_t)(fake->now_us - fake->scl_stretch_until) >= 0);
}

static int physical_sda(const fake_bus_t *fake)
{
    return !fake->master_sda_low && !fake->slave_sda_low;
}

static void observe_lines(fake_bus_t *fake)
{
    int scl = physical_scl(fake);
    int sda = physical_sda(fake);

    if (!fake->previous_scl && scl) {
        ASSERT_TRUE(fake->scl_rise_count < MAX_EDGES, "rise capacity");
        fake->scl_rise_at[fake->scl_rise_count] = fake->now_us;
        fake->sda_on_rise[fake->scl_rise_count++] = (uint8_t)sda;
    }
    if (fake->previous_scl && !scl) {
        ASSERT_TRUE(fake->scl_fall_count < MAX_EDGES, "fall capacity");
        fake->scl_fall_at[fake->scl_fall_count++] = fake->now_us;
    }
    if (fake->previous_scl && !fake->previous_sda && sda) {
        ASSERT_TRUE(fake->stop_count < MAX_EDGES, "stop capacity");
        fake->stop_at[fake->stop_count++] = fake->now_us;
    }
    if (fake->previous_scl && fake->previous_sda && !sda) {
        ASSERT_TRUE(fake->start_count < MAX_EDGES, "start capacity");
        fake->start_at[fake->start_count++] = fake->now_us;
    }
    fake->previous_scl = scl;
    fake->previous_sda = sda;
}

static void scl_low(void *context)
{
    fake_bus_t *fake = context;
    fake->master_scl_low = 1;
    observe_lines(fake);
}

static void scl_release(void *context)
{
    fake_bus_t *fake = context;
    fake->master_scl_low = 0;
    observe_lines(fake);
}

static int scl_read(void *context)
{
    fake_bus_t *fake = context;
    observe_lines(fake);
    return physical_scl(fake);
}

static void sda_low(void *context)
{
    fake_bus_t *fake = context;
    fake->master_sda_low = 1;
    ++fake->sda_low_calls;
    observe_lines(fake);
}

static void sda_release(void *context)
{
    fake_bus_t *fake = context;
    fake->master_sda_low = 0;
    ++fake->sda_release_calls;
    observe_lines(fake);
}

static int sda_read(void *context)
{
    fake_bus_t *fake = context;
    int value;

    if (fake->sample_index < fake->sample_count) {
        value = fake->samples[fake->sample_index++] != 0U;
    } else {
        value = physical_sda(fake);
    }
    return value;
}

static void delay_us(void *context, uint32_t us)
{
    fake_bus_t *fake = context;
    uint32_t scale = fake->delay_scale == 0U ? 1U : fake->delay_scale;
    fake->now_us += us * scale;
    observe_lines(fake);
}

static uint32_t micros(void *context)
{
    return ((fake_bus_t *)context)->now_us;
}

static chameleon_soft_i2c_ops_t make_ops(fake_bus_t *fake)
{
    chameleon_soft_i2c_ops_t ops;

    memset(&ops, 0, sizeof(ops));
    ops.context = fake;
    ops.scl_low = scl_low;
    ops.scl_release = scl_release;
    ops.scl_read = scl_read;
    ops.sda_low = sda_low;
    ops.sda_release = sda_release;
    ops.sda_read = sda_read;
    ops.delay_us = delay_us;
    ops.micros = micros;
    return ops;
}

static void load_samples(fake_bus_t *fake, const uint8_t *samples, size_t count)
{
    ASSERT_TRUE(count <= MAX_SAMPLES, "sample capacity");
    memcpy(fake->samples, samples, count);
    fake->sample_count = count;
}

static void initialize(fake_bus_t *fake, chameleon_soft_i2c_t *bus)
{
    chameleon_soft_i2c_ops_t ops;

    memset(fake, 0, sizeof(*fake));
    fake->previous_scl = 1;
    fake->previous_sda = 1;
    ops = make_ops(fake);
    ASSERT_EQ(chameleon_soft_i2c_init(bus, &ops), 1U, "init");
}

static void assert_released(const fake_bus_t *fake)
{
    ASSERT_EQ(fake->master_scl_low, 0U, "SCL master released");
    ASSERT_EQ(fake->master_sda_low, 0U, "SDA master released");
}

static void assert_rise_sequence(const fake_bus_t *fake, const uint8_t *bits,
                                 size_t bit_count, const char *label)
{
    size_t first;
    size_t bit;

    for (first = 0U; first + bit_count <= fake->scl_rise_count; ++first) {
        for (bit = 0U; bit < bit_count; ++bit) {
            if (fake->sda_on_rise[first + bit] != bits[bit]) {
                break;
            }
        }
        if (bit == bit_count) {
            return;
        }
    }
    ASSERT_TRUE(0, label);
}

static void test_probe_sends_shifted_write_address(void)
{
    fake_bus_t fake;
    chameleon_soft_i2c_t bus;
    static const uint8_t samples[] = { 1U, 1U, 0U };
    static const uint8_t address[] = { 0U, 0U, 0U, 1U, 0U, 0U, 0U, 0U };

    initialize(&fake, &bus);
    load_samples(&fake, samples, sizeof(samples));
    ASSERT_EQ(chameleon_soft_i2c_write(&bus, 0x08U, NULL, 0U),
              CHAMELEON_I2C_OK, "probe");
    assert_rise_sequence(&fake, address, sizeof(address), "shifted 0x10 address");
    assert_released(&fake);
}

static void test_address_nack_is_reported_and_lines_release(void)
{
    fake_bus_t fake;
    chameleon_soft_i2c_t bus;
    static const uint8_t samples[] = { 1U, 1U, 1U };

    initialize(&fake, &bus);
    load_samples(&fake, samples, sizeof(samples));
    ASSERT_EQ(chameleon_soft_i2c_write(&bus, 0x08U, NULL, 0U),
              CHAMELEON_I2C_ERR_NACK, "address NACK");
    ASSERT_EQ(fake.stop_count, 1U, "STOP after NACK");
    assert_released(&fake);
}

static void test_write_is_msb_first(void)
{
    fake_bus_t fake;
    chameleon_soft_i2c_t bus;
    uint8_t byte = 0xa5U;
    static const uint8_t samples[] = { 1U, 1U, 0U, 1U, 1U, 1U, 1U, 0U };
    static const uint8_t expected[] = { 1U, 0U, 1U, 0U, 0U, 1U, 0U, 1U };

    initialize(&fake, &bus);
    load_samples(&fake, samples, sizeof(samples));
    ASSERT_EQ(chameleon_soft_i2c_write(&bus, 0x08U, &byte, 1U),
              CHAMELEON_I2C_OK, "write");
    assert_rise_sequence(&fake, expected, sizeof(expected), "MSB-first 0xa5");
}

static void test_write_read_uses_repeated_start(void)
{
    fake_bus_t fake;
    chameleon_soft_i2c_t bus;
    uint8_t command = 0x30U;
    uint8_t result[2] = { 0U, 0U };
    static const uint8_t samples[] = {
        1U, 1U, 0U, 1U, 1U, 0U, 1U, 1U, 0U,
        1U, 0U, 1U, 0U, 0U, 1U, 0U, 1U,
        0U, 0U, 1U, 1U, 1U, 1U, 0U, 0U, 1U
    };

    initialize(&fake, &bus);
    load_samples(&fake, samples, sizeof(samples));
    ASSERT_EQ(chameleon_soft_i2c_write_read(&bus, 0x08U, &command, 1U,
                                             result, sizeof(result)),
              CHAMELEON_I2C_OK, "write/read");
    ASSERT_EQ(fake.start_count, 2U, "START plus repeated START");
    ASSERT_EQ(fake.stop_count, 1U, "one final STOP");
    ASSERT_EQ(result[0], 0xa5U, "first read byte");
    ASSERT_EQ(result[1], 0x3cU, "second read byte");
}

static void test_read_acks_intermediate_and_nacks_final_byte(void)
{
    fake_bus_t fake;
    chameleon_soft_i2c_t bus;
    uint8_t command = 0x30U;
    uint8_t result[2];
    static const uint8_t samples[] = {
        1U, 1U, 0U, 1U, 1U, 0U, 1U, 1U, 0U,
        1U, 0U, 1U, 0U, 0U, 1U, 0U, 1U,
        0U, 0U, 1U, 1U, 1U, 1U, 0U, 0U, 1U
    };

    initialize(&fake, &bus);
    load_samples(&fake, samples, sizeof(samples));
    ASSERT_EQ(chameleon_soft_i2c_write_read(&bus, 0x08U, &command, 1U,
                                             result, sizeof(result)),
              CHAMELEON_I2C_OK, "read ACK/NACK");
    ASSERT_EQ(fake.sda_on_rise[fake.scl_rise_count - 11U], 0U,
              "ACK after first read");
    ASSERT_EQ(fake.sda_on_rise[fake.scl_rise_count - 2U], 1U,
              "NACK after final read");
}

static void test_clock_stretch_completes_before_two_ms(void)
{
    fake_bus_t fake;
    chameleon_soft_i2c_t bus;
    static const uint8_t samples[] = { 1U, 1U, 0U };

    initialize(&fake, &bus);
    fake.scl_stretch_active = 1;
    fake.scl_stretch_until = 100U;
    load_samples(&fake, samples, sizeof(samples));
    ASSERT_EQ(chameleon_soft_i2c_write(&bus, 0x08U, NULL, 0U),
              CHAMELEON_I2C_OK, "stretch succeeds");
    ASSERT_TRUE(fake.now_us < CHAMELEON_SOFT_I2C_SCL_HIGH_TIMEOUT_US,
                "stretch bounded");
}

static void test_scl_held_low_times_out_and_releases_lines(void)
{
    fake_bus_t fake;
    chameleon_soft_i2c_t bus;
    static const uint8_t samples[] = { 1U };

    initialize(&fake, &bus);
    fake.scl_stuck_low = 1;
    load_samples(&fake, samples, sizeof(samples));
    ASSERT_EQ(chameleon_soft_i2c_write(&bus, 0x08U, NULL, 0U),
              CHAMELEON_I2C_ERR_TIMEOUT, "SCL timeout");
    ASSERT_TRUE(fake.now_us >= CHAMELEON_SOFT_I2C_SCL_HIGH_TIMEOUT_US,
                "timeout polled");
    assert_released(&fake);
}

static void test_sda_held_low_is_bus_fault(void)
{
    fake_bus_t fake;
    chameleon_soft_i2c_t bus;

    initialize(&fake, &bus);
    fake.slave_sda_low = 1;
    ASSERT_EQ(chameleon_soft_i2c_write(&bus, 0x08U, NULL, 0U),
              CHAMELEON_I2C_ERR_BUS, "SDA held low");
    assert_released(&fake);
}

static void test_sda_forced_low_during_transmitted_high_is_bus_fault(void)
{
    fake_bus_t fake;
    chameleon_soft_i2c_t bus;
    static const uint8_t samples[] = { 1U, 0U };

    initialize(&fake, &bus);
    load_samples(&fake, samples, sizeof(samples));
    ASSERT_EQ(chameleon_soft_i2c_write(&bus, 0x08U, NULL, 0U),
              CHAMELEON_I2C_ERR_BUS, "SDA collision");
    assert_released(&fake);
}

static void test_transaction_deadline_is_fifty_ms(void)
{
    fake_bus_t fake;
    chameleon_soft_i2c_t bus;
    uint8_t data[300];
    uint8_t samples[303];

    initialize(&fake, &bus);
    memset(data, 0, sizeof(data));
    memset(samples, 0, sizeof(samples));
    samples[0] = 1U;
    samples[1] = 1U;
    load_samples(&fake, samples, sizeof(samples));
    ASSERT_EQ(chameleon_soft_i2c_write(&bus, 0x08U, data, sizeof(data)),
              CHAMELEON_I2C_ERR_TIMEOUT, "transaction timeout");
    ASSERT_TRUE(fake.now_us >= CHAMELEON_SOFT_I2C_TXN_TIMEOUT_US,
                "fifty ms reached");
    assert_released(&fake);
}

static void test_deadlines_wrap_across_uint32_max(void)
{
    fake_bus_t fake;
    chameleon_soft_i2c_t bus;
    static const uint8_t samples[] = { 1U, 1U, 0U };

    initialize(&fake, &bus);
    fake.now_us = UINT32_MAX - 5U;
    load_samples(&fake, samples, sizeof(samples));
    ASSERT_EQ(chameleon_soft_i2c_write(&bus, 0x08U, NULL, 0U),
              CHAMELEON_I2C_OK, "wrapped deadline");
}

static void test_start_stop_timing_meets_standard_mode(void)
{
    fake_bus_t fake;
    chameleon_soft_i2c_t bus;
    static const uint8_t samples[] = { 1U, 1U, 0U };

    initialize(&fake, &bus);
    load_samples(&fake, samples, sizeof(samples));
    ASSERT_EQ(chameleon_soft_i2c_write(&bus, 0x08U, NULL, 0U),
              CHAMELEON_I2C_OK, "timing transaction");
    ASSERT_TRUE(fake.start_at[0] - fake.scl_rise_at[0] >= 5U,
                "START setup >= 4.7us");
    ASSERT_TRUE(fake.scl_fall_at[0] - fake.start_at[0] >= 4U,
                "START hold >= 4.0us");
    ASSERT_TRUE(fake.stop_at[0] - fake.scl_rise_at[fake.scl_rise_count - 1U] >= 4U,
                "STOP setup >= 4.0us");
    ASSERT_TRUE(fake.now_us - fake.stop_at[0] >= 5U,
                "bus free >= 4.7us");
}

static void test_bus_clear_clocks_nine_times_then_stops(void)
{
    fake_bus_t fake;
    chameleon_soft_i2c_t bus;
    static const uint8_t samples[] = { 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U };

    initialize(&fake, &bus);
    fake.slave_sda_low = 1;
    load_samples(&fake, samples, sizeof(samples));
    ASSERT_EQ(chameleon_soft_i2c_bus_clear(&bus), CHAMELEON_I2C_ERR_BUS,
              "clear reports held SDA");
    ASSERT_EQ(fake.scl_rise_count, 9U, "nine clear clocks only");
    ASSERT_EQ(fake.sda_low_calls, 1U, "bounded STOP drives SDA low");
    ASSERT_TRUE(fake.sda_release_calls >= 3U, "bounded STOP releases SDA");
    assert_released(&fake);
}

int main(void)
{
    test_probe_sends_shifted_write_address();
    test_address_nack_is_reported_and_lines_release();
    test_write_is_msb_first();
    test_write_read_uses_repeated_start();
    test_read_acks_intermediate_and_nacks_final_byte();
    test_clock_stretch_completes_before_two_ms();
    test_scl_held_low_times_out_and_releases_lines();
    test_sda_held_low_is_bus_fault();
    test_sda_forced_low_during_transmitted_high_is_bus_fault();
    test_transaction_deadline_is_fifty_ms();
    test_deadlines_wrap_across_uint32_max();
    test_start_stop_timing_meets_standard_mode();
    test_bus_clear_clocks_nine_times_then_stops();
    printf("test_chameleon_soft_i2c OK\n");
    return 0;
}
