#include "mock_chameleon_i2c.h"
#include <string.h>

static int      g_present                  = 1;
static uint8_t  g_status_after_trigger     = CHAMELEON_STATUS_READY;
static uint8_t  g_status_polls_until_ready = 0;
static uint8_t  g_status_polls_seen        = 0;
static int16_t  g_temp_x100                = 1987;
static uint32_t g_res_comp[3]              = {1100U, 10100U, 101200U};
static uint32_t g_res_raw[3]               = {1200U, 10200U, 102200U};
static uint32_t g_res_comp_first[3]        = {1100U, 10100U, 101200U};
static uint8_t  g_res_comp_use_first[3]    = {0U, 0U, 0U};   /* 1 = sequence mode active */
static uint8_t  g_res_comp_reads[3]        = {0U, 0U, 0U};   /* per-channel CAL reads seen */
static uint8_t  g_id[8]                    = {0x28, 0x6D, 0x6A, 0xDB, 0x0F, 0x00, 0x00, 0xF1};
static uint16_t g_battery_mv               = 3300;
static uint8_t  g_fail_command             = 0;

static size_t   g_trigger_count            = 0;
static size_t   g_status_poll_count        = 0;
static size_t   g_total_delay_ms           = 0;

void mock_chameleon_reset(void) {
    g_present                  = 1;
    g_status_after_trigger     = CHAMELEON_STATUS_READY;
    g_status_polls_until_ready = 0;
    g_status_polls_seen        = 0;
    g_temp_x100                = 1987;
    g_res_comp[0] = 1100U; g_res_comp[1] = 10100U; g_res_comp[2] = 101200U;
    g_res_raw[0]  = 1200U; g_res_raw[1]  = 10200U; g_res_raw[2]  = 102200U;
    g_res_comp_first[0] = 1100U; g_res_comp_first[1] = 10100U; g_res_comp_first[2] = 101200U;
    g_res_comp_use_first[0] = g_res_comp_use_first[1] = g_res_comp_use_first[2] = 0U;
    g_res_comp_reads[0] = g_res_comp_reads[1] = g_res_comp_reads[2] = 0U;
    {
        static const uint8_t default_id[8] = {0x28, 0x6D, 0x6A, 0xDB, 0x0F, 0x00, 0x00, 0xF1};
        memcpy(g_id, default_id, 8);
    }
    g_battery_mv               = 3300;
    g_fail_command             = 0;
    g_trigger_count            = 0;
    g_status_poll_count        = 0;
    g_total_delay_ms           = 0;
}

void mock_chameleon_set_present(int present)             { g_present = present; }
void mock_chameleon_set_status_after_trigger(uint8_t v)  { g_status_after_trigger = v; }
void mock_chameleon_set_status_ready_after_polls(uint8_t n) {
    g_status_polls_until_ready = n;
    g_status_polls_seen        = 0;
}
void mock_chameleon_set_temp_x100(int16_t v)             { g_temp_x100 = v; }
void mock_chameleon_set_resistance(uint8_t ch, uint32_t v) {
    if (ch < 3) {
        g_res_comp[ch] = v;
        g_res_raw[ch] = v;
    }
}
void mock_chameleon_set_resistance_comp(uint8_t ch, uint32_t v) {
    if (ch < 3) {
        g_res_comp[ch] = v;
        g_res_comp_use_first[ch] = 0U;
        g_res_comp_reads[ch] = 0U;
    }
}
void mock_chameleon_set_resistance_raw(uint8_t ch, uint32_t v) {
    if (ch < 3) g_res_raw[ch] = v;
}
void mock_chameleon_set_resistance_comp_sequence(uint8_t ch,
                                                 uint32_t first,
                                                 uint32_t subsequent) {
    if (ch < 3) {
        g_res_comp_first[ch]     = first;
        g_res_comp[ch]           = subsequent;
        g_res_comp_use_first[ch] = 1U;
        g_res_comp_reads[ch]     = 0U;
    }
}
void mock_chameleon_set_id(const uint8_t id[8])          { memcpy(g_id, id, 8); }
void mock_chameleon_set_battery_mv(uint16_t v)           { g_battery_mv = v; }
void mock_chameleon_fail_command(uint8_t cmd)            { g_fail_command = cmd; }

size_t mock_chameleon_trigger_count(void)        { return g_trigger_count; }
size_t mock_chameleon_status_poll_count(void)    { return g_status_poll_count; }
size_t mock_chameleon_total_delay_ms(void)       { return g_total_delay_ms; }
size_t mock_chameleon_comp_read_count(uint8_t ch) {
    return ch < 3U ? g_res_comp_reads[ch] : 0U;
}

