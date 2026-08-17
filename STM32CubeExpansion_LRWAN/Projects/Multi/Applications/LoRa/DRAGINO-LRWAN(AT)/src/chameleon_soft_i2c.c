#include "chameleon_soft_i2c.h"

#include <string.h>

static int expired(uint32_t start, uint32_t now, uint32_t limit_us)
{
    return (uint32_t)(now - start) >= limit_us;
}

static int ops_valid(const chameleon_soft_i2c_ops_t *ops)
{
    return ops != NULL && ops->scl_low != NULL && ops->scl_release != NULL
        && ops->scl_read != NULL && ops->sda_low != NULL
        && ops->sda_release != NULL && ops->sda_read != NULL
        && ops->delay_us != NULL && ops->micros != NULL;
}

static chameleon_i2c_status_t release_scl_and_wait(
    chameleon_soft_i2c_t *bus, uint32_t byte_start, uint32_t transaction_start)
{
    uint32_t wait_start = bus->ops.micros(bus->ops.context);

    if (expired(byte_start, wait_start, CHAMELEON_SOFT_I2C_BYTE_TIMEOUT_US)
            || expired(transaction_start, wait_start,
                       CHAMELEON_SOFT_I2C_TXN_TIMEOUT_US)) {
        return CHAMELEON_I2C_ERR_TIMEOUT;
    }

    bus->ops.scl_release(bus->ops.context);
    while (!bus->ops.scl_read(bus->ops.context)) {
        uint32_t now = bus->ops.micros(bus->ops.context);

        if (expired(wait_start, now, CHAMELEON_SOFT_I2C_SCL_HIGH_TIMEOUT_US)
                || expired(byte_start, now, CHAMELEON_SOFT_I2C_BYTE_TIMEOUT_US)
                || expired(transaction_start, now,
                           CHAMELEON_SOFT_I2C_TXN_TIMEOUT_US)) {
            return CHAMELEON_I2C_ERR_TIMEOUT;
        }
        bus->ops.delay_us(bus->ops.context, 1U);
    }
    return CHAMELEON_I2C_OK;
}

static chameleon_i2c_status_t start_condition(
    chameleon_soft_i2c_t *bus, uint32_t transaction_start)
{
    chameleon_i2c_status_t status;
    uint32_t byte_start = bus->ops.micros(bus->ops.context);

    bus->ops.sda_release(bus->ops.context);
    status = release_scl_and_wait(bus, byte_start, transaction_start);
    if (status != CHAMELEON_I2C_OK) {
        return status;
    }
    bus->ops.delay_us(bus->ops.context, CHAMELEON_SOFT_I2C_HALF_PERIOD_US);
    bus->ops.sda_low(bus->ops.context);
    bus->ops.delay_us(bus->ops.context, CHAMELEON_SOFT_I2C_HALF_PERIOD_US);
    bus->ops.scl_low(bus->ops.context);
    return CHAMELEON_I2C_OK;
}

static chameleon_i2c_status_t stop_condition(chameleon_soft_i2c_t *bus)
{
    uint32_t stop_start = bus->ops.micros(bus->ops.context);

    bus->ops.sda_low(bus->ops.context);
    bus->ops.delay_us(bus->ops.context, CHAMELEON_SOFT_I2C_HALF_PERIOD_US);
    bus->ops.scl_release(bus->ops.context);
    while (!bus->ops.scl_read(bus->ops.context)) {
        if (expired(stop_start, bus->ops.micros(bus->ops.context),
                    CHAMELEON_SOFT_I2C_SCL_HIGH_TIMEOUT_US)) {
            bus->ops.scl_release(bus->ops.context);
            bus->ops.sda_release(bus->ops.context);
            return CHAMELEON_I2C_ERR_TIMEOUT;
        }
        bus->ops.delay_us(bus->ops.context, 1U);
    }
    bus->ops.delay_us(bus->ops.context, CHAMELEON_SOFT_I2C_HALF_PERIOD_US);
    bus->ops.sda_release(bus->ops.context);
    bus->ops.delay_us(bus->ops.context, CHAMELEON_SOFT_I2C_HALF_PERIOD_US);
    return CHAMELEON_I2C_OK;
}

static chameleon_i2c_status_t finish_transaction(chameleon_soft_i2c_t *bus,
                                                  chameleon_i2c_status_t status)
{
    chameleon_i2c_status_t stop_status = stop_condition(bus);

    bus->ops.scl_release(bus->ops.context);
    bus->ops.sda_release(bus->ops.context);
    return status == CHAMELEON_I2C_OK ? stop_status : status;
}

