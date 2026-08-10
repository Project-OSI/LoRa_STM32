#ifndef ML3_STM32_ADC_PORT_H
#define ML3_STM32_ADC_PORT_H

#include <stdint.h>

#include "adc_precision.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t vrefint_enable_tick;
  uint32_t last_rtc_tick;
  uint32_t rtc_epoch_ms;
  bool rtc_tick_initialized;
} ml3_stm32_adc_port_context_t;

const adc_precision_port_t *ml3_stm32_adc_port_get(void);
bool ml3_stm32_adc_port_init(ml3_stm32_adc_port_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* ML3_STM32_ADC_PORT_H */
