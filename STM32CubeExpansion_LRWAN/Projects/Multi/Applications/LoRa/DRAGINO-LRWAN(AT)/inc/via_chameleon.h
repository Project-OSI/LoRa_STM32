#ifndef VIA_CHAMELEON_H
#define VIA_CHAMELEON_H

#include <stddef.h>
#include <stdint.h>

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

#define CHAMELEON_TEMP_SENTINEL_X100  ((int16_t)-12700)
#define CHAMELEON_RES_OPEN_OHMS       10000000U

typedef enum {
    CHAMELEON_I2C_OK = 0,
    CHAMELEON_I2C_ERR_NACK,
    CHAMELEON_I2C_ERR_BUS,
    CHAMELEON_I2C_ERR_TIMEOUT,
    CHAMELEON_I2C_ERR_SHORT
} chameleon_i2c_status_t;

typedef enum {
    CHAMELEON_RESULT_OK = 0,
    CHAMELEON_RESULT_I2C_INIT_FAILED,
    CHAMELEON_RESULT_NO_DEVICE,
    CHAMELEON_RESULT_TRIGGER_FAILED,
    CHAMELEON_RESULT_STATUS_IO_FAILED,
    CHAMELEON_RESULT_MEASUREMENT_TIMEOUT,
    CHAMELEON_RESULT_READ_FAILED,
    CHAMELEON_RESULT_PARTIAL_SAMPLE
} chameleon_result_t;

/* The board adapter implements one I2C transaction per call. write_read must
 * use a repeated start between the one-byte register command and the read. */
chameleon_i2c_status_t chameleon_board_i2c_write(uint8_t addr7,
                                                 const uint8_t *data,
                                                 size_t len);
chameleon_i2c_status_t chameleon_board_i2c_write_read(uint8_t addr7,
                                                      const uint8_t *wdata,
                                                      size_t wlen,
                                                      uint8_t *rdata,
                                                      size_t rlen);
void                   chameleon_board_delay_ms(uint32_t ms);
uint32_t               chameleon_board_millis(void);
uint16_t               chameleon_board_battery_mv(void);

chameleon_result_t via_chameleon_probe(void);
chameleon_result_t via_chameleon_trigger(void);
chameleon_result_t via_chameleon_wait_ready(uint32_t timeout_ms);
chameleon_result_t via_chameleon_read_sample(chameleon_sample_t *sample);
chameleon_result_t via_chameleon_measure(chameleon_sample_t *sample,
                                         uint32_t timeout_ms);

/* Compatibility wrapper for the existing MOD3 integration and host tests.
 * Hardware lifecycle code should call probe/measure inside an owned session. */
int via_chameleon_acquire(chameleon_sample_t *sample, uint16_t timeout_ms);

#endif /* VIA_CHAMELEON_H */
