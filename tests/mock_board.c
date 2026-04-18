#include "mock_board.h"
#include "dendrometer.h"
#include <string.h>

#define MOCK_EVT_CAP 512
#define MOCK_SEQ_CAP 512

static mock_event_t g_events[MOCK_EVT_CAP];
static size_t       g_event_count = 0;

static uint16_t g_sig_seq[MOCK_SEQ_CAP];
static size_t   g_sig_len = 0;
static size_t   g_sig_idx = 0;
static uint16_t g_sig_const = 0;
static int      g_sig_is_const = 1;

static uint16_t g_ref_seq[MOCK_SEQ_CAP];
static size_t   g_ref_len = 0;
static size_t   g_ref_idx = 0;
static uint16_t g_ref_const = 0;
static int      g_ref_is_const = 1;

static void record(mock_event_kind_t k, uint32_t v) {
    if (g_event_count < MOCK_EVT_CAP) {
        g_events[g_event_count].kind  = k;
        g_events[g_event_count].value = v;
        g_event_count++;
    }
}

void mock_board_reset(void) {
    g_event_count = 0;
    g_sig_len = 0; g_sig_idx = 0; g_sig_const = 0; g_sig_is_const = 1;
    g_ref_len = 0; g_ref_idx = 0; g_ref_const = 0; g_ref_is_const = 1;
}

void mock_board_set_signal_constant(uint16_t v)     { g_sig_const = v; g_sig_is_const = 1; }
void mock_board_set_reference_constant(uint16_t v)  { g_ref_const = v; g_ref_is_const = 1; }

void mock_board_set_signal_sequence(const uint16_t *seq, size_t len) {
    size_t n = len > MOCK_SEQ_CAP ? MOCK_SEQ_CAP : len;
    memcpy(g_sig_seq, seq, n * sizeof(uint16_t));
    g_sig_len = n; g_sig_idx = 0; g_sig_is_const = 0;
}
void mock_board_set_reference_sequence(const uint16_t *seq, size_t len) {
    size_t n = len > MOCK_SEQ_CAP ? MOCK_SEQ_CAP : len;
    memcpy(g_ref_seq, seq, n * sizeof(uint16_t));
    g_ref_len = n; g_ref_idx = 0; g_ref_is_const = 0;
}

size_t              mock_board_event_count(void) { return g_event_count; }
const mock_event_t *mock_board_events(void)      { return g_events; }

/* ---- dendro_board_* implementations --------------------------------- */

void dendro_board_5v_on(void)  { record(MOCK_EVT_5V_ON,  0); }
void dendro_board_5v_off(void) { record(MOCK_EVT_5V_OFF, 0); }

uint16_t dendro_board_adc_read_signal(void) {
    uint16_t v;
    if (g_sig_is_const) v = g_sig_const;
    else { v = (g_sig_idx < g_sig_len) ? g_sig_seq[g_sig_idx++] : 0; }
    record(MOCK_EVT_ADC_SIG, v);
    return v;
}
uint16_t dendro_board_adc_read_reference(void) {
    uint16_t v;
    if (g_ref_is_const) v = g_ref_const;
    else { v = (g_ref_idx < g_ref_len) ? g_ref_seq[g_ref_idx++] : 0; }
    record(MOCK_EVT_ADC_REF, v);
    return v;
}
void dendro_board_delay_ms(uint32_t ms) { record(MOCK_EVT_DELAY, ms); }
