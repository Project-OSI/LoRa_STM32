#ifndef ML3_STM32_ADC_PORT_H
#define ML3_STM32_ADC_PORT_H

#include <stdint.h>

#include "adc_precision.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t vrefint_enable_tick;
} ml3_stm32_adc_port_context_t;

const adc_precision_port_t *ml3_stm32_adc_port_get(void);
void ml3_stm32_adc_port_init(ml3_stm32_adc_port_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* ML3_STM32_ADC_PORT_H */