static chameleon_i2c_status_t write_bit(
    chameleon_soft_i2c_t *bus, uint8_t bit,
    uint32_t byte_start, uint32_t transaction_start)
{
    chameleon_i2c_status_t status;

    if (bit != 0U) {
        bus->ops.sda_release(bus->ops.context);
    } else {
        bus->ops.sda_low(bus->ops.context);
    }
    bus->ops.delay_us(bus->ops.context, CHAMELEON_SOFT_I2C_HALF_PERIOD_US);
    status = release_scl_and_wait(bus, byte_start, transaction_start);
    if (status != CHAMELEON_I2C_OK) {
        return status;
    }
    if (bit != 0U && !bus->ops.sda_read(bus->ops.context)) {
        bus->ops.scl_low(bus->ops.context);
        return CHAMELEON_I2C_ERR_BUS;
    }
    bus->ops.delay_us(bus->ops.context, CHAMELEON_SOFT_I2C_HALF_PERIOD_US);
    bus->ops.scl_low(bus->ops.context);
    return CHAMELEON_I2C_OK;
}

static chameleon_i2c_status_t read_bit(
    chameleon_soft_i2c_t *bus, uint8_t *bit,
    uint32_t byte_start, uint32_t transaction_start)
{
    chameleon_i2c_status_t status;

    bus->ops.sda_release(bus->ops.context);
    bus->ops.delay_us(bus->ops.context, CHAMELEON_SOFT_I2C_HALF_PERIOD_US);
    status = release_scl_and_wait(bus, byte_start, transaction_start);
    if (status != CHAMELEON_I2C_OK) {
        return status;
    }
    *bit = (uint8_t)(bus->ops.sda_read(bus->ops.context) != 0);
    bus->ops.delay_us(bus->ops.context, CHAMELEON_SOFT_I2C_HALF_PERIOD_US);
    bus->ops.scl_low(bus->ops.context);
    return CHAMELEON_I2C_OK;
}

static chameleon_i2c_status_t write_byte(
    chameleon_soft_i2c_t *bus, uint8_t value, uint32_t transaction_start)
{
    chameleon_i2c_status_t status;
    uint32_t byte_start = bus->ops.micros(bus->ops.context);
    unsigned bit;

    for (bit = 0U; bit < 8U; ++bit) {
        status = write_bit(bus, (uint8_t)(value & 0x80U), byte_start,
                           transaction_start);
        if (status != CHAMELEON_I2C_OK) {
            return status;
        }
        value <<= 1;
    }
    status = read_bit(bus, &value, byte_start, transaction_start);
    if (status != CHAMELEON_I2C_OK) {
        return status;
    }
    return value == 0U ? CHAMELEON_I2C_OK : CHAMELEON_I2C_ERR_NACK;
}

static chameleon_i2c_status_t read_byte(
    chameleon_soft_i2c_t *bus, uint8_t *value, int final_byte,
    uint32_t transaction_start)
{
    chameleon_i2c_status_t status;
    uint32_t byte_start = bus->ops.micros(bus->ops.context);
    uint8_t bit;
    unsigned index;

    *value = 0U;
    for (index = 0U; index < 8U; ++index) {
        status = read_bit(bus, &bit, byte_start, transaction_start);
        if (status != CHAMELEON_I2C_OK) {
            return status;
        }
        *value = (uint8_t)((*value << 1) | bit);
    }
    return write_bit(bus, (uint8_t)(final_byte != 0), byte_start,
                     transaction_start);
}

int chameleon_soft_i2c_init(chameleon_soft_i2c_t *bus,
                            const chameleon_soft_i2c_ops_t *ops)
{
    if (bus == NULL || !ops_valid(ops)) {
        return 0;
    }
    memset(bus, 0, sizeof(*bus));
    bus->ops = *ops;
    bus->active = 1U;
    bus->ops.scl_release(bus->ops.context);
    bus->ops.sda_release(bus->ops.context);
    return 1;
}

void chameleon_soft_i2c_shutdown(chameleon_soft_i2c_t *bus)
{
    if (bus == NULL || bus->active == 0U) {
        return;
    }
    bus->ops.scl_release(bus->ops.context);
    bus->ops.sda_release(bus->ops.context);
    bus->active = 0U;
}

