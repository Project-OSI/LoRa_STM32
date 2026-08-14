#include "chameleon_lsn50_hw.h"
#ifndef CHAMELEON_HOST_TEST
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
        && ops->battery_mv != 0;
}

static void clean_session(const chameleon_lsn50_ops_t *ops, int initialized)
{
    if (initialized) {
        ops->i2c_deinit(ops->context);
    }
    ops->bus_isolate(ops->context);
    ops->rail_off(ops->context);
}

static chameleon_result_t bounded_probe(const chameleon_lsn50_ops_t *ops)
{
    const uint32_t start = ops->millis(ops->context);
    int first_probe = 1;

    for (;;) {
        chameleon_result_t result;
        uint32_t elapsed = ops->millis(ops->context) - start;
        uint32_t delay;

        if (!first_probe && elapsed >= CHAMELEON_PROBE_TIMEOUT_MS) {
            return CHAMELEON_RESULT_NO_DEVICE;
        }
        first_probe = 0;
        result = ops->probe(ops->context);
        if (result == CHAMELEON_RESULT_OK) {
            return result;
        }
        elapsed = ops->millis(ops->context) - start;
        if (elapsed >= CHAMELEON_PROBE_TIMEOUT_MS) {
            return CHAMELEON_RESULT_NO_DEVICE;
        }
        delay = CHAMELEON_PROBE_TIMEOUT_MS - elapsed;
        if (delay > CHAMELEON_PROBE_INTERVAL_MS) {
            delay = CHAMELEON_PROBE_INTERVAL_MS;
        }
        ops->delay_ms(ops->context, delay);
    }
}

static chameleon_result_t run_one_session(const chameleon_lsn50_ops_t *ops,
                                          chameleon_sample_t *sample,
                                          uint32_t measurement_timeout_ms)
{
    chameleon_result_t result;
    int initialized = 0;

    ops->rail_off(ops->context);
    ops->bus_isolate(ops->context);
    ops->rail_on(ops->context);
    ops->delay_ms(ops->context, CHAMELEON_POWER_STABILIZE_MS);

    if (!ops->i2c_init(ops->context)) {
        result = CHAMELEON_RESULT_I2C_INIT_FAILED;
        clean_session(ops, initialized);
        return result;
    }
    initialized = 1;

    result = bounded_probe(ops);
    if (result == CHAMELEON_RESULT_OK) {
        result = ops->wait_ready(ops->context, measurement_timeout_ms);
    }
    if (result == CHAMELEON_RESULT_OK) {
        result = ops->measure(ops->context, sample, measurement_timeout_ms);
    }
    clean_session(ops, initialized);
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
    chameleon_result_t result;
    unsigned attempt;

    if (!ops_valid(ops) || sample == 0) {
        last_attempts = 0U;
        return CHAMELEON_RESULT_I2C_INIT_FAILED;
    }

    last_attempts = 0U;
    for (attempt = 0U; attempt < 2U; ++attempt) {
        last_attempts = (uint8_t)(attempt + 1U);
        memset(sample, 0, sizeof(*sample));
        result = run_one_session(ops, sample, measurement_timeout_ms);
        if (result == CHAMELEON_RESULT_OK) {
            break;
        }
        if (attempt == 0U) {
            ops->delay_ms(ops->context, CHAMELEON_COLD_RETRY_OFF_MS);
        }
    }

    apply_result_flags(sample, result);
    sample->battery_mv = ops->battery_mv(ops->context);
    return result;
}

#ifndef CHAMELEON_HOST_TEST

#include "hw.h"

#if defined(CHAMELEON_POWER_EXTERNAL_PMOS) == defined(CHAMELEON_POWER_LSN50_5V)
#error "Select exactly one Chameleon power backend"
#endif