chameleon_i2c_status_t chameleon_board_i2c_write(uint8_t addr7,
                                                 const uint8_t *data,
                                                 size_t len) {
    if (!g_present) return CHAMELEON_I2C_ERR_NACK;
    if (addr7 != CHAMELEON_I2C_ADDR_7BIT) return CHAMELEON_I2C_ERR_NACK;
    if (len == 0) return CHAMELEON_I2C_OK;
    if (len == 1 && data[0] == CHAMELEON_CMD_TRIGGER) {
        g_trigger_count++;
        g_status_polls_seen = 0;
        return CHAMELEON_I2C_OK;
    }
    return CHAMELEON_I2C_OK;
}

chameleon_i2c_status_t chameleon_board_i2c_write_read(uint8_t addr7,
                                                      const uint8_t *wdata,
                                                      size_t wlen,
                                                      uint8_t *rdata,
                                                      size_t rlen) {
    if (!g_present) return CHAMELEON_I2C_ERR_NACK;
    if (addr7 != CHAMELEON_I2C_ADDR_7BIT) return CHAMELEON_I2C_ERR_NACK;
    if (wlen != 1) return CHAMELEON_I2C_ERR_BUS;
    if (wdata[0] == g_fail_command) return CHAMELEON_I2C_ERR_BUS;

    switch (wdata[0]) {
    case CHAMELEON_CMD_STATUS:
        if (rlen != 1) return CHAMELEON_I2C_ERR_BUS;
        g_status_poll_count++;
        if (g_status_polls_seen < g_status_polls_until_ready) {
            g_status_polls_seen++;
            rdata[0] = 0x00;
        } else {
            rdata[0] = g_status_after_trigger;
        }
        return CHAMELEON_I2C_OK;
    case CHAMELEON_CMD_TEMP:
        if (rlen != 2) return CHAMELEON_I2C_ERR_BUS;
        rdata[0] = (uint8_t)(g_temp_x100 & 0xFF);
        rdata[1] = (uint8_t)(((uint16_t)g_temp_x100 >> 8) & 0xFF);
        return CHAMELEON_I2C_OK;
    case CHAMELEON_CMD_RES_CAL1:
    case CHAMELEON_CMD_RES_CAL2:
    case CHAMELEON_CMD_RES_CAL3: {
        if (rlen != 4) return CHAMELEON_I2C_ERR_BUS;
        uint8_t ch = (uint8_t)(wdata[0] - CHAMELEON_CMD_RES_CAL1);
        uint32_t v;
        if (g_res_comp_use_first[ch] && g_res_comp_reads[ch] == 0U) {
            v = g_res_comp_first[ch];
        } else {
            v = g_res_comp[ch];
        }
        if (g_res_comp_reads[ch] < 255U) g_res_comp_reads[ch]++;
        rdata[0] = (uint8_t)(v & 0xFF);
        rdata[1] = (uint8_t)((v >> 8) & 0xFF);
        rdata[2] = (uint8_t)((v >> 16) & 0xFF);
        rdata[3] = (uint8_t)((v >> 24) & 0xFF);
        return CHAMELEON_I2C_OK;
    }
    case CHAMELEON_CMD_RES_RAW1:
    case CHAMELEON_CMD_RES_RAW2:
    case CHAMELEON_CMD_RES_RAW3: {
        if (rlen != 4) return CHAMELEON_I2C_ERR_BUS;
        uint32_t v = g_res_raw[wdata[0] - CHAMELEON_CMD_RES_RAW1];
        rdata[0] = (uint8_t)(v & 0xFF);
        rdata[1] = (uint8_t)((v >> 8) & 0xFF);
        rdata[2] = (uint8_t)((v >> 16) & 0xFF);
        rdata[3] = (uint8_t)((v >> 24) & 0xFF);
        return CHAMELEON_I2C_OK;
    }
    case CHAMELEON_CMD_ID:
        if (rlen != 8) return CHAMELEON_I2C_ERR_BUS;
        memcpy(rdata, g_id, 8);
        return CHAMELEON_I2C_OK;
    default:
        return CHAMELEON_I2C_ERR_BUS;
    }
}

void chameleon_board_delay_ms(uint32_t ms) { g_total_delay_ms += ms; }

uint16_t chameleon_board_battery_mv(void) { return g_battery_mv; }
