#include "ml3_stm32_adc_port.h"

#include <stddef.h>

/* hw.h supplies the board-selected STM32L072 CMSIS register definitions and
 * the RTC conversion API. Its vendor include chain has pre-existing GCC-only
 * -Wundef/-Wconversion diagnostics, so scope the existing bench-port waiver
 * to that chain rather than weakening this adapter's strict build. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include "hw.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* hw_rtc.c sets N_PREDIV_S = 10, so the raw RTC counter runs at 1024 Hz:
 * one tick is about 976.6 us and two ticks are approximately 1.95 ms. This
 * exceeds the documented 10 us VREFINT requirement with large margin and
 * covers the observed residual settling tail without adding an unbounded
 * delay. */
enum { ML3_STM32_ADC_VREFINT_SETTLE_TICKS = 2U };

/* HW_RTC_Tick2ms uses floor(raw_tick * 125 / 128), so it resets when the
 * 32-bit raw counter wraps. The raw counter wrap has a ~4,194,304,000ms period;
 * plain subtraction of converted milliseconds would therefore produce the
 * wrong elapsed value at that boundary. The epoch makes the uint32_t millisecond time continuous modulo 2^32 for sampled acquisitions. */
#define ML3_STM32_ADC_RTC_WRAP_MS UINT32_C(4194304000)

static ml3_stm32_adc_port_context_t *ml3_stm32_adc_port_active_context;

static bool ml3_stm32_adc_port_context_is_active(const void *port_ctx) {
  return (port_ctx != NULL) &&
    (port_ctx == ml3_stm32_adc_port_active_context);
}

static uint32_t ml3_stm32_adc_port_channel_bit(uint16_t channel) {
  switch (channel) {
    case 0U:
      return ADC_CHSELR_CHSEL0;
    case 1U:
      return ADC_CHSELR_CHSEL1;
    case 2U:
      return ADC_CHSELR_CHSEL2;
    case 4U:
      return ADC_CHSELR_CHSEL4;
    case 17U:
      return ADC_CHSELR_CHSEL17;
    case 18U:
      return ADC_CHSELR_CHSEL18;
    default:
      return 0U;
  }
}

uint32_t ml3_stm32_adc_port_now_ms(void *port_ctx) {
  ml3_stm32_adc_port_context_t *context =
    (ml3_stm32_adc_port_context_t *)port_ctx;
  uint32_t now_tick = HW_RTC_GetTimerValue();

  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return (uint32_t)HW_RTC_Tick2ms(now_tick);
  }
  if (!context->rtc_tick_initialized) {
    context->last_rtc_tick = now_tick;
    context->rtc_tick_initialized = true;
  } else if (now_tick < context->last_rtc_tick) {
    context->rtc_epoch_ms += ML3_STM32_ADC_RTC_WRAP_MS;
    context->last_rtc_tick = now_tick;
  } else {
    context->last_rtc_tick = now_tick;
  }
  return context->rtc_epoch_ms + (uint32_t)HW_RTC_Tick2ms(now_tick);
}

static void ml3_stm32_adc_port_request_stop_conversion(void *port_ctx) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return;
  }
  ADC1->CR |= ADC_CR_ADSTP;
}

static bool ml3_stm32_adc_port_is_conversion_stopped(void *port_ctx) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return false;
  }
  return (ADC1->CR & ADC_CR_ADSTART) == 0U;
}

static void ml3_stm32_adc_port_request_disable_adc(void *port_ctx) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return;
  }
  ADC1->CR |= ADC_CR_ADDIS;
}

static bool ml3_stm32_adc_port_is_adc_disabled(void *port_ctx) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return false;
  }
  return (ADC1->CR & ADC_CR_ADEN) == 0U;
}

static void ml3_stm32_adc_port_configure(
    void *port_ctx, const adc_precision_config_t *config) {
  (void)config;

  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return;
  }

  /* adc_precision_prepare() calls configure only after it has observed ADEN
   * clear. The zero-valued fields explicitly select 12-bit/right-aligned,
   * software-triggered, non-continuous and DMA-free operation. */
  ADC1->CFGR1 = ADC_RESOLUTION_12B | ADC_DATAALIGN_RIGHT |
    ADC_EXTERNALTRIGCONVEDGE_NONE;
  ADC1->CFGR2 = ADC_CLOCK_SYNC_PCLK_DIV4 | ADC_CFGR2_OVSE |
    ADC_OVERSAMPLING_RATIO_256 | ADC_RIGHTBITSHIFT_4;
  ADC1->SMPR = ADC_SAMPLETIME_160CYCLES_5;
}

