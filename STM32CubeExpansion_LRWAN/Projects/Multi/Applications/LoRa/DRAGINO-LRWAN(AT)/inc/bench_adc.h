#ifndef BENCH_ADC_H
#define BENCH_ADC_H

/*
 * Bench-only ADC readout tool for Gate 0 Section 4
 * (docs/ml3-gate0-bench-runbook.md). Compiled only when ML3_BENCH_TOOLS=1
 * (see gcc/Makefile: `make -C gcc BENCH=1`).
 *
 * NOT FOR FIELD DEPLOYMENT: a unit whose `AT+ML3VER=?` response carries the
 * `+BENCH` suffix (added in at.c under this same guard) must never be
 * installed on a farm gateway.
 *
 * This is a direct STM32L0 HAL/register implementation. It deliberately
 * does NOT go through the adc_precision.c port (the gated production
 * acquisition engine) -- no Task 10 decisions, no EEPROM, no coupling to
 * ML3_CONFIG_*_READY gates. Pure passive reads of PA0/PA1/PA4 (ADC
 * channels IN0/IN1/IN4, the same pins already wired on the LSN50v2 for
 * Oil_LEVEL / ADC_IN1 / ADC_IN4 / BAT_LEVEL) plus VREFINT; no TX, no rail
 * control, no dependence on ML3 mode 10. Safe to run while a bench supply
 * drives the probe.
 */

#if ML3_BENCH_TOOLS

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Software-averaged conversions per channel per invocation. */
enum { BENCH_ADC_SAMPLE_COUNT = 64U };

/* Native ADC resolution full-scale code (12-bit). */
enum { BENCH_ADC_FULL_SCALE_CODE = 4095U };

/* External channel selection bitmask (bit set => sample that channel).
 * Index into bench_adc_result_t.channel[] matches the bit position. */
enum {
  BENCH_ADC_CH_PA0 = 0x01U, /* index 0, IN0 */
  BENCH_ADC_CH_PA1 = 0x02U, /* index 1, IN1 */
  BENCH_ADC_CH_PA4 = 0x04U, /* index 2, IN4 */
  BENCH_ADC_CH_ALL = 0x07U
};

enum { BENCH_ADC_CHANNEL_COUNT = 3U };

typedef struct {
  bool sampled;             /* true if this channel was requested and read */
  uint32_t mean_code_x100;  /* (sum of 64 raw codes * 100 + 32) / 64 */
  uint16_t min_code;
  uint16_t max_code;
  uint32_t uv;               /* channel voltage vs ground, using VDDA_first */
} bench_adc_channel_result_t;

typedef struct {
  bool ok; /* false if calibration or any conversion failed; on false the
            * run is aborted at the first failure and remaining fields may
            * be incomplete/zero. */
  bench_adc_channel_result_t channel[BENCH_ADC_CHANNEL_COUNT];
  uint32_t vrefint_mean_code_x100_first;
  uint16_t vrefint_min_code_first;
  uint16_t vrefint_max_code_first;
  uint32_t vrefint_mean_code_x100_last;
  uint16_t vrefint_min_code_last;
  uint16_t vrefint_max_code_last;
  uint32_t vdda_mv_first;
  uint32_t vdda_mv_last;
  uint32_t calibration_factor;
} bench_adc_result_t;

/*
 * Runs one full AT+ML3ADC invocation:
 *   1. Configures PA0/PA1/PA4 as analog inputs and the ADC (PCLK/4-derived
 *      clock, 160.5-cycle sampling time, 12-bit, single software-triggered
 *      conversions, hardware oversampling off).
 *   2. Runs ADC self-calibration once (HAL_ADCEx_Calibration_Start) and
 *      records the resulting calibration factor.
 *   3. Reads VREFINT, N=64 software-averaged, with min/max spread.
 *   4. Reads each channel selected by `channel_mask` (subset/all of
 *      BENCH_ADC_CH_PA0/PA1/PA4), N=64 software-averaged each, with
 *      min/max spread; computes each channel's microvolts-vs-ground using
 *      VDDA_first (the VDDA computed from step 3 -- the reading closest in
 *      time to this sampling loop).
 *   5. Reads VREFINT again, N=64 software-averaged.
 *   6. Computes VDDA at both VREFINT reads from the factory VREFINT_CAL
 *      calibration word at system memory address 0x1FF80078 (the standard
 *      VDDA_mV = 3000 * VREFINT_CAL / VREFINT_code formula).
 *
 * Returns false (result->ok == false) on any calibration or conversion
 * failure/timeout; the run is aborted at the first failure rather than
 * left to hang, since a bench operator needs a fast, clear failure signal.
 * `result` must not be NULL.
 */
bool bench_adc_run(uint8_t channel_mask, bench_adc_result_t* result);

#ifdef __cplusplus
}
#endif

#endif /* ML3_BENCH_TOOLS */

#endif /* BENCH_ADC_H */
