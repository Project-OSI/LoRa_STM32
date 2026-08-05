#include "bench_adc.h"

#if ML3_BENCH_TOOLS

#include <stddef.h>

/*
 * bench_adc.c is held to the same -Werror strict set as the 7 ML3-branch
 * modules (see gcc/Makefile ML3_STRICT_CFLAGS / gcc/README.md), but unlike
 * those modules -- which are pure, host-testable logic with no HAL/CMSIS
 * dependency -- this file necessarily pulls in the vendor HAL/CMSIS tree
 * (it IS the direct-register bench port). That tree was never built under
 * -Wundef/-Wconversion before now and, like the pre-existing vendor
 * warnings already catalogued in gcc/README.md ("Vendor warnings that look
 * like real bugs"), it has two genuine issues neither caused by nor fixable
 * from this file, both confirmed pre-existing and out of scope ("no changes
 * to ... vendor logic"):
 *   - Drivers/CMSIS/Include/cmsis_gcc.h checks `__CORTEX_SC >= 300U` in
 *     several `#if`s. `__CORTEX_SC` is only ever defined by the SecurCore
 *     variant headers (core_sc000.h/core_sc300.h -- see their content and
 *     Drivers/CMSIS/Documentation/Core/html/device_h_pg.html, which
 *     documents the non-SecurCore default as `(000)`); this STM32L072
 *     (core_cm0plus.h) is a plain M0+ core and never defines it, tripping
 *     -Wundef on every reference.
 *   - Middlewares/Third_Party/Lora/Utilities/utilities.h's `__ffs()` is
 *     declared to return uint8_t but its body casts the result to
 *     `(uint32_t)` instead of `(uint8_t)` -- a real mismatched-cast bug,
 *     tripping -Wconversion on the implicit narrowing return. (Not called
 *     from this file; the diagnostic fires purely because the header is
 *     parsed, regardless of use.)
 * Scoped to exactly this one include so neither diagnostic is silenced
 * anywhere a real bug in bench_adc.c's own logic could hide.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#pragma GCC diagnostic ignored "-Wconversion"
#include "hw.h"
#pragma GCC diagnostic pop

/*
 * Direct-register/HAL-level bench ADC readout -- see inc/bench_adc.h for
 * the full contract and rationale (deliberately not routed through
 * adc_precision.c).
 *
 * The ADC1 configuration below mirrors the field-proven setup already used
 * by HW_AdcInit()/HW_AdcReadChannel() in stm32l0xx_hw.c (same MCU, same
 * PCLK/4-derived clock, same 160.5-cycle sampling time) and adds N=64
 * software averaging with min/max spread tracking, which the production
 * battery-level reader does not need.
 */

/* Factory VREFINT calibration word, system memory, Vref+ = 3.00 V (+-10 mV)
 * at 30 DegC (+-5 DegC) -- STM32L072 reference manual / datasheet.
 * Verified against this tree's own
 * Drivers/STM32L0xx_HAL_Driver/Inc/stm32l0xx_ll_adc.h
 * (VREFINT_CAL_ADDR / VREFINT_CAL_VREF); redefined locally so this module
 * has no dependency beyond what its own header pulls in. */
#define BENCH_ADC_VREFINT_CAL_ADDR \
  ((const uint16_t*)(uint32_t)0x1FF80078UL)
#define BENCH_ADC_VREFINT_CAL_VREF_MV 3000U

/* Bounded per-conversion poll timeout. At the ADC's PCLK/4-derived clock
 * (~8 MHz on this board -- matches ADC_PRECISION_ADC_CLOCK_HZ in
 * adc_precision.h for the same hardware), 160.5 cycles is ~20 us; 10 ms
 * leaves >500x margin while still failing fast on genuinely stuck hardware
 * instead of hanging the AT console on HAL_MAX_DELAY. */
#define BENCH_ADC_CONVERSION_TIMEOUT_MS 10U

typedef struct {
  GPIO_TypeDef* port;
  uint16_t pin;
  uint32_t adc_channel;
} bench_adc_channel_map_t;

/* Index order matches bench_adc_result_t.channel[] / BENCH_ADC_CH_* bits:
 * 0 = PA0/IN0, 1 = PA1/IN1, 2 = PA4/IN4. */
