#ifndef MOCK_BOARD_H
#define MOCK_BOARD_H

#include <stdint.h>
#include <stddef.h>

/* Scripted sample injection for board ADC primitives. */
void mock_board_reset(void);
void mock_board_set_signal_constant(uint16_t value);
void mock_board_set_reference_constant(uint16_t value);
void mock_board_set_signal_sequence(const uint16_t *seq, size_t len);
void mock_board_set_reference_sequence(const uint16_t *seq, size_t len);

/* Event log — captures every board call in order. */
typedef enum {
    MOCK_EVT_5V_ON,
    MOCK_EVT_5V_OFF,
    MOCK_EVT_ADC_SIG,
    MOCK_EVT_ADC_REF,
    MOCK_EVT_DELAY
} mock_event_kind_t;

typedef struct {
    mock_event_kind_t kind;
    uint32_t          value;   /* ADC value for ADC events, ms for delay */
} mock_event_t;

size_t              mock_board_event_count(void);
const mock_event_t *mock_board_events(void);

#endif