#define CHAMELEON_SCL_PIN          GPIO_PIN_13
#define CHAMELEON_SDA_PIN          GPIO_PIN_14
#define CHAMELEON_I2C_TIMING_100KHZ 0x10A13E56U
#define CHAMELEON_I2C_TIMING_400KHZ 0x00B1112EU
#ifdef CHAMELEON_FIELD_DEBUG
#define CHAMELEON_I2C_TIMING CHAMELEON_I2C_TIMING_100KHZ
#else
#define CHAMELEON_I2C_TIMING CHAMELEON_I2C_TIMING_400KHZ
#endif
#define CHAMELEON_I2C_TXN_MS       1000U

#ifdef CHAMELEON_FIELD_DEBUG
#define CHAMELEON_FIELD_DEBUG_MAGIC 0x43480000UL
#define CHAMELEON_FIELD_DEBUG_MASK  0xFFFF0000UL

static chameleon_probe_debug_t chameleon_probe_debug;

void chameleon_field_debug_set_stage(uint32_t stage)
{
    HAL_PWR_EnableBkUpAccess();
    RTC->BKP4R = CHAMELEON_FIELD_DEBUG_MAGIC | stage;
}

uint32_t chameleon_field_debug_get_stage(void)
{
    uint32_t value = RTC->BKP4R;
    if ((value & CHAMELEON_FIELD_DEBUG_MASK) != CHAMELEON_FIELD_DEBUG_MAGIC) {
        return 0U;
    }
    return value & ~CHAMELEON_FIELD_DEBUG_MASK;
}

void chameleon_field_debug_clear_stage(void)
{
    HAL_PWR_EnableBkUpAccess();
    RTC->BKP4R = 0U;
}

void chameleon_field_debug_get_probe(chameleon_probe_debug_t *debug)
{
    if (debug != 0) {
        *debug = chameleon_probe_debug;
    }
}
#endif

#if defined(CHAMELEON_POWER_EXTERNAL_PMOS)
#define CHAMELEON_POWER_PIN        GPIO_PIN_12
#else
#define CHAMELEON_POWER_PIN        GPIO_PIN_5
#endif

extern uint16_t batteryLevel_mV;

static I2C_HandleTypeDef chameleon_i2c2;

static void stm32_rail_off(void *context)
{
    GPIO_InitTypeDef gpio;
    (void)context;
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOB, CHAMELEON_POWER_PIN, GPIO_PIN_SET);
    memset(&gpio, 0, sizeof(gpio));
    gpio.Pin = CHAMELEON_POWER_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
#if defined(CHAMELEON_POWER_LSN50_5V)
    gpio.Pull = GPIO_PULLUP;
#else
    gpio.Pull = GPIO_NOPULL;
#endif
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static void stm32_rail_on(void *context)
{
    (void)context;
    HAL_GPIO_WritePin(GPIOB, CHAMELEON_POWER_PIN, GPIO_PIN_RESET);
}

