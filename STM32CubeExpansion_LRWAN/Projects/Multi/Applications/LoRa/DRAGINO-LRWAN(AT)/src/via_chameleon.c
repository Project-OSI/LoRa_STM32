#include "via_chameleon.h"

#include <string.h>

static uint32_t elapsed_ms(uint32_t start, uint32_t now)
{
    return now - start;
}

static uint32_t le32(const uint8_t *p)
{
    return ((uint32_t)p[0])
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static int16_t le16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0]) | ((uint16_t)p[1] << 8));
}

static chameleon_i2c_status_t read_register(uint8_t command,
                                            uint8_t *data,
                                            size_t length)
{
    return chameleon_board_i2c_write_read(CHAMELEON_I2C_ADDR_7BIT,
                                          &command, 1U, data, length);
}

chameleon_result_t via_chameleon_probe(void)
{
    chameleon_i2c_status_t status =
        chameleon_board_i2c_write(CHAMELEON_I2C_ADDR_7BIT, 0, 0U);
    return status == CHAMELEON_I2C_OK
        ? CHAMELEON_RESULT_OK
        : CHAMELEON_RESULT_NO_DEVICE;
}

chameleon_result_t via_chameleon_trigger(void)
{
    uint8_t command = CHAMELEON_CMD_TRIGGER;
    return chameleon_board_i2c_write(CHAMELEON_I2C_ADDR_7BIT, &command, 1U)
            == CHAMELEON_I2C_OK
        ? CHAMELEON_RESULT_OK
        : CHAMELEON_RESULT_TRIGGER_FAILED;
}

chameleon_result_t via_chameleon_wait_ready(uint32_t timeout_ms)
{
    const uint32_t start = chameleon_board_millis();
    int first_poll = 1;

    for (;;) {
        uint8_t status = 0U;
        uint32_t elapsed = elapsed_ms(start, chameleon_board_millis());
        uint32_t delay;

        if (!first_poll && elapsed >= timeout_ms) {
            return CHAMELEON_RESULT_MEASUREMENT_TIMEOUT;
        }
        first_poll = 0;
        if (read_register(CHAMELEON_CMD_STATUS, &status, 1U)
                != CHAMELEON_I2C_OK) {
            return CHAMELEON_RESULT_STATUS_IO_FAILED;
        }
        if (status == CHAMELEON_STATUS_READY) {
            return CHAMELEON_RESULT_OK;
        }

        elapsed = elapsed_ms(start, chameleon_board_millis());
        if (elapsed >= timeout_ms) {
            return CHAMELEON_RESULT_MEASUREMENT_TIMEOUT;
        }
        delay = timeout_ms - elapsed;
        if (delay > CHAMELEON_POLL_INTERVAL_MS) {
            delay = CHAMELEON_POLL_INTERVAL_MS;
        }
        chameleon_board_delay_ms(delay);
    }
}

static void apply_sentinel_flags(chameleon_sample_t *sample)
{
    int all_ff = 1;
    size_t i;

    if (sample->soil_temp_c_x100 == CHAMELEON_TEMP_SENTINEL_X100) {
        sample->status_flags |= CHAMELEON_FLAG_TEMP_FAULT;
    }
    for (i = 0U; i < sizeof(sample->array_id); ++i) {
        if (sample->array_id[i] != 0xFFU) {
            all_ff = 0;
            break;
        }
    }
    if (all_ff) {
        sample->status_flags |= CHAMELEON_FLAG_ID_FAULT;
    }
    if (sample->r1_ohm_comp == CHAMELEON_RES_OPEN_OHMS
            || sample->r1_ohm_raw == CHAMELEON_RES_OPEN_OHMS) {
        sample->status_flags |= CHAMELEON_FLAG_CH1_OPEN;
    }
    if (sample->r2_ohm_comp == CHAMELEON_RES_OPEN_OHMS
            || sample->r2_ohm_raw == CHAMELEON_RES_OPEN_OHMS) {
        sample->status_flags |= CHAMELEON_FLAG_CH2_OPEN;
    }
    if (sample->r3_ohm_comp == CHAMELEON_RES_OPEN_OHMS
            || sample->r3_ohm_raw == CHAMELEON_RES_OPEN_OHMS) {
        sample->status_flags |= CHAMELEON_FLAG_CH3_OPEN;
    }
}

