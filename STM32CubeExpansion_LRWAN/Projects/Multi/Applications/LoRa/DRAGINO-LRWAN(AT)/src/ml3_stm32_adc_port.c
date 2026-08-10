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

enum { ML3_STM32_ADC_VREFINT_SETTLE_TICKS = 2U };

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

static uint32_t ml3_stm32_adc_port_now_ms(void *port_ctx) {
  (void)port_ctx;
  return (uint32_t)HW_RTC_Tick2ms(HW_RTC_GetTimerValue());
}

static void ml3_stm32_adc_port_request_stop_conversion(void *port_ctx) {
  (void)port_ctx;
  ADC1->CR |= ADC_CR_ADSTP;
}

static bool ml3_stm32_adc_port_is_conversion_stopped(void *port_ctx) {
  (void)port_ctx;
  return (ADC1->CR & ADC_CR_ADSTART) == 0U;
}

static void ml3_stm32_adc_port_request_disable_adc(void *port_ctx) {
  (void)port_ctx;
  ADC1->CR |= ADC_CR_ADDIS;
}

static bool ml3_stm32_adc_port_is_adc_disabled(void *port_ctx) {
  (void)port_ctx;
  return (ADC1->CR & ADC_CR_ADEN) == 0U;
}

static void ml3_stm32_adc_port_configure(
    void *port_ctx, const adc_precision_config_t *config) {
  (void)port_ctx;
  (void)config;

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
  (void)port_ctx;
  ADC1->CR |= ADC_CR_ADCAL;
}

static bool ml3_stm32_adc_port_is_calibration_complete(void *port_ctx) {
  (void)port_ctx;
  return (ADC1->CR & ADC_CR_ADCAL) == 0U;
}

static void ml3_stm32_adc_port_request_enable_adc(void *port_ctx) {
  (void)port_ctx;
  ADC1->ISR = ADC_ISR_ADRDY;
  ADC1->CR |= ADC_CR_ADEN;
}

static bool ml3_stm32_adc_port_is_adc_ready(void *port_ctx) {
  (void)port_ctx;
  return (ADC1->ISR & ADC_ISR_ADRDY) != 0U;
}

static void ml3_stm32_adc_port_enable_vrefint_gate(void *port_ctx) {
  ml3_stm32_adc_port_context_t *context =
    (ml3_stm32_adc_port_context_t *)port_ctx;

  ADC->CCR |= ADC_CCR_VREFEN;
  context->vrefint_enable_tick = HW_RTC_GetTimerValue();
}

static void ml3_stm32_adc_port_enable_temperature_gate(void *port_ctx) {
  (void)port_ctx;
  ADC->CCR |= ADC_CCR_TSEN;
}

static void ml3_stm32_adc_port_enable_vrefint_buffer_gate(void *port_ctx) {
  (void)port_ctx;
  SYSCFG->CFGR3 |= SYSCFG_CFGR3_ENBUF_VREFINT_ADC;
}

static void ml3_stm32_adc_port_enable_temperature_buffer_gate(void *port_ctx) {
  (void)port_ctx;
  SYSCFG->CFGR3 |= SYSCFG_CFGR3_ENBUF_SENSOR_ADC;
}

static bool ml3_stm32_adc_port_is_vrefint_ready(void *port_ctx) {
  (void)port_ctx;
  return (SYSCFG->CFGR3 & SYSCFG_CFGR3_VREFINT_RDYF) != 0U;
}

static bool ml3_stm32_adc_port_is_temperature_ready(void *port_ctx) {
  (void)port_ctx;
  return (SYSCFG->CFGR3 & SYSCFG_CFGR3_SENSOR_ADC_RDYF) != 0U;
}

static bool ml3_stm32_adc_port_is_vrefint_buffer_ready(void *port_ctx) {
  (void)port_ctx;
  return (SYSCFG->CFGR3 & SYSCFG_CFGR3_VREFINT_ADC_RDYF) != 0U;
}

static bool ml3_stm32_adc_port_is_temperature_buffer_ready(void *port_ctx) {
  (void)port_ctx;
  return (SYSCFG->CFGR3 & SYSCFG_CFGR3_SENSOR_ADC_RDYF) != 0U;
}

static bool ml3_stm32_adc_port_is_reference_settled(void *port_ctx) {
  ml3_stm32_adc_port_context_t *context =
    (ml3_stm32_adc_port_context_t *)port_ctx;

  return (HW_RTC_GetTimerValue() - context->vrefint_enable_tick)
    >= ML3_STM32_ADC_VREFINT_SETTLE_TICKS;
}

static void ml3_stm32_adc_port_select_channel(void *port_ctx, uint16_t channel) {
  (void)port_ctx;
  ADC1->CHSELR = ml3_stm32_adc_port_channel_bit(channel);
}

static void ml3_stm32_adc_port_start_conversion(void *port_ctx) {
  (void)port_ctx;
  ADC1->CR |= ADC_CR_ADSTART;
}

static bool ml3_stm32_adc_port_is_conversion_complete(void *port_ctx) {
  (void)port_ctx;
  return (ADC1->ISR & ADC_ISR_EOC) != 0U;
}

static bool ml3_stm32_adc_port_read_raw(
    void *port_ctx, uint16_t *raw_code, bool *overrun) {
  bool observed_overrun;

  (void)port_ctx;
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

void ml3_stm32_adc_port_init(ml3_stm32_adc_port_context_t *context) {
  if (context != NULL) {
    context->vrefint_enable_tick = 0U;
  }
  RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
  RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
}
