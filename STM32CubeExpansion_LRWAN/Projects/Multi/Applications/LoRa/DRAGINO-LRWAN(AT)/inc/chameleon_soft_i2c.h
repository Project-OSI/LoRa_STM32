#ifndef CHAMELEON_SOFT_I2C_H
#define CHAMELEON_SOFT_I2C_H

#include <stddef.h>
#include <stdint.h>

#include "via_chameleon.h"

#define CHAMELEON_SOFT_I2C_HALF_PERIOD_US       10U
#define CHAMELEON_SOFT_I2C_SCL_HIGH_TIMEOUT_US 2000U
#define CHAMELEON_SOFT_I2C_BYTE_TIMEOUT_US    10000U
#define CHAMELEON_SOFT_I2C_TXN_TIMEOUT_US     50000U

typedef struct {
    void *context;
    void (*scl_low)(void *context);
    void (*scl_release)(void *context);
    int  (*scl_read)(void *context);
    void (*sda_low)(void *context);
    void (*sda_release)(void *context);
    int  (*sda_read)(void *context);
    void (*delay_us)(void *context, uint32_t us);
    uint32_t (*micros)(void *context);
} chameleon_soft_i2c_ops_t;

typedef struct {
    chameleon_soft_i2c_ops_t ops;
    uint8_t active;
} chameleon_soft_i2c_t;

int chameleon_soft_i2c_init(chameleon_soft_i2c_t *bus,
                            const chameleon_soft_i2c_ops_t *ops);
void chameleon_soft_i2c_shutdown(chameleon_soft_i2c_t *bus);
chameleon_i2c_status_t chameleon_soft_i2c_write(
    chameleon_soft_i2c_t *bus, uint8_t addr7,
    const uint8_t *data, size_t len);
chameleon_i2c_status_t chameleon_soft_i2c_write_read(
    chameleon_soft_i2c_t *bus, uint8_t addr7,
    const uint8_t *wdata, size_t wlen,
    uint8_t *rdata, size_t rlen);
chameleon_i2c_status_t chameleon_soft_i2c_bus_clear(
    chameleon_soft_i2c_t *bus);

#endif /* CHAMELEON_SOFT_I2C_H */