static void ml3_stm32_adc_port_request_self_calibration(void *port_ctx) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return;
  }
  ADC1->CR |= ADC_CR_ADCAL;
}

static bool ml3_stm32_adc_port_is_calibration_complete(void *port_ctx) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return false;
  }
  return (ADC1->CR & ADC_CR_ADCAL) == 0U;
}

static void ml3_stm32_adc_port_request_enable_adc(void *port_ctx) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return;
  }
  ADC1->ISR = ADC_ISR_ADRDY;
  ADC1->CR |= ADC_CR_ADEN;
}

static bool ml3_stm32_adc_port_is_adc_ready(void *port_ctx) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return false;
  }
  return (ADC1->ISR & ADC_ISR_ADRDY) != 0U;
}

static void ml3_stm32_adc_port_enable_vrefint_gate(void *port_ctx) {
  ml3_stm32_adc_port_context_t *context;

  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return;
  }
  context = (ml3_stm32_adc_port_context_t *)port_ctx;

  ADC->CCR |= ADC_CCR_VREFEN;
  context->vrefint_enable_tick = HW_RTC_GetTimerValue();
}

static void ml3_stm32_adc_port_enable_temperature_gate(void *port_ctx) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return;
  }
  ADC->CCR |= ADC_CCR_TSEN;
}

static void ml3_stm32_adc_port_enable_vrefint_buffer_gate(void *port_ctx) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return;
  }
  SYSCFG->CFGR3 |= SYSCFG_CFGR3_ENBUF_VREFINT_ADC;
}

static void ml3_stm32_adc_port_enable_temperature_buffer_gate(void *port_ctx) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return;
  }
  SYSCFG->CFGR3 |= SYSCFG_CFGR3_ENBUF_SENSOR_ADC;
}

/* STM32L072 exposes only SYSCFG_CFGR3_VREFINT_RDYF for this group: the
 * SENSOR_ADC_RDYF, VREFINT_ADC_RDYF, and VREFINT_COMP_RDYF names alias it.
 * The adc_precision_port_t names are retained for its generic interface, but
 * all four ready callbacks observe this one bit and do not independently
 * prove temperature or buffer readiness.
 * is_reference_settled supplies the independent elapsed-time guard. */
static bool ml3_stm32_adc_port_is_vrefint_ready(void *port_ctx) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return false;
  }
  return (SYSCFG->CFGR3 & SYSCFG_CFGR3_VREFINT_RDYF) != 0U;
}

static bool ml3_stm32_adc_port_is_temperature_ready(void *port_ctx) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return false;
  }
  return (SYSCFG->CFGR3 & SYSCFG_CFGR3_SENSOR_ADC_RDYF) != 0U;
}

static bool ml3_stm32_adc_port_is_vrefint_buffer_ready(void *port_ctx) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return false;
  }
  return (SYSCFG->CFGR3 & SYSCFG_CFGR3_VREFINT_ADC_RDYF) != 0U;
}

static bool ml3_stm32_adc_port_is_temperature_buffer_ready(void *port_ctx) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return false;
  }
  return (SYSCFG->CFGR3 & SYSCFG_CFGR3_SENSOR_ADC_RDYF) != 0U;
}

static bool ml3_stm32_adc_port_is_reference_settled(void *port_ctx) {
  ml3_stm32_adc_port_context_t *context;

  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return false;
  }
  context = (ml3_stm32_adc_port_context_t *)port_ctx;

  return (HW_RTC_GetTimerValue() - context->vrefint_enable_tick)
    >= ML3_STM32_ADC_VREFINT_SETTLE_TICKS;
}

static void ml3_stm32_adc_port_select_channel(void *port_ctx, uint16_t channel) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return;
  }
  ADC1->CHSELR = ml3_stm32_adc_port_channel_bit(channel);
}

