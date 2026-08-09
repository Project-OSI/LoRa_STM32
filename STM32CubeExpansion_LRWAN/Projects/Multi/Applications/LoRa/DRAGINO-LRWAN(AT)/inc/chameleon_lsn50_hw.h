#ifndef CHAMELEON_LSN50_HW_H
#define CHAMELEON_LSN50_HW_H

#include <stdint.h>

#include "via_chameleon.h"

#define CHAMELEON_POWER_STABILIZE_MS  100U
#define CHAMELEON_PROBE_TIMEOUT_MS    100U
#define CHAMELEON_PROBE_INTERVAL_MS    10U
#define CHAMELEON_COLD_RETRY_OFF_MS   200U

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
    chameleon_result_t (*measure)(void *context,
                                  chameleon_sample_t *sample,
                                  uint32_t timeout_ms);
    uint16_t (*battery_mv)(void *context);
} chameleon_lsn50_ops_t;

chameleon_result_t chameleon_lsn50_run(const chameleon_lsn50_ops_t *ops,
                                       chameleon_sample_t *sample,
                                       uint32_t measurement_timeout_ms);

#ifndef CHAMELEON_HOST_TEST
chameleon_result_t chameleon_lsn50_acquire(chameleon_sample_t *sample,
                                           uint32_t measurement_timeout_ms);
void chameleon_lsn50_prepare_sleep(void);
#endif

#endif /* CHAMELEON_LSN50_HW_H */
