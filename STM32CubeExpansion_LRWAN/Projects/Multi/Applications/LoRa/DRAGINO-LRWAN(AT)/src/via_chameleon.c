#include "via_chameleon.h"
#include <string.h>

static uint32_t le32(const uint8_t *p) {
    return ((uint32_t)p[0])
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static int16_t le16(const uint8_t *p) {
    return (int16_t)(((uint16_t)p[0]) | ((uint16_t)p[1] << 8));
}

int via_chameleon_probe(void) {
    return chameleon_board_i2c_write(CHAMELEON_I2C_ADDR_7BIT, 0, 0) == CHAMELEON_I2C_OK ? 1 : 0;
}

int via_chameleon_trigger(void) {
    uint8_t cmd = CHAMELEON_CMD_TRIGGER;
    return chameleon_board_i2c_write(CHAMELEON_I2C_ADDR_7BIT, &cmd, 1) == CHAMELEON_I2C_OK ? 1 : 0;
}

static int read_status_byte(uint8_t *out) {
    uint8_t cmd = CHAMELEON_CMD_STATUS;
    return chameleon_board_i2c_write_read(CHAMELEON_I2C_ADDR_7BIT, &cmd, 1, out, 1) == CHAMELEON_I2C_OK ? 1 : 0;
}

int via_chameleon_wait_ready(uint16_t timeout_ms) {
    uint32_t elapsed = 0;
    while (elapsed <= timeout_ms) {
        uint8_t status = 0;
        if (read_status_byte(&status) && status == CHAMELEON_STATUS_READY) {
            return 1;
        }
        chameleon_board_delay_ms(CHAMELEON_POLL_INTERVAL_MS);
        elapsed += CHAMELEON_POLL_INTERVAL_MS;
    }
    return 0;
}

int via_chameleon_read_sample(chameleon_sample_t *sample) {
    if (sample == 0) return 0;

    int all_ok = 1;
    uint8_t cmd;
    uint8_t buf4[4];
    uint8_t buf2[2];
    static const uint8_t comp_cmds[3] = {
        CHAMELEON_CMD_RES_CAL1,
        CHAMELEON_CMD_RES_CAL2,
        CHAMELEON_CMD_RES_CAL3
    };
    static const uint8_t raw_cmds[3] = {
        CHAMELEON_CMD_RES_RAW1,
        CHAMELEON_CMD_RES_RAW2,
        CHAMELEON_CMD_RES_RAW3
    };
    uint32_t *comp_outs[3] = {
        &sample->r1_ohm_comp,
        &sample->r2_ohm_comp,
        &sample->r3_ohm_comp
    };
    uint32_t *raw_outs[3] = {
        &sample->r1_ohm_raw,
        &sample->r2_ohm_raw,
        &sample->r3_ohm_raw
    };

    cmd = CHAMELEON_CMD_TEMP;
    if (chameleon_board_i2c_write_read(CHAMELEON_I2C_ADDR_7BIT, &cmd, 1, buf2, 2) == CHAMELEON_I2C_OK) {
        sample->soil_temp_c_x100 = le16(buf2);
    } else {
        sample->soil_temp_c_x100 = 0;
        all_ok = 0;
    }

    /* Settle: STATUS_READY can fire before CAL2/CAL3 are populated on the
     * peripheral side. Wait unconditionally before reading any CAL register. */
    chameleon_board_delay_ms(CHAMELEON_POST_READY_SETTLE_MS);

    for (int i = 0; i < 3; i++) {
        cmd = comp_cmds[i];
        if (chameleon_board_i2c_write_read(CHAMELEON_I2C_ADDR_7BIT, &cmd, 1, buf4, 4) == CHAMELEON_I2C_OK) {
            *comp_outs[i] = le32(buf4);
        } else {
            *comp_outs[i] = 0;
            all_ok = 0;
        }

        cmd = raw_cmds[i];
        if (chameleon_board_i2c_write_read(CHAMELEON_I2C_ADDR_7BIT, &cmd, 1, buf4, 4) == CHAMELEON_I2C_OK) {
            *raw_outs[i] = le32(buf4);
        } else {
            *raw_outs[i] = 0;
            all_ok = 0;
        }

        /* Retry CAL[i] when the peripheral returned CAL == RAW for a
         * non-open channel — that is the pass-through signature observed on
         * kaba100 when STATUS_READY fires before per-channel compensation
         * has actually been computed. */
        if (*comp_outs[i] == *raw_outs[i] &&
            *raw_outs[i] != CHAMELEON_RES_OPEN_OHMS) {
            for (uint8_t r = 0; r < CHAMELEON_CAL_RETRY_COUNT; r++) {
                chameleon_board_delay_ms(CHAMELEON_CAL_RETRY_DELAY_MS);
                cmd = comp_cmds[i];
                if (chameleon_board_i2c_write_read(CHAMELEON_I2C_ADDR_7BIT,
                                                   &cmd, 1, buf4, 4)
                    == CHAMELEON_I2C_OK) {
                    *comp_outs[i] = le32(buf4);
                }
                if (*comp_outs[i] != *raw_outs[i]) break;
            }
        }
    }

    cmd = CHAMELEON_CMD_ID;
    if (chameleon_board_i2c_write_read(CHAMELEON_I2C_ADDR_7BIT, &cmd, 1, sample->array_id, 8) != CHAMELEON_I2C_OK) {
        memset(sample->array_id, 0, 8);
        all_ok = 0;
    }

    if (sample->soil_temp_c_x100 == CHAMELEON_TEMP_SENTINEL_X100) {
        sample->status_flags |= CHAMELEON_FLAG_TEMP_FAULT;
    }
    {
        int all_ff = 1;
        for (int i = 0; i < 8; i++) {
            if (sample->array_id[i] != 0xFF) {
                all_ff = 0;
                break;
            }
        }
        if (all_ff) sample->status_flags |= CHAMELEON_FLAG_ID_FAULT;
    }
    if (sample->r1_ohm_comp == CHAMELEON_RES_OPEN_OHMS || sample->r1_ohm_raw == CHAMELEON_RES_OPEN_OHMS) {
        sample->status_flags |= CHAMELEON_FLAG_CH1_OPEN;
    }
    if (sample->r2_ohm_comp == CHAMELEON_RES_OPEN_OHMS || sample->r2_ohm_raw == CHAMELEON_RES_OPEN_OHMS) {
        sample->status_flags |= CHAMELEON_FLAG_CH2_OPEN;
    }
    if (sample->r3_ohm_comp == CHAMELEON_RES_OPEN_OHMS || sample->r3_ohm_raw == CHAMELEON_RES_OPEN_OHMS) {
        sample->status_flags |= CHAMELEON_FLAG_CH3_OPEN;
    }

    return all_ok;
}

int via_chameleon_acquire(chameleon_sample_t *sample, uint16_t timeout_ms) {
    if (sample == 0) return 0;
    memset(sample, 0, sizeof(*sample));

#ifdef CHAMELEON_DUMMY
    static const uint8_t dummy_id[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0xDE, 0xAD, 0xBE, 0xEF};
    sample->soil_temp_c_x100 = 2000;
    sample->r1_ohm_comp      = 1600U;
    sample->r2_ohm_comp      = 100000U;
    sample->r3_ohm_comp      = 1600000U;
    sample->r1_ohm_raw       = 1600U;
    sample->r2_ohm_raw       = 100000U;
    sample->r3_ohm_raw       = 1600000U;
    for (int i = 0; i < 8; i++) sample->array_id[i] = dummy_id[i];
    sample->battery_mv       = chameleon_board_battery_mv();
    sample->status_flags     = 0;
    (void)timeout_ms;
    return 1;
#endif

    if (!via_chameleon_probe()) {
        sample->status_flags |= CHAMELEON_FLAG_I2C_MISSING;
        sample->battery_mv = chameleon_board_battery_mv();
        return 0;
    }

    if (!via_chameleon_trigger()) {
        sample->status_flags |= CHAMELEON_FLAG_TIMEOUT;
        sample->battery_mv = chameleon_board_battery_mv();
        return 1;
    }

    if (!via_chameleon_wait_ready(timeout_ms)) {
        sample->status_flags |= CHAMELEON_FLAG_TIMEOUT;
        sample->battery_mv = chameleon_board_battery_mv();
        return 1;
    }

    if (!via_chameleon_read_sample(sample)) {
        sample->status_flags |= CHAMELEON_FLAG_I2C_MISSING;
    }
    sample->battery_mv = chameleon_board_battery_mv();
    return 1;
}