static const bench_adc_channel_map_t BENCH_ADC_CHANNEL_MAP[BENCH_ADC_CHANNEL_COUNT] = {
  { Oil_LEVEL_PORT, Oil_LEVEL_PIN, ADC_CHANNEL_0 },
  { ADC_IN1_LEVEL_PORT, ADC_IN1_LEVEL_PIN, ADC_CHANNEL_1 },
  { ADC_IN4_LEVEL_PORT, ADC_IN4_LEVEL_PIN, ADC_CHANNEL_4 }
};

/*
 * File-scope static, deliberately NOT a bench_adc_run() stack local --
 * matches the vendor's own idiom in stm32l0xx_hw.c ("static ADC_HandleTypeDef
 * hadc;", module-scope). Named bench_hadc (not hadc) specifically so it
 * does not collide with the `hadc` parameter name used by every helper
 * below -- a same-named file-scope static would make those parameters
 * shadow it, which -Wshadow (part of this file's strict flag set) would
 * correctly reject.
 *
 * This is not just style: Drivers/STM32L0xx_HAL_Driver/Src/
 * stm32l0xx_hal_adc.c's HAL_ADC_Init() only does `hadc->Lock = HAL_UNLOCKED;`
 * inside `if (hadc->State == HAL_ADC_STATE_RESET)` (HAL_ADC_STATE_RESET is
 * 0). A stack-automatic handle has indeterminate initial content; if
 * whatever garbage lands in .State happens not to equal 0, that branch (and
 * the Lock (re-)initialization inside it) is skipped, and .Lock is left
 * indeterminate too -- if it happens to equal HAL_LOCKED (1), every
 * subsequent __HAL_LOCK() user (e.g. HAL_ADC_ConfigChannel(), see
 * stm32l0xx_hal_adc.c) spuriously returns HAL_BUSY, i.e. an intermittent,
 * stack-content-dependent command failure with no hardware cause. A static
 * has implicit, guaranteed zero-initialization (State == HAL_ADC_STATE_RESET
 * == 0, Lock == HAL_UNLOCKED == 0) with no explicit "= {0}" brace-initializer
 * needed (so no -Wmissing-field-initializers exposure under the strict
 * flags), so the first bench_adc_run() call is guaranteed to take the
 * State==RESET branch. Every call after that is equally safe: HAL_ADC_DeInit()
 * (called at the end of every bench_adc_run(), success or failure) sets
 * hadc->State = HAL_ADC_STATE_RESET on its own success path AND
 * unconditionally does __HAL_UNLOCK(hadc) (hadc->Lock = HAL_UNLOCKED) right
 * before returning regardless of that path's outcome -- so .Lock can never
 * be left indeterminate between invocations either way, and the common case
 * (DeInit succeeds) also re-arms .State == RESET so the *next* HAL_ADC_Init()
 * re-enters the MspInit/Lock-init branch exactly like the first call did.
 */
static ADC_HandleTypeDef bench_hadc;

static void bench_adc_configure_gpio(void) {
  GPIO_InitTypeDef init;
  size_t index;

  init.Mode = GPIO_MODE_ANALOG;
  init.Pull = GPIO_NOPULL;
  init.Speed = GPIO_SPEED_FREQ_HIGH;

  for (index = 0U; index < (size_t)BENCH_ADC_CHANNEL_COUNT; ++index) {
    HW_GPIO_Init(BENCH_ADC_CHANNEL_MAP[index].port,
      BENCH_ADC_CHANNEL_MAP[index].pin, &init);
  }
}

static bool bench_adc_hal_init(ADC_HandleTypeDef* hadc) {
  hadc->Instance = ADC1;

  hadc->Init.OversamplingMode      = DISABLE;
  hadc->Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc->Init.LowPowerAutoPowerOff  = DISABLE;
  hadc->Init.LowPowerFrequencyMode = ENABLE;
  hadc->Init.LowPowerAutoWait      = DISABLE;
  hadc->Init.Resolution            = ADC_RESOLUTION_12B;
  hadc->Init.SamplingTime          = ADC_SAMPLETIME_160CYCLES_5;
  hadc->Init.ScanConvMode          = ADC_SCAN_DIRECTION_FORWARD;
  hadc->Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc->Init.ContinuousConvMode    = DISABLE;
  hadc->Init.DiscontinuousConvMode = DISABLE;
  hadc->Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc->Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
  hadc->Init.DMAContinuousRequests = DISABLE;

  __HAL_RCC_ADC1_CLK_ENABLE();

  return HAL_ADC_Init(hadc) == HAL_OK;
}

