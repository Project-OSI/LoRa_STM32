#ifndef VIA_CHAMELEON_H
#define VIA_CHAMELEON_H

#include <stdint.h>
#include <stddef.h>
#include "chameleon_payload.h"

#define CHAMELEON_I2C_ADDR_7BIT       0x08U
#define CHAMELEON_CMD_TEMP            0x01U
#define CHAMELEON_CMD_RES_CAL1        0x11U
#define CHAMELEON_CMD_RES_CAL2        0x12U
#define CHAMELEON_CMD_RES_CAL3        0x13U
#define CHAMELEON_CMD_RES_RAW1        0x21U
#define CHAMELEON_CMD_RES_RAW2        0x22U
#define CHAMELEON_CMD_RES_RAW3        0x23U
#define CHAMELEON_CMD_ID              0x30U
#define CHAMELEON_CMD_TRIGGER         0x40U
#define CHAMELEON_CMD_STATUS          0x41U

#define CHAMELEON_STATUS_READY        0x01U

#define CHAMELEON_DEFAULT_TIMEOUT_MS  2000U
#define CHAMELEON_POLL_INTERVAL_MS    50U
#define CHAMELEON_POST_READY_SETTLE_MS  250U   /* blind settle after STATUS_READY before reading CAL/RAW */
#define CHAMELEON_CAL_RETRY_DELAY_MS    150U   /* delay between first read and the single retry */
#define CHAMELEON_CAL_RETRY_COUNT       1U     /* retries per channel when CAL[i] == RAW[i] */

#define CHAMELEON_TEMP_SENTINEL_X100  ((int16_t)-12700)
#define CHAMELEON_RES_OPEN_OHMS       10000000U

typedef enum {
    CHAMELEON_I2C_OK = 0,
    CHAMELEON_I2C_ERR_NACK,
    CHAMELEON_I2C_ERR_BUS,
    CHAMELEON_I2C_ERR_TIMEOUT
} chameleon_i2c_status_t;

chameleon_i2c_status_t chameleon_board_i2c_write(uint8_t addr7,
                                                 const uint8_t *data,
                                                 size_t len);
chameleon_i2c_status_t chameleon_board_i2c_write_read(uint8_t addr7,
                                                      const uint8_t *wdata,
                                                      size_t wlen,
                                                      uint8_t *rdata,
                                                      size_t rlen);
void                   chameleon_board_delay_ms(uint32_t ms);
uint16_t               chameleon_board_battery_mv(void);

int  via_chameleon_probe(void);
int  via_chameleon_trigger(void);
int  via_chameleon_wait_ready(uint16_t timeout_ms);
int  via_chameleon_read_sample(chameleon_sample_t *sample);
/* Returns 0 only when no Chameleon device is present or sample is NULL.
 * Returns 1 when a fixed-shape sample is populated; callers must inspect
 * sample->status_flags before trusting trailing Chameleon measurement fields. */
int  via_chameleon_acquire(chameleon_sample_t *sample, uint16_t timeout_ms);

#endif /* VIA_CHAMELEON_H */