chameleon_result_t via_chameleon_read_sample(chameleon_sample_t *sample)
{
    static const uint8_t commands[] = {
        CHAMELEON_CMD_TEMP,
        CHAMELEON_CMD_RES_CAL1,
        CHAMELEON_CMD_RES_CAL2,
        CHAMELEON_CMD_RES_CAL3,
        CHAMELEON_CMD_RES_RAW1,
        CHAMELEON_CMD_RES_RAW2,
        CHAMELEON_CMD_RES_RAW3,
        CHAMELEON_CMD_ID
    };
    uint8_t buffer[8];
    size_t successful_reads = 0U;
    size_t i;

    if (sample == 0) {
        return CHAMELEON_RESULT_READ_FAILED;
    }

    for (i = 0U; i < sizeof(commands); ++i) {
        const uint8_t command = commands[i];
        size_t length = command == CHAMELEON_CMD_TEMP ? 2U
                      : command == CHAMELEON_CMD_ID ? 8U : 4U;
        chameleon_i2c_status_t status = read_register(command, buffer, length);

        if (status != CHAMELEON_I2C_OK) {
            continue;
        }
        ++successful_reads;
        switch (command) {
        case CHAMELEON_CMD_TEMP:     sample->soil_temp_c_x100 = le16(buffer); break;
        case CHAMELEON_CMD_RES_CAL1: sample->r1_ohm_comp = le32(buffer); break;
        case CHAMELEON_CMD_RES_CAL2: sample->r2_ohm_comp = le32(buffer); break;
        case CHAMELEON_CMD_RES_CAL3: sample->r3_ohm_comp = le32(buffer); break;
        case CHAMELEON_CMD_RES_RAW1: sample->r1_ohm_raw = le32(buffer); break;
        case CHAMELEON_CMD_RES_RAW2: sample->r2_ohm_raw = le32(buffer); break;
        case CHAMELEON_CMD_RES_RAW3: sample->r3_ohm_raw = le32(buffer); break;
        case CHAMELEON_CMD_ID:       memcpy(sample->array_id, buffer, 8U); break;
        default: break;
        }
    }

    apply_sentinel_flags(sample);
    if (successful_reads == 0U) {
        return CHAMELEON_RESULT_READ_FAILED;
    }
    if (successful_reads != sizeof(commands)) {
        return CHAMELEON_RESULT_PARTIAL_SAMPLE;
    }
    return CHAMELEON_RESULT_OK;
}

chameleon_result_t via_chameleon_measure(chameleon_sample_t *sample,
                                         uint32_t timeout_ms)
{
    chameleon_result_t result;

    result = via_chameleon_trigger();
    if (result != CHAMELEON_RESULT_OK) {
        return result;
    }
    result = via_chameleon_wait_ready(timeout_ms);
    if (result != CHAMELEON_RESULT_OK) {
        return result;
    }
    return via_chameleon_read_sample(sample);
}

static void apply_transport_flags(chameleon_sample_t *sample,
                                  chameleon_result_t result)
{
    if (result == CHAMELEON_RESULT_NO_DEVICE
            || result == CHAMELEON_RESULT_READ_FAILED
            || result == CHAMELEON_RESULT_PARTIAL_SAMPLE) {
        sample->status_flags |= CHAMELEON_FLAG_I2C_MISSING;
    } else if (result != CHAMELEON_RESULT_OK) {
        sample->status_flags |= CHAMELEON_FLAG_TIMEOUT;
    }
}

int via_chameleon_acquire(chameleon_sample_t *sample, uint16_t timeout_ms)
{
    chameleon_result_t result;

    if (sample == 0) {
        return 0;
    }
    memset(sample, 0, sizeof(*sample));

#ifdef CHAMELEON_DUMMY
    {
        static const uint8_t dummy_id[8] = {
            0xDE, 0xAD, 0xBE, 0xEF, 0xDE, 0xAD, 0xBE, 0xEF
        };
        sample->soil_temp_c_x100 = 2000;
        sample->r1_ohm_comp = 1600U;
        sample->r2_ohm_comp = 100000U;
        sample->r3_ohm_comp = 1600000U;
        sample->r1_ohm_raw = 1600U;
        sample->r2_ohm_raw = 100000U;
        sample->r3_ohm_raw = 1600000U;
        memcpy(sample->array_id, dummy_id, sizeof(dummy_id));
        sample->battery_mv = chameleon_board_battery_mv();
        (void)timeout_ms;
        return 1;
    }
#endif

    result = via_chameleon_probe();
    if (result == CHAMELEON_RESULT_OK) {
        result = via_chameleon_measure(sample, timeout_ms);
    }
    apply_transport_flags(sample, result);
    sample->battery_mv = chameleon_board_battery_mv();
    return result == CHAMELEON_RESULT_NO_DEVICE ? 0 : 1;
}
