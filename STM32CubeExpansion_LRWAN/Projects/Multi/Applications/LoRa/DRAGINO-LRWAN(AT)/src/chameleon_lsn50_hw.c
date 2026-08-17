#include "chameleon_lsn50_hw.h"

#ifndef CHAMELEON_HOST_TEST
#include "chameleon_soft_i2c.h"
#include "hw.h"
#include "iwdg.h"
#include "timeServer.h"
#endif

#include <string.h>

static uint8_t last_attempts;

uint8_t chameleon_lsn50_last_attempts(void)
{
    return last_attempts;
}

const char *chameleon_result_name(chameleon_result_t result)
{
    switch (result) {
    case CHAMELEON_RESULT_OK: return "ok";
    case CHAMELEON_RESULT_I2C_INIT_FAILED: return "i2c_init_failed";
    case CHAMELEON_RESULT_NO_DEVICE: return "no_device";
    case CHAMELEON_RESULT_TRIGGER_FAILED: return "trigger_failed";
    case CHAMELEON_RESULT_STATUS_IO_FAILED: return "status_io_failed";
    case CHAMELEON_RESULT_MEASUREMENT_TIMEOUT: return "measurement_timeout";
    case CHAMELEON_RESULT_READ_FAILED: return "read_failed";
    case CHAMELEON_RESULT_PARTIAL_SAMPLE: return "partial_sample";
    default: return "unknown";
    }
}

static int ops_valid(const chameleon_lsn50_ops_t *ops)
{
    return ops != 0
        && ops->rail_off != 0
        && ops->bus_isolate != 0
        && ops->rail_on != 0
        && ops->i2c_init != 0
        && ops->i2c_deinit != 0
        && ops->delay_ms != 0
        && ops->millis != 0
        && ops->probe != 0
        && ops->wait_ready != 0
        && ops->measure != 0
        && ops->bus_clear != 0
        && ops->watchdog_refresh != 0
        && ops->battery_mv != 0;
}

static void clean_session(const chameleon_lsn50_ops_t *ops)
{
    ops->i2c_deinit(ops->context);
    ops->bus_isolate(ops->context);
    ops->rail_off(ops->context);
}

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static uint32_t remaining_ms(const chameleon_lsn50_ops_t *ops,
                             uint32_t acquire_started)
{
    uint32_t elapsed = ops->millis(ops->context) - acquire_started;

    if (elapsed >= CHAMELEON_ACQUIRE_TIMEOUT_MS) {
        return 0U;
    }
    return CHAMELEON_ACQUIRE_TIMEOUT_MS - elapsed;
}

static void bounded_delay(const chameleon_lsn50_ops_t *ops, uint32_t delay_ms)
{
    while (delay_ms != 0U) {
        uint32_t slice = min_u32(delay_ms, CHAMELEON_WATCHDOG_SLICE_MS);

        ops->watchdog_refresh(ops->context);
        ops->delay_ms(ops->context, slice);
        ops->watchdog_refresh(ops->context);
        delay_ms -= slice;
    }
}

static chameleon_result_t bounded_probe(const chameleon_lsn50_ops_t *ops,
                                        uint32_t acquire_started)
{
    uint32_t probe_started = ops->millis(ops->context);
    int first_probe = 1;

    for (;;) {
        uint32_t probe_elapsed = ops->millis(ops->context) - probe_started;
        uint32_t global_remaining = remaining_ms(ops, acquire_started);
        uint32_t local_remaining;
        uint32_t delay;
        chameleon_result_t result;

        if ((!first_probe && probe_elapsed >= CHAMELEON_PROBE_TIMEOUT_MS)
                || global_remaining <= 50U) {
            return CHAMELEON_RESULT_NO_DEVICE;
        }
        first_probe = 0;
        ops->watchdog_refresh(ops->context);
        result = ops->probe(ops->context);
        ops->watchdog_refresh(ops->context);
        if (result == CHAMELEON_RESULT_OK) {
            return result;
        }

        probe_elapsed = ops->millis(ops->context) - probe_started;
        global_remaining = remaining_ms(ops, acquire_started);
        if (probe_elapsed >= CHAMELEON_PROBE_TIMEOUT_MS
                || global_remaining <= 50U) {
            return CHAMELEON_RESULT_NO_DEVICE;
        }
        local_remaining = CHAMELEON_PROBE_TIMEOUT_MS - probe_elapsed;
        delay = min_u32(CHAMELEON_PROBE_INTERVAL_MS, local_remaining);
        delay = min_u32(delay, global_remaining - 50U);
        if (delay == 0U) {
            return CHAMELEON_RESULT_NO_DEVICE;
        }
        bounded_delay(ops, delay);
    }
}

