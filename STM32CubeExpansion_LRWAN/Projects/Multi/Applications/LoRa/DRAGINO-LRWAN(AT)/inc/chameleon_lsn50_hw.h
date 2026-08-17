#ifndef CHAMELEON_LSN50_HW_H
#define CHAMELEON_LSN50_HW_H

#include <stdint.h>

#include "chameleon_soft_i2c.h"
#include "via_chameleon.h"

#define CHAMELEON_POWER_STABILIZE_MS   100U
#define CHAMELEON_PROBE_TIMEOUT_MS     400U
#define CHAMELEON_PROBE_INTERVAL_MS    50U
#define CHAMELEON_COLD_RETRY_OFF_MS   1000U
#define CHAMELEON_COLD_RETRY_ENABLED      1U
#define CHAMELEON_RETRY_SESSION_RESERVE_MS 5150U
#define CHAMELEON_ACQUIRE_TIMEOUT_MS  12000U
#define CHAMELEON_WATCHDOG_SLICE_MS    1000U
#define CHAMELEON_LIFECYCLE_TXN_RESERVE_MS \
    (CHAMELEON_SOFT_I2C_TXN_TIMEOUT_US / 1000U)

#if (CHAMELEON_SOFT_I2C_TXN_TIMEOUT_US % 1000U) != 0U
#error "Software I2C transaction reserve must use whole milliseconds"
#endif

typedef struct {
    uint16_t last_count;
    uint32_t elapsed_us;
} chameleon_tim2_clock_t;

static inline void chameleon_tim2_clock_reset(
    chameleon_tim2_clock_t *clock, uint16_t count)
{
    clock->last_count = count;
    clock->elapsed_us = 0U;
}

static inline uint32_t chameleon_tim2_clock_update(
    chameleon_tim2_clock_t *clock, uint16_t count)
{
    clock->elapsed_us += (uint16_t)(count - clock->last_count);
    clock->last_count = count;
    return clock->elapsed_us;
}

typedef struct {
    void *context;
    void (*rail_off)(void *context);
    void (*bus_isolate)(void *context);
    void (*rail_on)(void *context);
    int (*i2c_init)(void *context);
    void (*i2c_deinit)(void *context);
    void (*delay_ms)(void *context, uint32_t ms);
    uint32_t (*millis)(void *context);
    chameleon_result_t (*probe)(void *context);
    chameleon_result_t (*wait_ready)(void *context, uint32_t timeout_ms);
    chameleon_result_t (*measure)(void *context,
                                  chameleon_sample_t *sample,
                                  uint32_t timeout_ms);
    chameleon_i2c_status_t (*bus_clear)(void *context);
    void (*watchdog_refresh)(void *context);
    uint16_t (*battery_mv)(void *context);
} chameleon_lsn50_ops_t;

chameleon_result_t chameleon_lsn50_run(const chameleon_lsn50_ops_t *ops,
                                       chameleon_sample_t *sample,
                                       uint32_t measurement_timeout_ms);
uint8_t chameleon_lsn50_last_attempts(void);
const char *chameleon_result_name(chameleon_result_t result);

#ifndef CHAMELEON_HOST_TEST
chameleon_result_t chameleon_lsn50_acquire(chameleon_sample_t *sample,
                                           uint32_t measurement_timeout_ms);
void chameleon_lsn50_prepare_sleep(void);
#ifdef CHAMELEON_FIELD_DEBUG
typedef struct {
    uint32_t probe_calls;
    uint32_t hal_status;
    uint32_t hal_error;
    uint32_t hal_state;
    uint32_t i2c_isr;
    uint32_t line_state;
} chameleon_probe_debug_t;

void chameleon_field_debug_set_stage(uint32_t stage);
uint32_t chameleon_field_debug_get_stage(void);
void chameleon_field_debug_clear_stage(void);
void chameleon_field_debug_get_probe(chameleon_probe_debug_t *debug);
#endif
#endif

#endif /* CHAMELEON_LSN50_HW_H */
