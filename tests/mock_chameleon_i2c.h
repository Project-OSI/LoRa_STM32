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
void mock_chameleon_set_id(const uint8_t id[8]);
void mock_chameleon_set_battery_mv(uint16_t v);

size_t mock_chameleon_trigger_count(void);
size_t mock_chameleon_status_poll_count(void);
size_t mock_chameleon_total_delay_ms(void);

#endif /* MOCK_CHAMELEON_I2C_H */