static int needs_bus_clear(chameleon_result_t result)
{
    return result == CHAMELEON_RESULT_NO_DEVICE
        || result == CHAMELEON_RESULT_TRIGGER_FAILED
        || result == CHAMELEON_RESULT_STATUS_IO_FAILED
        || result == CHAMELEON_RESULT_READ_FAILED
        || result == CHAMELEON_RESULT_PARTIAL_SAMPLE;
}

static chameleon_result_t run_one_session(const chameleon_lsn50_ops_t *ops,
                                          chameleon_sample_t *sample,
                                          uint32_t measurement_timeout_ms,
                                          uint32_t acquire_started)
{
    chameleon_result_t result;
    uint32_t remaining;
    uint32_t timeout;

    clean_session(ops);
    ops->rail_on(ops->context);
    bounded_delay(ops, CHAMELEON_POWER_STABILIZE_MS);
    if (remaining_ms(ops, acquire_started) == 0U) {
        result = CHAMELEON_RESULT_MEASUREMENT_TIMEOUT;
        clean_session(ops);
        return result;
    }

    ops->watchdog_refresh(ops->context);
    if (!ops->i2c_init(ops->context)) {
        ops->watchdog_refresh(ops->context);
        clean_session(ops);
        return CHAMELEON_RESULT_I2C_INIT_FAILED;
    }
    ops->watchdog_refresh(ops->context);

    result = bounded_probe(ops, acquire_started);
    if (result == CHAMELEON_RESULT_OK) {
        remaining = remaining_ms(ops, acquire_started);
        timeout = remaining > 50U
                ? min_u32(measurement_timeout_ms, remaining - 50U) : 0U;
        if (timeout == 0U) {
            result = CHAMELEON_RESULT_MEASUREMENT_TIMEOUT;
        } else {
            ops->watchdog_refresh(ops->context);
            result = ops->wait_ready(ops->context, timeout);
            ops->watchdog_refresh(ops->context);
        }
    }
    if (result == CHAMELEON_RESULT_OK) {
        remaining = remaining_ms(ops, acquire_started);
        timeout = remaining > 500U
                ? min_u32(measurement_timeout_ms, remaining - 500U) : 0U;
        if (timeout == 0U) {
            result = CHAMELEON_RESULT_MEASUREMENT_TIMEOUT;
        } else {
            ops->watchdog_refresh(ops->context);
            result = ops->measure(ops->context, sample, timeout);
            ops->watchdog_refresh(ops->context);
        }
    }
    if (needs_bus_clear(result)) {
        ops->watchdog_refresh(ops->context);
        (void)ops->bus_clear(ops->context);
        ops->watchdog_refresh(ops->context);
    }
    clean_session(ops);
    return result;
}

static void apply_result_flags(chameleon_sample_t *sample,
                               chameleon_result_t result)
{
    if (result == CHAMELEON_RESULT_I2C_INIT_FAILED
            || result == CHAMELEON_RESULT_NO_DEVICE
            || result == CHAMELEON_RESULT_READ_FAILED
            || result == CHAMELEON_RESULT_PARTIAL_SAMPLE) {
        sample->status_flags |= CHAMELEON_FLAG_I2C_MISSING;
    } else if (result != CHAMELEON_RESULT_OK) {
        sample->status_flags |= CHAMELEON_FLAG_TIMEOUT;
    }
}

chameleon_result_t chameleon_lsn50_run(const chameleon_lsn50_ops_t *ops,
                                       chameleon_sample_t *sample,
                                       uint32_t measurement_timeout_ms)
{
    chameleon_result_t result = CHAMELEON_RESULT_I2C_INIT_FAILED;
    uint32_t acquire_started;
    unsigned attempt;

    if (!ops_valid(ops) || sample == 0) {
        last_attempts = 0U;
        return CHAMELEON_RESULT_I2C_INIT_FAILED;
    }

    acquire_started = ops->millis(ops->context);
    last_attempts = 0U;
    for (attempt = 0U; attempt < 1U + CHAMELEON_COLD_RETRY_ENABLED; ++attempt) {
        last_attempts = (uint8_t)(attempt + 1U);
        memset(sample, 0, sizeof(*sample));
        result = run_one_session(ops, sample, measurement_timeout_ms,
                                 acquire_started);
        if (result == CHAMELEON_RESULT_OK) {
            break;
        }
        if (attempt == 0U
                && remaining_ms(ops, acquire_started)
                    >= CHAMELEON_COLD_RETRY_OFF_MS
                    + CHAMELEON_RETRY_SESSION_RESERVE_MS) {
            bounded_delay(ops, CHAMELEON_COLD_RETRY_OFF_MS);
        } else {
            break;
        }
    }

    apply_result_flags(sample, result);
    sample->battery_mv = ops->battery_mv(ops->context);
    return result;
}