static void ml3_stm32_adc_port_start_conversion(void *port_ctx) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return;
  }
  ADC1->CR |= ADC_CR_ADSTART;
}

static bool ml3_stm32_adc_port_is_conversion_complete(void *port_ctx) {
  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return false;
  }
  return (ADC1->ISR & ADC_ISR_EOC) != 0U;
}

static bool ml3_stm32_adc_port_read_raw(
    void *port_ctx, uint16_t *raw_code, bool *overrun) {
  bool observed_overrun;

  if (!ml3_stm32_adc_port_context_is_active(port_ctx)) {
    return false;
  }
  if ((raw_code == NULL) || (overrun == NULL)) {
    return false;
  }
  observed_overrun = (ADC1->ISR & ADC_ISR_OVR) != 0U;
  *raw_code = (uint16_t)ADC1->DR;
  *overrun = observed_overrun;
  ADC1->ISR = ADC_ISR_OVR;
  return true;
}

static const adc_precision_port_t k_ml3_stm32_adc_port = {
  .now_ms = ml3_stm32_adc_port_now_ms,
  .request_stop_conversion = ml3_stm32_adc_port_request_stop_conversion,
  .is_conversion_stopped = ml3_stm32_adc_port_is_conversion_stopped,
  .request_disable_adc = ml3_stm32_adc_port_request_disable_adc,
  .is_adc_disabled = ml3_stm32_adc_port_is_adc_disabled,
  .configure = ml3_stm32_adc_port_configure,
  .request_self_calibration = ml3_stm32_adc_port_request_self_calibration,
  .is_calibration_complete = ml3_stm32_adc_port_is_calibration_complete,
  .request_enable_adc = ml3_stm32_adc_port_request_enable_adc,
  .is_adc_ready = ml3_stm32_adc_port_is_adc_ready,
  .enable_vrefint_gate = ml3_stm32_adc_port_enable_vrefint_gate,
  .enable_temperature_gate = ml3_stm32_adc_port_enable_temperature_gate,
  .enable_vrefint_buffer_gate = ml3_stm32_adc_port_enable_vrefint_buffer_gate,
  .enable_temperature_buffer_gate = ml3_stm32_adc_port_enable_temperature_buffer_gate,
  .is_vrefint_ready = ml3_stm32_adc_port_is_vrefint_ready,
  .is_temperature_ready = ml3_stm32_adc_port_is_temperature_ready,
  .is_vrefint_buffer_ready = ml3_stm32_adc_port_is_vrefint_buffer_ready,
  .is_temperature_buffer_ready = ml3_stm32_adc_port_is_temperature_buffer_ready,
  .is_reference_settled = ml3_stm32_adc_port_is_reference_settled,
  .select_channel = ml3_stm32_adc_port_select_channel,
  .start_conversion = ml3_stm32_adc_port_start_conversion,
  .is_conversion_complete = ml3_stm32_adc_port_is_conversion_complete,
  .read_raw = ml3_stm32_adc_port_read_raw
};

const adc_precision_port_t *ml3_stm32_adc_port_get(void) {
  return &k_ml3_stm32_adc_port;
}

bool ml3_stm32_adc_port_init(ml3_stm32_adc_port_context_t *context) {
  GPIO_InitTypeDef init;

  if (context == NULL) {
    return false;
  }
  context->vrefint_enable_tick = 0U;
  context->last_rtc_tick = 0U;
  context->rtc_epoch_ms = 0U;
  context->rtc_tick_initialized = false;
  RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
  RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

  init.Mode = GPIO_MODE_ANALOG;
  init.Pull = GPIO_NOPULL;
  init.Speed = GPIO_SPEED_FREQ_HIGH;
  HW_GPIO_Init(GPIOA, GPIO_PIN_0, &init);
  HW_GPIO_Init(GPIOA, GPIO_PIN_1, &init);
  HW_GPIO_Init(GPIOA, GPIO_PIN_4, &init);
  /* PA2 remains LPUART1 TX. It is ADC_IN2, but the trial reads that channel
   * passively and must not take over the vendor UART pin. */

  ml3_stm32_adc_port_active_context = context;
  return true;
}
