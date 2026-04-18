#ifndef DENDROMETER_H
#define DENDROMETER_H

#include <stdint.h>

/**
 * Raw 12-bit ADC averages from 20 paired samples of the ratiometric divider.
 * The caller is responsible for converting these to mV using batteryLevel_mV
 * (VDDA ≈ batV on LSN50 V2). The ratio PA0/PA1 cancels batV, so the raw
 * codes are sufficient for ratiometric analytics on the gateway.
 */
typedef struct {
    uint16_t signal_raw;     /* PA0 — 20-sample average, 12-bit code */
    uint16_t reference_raw;  /* PA1 — 20-sample average, 12-bit code */
} dendrometer_result_t;

/**
 * Enable dendrometer 5 V rail, wait for settle, take 20 paired samples of
 * PA0 (signal) and PA1 (reference), average, disable 5 V rail.
 * Must be called with interrupts usable and HAL ADC initialised.
 */
void dendrometer_measure(dendrometer_result_t *result);

#endif /* DENDROMETER_H */