#ifndef CHAMELEON_HOST_TEST

#if defined(DEBUG) && defined(USE_CHAMELEON)
#error "DEBUG drives PB12/PB13 push-pull and is incompatible with Chameleon"
#endif
#if !defined(CHAMELEON_POWER_LSN50_5V)
#error "This branch requires the stock PB5-switched +5 V backend"
#endif
#if !defined(CHAMELEON_SOFT_I2C_PB12_PB13)
#error "This branch requires PB12/PB13 software I2C"
#endif
#if (CHAMELEON_POLL_INTERVAL_MS * 1000U) >= 65536U
#error "VIA poll interval can skip a complete TIM2 wrap"
#endif
#if (CHAMELEON_PROBE_INTERVAL_MS * 1000U) >= 65536U
#error "Probe interval can skip a complete TIM2 wrap"
#endif

#define CHAMELEON_SCL_PIN GPIO_PIN_13
#define CHAMELEON_SDA_PIN GPIO_PIN_12

extern uint16_t batteryLevel_mV;

static chameleon_soft_i2c_t chameleon_bus;
static chameleon_tim2_clock_t tim2_clock;
static int tim2_active;

static void stm32_i2c_deinit(void *context);

static void stm32_rail_off(void *context)
{
    GPIO_InitTypeDef gpio;
    (void)context;
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
    memset(&gpio, 0, sizeof(gpio));
    gpio.Pin = GPIO_PIN_5;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static void stm32_rail_on(void *context)
{
    (void)context;
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
}

static void stm32_bus_isolate(void *context)
{
    GPIO_InitTypeDef gpio;
    (void)context;
    __HAL_RCC_GPIOB_CLK_ENABLE();
    memset(&gpio, 0, sizeof(gpio));
    gpio.Pin = CHAMELEON_SCL_PIN | CHAMELEON_SDA_PIN;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static void stm32_bus_activate(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, CHAMELEON_SCL_PIN | CHAMELEON_SDA_PIN, GPIO_PIN_SET);
    memset(&gpio, 0, sizeof(gpio));
    gpio.Pin = CHAMELEON_SCL_PIN | CHAMELEON_SDA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static int stm32_timer_start(void)
{
    uint32_t pclk_hz = HAL_RCC_GetPCLK1Freq();

    if (pclk_hz != 32000000U) {
        return 0;
    }
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM2_FORCE_RESET();
    __HAL_RCC_TIM2_RELEASE_RESET();
    TIM2->PSC = (pclk_hz / 1000000U) - 1U;
    TIM2->ARR = 0xffffU;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CNT = 0U;
    chameleon_tim2_clock_reset(&tim2_clock, 0U);
    TIM2->CR1 = TIM_CR1_CEN;
    tim2_active = 1;
    return 1;
}

static void stm32_timer_stop(void)
{
    if (tim2_active == 0) {
        return;
    }
    TIM2->CR1 &= ~TIM_CR1_CEN;
    __HAL_RCC_TIM2_FORCE_RESET();
    __HAL_RCC_TIM2_RELEASE_RESET();
    __HAL_RCC_TIM2_CLK_DISABLE();
    tim2_active = 0;
}

static uint32_t stm32_micros(void *context)
{
    (void)context;
    return chameleon_tim2_clock_update(&tim2_clock, (uint16_t)TIM2->CNT);
}

static void stm32_delay_us(void *context, uint32_t delay_us)
{
    uint32_t start = stm32_micros(context);

    while ((uint32_t)(stm32_micros(context) - start) < delay_us) {
    }
}

static void stm32_scl_low(void *context)
{
    (void)context;
    HAL_GPIO_WritePin(GPIOB, CHAMELEON_SCL_PIN, GPIO_PIN_RESET);
}

static void stm32_scl_release(void *context)
{
    (void)context;
    HAL_GPIO_WritePin(GPIOB, CHAMELEON_SCL_PIN, GPIO_PIN_SET);
}

static int stm32_scl_read(void *context)
{
    (void)context;
    return HAL_GPIO_ReadPin(GPIOB, CHAMELEON_SCL_PIN) == GPIO_PIN_SET;
}

static void stm32_sda_low(void *context)
{
    (void)context;
    HAL_GPIO_WritePin(GPIOB, CHAMELEON_SDA_PIN, GPIO_PIN_RESET);
}

static void stm32_sda_release(void *context)
{
    (void)context;
    HAL_GPIO_WritePin(GPIOB, CHAMELEON_SDA_PIN, GPIO_PIN_SET);
}

static int stm32_sda_read(void *context)
{
    (void)context;
    return HAL_GPIO_ReadPin(GPIOB, CHAMELEON_SDA_PIN) == GPIO_PIN_SET;
}

static const chameleon_soft_i2c_ops_t stm32_soft_i2c_ops = {
    0,
    stm32_scl_low,
    stm32_scl_release,
    stm32_scl_read,
    stm32_sda_low,
    stm32_sda_release,
    stm32_sda_read,
    stm32_delay_us,
    stm32_micros
};

static int stm32_i2c_init(void *context)
{
    (void)context;
    if (!stm32_timer_start()) {
        return 0;
    }
    stm32_bus_activate();
    if (!chameleon_soft_i2c_init(&chameleon_bus, &stm32_soft_i2c_ops)) {
        stm32_i2c_deinit(context);
        return 0;
    }
    return 1;
}

static void stm32_i2c_deinit(void *context)
{
    (void)context;
    chameleon_soft_i2c_shutdown(&chameleon_bus);
    stm32_timer_stop();
}

static chameleon_i2c_status_t stm32_bus_clear(void *context)
{
    (void)context;
    return chameleon_soft_i2c_bus_clear(&chameleon_bus);
}

static void stm32_watchdog_refresh(void *context)
{
    (void)context;
    IWDG_Refresh();
}

static void stm32_delay_ms(void *context, uint32_t ms)
{
    (void)context;
    HAL_Delay(ms);
}

static uint32_t stm32_millis(void *context)
{
    (void)context;
    return TimerGetCurrentTime();
}

static chameleon_result_t stm32_probe(void *context)
{
    (void)context;
    return via_chameleon_probe();
}

static chameleon_result_t stm32_measure(void *context,
                                        chameleon_sample_t *sample,
                                        uint32_t timeout_ms)
{
    (void)context;
    return via_chameleon_measure(sample, timeout_ms);
}

static chameleon_result_t stm32_wait_ready(void *context, uint32_t timeout_ms)
{
    (void)context;
    return via_chameleon_wait_ready(timeout_ms);
}

static uint16_t stm32_battery_mv(void *context)
{
    (void)context;
    return batteryLevel_mV;
}

static const chameleon_lsn50_ops_t stm32_ops = {
    0,
    stm32_rail_off,
    stm32_bus_isolate,
    stm32_rail_on,
    stm32_i2c_init,
    stm32_i2c_deinit,
    stm32_delay_ms,
    stm32_millis,
    stm32_probe,
    stm32_wait_ready,
    stm32_measure,
    stm32_bus_clear,
    stm32_watchdog_refresh,
    stm32_battery_mv
};

chameleon_i2c_status_t chameleon_board_i2c_write(uint8_t addr7,
                                                 const uint8_t *data,
                                                 size_t len)
{
    return chameleon_soft_i2c_write(&chameleon_bus, addr7, data, len);
}

chameleon_i2c_status_t chameleon_board_i2c_write_read(uint8_t addr7,
                                                      const uint8_t *wdata,
                                                      size_t wlen,
                                                      uint8_t *rdata,
                                                      size_t rlen)
{
    return chameleon_soft_i2c_write_read(&chameleon_bus, addr7,
                                         wdata, wlen, rdata, rlen);
}

void chameleon_board_delay_ms(uint32_t ms) { HAL_Delay(ms); }
uint32_t chameleon_board_millis(void) { return TimerGetCurrentTime(); }
uint16_t chameleon_board_battery_mv(void) { return batteryLevel_mV; }

chameleon_result_t chameleon_lsn50_acquire(chameleon_sample_t *sample,
                                           uint32_t measurement_timeout_ms)
{
    return chameleon_lsn50_run(&stm32_ops, sample, measurement_timeout_ms);
}

void chameleon_lsn50_prepare_sleep(void)
{
    clean_session(&stm32_ops);
}

#endif /* CHAMELEON_HOST_TEST */
