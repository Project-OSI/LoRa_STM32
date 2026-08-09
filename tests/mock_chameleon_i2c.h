#ifndef MOCK_CHAMELEON_I2C_H
#define MOCK_CHAMELEON_I2C_H

#include <stdint.h>
#include <stddef.h>
#include "via_chameleon.h"

void mock_chameleon_reset(void);

void mock_chameleon_set_present(int present);
void mock_chameleon_set_status_after_trigger(uint8_t v);
void mock_chameleon_set_status_ready_after_polls(uint8_t polls);
void mock_chameleon_set_temp_x100(int16_t v);
void mock_chameleon_set_resistance(uint8_t channel, uint32_t ohms);

/* Set CAL[ch] only (returns this value on every CAL[ch] read). */
void mock_chameleon_set_resistance_comp(uint8_t channel, uint32_t ohms);

/* Set RAW[ch] only (returns this value on every RAW[ch] read). */
void mock_chameleon_set_resistance_raw(uint8_t channel, uint32_t ohms);

/* On the first CAL[ch] read after reset, return `first`. On every subsequent
 * CAL[ch] read, return `subsequent`. Use to simulate a peripheral whose
 * compensated register becomes valid only on a retry. RAW[ch] is unaffected
 * and continues to return whatever mock_chameleon_set_resistance_raw set
 * (or its default). */
void mock_chameleon_set_resistance_comp_sequence(uint8_t channel,
                                                 uint32_t first,
                                                 uint32_t subsequent);

void mock_chameleon_set_id(const uint8_t id[8]);
void mock_chameleon_set_battery_mv(uint16_t v);
void mock_chameleon_fail_command(uint8_t cmd);

size_t mock_chameleon_trigger_count(void);
size_t mock_chameleon_status_poll_count(void);
size_t mock_chameleon_total_delay_ms(void);
size_t mock_chameleon_comp_read_count(uint8_t channel);

#endif /* MOCK_CHAMELEON_I2C_H */
