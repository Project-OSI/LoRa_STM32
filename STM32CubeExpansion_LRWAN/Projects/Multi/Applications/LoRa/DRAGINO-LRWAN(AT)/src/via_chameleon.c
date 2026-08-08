#include "via_chameleon.h"
#include <string.h>

#ifdef STM32L072xx
#include "hw.h"

/*
 * Chameleon v1.x LSN50V2 wiring
 * -----------------------------
 * PB12  -> P-channel MOSFET gate (open-drain control, external pull-up to VDD)
 * PB13  -> I2C2 SCL
 * PB14  -> I2C2 SDA
 * VDD   -> P-MOSFET source
 * MOSFET drain -> Chameleon VCC and both external I2C pull-ups
 * GND   -> Chameleon GND
 *
 * PB6/PB7 are deliberately not used. The LSN50V2 board has permanent pull-ups
 * on those I2C1 pins, which can back-power an otherwise switched-off slave.
 */
#define CHAMELEON_PWR_PORT        GPIOB
#define CHAMELEON_PWR_PIN         GPIO_PIN_12
#define CHAMELEON_SCL_PORT        GPIOB
#define CHAMELEON_SCL_PIN         GPIO_PIN_13
#define CHAMELEON_SDA_PORT        GPIOB
#define CHAMELEON_SDA_PIN         GPIO_PIN_14
#define CHAMELEON_I2C_AF          GPIO_AF5_I2C2
#define CHAMELEON_I2C_TIMING      0x00B1112EU

extern I2C_HandleTypeDef I2cHandle1;

static int chameleon_hw_session_begin(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB14 is normally configured by stock LSN50 firmware as EXTI14. MOD=3
     * Chameleon builds use it as SDA instead, so mask the EXTI line before the
     * bus becomes active. Leaving EXTI14 armed would turn normal SDA edges into
     * interrupts. */
    EXTI->IMR  &= ~(uint32_t)CHAMELEON_SDA_PIN;
    EXTI->RTSR &= ~(uint32_t)CHAMELEON_SDA_PIN;
    EXTI->FTSR &= ~(uint32_t)CHAMELEON_SDA_PIN;
    __HAL_GPIO_EXTI_CLEAR_IT(CHAMELEON_SDA_PIN);

    /* Set the P-MOSFET control OFF before changing PB12 to an output. With
     * open-drain mode, SET means high impedance; the external gate-to-source
     * pull-up therefore keeps the MOSFET off without driving above MCU VDD. */
    HAL_GPIO_WritePin(CHAMELEON_PWR_PORT, CHAMELEON_PWR_PIN, GPIO_PIN_SET);
    memset(&gpio, 0, sizeof(gpio));
    gpio.Pin   = CHAMELEON_PWR_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(CHAMELEON_PWR_PORT, &gpio);

    /* Power on: pulling the P-MOSFET gate low connects LSN50 VDD to the
     * Chameleon board. */
    HAL_GPIO_WritePin(CHAMELEON_PWR_PORT, CHAMELEON_PWR_PIN, GPIO_PIN_RESET);
    HAL_Delay(CHAMELEON_POWER_SETTLE_MS);

    /* Configure the exposed I2C2 pair. Pull-ups must be external and tied to
     * switched Chameleon VCC so SDA/SCL collapse when the sensor is off. */
    gpio.Pin       = CHAMELEON_SCL_PIN | CHAMELEON_SDA_PIN;
    gpio.Mode      = GPIO_MODE_AF_OD;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = CHAMELEON_I2C_AF;
    HAL_GPIO_Init(GPIOB, &gpio);

    __HAL_RCC_I2C2_CLK_ENABLE();
    __HAL_RCC_I2C2_FORCE_RESET();
    __HAL_RCC_I2C2_RELEASE_RESET();

    /* Reuse the existing handle used by the Chameleon board abstraction, but
     * point it at I2C2. The I2C2 peripheral clock and GPIOs are configured
     * explicitly above; this avoids the stock HAL_I2C_MspInit(), which is
     * hard-wired to I2C1/PB6/PB7 in Dragino's BSP. */
    I2cHandle1.Instance              = I2C2;
    I2cHandle1.State                 = HAL_I2C_STATE_RESET;
    I2cHandle1.Init.Timing           = CHAMELEON_I2C_TIMING;
    I2cHandle1.Init.OwnAddress1      = 0xF0;
    I2cHandle1.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    I2cHandle1.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    I2cHandle1.Init.OwnAddress2      = 0xFE;
    I2cHandle1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    I2cHandle1.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    I2cHandle1.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;

    /* HAL_I2C_Init would call Dragino's generic MSP init when State==RESET and
     * incorrectly configure PB6/PB7. Set READY temporarily so HAL configures
     * the peripheral registers without invoking that board callback. */
    I2cHandle1.State = HAL_I2C_STATE_READY;
    if (HAL_I2C_Init(&I2cHandle1) != HAL_OK) {
        HAL_GPIO_WritePin(CHAMELEON_PWR_PORT, CHAMELEON_PWR_PIN, GPIO_PIN_SET);
        HAL_GPIO_DeInit(GPIOB, CHAMELEON_SCL_PIN | CHAMELEON_SDA_PIN);
        __HAL_RCC_I2C2_CLK_DISABLE();
        return 0;
    }

    return 1;
}