static void bench_adc_hal_deinit(ADC_HandleTypeDef* hadc) {
  (void)HAL_ADC_DeInit(hadc);
  __HAL_RCC_ADC1_CLK_DISABLE();
}

static bool bench_adc_read_raw(ADC_HandleTypeDef* hadc, uint32_t channel,
    uint16_t* out_code) {
  ADC_ChannelConfTypeDef adc_conf;

  /* Deselect all channels first -- matches HW_AdcReadChannel()'s pattern
   * in stm32l0xx_hw.c, keeping the sequencer state deterministic between
   * calls regardless of what channel was selected previously. */
  adc_conf.Channel = ADC_CHANNEL_MASK;
  adc_conf.Rank = ADC_RANK_NONE;
  if (HAL_ADC_ConfigChannel(hadc, &adc_conf) != HAL_OK) {
    return false;
  }

  adc_conf.Channel = channel;
  adc_conf.Rank = ADC_RANK_CHANNEL_NUMBER;
  if (HAL_ADC_ConfigChannel(hadc, &adc_conf) != HAL_OK) {
    return false;
  }

  if (HAL_ADC_Start(hadc) != HAL_OK) {
    return false;
  }
  if (HAL_ADC_PollForConversion(hadc, (uint32_t)BENCH_ADC_CONVERSION_TIMEOUT_MS)
      != HAL_OK) {
    (void)HAL_ADC_Stop(hadc);
    return false;
  }

  *out_code = (uint16_t)HAL_ADC_GetValue(hadc);
  (void)HAL_ADC_Stop(hadc);
  return true;
}

static bool bench_adc_sample_channel(ADC_HandleTypeDef* hadc, uint32_t channel,
    uint32_t* out_mean_code_x100, uint16_t* out_min, uint16_t* out_max) {
  uint32_t sample_index;
  uint32_t sum = 0U;
  uint16_t min_code = 0xFFFFU;
  uint16_t max_code = 0U;

  for (sample_index = 0U; sample_index < (uint32_t)BENCH_ADC_SAMPLE_COUNT;
      ++sample_index) {
    uint16_t code = 0U;

    if (!bench_adc_read_raw(hadc, channel, &code)) {
      return false;
    }
    sum += code;
    if (code < min_code) {
      min_code = code;
    }
    if (code > max_code) {
      max_code = code;
    }
  }

  *out_mean_code_x100 = (sum * 100U + ((uint32_t)BENCH_ADC_SAMPLE_COUNT / 2U))
    / (uint32_t)BENCH_ADC_SAMPLE_COUNT;
  *out_min = min_code;
  *out_max = max_code;
  return true;
}

static uint32_t bench_adc_compute_vdda_mv(uint16_t vrefint_code) {
  uint32_t factory_cal = (uint32_t)(*BENCH_ADC_VREFINT_CAL_ADDR);

  if (vrefint_code == 0U) {
    return 0U;
  }
  return (BENCH_ADC_VREFINT_CAL_VREF_MV * factory_cal) / (uint32_t)vrefint_code;
}

static uint32_t bench_adc_compute_channel_uv(uint32_t mean_code_x100,
    uint32_t vdda_mv) {
  /* uv = code * vdda_mv * 1000 / 4095, kept in x100 fixed point through the
   * division: (mean_code_x100 * vdda_mv * 1000) / (4095 * 100). Widened to
   * 64 bits: worst case mean_code_x100 (409500) * vdda_mv (bounded well
   * under 10000 in practice) * 1000 can approach ~4e12, which overflows
   * uint32_t (max ~4.29e9); the narrowing cast back to uint32_t at the end
   * is safe because the final microvolt value (a few volts at most) is
   * far below the uint32_t range. */
  uint64_t numerator = (uint64_t)mean_code_x100 * (uint64_t)vdda_mv * 1000ULL;
  uint64_t denominator = (uint64_t)BENCH_ADC_FULL_SCALE_CODE * 100ULL;

  return (uint32_t)(numerator / denominator);
}