static void stm32_bus_isolate(void *context)
{
    GPIO_InitTypeDef gpio;
    (void)context;
    __HAL_RCC_GPIOB_CLK_ENABLE();
    EXTI->IMR &= ~(uint32_t)CHAMELEON_SDA_PIN;
    EXTI->RTSR &= ~(uint32_t)CHAMELEON_SDA_PIN;
    EXTI->FTSR &= ~(uint32_t)CHAMELEON_SDA_PIN;
    __HAL_GPIO_EXTI_CLEAR_IT(CHAMELEON_SDA_PIN);
    memset(&gpio, 0, sizeof(gpio));
    gpio.Pin = CHAMELEON_SCL_PIN | CHAMELEON_SDA_PIN;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static int stm32_i2c_init(void *context)
{
    (void)context;
    memset(&chameleon_i2c2, 0, sizeof(chameleon_i2c2));
    chameleon_i2c2.Instance = I2C2;
    chameleon_i2c2.Init.Timing = CHAMELEON_I2C_TIMING;
    chameleon_i2c2.Init.OwnAddress1 = 0U;
    chameleon_i2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    chameleon_i2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    chameleon_i2c2.Init.OwnAddress2 = 0U;
    chameleon_i2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    chameleon_i2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    chameleon_i2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    return HAL_I2C_Init(&chameleon_i2c2) == HAL_OK;
}

static void stm32_i2c_deinit(void *context)
{
    (void)context;
    (void)HAL_I2C_DeInit(&chameleon_i2c2);
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
    stm32_battery_mv
};

chameleon_i2c_status_t chameleon_board_i2c_write(uint8_t addr7,
                                                 const uint8_t *data,
                                                 size_t len)
{
    HAL_StatusTypeDef status;
    uint16_t address = (uint16_t)addr7 << 1;

    if (len == 0U) {
        status = HAL_I2C_IsDeviceReady(&chameleon_i2c2, address, 1U,
                                       CHAMELEON_I2C_TXN_MS);
#ifdef CHAMELEON_FIELD_DEBUG
        chameleon_probe_debug.probe_calls++;
        chameleon_probe_debug.hal_status = (uint32_t)status;
        chameleon_probe_debug.hal_error = HAL_I2C_GetError(&chameleon_i2c2);
        chameleon_probe_debug.hal_state = (uint32_t)HAL_I2C_GetState(&chameleon_i2c2);
        chameleon_probe_debug.i2c_isr = I2C2->ISR;
        chameleon_probe_debug.line_state =
            ((GPIOB->IDR & CHAMELEON_SCL_PIN) != 0U ? 2U : 0U) |
            ((GPIOB->IDR & CHAMELEON_SDA_PIN) != 0U ? 1U : 0U);
#endif
    } else {
        status = HAL_I2C_Master_Transmit(&chameleon_i2c2, address,
                                         (uint8_t *)data, (uint16_t)len,
                                         CHAMELEON_I2C_TXN_MS);
    }
    if (status == HAL_OK) return CHAMELEON_I2C_OK;
    if (status == HAL_TIMEOUT) return CHAMELEON_I2C_ERR_TIMEOUT;
    if (HAL_I2C_GetError(&chameleon_i2c2) == HAL_I2C_ERROR_AF) {
        return CHAMELEON_I2C_ERR_NACK;
    }
    return CHAMELEON_I2C_ERR_BUS;
}

chameleon_i2c_status_t chameleon_board_i2c_write_read(uint8_t addr7,
                                                      const uint8_t *wdata,
                                                      size_t wlen,
                                                      uint8_t *rdata,
                                                      size_t rlen)
{
    HAL_StatusTypeDef status;

    if (wlen != 1U || rlen == 0U || rlen > UINT16_MAX) {
        return CHAMELEON_I2C_ERR_BUS;
    }
    status = HAL_I2C_Mem_Read(&chameleon_i2c2, (uint16_t)addr7 << 1,
                              wdata[0], I2C_MEMADD_SIZE_8BIT,
                              rdata, (uint16_t)rlen, CHAMELEON_I2C_TXN_MS);
    if (status == HAL_OK) return CHAMELEON_I2C_OK;
    if (status == HAL_TIMEOUT) return CHAMELEON_I2C_ERR_TIMEOUT;
    if (HAL_I2C_GetError(&chameleon_i2c2) == HAL_I2C_ERROR_AF) {
        return CHAMELEON_I2C_ERR_NACK;
    }
    return CHAMELEON_I2C_ERR_BUS;
}

void chameleon_board_delay_ms(uint32_t ms) { HAL_Delay(ms); }
uint32_t chameleon_board_millis(void) { return TimerGetCurrentTime(); }
uint16_t chameleon_board_battery_mv(void) { return batteryLevel_mV; }

chameleon_result_t chameleon_lsn50_acquire(chameleon_sample_t *sample,
                                           uint32_t measurement_timeout_ms)
{
#ifdef CHAMELEON_FIELD_DEBUG
    memset(&chameleon_probe_debug, 0, sizeof(chameleon_probe_debug));
#endif
    return chameleon_lsn50_run(&stm32_ops, sample, measurement_timeout_ms);
}

void chameleon_lsn50_prepare_sleep(void)
{
    stm32_rail_off(0);
    stm32_bus_isolate(0);
}

#endif /* CHAMELEON_HOST_TEST */