static void chameleon_hw_session_end(void)
{
    GPIO_InitTypeDef gpio;

    /* Stop the peripheral first, then isolate both bus lines before removing
     * sensor power. This prevents the MCU from sourcing the unpowered slave
     * through its I/O protection structures. */
    __HAL_I2C_DISABLE(&I2cHandle1);
    __HAL_RCC_I2C2_CLK_DISABLE();

    memset(&gpio, 0, sizeof(gpio));
    gpio.Pin  = CHAMELEON_SCL_PIN | CHAMELEON_SDA_PIN;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* Release the open-drain gate control. The external pull-up turns the
     * P-MOSFET off. */
    HAL_GPIO_WritePin(CHAMELEON_PWR_PORT, CHAMELEON_PWR_PIN, GPIO_PIN_SET);
}
#endif /* STM32L072xx */

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

    /* If any non-open channel finished with CAL == RAW, flag the sample as
     * compensation-pending so the back end can mark this row invalid. */
    for (int i = 0; i < 3; i++) {
        if (*comp_outs[i] == *raw_outs[i] &&
            *raw_outs[i] != CHAMELEON_RES_OPEN_OHMS) {
            sample->status_flags |= CHAMELEON_FLAG_COMP_PENDING;
            break;
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
    int result = 0;

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

#ifdef STM32L072xx
    if (!chameleon_hw_session_begin()) {
        sample->status_flags |= CHAMELEON_FLAG_I2C_MISSING;
        sample->battery_mv = chameleon_board_battery_mv();
        chameleon_hw_session_end();
        return 0;
    }
#endif

    if (!via_chameleon_probe()) {
        sample->status_flags |= CHAMELEON_FLAG_I2C_MISSING;
        sample->battery_mv = chameleon_board_battery_mv();
        result = 0;
        goto done;
    }

    if (!via_chameleon_trigger()) {
        sample->status_flags |= CHAMELEON_FLAG_TIMEOUT;
        sample->battery_mv = chameleon_board_battery_mv();
        result = 1;
        goto done;
    }

    if (!via_chameleon_wait_ready(timeout_ms)) {
        sample->status_flags |= CHAMELEON_FLAG_TIMEOUT;
        sample->battery_mv = chameleon_board_battery_mv();
        result = 1;
        goto done;
    }

    if (!via_chameleon_read_sample(sample)) {
        sample->status_flags |= CHAMELEON_FLAG_I2C_MISSING;
    }
    sample->battery_mv = chameleon_board_battery_mv();
    result = 1;

done:
#ifdef STM32L072xx
    chameleon_hw_session_end();
#endif
    return result;
}