bool bench_adc_run(uint8_t channel_mask, bench_adc_result_t* result) {
  /* Note: bench_hadc is the file-scope static declared above, not a local
   * -- see its declaration comment for why (HAL_ADC_Init()'s Lock
   * initialization is gated on ->State, which must be a known value, not
   * stack garbage). */
  bench_adc_result_t local;
  size_t index;
  uint16_t vrefint_code_first;
  uint16_t vrefint_code_last;

  if (result == NULL) {
    return false;
  }

  local.ok = false;
  for (index = 0U; index < (size_t)BENCH_ADC_CHANNEL_COUNT; ++index) {
    local.channel[index].sampled = false;
    local.channel[index].mean_code_x100 = 0U;
    local.channel[index].min_code = 0U;
    local.channel[index].max_code = 0U;
    local.channel[index].uv = 0U;
  }
  local.vrefint_mean_code_x100_first = 0U;
  local.vrefint_min_code_first = 0U;
  local.vrefint_max_code_first = 0U;
  local.vrefint_mean_code_x100_last = 0U;
  local.vrefint_min_code_last = 0U;
  local.vrefint_max_code_last = 0U;
  local.vdda_mv_first = 0U;
  local.vdda_mv_last = 0U;
  local.calibration_factor = 0U;

  bench_adc_configure_gpio();

  if (!bench_adc_hal_init(&bench_hadc)) {
    *result = local;
    return false;
  }

  if (HAL_ADCEx_Calibration_Start(&bench_hadc, ADC_SINGLE_ENDED) != HAL_OK) {
    bench_adc_hal_deinit(&bench_hadc);
    *result = local;
    return false;
  }
  local.calibration_factor =
    HAL_ADCEx_Calibration_GetValue(&bench_hadc, ADC_SINGLE_ENDED);

  if (!bench_adc_sample_channel(&bench_hadc, ADC_CHANNEL_VREFINT,
      &local.vrefint_mean_code_x100_first, &local.vrefint_min_code_first,
      &local.vrefint_max_code_first)) {
    bench_adc_hal_deinit(&bench_hadc);
    *result = local;
    return false;
  }
  vrefint_code_first =
    (uint16_t)((local.vrefint_mean_code_x100_first + 50U) / 100U);
  local.vdda_mv_first = bench_adc_compute_vdda_mv(vrefint_code_first);

  for (index = 0U; index < (size_t)BENCH_ADC_CHANNEL_COUNT; ++index) {
    uint8_t bit = (uint8_t)(1U << index);

    if ((uint8_t)(channel_mask & bit) == 0U) {
      continue;
    }
    if (!bench_adc_sample_channel(&bench_hadc,
        BENCH_ADC_CHANNEL_MAP[index].adc_channel,
        &local.channel[index].mean_code_x100,
        &local.channel[index].min_code,
        &local.channel[index].max_code)) {
      bench_adc_hal_deinit(&bench_hadc);
      *result = local;
      return false;
    }
    local.channel[index].uv = bench_adc_compute_channel_uv(
      local.channel[index].mean_code_x100, local.vdda_mv_first);
    local.channel[index].sampled = true;
  }

  if (!bench_adc_sample_channel(&bench_hadc, ADC_CHANNEL_VREFINT,
      &local.vrefint_mean_code_x100_last, &local.vrefint_min_code_last,
      &local.vrefint_max_code_last)) {
    bench_adc_hal_deinit(&bench_hadc);
    *result = local;
    return false;
  }
  vrefint_code_last =
    (uint16_t)((local.vrefint_mean_code_x100_last + 50U) / 100U);
  local.vdda_mv_last = bench_adc_compute_vdda_mv(vrefint_code_last);

  bench_adc_hal_deinit(&bench_hadc);

  local.ok = true;
  *result = local;
  return true;
}

#endif /* ML3_BENCH_TOOLS */