chameleon_i2c_status_t chameleon_soft_i2c_write(
    chameleon_soft_i2c_t *bus, uint8_t addr7, const uint8_t *data, size_t len)
{
    chameleon_i2c_status_t status;
    uint32_t transaction_start;
    size_t index;

    if (bus == NULL || bus->active == 0U || addr7 > 0x7fU
            || (len != 0U && data == NULL)) {
        return CHAMELEON_I2C_ERR_BUS;
    }
    transaction_start = bus->ops.micros(bus->ops.context);
    bus->ops.sda_release(bus->ops.context);
    bus->ops.scl_release(bus->ops.context);
    status = bus->ops.sda_read(bus->ops.context) ? CHAMELEON_I2C_OK
                                                   : CHAMELEON_I2C_ERR_BUS;
    if (status == CHAMELEON_I2C_OK) {
        status = start_condition(bus, transaction_start);
    }
    if (status == CHAMELEON_I2C_OK) {
        status = write_byte(bus, (uint8_t)(addr7 << 1), transaction_start);
    }
    for (index = 0U; status == CHAMELEON_I2C_OK && index < len; ++index) {
        status = write_byte(bus, data[index], transaction_start);
    }
    return finish_transaction(bus, status);
}

chameleon_i2c_status_t chameleon_soft_i2c_write_read(
    chameleon_soft_i2c_t *bus, uint8_t addr7, const uint8_t *wdata, size_t wlen,
    uint8_t *rdata, size_t rlen)
{
    chameleon_i2c_status_t status;
    uint32_t transaction_start;
    size_t index;

    if (bus == NULL || bus->active == 0U || addr7 > 0x7fU || wlen == 0U
            || rlen == 0U || wdata == NULL || rdata == NULL) {
        return CHAMELEON_I2C_ERR_BUS;
    }
    transaction_start = bus->ops.micros(bus->ops.context);
    bus->ops.sda_release(bus->ops.context);
    bus->ops.scl_release(bus->ops.context);
    status = bus->ops.sda_read(bus->ops.context) ? CHAMELEON_I2C_OK
                                                   : CHAMELEON_I2C_ERR_BUS;
    if (status == CHAMELEON_I2C_OK) {
        status = start_condition(bus, transaction_start);
    }
    if (status == CHAMELEON_I2C_OK) {
        status = write_byte(bus, (uint8_t)(addr7 << 1), transaction_start);
    }
    for (index = 0U; status == CHAMELEON_I2C_OK && index < wlen; ++index) {
        status = write_byte(bus, wdata[index], transaction_start);
    }
    if (status == CHAMELEON_I2C_OK) {
        status = start_condition(bus, transaction_start);
    }
    if (status == CHAMELEON_I2C_OK) {
        status = write_byte(bus, (uint8_t)((addr7 << 1) | 1U), transaction_start);
    }
    for (index = 0U; status == CHAMELEON_I2C_OK && index < rlen; ++index) {
        status = read_byte(bus, &rdata[index], index + 1U == rlen,
                           transaction_start);
    }
    return finish_transaction(bus, status);
}

chameleon_i2c_status_t chameleon_soft_i2c_bus_clear(chameleon_soft_i2c_t *bus)
{
    chameleon_i2c_status_t status = CHAMELEON_I2C_OK;
    uint32_t transaction_start;
    unsigned pulse;

    if (bus == NULL || bus->active == 0U) {
        return CHAMELEON_I2C_ERR_BUS;
    }
    transaction_start = bus->ops.micros(bus->ops.context);
    bus->ops.sda_release(bus->ops.context);
    for (pulse = 0U; pulse < 9U; ++pulse) {
        uint32_t byte_start;

        if (bus->ops.sda_read(bus->ops.context)) {
            break;
        }
        bus->ops.scl_low(bus->ops.context);
        bus->ops.delay_us(bus->ops.context, CHAMELEON_SOFT_I2C_HALF_PERIOD_US);
        byte_start = bus->ops.micros(bus->ops.context);
        status = release_scl_and_wait(bus, byte_start, transaction_start);
        if (status != CHAMELEON_I2C_OK) {
            break;
        }
        bus->ops.delay_us(bus->ops.context, CHAMELEON_SOFT_I2C_HALF_PERIOD_US);
    }
    if (status == CHAMELEON_I2C_OK && !bus->ops.sda_read(bus->ops.context)) {
        status = CHAMELEON_I2C_ERR_BUS;
    }
    return finish_transaction(bus, status);
}
