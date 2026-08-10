 /******************************************************************************
  * @file    bsp.c
  * @author  MCD Application Team
  * @version V1.1.4
  * @date    08-January-2018
  * @brief   manages the sensors on the application
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2017 STMicroelectronics International N.V. 
  * All rights reserved.</center></h2>
  *
  * Redistribution and use in source and binary forms, with or without 
  * modification, are permitted, provided that the following conditions are met:
  *
  * 1. Redistribution of source code must retain the above copyright notice, 
  *    this list of conditions and the following disclaimer.
  * 2. Redistributions in binary form must reproduce the above copyright notice,
  *    this list of conditions and the following disclaimer in the documentation
  *    and/or other materials provided with the distribution.
  * 3. Neither the name of STMicroelectronics nor the names of other 
  *    contributors to this software may be used to endorse or promote products 
  *    derived from this software without specific written permission.
  * 4. This software, including modifications and/or derivative works of this 
  *    software, must execute solely and exclusively on microcontroller or
  *    microprocessor devices manufactured by or for STMicroelectronics.
  * 5. Redistribution and use of this software other than as permitted under 
  *    this license is void and will automatically terminate your rights under 
  *    this license. 
  *
  * THIS SOFTWARE IS PROVIDED BY STMICROELECTRONICS AND CONTRIBUTORS "AS IS" 
  * AND ANY EXPRESS, IMPLIED OR STATUTORY WARRANTIES, INCLUDING, BUT NOT 
  * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A 
  * PARTICULAR PURPOSE AND NON-INFRINGEMENT OF THIRD PARTY INTELLECTUAL PROPERTY
  * RIGHTS ARE DISCLAIMED TO THE FULLEST EXTENT PERMITTED BY LAW. IN NO EVENT 
  * SHALL STMICROELECTRONICS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
  * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
  * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
  * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */
  
  /* Includes ------------------------------------------------------------------*/
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "hw.h"
#include "timeServer.h"
#include "bsp.h"
#include "ml3_config.h"
#include "delay.h"
#include "vcom.h"
#include "lora.h"
#include "radio.h"
#include "adc_precision.h"
#include "ml3_measurement.h"
#include "ml3_calibration.h"
#include "ml3_payload.h"
#include "ml3_quality.h"
#include "ml3_stm32_adc_port.h"
/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
#if defined(LoRa_Sensor_Node)
#include "ds18b20.h"
#include "oil_float.h"
#include "gpio_exti.h"
#include "sht20.h"
#include "sht31.h"
#include "pwr_out.h"
#include "ult.h"
#include "lidar_lite_v3hp.h"
#include "weight.h"
#include "iwdg.h"
#include "bh1750.h"
#include "tfsensor.h"
#endif
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Exported functions ---------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
bool bh1750flags=0;
uint8_t mode2_flag=0;
static uint16_t AD_code1=0;
static uint16_t AD_code2=0;
static uint16_t AD_code3=0;

#ifdef USE_SHT
uint8_t flags=0;
#endif

//static GPIO_InitTypeDef  GPIO_InitStruct;
extern uint16_t batteryLevel_mV;
extern uint16_t fire_version;

#ifdef USE_SHT
extern float sht31_tem,sht31_hum;
extern I2C_HandleTypeDef I2cHandle1;
extern I2C_HandleTypeDef I2cHandle2;
extern I2C_HandleTypeDef I2cHandle3;
tfsensor_reading_t reading_t;
#endif

extern uint8_t mode;
extern uint8_t inmode,inmode2,inmode3;
extern uint16_t power_time;
extern uint32_t COUNT,COUNT2;

/* ML3 state is deliberately kept in this target adapter rather than in the
 * generic sensor structure.  No EEPROM slot or device-hash algorithm is
 * assumed here; calibration chunks are only staged until those release
 * inputs are approved. */
static bool ml3_initialized;
static bool ml3_active;
static bool ml3_request_pending;
static bool ml3_battery_valid;
static uint8_t ml3_battery_level;
static uint16_t ml3_warmup_ms = (uint16_t)ML3_CONFIG_WARMUP_TIME_MS;
static uint8_t ml3_cycles = 4U;
static uint8_t ml3_raw_enabled;
static uint8_t ml3_calibration_staging[ML3_AT_CALIBRATION_MAX_RECORD_LENGTH];
static uint16_t ml3_calibration_total;
static uint16_t ml3_calibration_next_offset;
static bool ml3_calibration_staging_active;

#if ML3_CONFIG_ACQUISITION_READY

#define ML3_TARGET_VREFINT_CAL_ADDR \
  ((const uint16_t *)(uint32_t)0x1FF80078UL)
#define ML3_TARGET_TEMPSENSOR_CAL1_ADDR \
  ((const uint16_t *)(uint32_t)0x1FF8007AUL)
#define ML3_TARGET_TEMPSENSOR_CAL2_ADDR \
  ((const uint16_t *)(uint32_t)0x1FF8007EUL)
#define ML3_TARGET_TEMPSENSOR_CAL_VDDA_UV UINT32_C(3000000)
#define ML3_TARGET_LORAMAC_BUSY_MASK \
  (UINT32_C(0x00000001) | UINT32_C(0x00000010))

static ml3_stm32_adc_port_context_t ml3_adc_port_context;
static adc_precision_context_t ml3_adc_precision_context;
static ml3_measurement_ctx_t ml3_measurement_context;
static ml3_quality_result_t ml3_quality_result;
static uint8_t ml3_routine_frame[ML3_PAYLOAD_ROUTINE_LENGTH];
static size_t ml3_routine_frame_length;

extern uint32_t LoRaMacState;

static bool ml3_target_configure_analog_pins(void *context)
{
  return ml3_stm32_adc_port_init((ml3_stm32_adc_port_context_t *)context);
}

static bool ml3_target_set_power_5v(void *context, bool enabled)
{
  (void)context;
  HAL_GPIO_WritePin(PWR_OUT_PORT, PWR_OUT_PIN,
    enabled ? GPIO_PIN_RESET : GPIO_PIN_SET);
  return true;
}

static bool ml3_target_set_thermistor_excitation(void *context, bool enabled)
{
  (void)context;
  (void)enabled;
  return true;
}

static bool ml3_target_request_radio_sleep(void *context)
{
  (void)context;
  if ((LoRaMacState & ML3_TARGET_LORAMAC_BUSY_MASK) != 0U)
  {
    return false;
  }
  Radio.Sleep();
  return true;
}

static bool ml3_target_watchdog_refresh(void *context)
{
  (void)context;
  IWDG_Refresh();
  return true;
}

static uint32_t ml3_target_read_reset_cause(void *context)
{
  (void)context;
  return RCC->CSR;
}

static bool ml3_target_raw_to_uv(
  uint16_t raw_code,
  uint32_t vdda_uv,
  int64_t *out_uv)
{
  uint32_t uv;

  if ((out_uv == NULL) || (vdda_uv == 0U))
  {
    return false;
  }
  if (adc_precision_compute_channel_uv(
        (uint32_t)raw_code,
        vdda_uv,
        &uv) != ADC_PRECISION_OK)
  {
    return false;
  }
  *out_uv = (int64_t)uv;
  return true;
}

static bool ml3_target_v5_values(
  const ml3_measurement_result_t *result,
  uint64_t *pre_v5_uv,
  uint64_t *post_v5_uv)
{
  uint32_t pre_pa4_uv;
  uint32_t post_pa4_uv;

  if ((result == NULL) || (pre_v5_uv == NULL) || (post_v5_uv == NULL)
      || !result->has_pre_v5_raw || !result->has_post_v5_raw
      || !result->has_vdda_pre_uv || !result->has_vdda_post_uv
      || (result->vdda_pre_uv == 0U) || (result->vdda_post_uv == 0U)
      || (ML3_CONFIG_V5_DIVIDER_RATIO_PPM == 0U))
  {
    return false;
  }
  if ((adc_precision_compute_channel_uv(
         (uint32_t)result->pre_v5_raw,
         result->vdda_pre_uv,
         &pre_pa4_uv) != ADC_PRECISION_OK)
      || (adc_precision_compute_channel_uv(
         (uint32_t)result->post_v5_raw,
         result->vdda_post_uv,
         &post_pa4_uv) != ADC_PRECISION_OK))
  {
    return false;
  }
  *pre_v5_uv = ((uint64_t)pre_pa4_uv * UINT64_C(1000000))
    / (uint64_t)ML3_CONFIG_V5_DIVIDER_RATIO_PPM;
  *post_v5_uv = ((uint64_t)post_pa4_uv * UINT64_C(1000000))
    / (uint64_t)ML3_CONFIG_V5_DIVIDER_RATIO_PPM;
  return true;
}

static bool ml3_target_die_temp_centic(
  const ml3_measurement_result_t *result,
  int32_t *out_centic)
{
  const uint16_t cal1 = *ML3_TARGET_TEMPSENSOR_CAL1_ADDR;
  const uint16_t cal2 = *ML3_TARGET_TEMPSENSOR_CAL2_ADDR;
  uint64_t normalized_oversampled_code;
  int64_t normalized_code;
  int64_t centic;

  if ((result == NULL) || (out_centic == NULL) || !result->has_die_temp_raw
      || !result->has_vdda_post_uv || (result->vdda_post_uv == 0U)
      || (cal2 <= cal1))
  {
    return false;
  }
  normalized_oversampled_code =
    ((uint64_t)result->die_temp_raw * (uint64_t)result->vdda_post_uv)
    / ML3_TARGET_TEMPSENSOR_CAL_VDDA_UV;
  normalized_code = (int64_t)(normalized_oversampled_code
    / (uint64_t)ADC_PRECISION_OVERSAMPLING_SCALE);
  centic = INT64_C(3000) + ((normalized_code - (int64_t)cal1)
    * INT64_C(10000)) / ((int64_t)cal2 - (int64_t)cal1);
  if ((centic < (int64_t)INT32_MIN) || (centic > (int64_t)INT32_MAX))
  {
    return false;
  }
  *out_centic = (int32_t)centic;
  return true;
}

/*
 * Populate the quality module's input from whatever the measurement engine
 * actually produced. A reduced valid-cycle count (any per-cycle ABBA fault,
 * commit 8202ae8) and absent burst statistics (below the three-cycle
 * validity floor - ml3_measurement.c compute_uv_stats returns without
 * setting the mean/median/noise/drift fields once valid_count < 3) are both
 * normal, expected input, not reasons to discard the reading. They pass
 * through to quality evaluation unmodified: it already applies its own
 * floor (ml3_quality.c, valid_cycles < 3) and marks incomplete evidence
 * when a field it needs is missing; duplicating either decision here would
 * risk disagreeing with it. Every optional field below is copied through
 * with its own has_* flag rather than being required, so an absent value
 * becomes an unavailable field in ml3_quality_input_t (and, downstream, a
 * transmitted sentinel) instead of an all-or-nothing rejection.
 *
 * Only a NULL argument or a result the engine never populated at all (no
 * has_abba_raw / no has_valid_cycle_count) is genuinely unusable and
 * rejected here.
 */
static bool ml3_target_fill_quality_input(
  const ml3_measurement_result_t *result,
  ml3_quality_input_t *quality_input)
{
  uint64_t pre_v5_uv = 0U;
  uint64_t post_v5_uv = 0U;
  int32_t die_temp_centic = 0;
  bool rail_samples_ok;
  bool v5_ok;
  bool die_temp_ok;
  size_t index;

  if ((result == NULL) || (quality_input == NULL)
      || !result->has_abba_raw || !result->has_valid_cycle_count)
  {
    return false;
  }

  (void)memset(quality_input, 0, sizeof(*quality_input));
  quality_input->seed_flags = result->has_faults ? result->fault_flags : 0U;
  quality_input->valid_cycles = (uint8_t)result->valid_cycle_count;
  quality_input->burst_cycles = (uint8_t)result->abba_raw_cycle_count;

  rail_samples_ok = result->has_vdda_pre_uv
    && (result->abba_raw_cycle_count > 0U);
  for (index = 0U;
       rail_samples_ok && (index < result->abba_raw_cycle_count);
       ++index)
  {
    const size_t sample_index = index * 2U;

    if (!ml3_target_raw_to_uv(result->abba_h1_raw[index],
          result->vdda_pre_uv, &quality_input->hi_samples_uv[sample_index])
        || !ml3_target_raw_to_uv(result->abba_h2_raw[index],
          result->vdda_pre_uv, &quality_input->hi_samples_uv[sample_index + 1U])
        || !ml3_target_raw_to_uv(result->abba_l1_raw[index],
          result->vdda_pre_uv, &quality_input->lo_samples_uv[sample_index])
        || !ml3_target_raw_to_uv(result->abba_l2_raw[index],
          result->vdda_pre_uv, &quality_input->lo_samples_uv[sample_index + 1U]))
    {
      rail_samples_ok = false;
    }
  }
  quality_input->has_rail_samples = rail_samples_ok;
  quality_input->rail_sample_count = rail_samples_ok
    ? (uint8_t)(result->abba_raw_cycle_count * 2U) : 0U;

  quality_input->has_mean_hi_uv = result->has_mean_hi_uv;
  quality_input->mean_hi_uv =
    result->has_mean_hi_uv ? result->mean_hi_uv : 0;
  quality_input->has_median_diff_uv = result->has_median_diff_uv;
  quality_input->median_diff_uv =
    result->has_median_diff_uv ? result->median_diff_uv : 0;
  quality_input->has_mean_lo_uv = result->has_mean_lo_uv;
  quality_input->mean_lo_uv =
    result->has_mean_lo_uv ? result->mean_lo_uv : 0;
  quality_input->has_noise_sd_uv = result->has_sd_uv && (result->sd_uv >= 0);
  quality_input->noise_sd_uv =
    quality_input->has_noise_sd_uv ? (uint64_t)result->sd_uv : 0U;
  quality_input->has_warmup_drift_uv = result->has_drift_uv;
  quality_input->warmup_drift_uv =
    result->has_drift_uv ? result->drift_uv : 0;

  quality_input->has_vdda_uv =
    result->has_vdda_pre_uv && result->has_vdda_post_uv;
  quality_input->vdda_pre_uv =
    result->has_vdda_pre_uv ? result->vdda_pre_uv : 0U;
  quality_input->vdda_post_uv =
    result->has_vdda_post_uv ? result->vdda_post_uv : 0U;

  v5_ok = ml3_target_v5_values(result, &pre_v5_uv, &post_v5_uv);
  quality_input->has_v5_uv = v5_ok;
  quality_input->v5_pre_uv = v5_ok ? pre_v5_uv : 0U;
  quality_input->v5_post_uv = v5_ok ? post_v5_uv : 0U;

  die_temp_ok = ml3_target_die_temp_centic(result, &die_temp_centic);
  quality_input->has_die_temp_centic = die_temp_ok;
  quality_input->die_temp_centic = die_temp_ok ? die_temp_centic : 0;

  /* No EEPROM calibration and no thermistor circuit in this trial's scope
   * (docs/2026-07-12-lsn50v2-ml3-firmware-plan.md); both report as always
   * valid rather than as a fault this adapter cannot actually detect. */
  quality_input->has_calibration_status = true;
  quality_input->calibration_valid = true;
  quality_input->has_thermistor_status = true;
  quality_input->thermistor_valid = true;
  return true;
}

static bool ml3_target_on_process(void *context,
  const ml3_measurement_result_t *result)
{
  ml3_quality_input_t quality_input;
  ml3_quality_thresholds_t thresholds;
  ml3_quality_status_t status;

  (void)context;
  if (!ml3_target_fill_quality_input(result, &quality_input))
  {
    return false;
  }
  if (ml3_quality_thresholds_from_config(&thresholds) != ML3_QUALITY_STATUS_OK)
  {
    return false;
  }
  status = ml3_quality_evaluate(&quality_input, &thresholds,
    &ml3_quality_result);
  /*
   * STATUS_INCOMPLETE still fully populates ml3_quality_result (state
   * INVALID, incomplete_reason recorded): that is the quality module's own
   * signal that required evidence was missing, and per plan section 3.9 it
   * must reach the radio like any other invalid reading, not be dropped.
   * Only INVALID_ARGUMENT/CONFIG_PENDING leave the result unpopulated, so
   * those remain the only cases treated as a build failure here.
   */
  return (status == ML3_QUALITY_STATUS_OK)
    || (status == ML3_QUALITY_STATUS_INCOMPLETE);
}

/*
 * Every numeric field below is modeled by ml3_payload_routine_t as
 * available/unavailable; ml3_payload_build_routine already encodes a
 * sentinel for whichever fields are marked unavailable (and forces every
 * numeric field to a sentinel when status_flags carries a core ADC
 * failure), so a value missing here becomes a sentinel in the transmitted
 * frame, not a dropped frame (plan section 3.9). sequence has no sentinel
 * representation in the frame format, so a result without one is the one
 * case this still refuses to build.
 */
static bool ml3_target_on_build_payload(void *context,
  const ml3_measurement_result_t *result)
{
  ml3_payload_routine_t payload;
  uint64_t pre_v5_uv = 0U;
  uint64_t post_v5_uv = 0U;
  int32_t die_temp_centic = 0;
  bool v5_ok;
  bool die_temp_ok;
  bool noise_ok;

  (void)context;
  if ((result == NULL) || !result->has_sequence)
  {
    return false;
  }

  v5_ok = ml3_target_v5_values(result, &pre_v5_uv, &post_v5_uv);
  die_temp_ok = ml3_target_die_temp_centic(result, &die_temp_centic);
  noise_ok = result->has_sd_uv && (result->sd_uv >= 0);
  (void)post_v5_uv;

  (void)memset(&payload, 0, sizeof(payload));
  payload.status_flags = ml3_quality_result.flags;
  payload.sequence = result->sequence;
  payload.corrected_diff_available = false;
  payload.mean_hi_available = result->has_mean_hi_uv;
  payload.mean_hi_uncalibrated_uv =
    result->has_mean_hi_uv ? result->mean_hi_uv : 0;
  payload.mean_lo_available = result->has_mean_lo_uv;
  payload.mean_lo_uncalibrated_uv =
    result->has_mean_lo_uv ? result->mean_lo_uv : 0;
  payload.vdda_available = result->has_vdda_pre_uv;
  payload.vdda_uv =
    result->has_vdda_pre_uv ? (int64_t)result->vdda_pre_uv : 0;
  payload.v5_available = v5_ok;
  payload.v5_uv = v5_ok ? (int64_t)pre_v5_uv : 0;
  payload.noise_available = noise_ok;
  payload.noise_uv = noise_ok ? result->sd_uv : 0;
  payload.die_temperature_available = die_temp_ok;
  payload.die_temperature_millic =
    die_temp_ok ? ((int64_t)die_temp_centic * INT64_C(10)) : 0;
  payload.soil_temperature_available = false;
  payload.quality_state = ml3_quality_result.state;
  payload.valid_cycle_count = (uint8_t)result->valid_cycle_count;
  payload.calibration_id = 0U;
  return ml3_payload_build_routine(&payload, ml3_routine_frame,
    ML3_PAYLOAD_ROUTINE_LENGTH, ML3_PAYLOAD_ROUTINE_LENGTH,
    &ml3_routine_frame_length) == ML3_PAYLOAD_OK;
}

static bool ml3_target_on_queue(void *context,
  const ml3_measurement_result_t *result)
{
  lora_AppData_t app_data;

  (void)context;
  (void)result;
  if (ml3_routine_frame_length != ML3_PAYLOAD_ROUTINE_LENGTH)
  {
    return false;
  }
  app_data.Buff = ml3_routine_frame;
  app_data.BuffSize = (uint8_t)ml3_routine_frame_length;
  app_data.Port = ML3_CONFIG_FPORT;
  return LORA_send(&app_data, LORAWAN_UNCONFIRMED_MSG) == LORA_SUCCESS;
}

static const ml3_measurement_port_t ml3_target_measurement_port = {
  .context = &ml3_adc_port_context,
  .now_ms = ml3_stm32_adc_port_now_ms,
  .read_reset_cause = ml3_target_read_reset_cause,
  .request_radio_sleep = ml3_target_request_radio_sleep,
  .configure_analog_pins = ml3_target_configure_analog_pins,
  .set_power_5v = ml3_target_set_power_5v,
  .set_thermistor_excitation = ml3_target_set_thermistor_excitation,
  .watchdog_refresh = ml3_target_watchdog_refresh,
  .on_process = ml3_target_on_process,
  .on_build_payload = ml3_target_on_build_payload,
  .on_queue = ml3_target_on_queue
};

#endif /* ML3_CONFIG_ACQUISITION_READY */

static bool ml3_mode_selected(void)
{
  return mode == ML3_CONFIG_MODE_ML3;
}

static int ml3_hex_value(uint8_t byte)
{
  if ((byte >= (uint8_t)'0') && (byte <= (uint8_t)'9'))
  {
    return (int)(byte - (uint8_t)'0');
  }
  if ((byte >= (uint8_t)'A') && (byte <= (uint8_t)'F'))
  {
    return (int)(byte - (uint8_t)'A') + 10;
  }
  if ((byte >= (uint8_t)'a') && (byte <= (uint8_t)'f'))
  {
    return (int)(byte - (uint8_t)'a') + 10;
  }
  return -1;
}

static uint16_t ml3_target_read_u16_le(const uint8_t *bytes)
{
  return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t ml3_target_read_u32_le(const uint8_t *bytes)
{
  return (uint32_t)bytes[0]
    | ((uint32_t)bytes[1] << 8U)
    | ((uint32_t)bytes[2] << 16U)
    | ((uint32_t)bytes[3] << 24U);
}

void BSP_ML3_Init(void)
{
#if defined(STM32L072xx)
  /* HW_Init runs before EEPROM mode restore and therefore initializes the
   * legacy HAL ADC. It does not start a conversion; fence its peripheral
   * state here before ML3 can ever acquire it (RM0376 ADC control sequence). */
  if ((ADC1->CR & ADC_CR_ADSTART) != 0U)
  {
    ADC1->CR |= ADC_CR_ADSTP;
  }
  if ((ADC1->CR & ADC_CR_ADEN) != 0U)
  {
    ADC1->CR |= ADC_CR_ADDIS;
  }
  RCC->APB2ENR &= ~RCC_APB2ENR_ADCEN;
#endif
  ml3_initialized = true;
  ml3_active = false;
  ml3_request_pending = false;
  ml3_battery_valid = false;
  ml3_battery_level = UINT8_C(0xFF);
  ml3_raw_enabled = 0U;
  ml3_calibration_total = 0U;
  ml3_calibration_next_offset = 0U;
  ml3_calibration_staging_active = false;
#if ML3_CONFIG_ACQUISITION_READY
  ml3_measurement_config_t config = {
    0U, 1U, 4U, 2U, (uint16_t)ml3_cycles,
    0U,
    (uint32_t)ml3_warmup_ms,
    ML3_CONFIG_DISCHARGE_THRESHOLD_MV / 2U,
    ML3_CONFIG_DISCHARGE_TIMEOUT_MS,
    1U
  };
  adc_precision_timeouts_t ml3_adc_timeouts;

  config.vrefint_calibration_word = *ML3_TARGET_VREFINT_CAL_ADDR;
  if (adc_precision_default_timeouts(&ml3_adc_timeouts) != ADC_PRECISION_OK)
  {
    ml3_initialized = false;
    return;
  }
  ml3_adc_timeouts.conversion_ms = 10U;
  adc_precision_init(&ml3_adc_precision_context,
    ml3_stm32_adc_port_get(), &ml3_adc_port_context);
  if (ml3_measurement_init(&ml3_measurement_context, &config,
      &ml3_target_measurement_port, &ml3_adc_precision_context,
      &ml3_adc_timeouts) != ML3_MEASUREMENT_OK)
  {
    ml3_initialized = false;
  }
  ml3_routine_frame_length = 0U;
#endif
}

void BSP_ML3_Service(void)
{
#if ML3_CONFIG_ACQUISITION_READY
  if (!ml3_mode_selected())
  {
    if (ml3_measurement_context.active || ml3_active || ml3_request_pending)
    {
      ml3_measurement_abort(&ml3_measurement_context);
      ml3_active = false;
      ml3_request_pending = false;
    }
    return;
  }
#endif
  if (!ml3_mode_selected() || !ml3_initialized)
  {
    return;
  }

  /* This is the only target-side acquisition entry point.  It remains
   * intentionally inert until the measured Gate 0/Phase 2 contract is true;
   * mode 10 still owns the ADC lockout while the gate is false. */
  if (!ML3_CONFIG_ACQUISITION_READY)
  {
    ml3_active = false;
    ml3_request_pending = false;
    return;
  }

#if ML3_CONFIG_ACQUISITION_READY
  ml3_measurement_step_t step;

  if (ml3_request_pending && !ml3_measurement_context.active)
  {
    ml3_quality_thresholds_t precondition_thresholds;

    /*
     * task-F1: evaluate the quality-threshold precondition before anything
     * is powered or sampled. Loading it later, inside on_process
     * (ml3_target_on_process), let Gate 0 alone start a full acquisition -
     * PB5 asserted, warm-up run, the whole ABBA burst captured - only to
     * discard it at the very end because thresholds were still pending:
     * battery cost with zero data, invisible remotely. Declining to start
     * at all here means fail-closed is "not acting", never "acting and
     * discarding".
     */
    if (ml3_quality_thresholds_from_config(&precondition_thresholds)
        != ML3_QUALITY_STATUS_OK)
    {
      ml3_active = false;
      ml3_request_pending = false;
      return;
    }
    if (ml3_measurement_start(&ml3_measurement_context) != ML3_MEASUREMENT_OK)
    {
      ml3_active = false;
      ml3_request_pending = false;
      return;
    }
  }
  if (!ml3_measurement_context.active)
  {
    ml3_active = false;
    return;
  }

  step = ml3_measurement_step(&ml3_measurement_context);
  ml3_active = ml3_measurement_context.active;
  if ((step == ML3_MEASUREMENT_STEP_DONE)
      || (step == ML3_MEASUREMENT_STEP_ERROR))
  {
    ml3_request_pending = false;
    ml3_active = false;
  }
#endif
}

bool BSP_ML3_IsActive(void)
{
  return ml3_active;
}

bool BSP_ML3_RequestRoutine(void)
{
  if (!ml3_mode_selected() || !ml3_initialized || !ML3_CONFIG_ACQUISITION_READY)
  {
    return false;
  }
  if (ml3_active || ml3_request_pending)
  {
    return false;
  }
  ml3_request_pending = true;
  return true;
}

bool BSP_ML3_RequestDiagnostic(void)
{
  return BSP_ML3_RequestRoutine();
}

void BSP_ML3_Abort(void)
{
#if ML3_CONFIG_ACQUISITION_READY
  ml3_measurement_abort(&ml3_measurement_context);
#endif
  ml3_active = false;
  ml3_request_pending = false;
  ml3_battery_valid = false;
  ml3_battery_level = UINT8_C(0xFF);
}

uint8_t BSP_ML3_GetBatteryLevel(void)
{
  if (ml3_mode_selected())
  {
    return ml3_battery_valid ? ml3_battery_level : UINT8_C(0xFF);
  }
  return HW_GetBatteryLevel();
}

uint16_t BSP_ML3_GetTemperatureLevel(void)
{
  if (ml3_mode_selected())
  {
    return UINT16_C(0xFFFF);
  }
  return HW_GetTemperatureLevel();
}

bool BSP_ML3_GetSettings(uint16_t *warmup_ms, uint8_t *cycles,
  uint8_t *raw_enabled)
{
  if ((warmup_ms == NULL) || (cycles == NULL) || (raw_enabled == NULL))
  {
    return false;
  }
  *warmup_ms = ml3_warmup_ms;
  *cycles = ml3_cycles;
  *raw_enabled = ml3_raw_enabled;
  return true;
}

bool BSP_ML3_SetWarmup(uint16_t warmup_ms)
{
  if ((warmup_ms < ML3_MEASUREMENT_MIN_WARMUP_MS)
      || (warmup_ms > ML3_MEASUREMENT_MAX_WARMUP_MS)
      || ml3_active)
  {
    return false;
  }
  ml3_warmup_ms = warmup_ms;
#if ML3_CONFIG_ACQUISITION_READY
  if (ml3_initialized)
  {
    ml3_measurement_context.config.warmup_ms = (uint32_t)warmup_ms;
  }
#endif
  return true;
}

bool BSP_ML3_SetCycles(uint8_t cycles)
{
  if ((cycles < ML3_MEASUREMENT_MIN_ABBA_CYCLES)
      || (cycles > ML3_MEASUREMENT_MAX_ABBA_CYCLES)
      || ml3_active)
  {
    return false;
  }
  ml3_cycles = cycles;
#if ML3_CONFIG_ACQUISITION_READY
  if (ml3_initialized)
  {
    ml3_measurement_context.config.abba_cycles = (uint16_t)cycles;
  }
#endif
  return true;
}

bool BSP_ML3_SetRaw(uint8_t raw_enabled)
{
  if ((raw_enabled != 0U) || ml3_active)
  {
    return false;
  }
  ml3_raw_enabled = 0U;
  return true;
}

bool BSP_ML3_CalibrationChunk(const ml3_at_calibration_chunk_t *chunk)
{
  uint8_t decoded[ML3_AT_CALIBRATION_CHUNK_MAX_BYTES];
  size_t byte_count;
  size_t index;

  if ((chunk == NULL) || (chunk->hex == NULL)
      || (chunk->total_length < ML3_AT_CALIBRATION_MIN_RECORD_LENGTH)
      || (chunk->total_length > ML3_AT_CALIBRATION_MAX_RECORD_LENGTH)
      || (chunk->hex_length == 0U) || ((chunk->hex_length & 1U) != 0U)
      || (chunk->hex_length > ML3_AT_CALIBRATION_CHUNK_MAX_HEX))
  {
    return false;
  }
  byte_count = chunk->hex_length / 2U;
  if ((size_t)chunk->offset + byte_count > chunk->total_length)
  {
    return false;
  }
  for (index = 0U; index < byte_count; ++index)
  {
    int high = ml3_hex_value(chunk->hex[index * 2U]);
    int low = ml3_hex_value(chunk->hex[(index * 2U) + 1U]);
    if ((high < 0) || (low < 0))
    {
      return false;
    }
    decoded[index] = (uint8_t)((high << 4) | low);
  }
  if (chunk->offset == 0U)
  {
    ml3_calibration_total = chunk->total_length;
    ml3_calibration_next_offset = 0U;
    ml3_calibration_staging_active = true;
  }
  if (!ml3_calibration_staging_active
      || chunk->total_length != ml3_calibration_total
      || chunk->offset != ml3_calibration_next_offset)
  {
    return false;
  }
  (void)memcpy(&ml3_calibration_staging[chunk->offset], decoded, byte_count);
  ml3_calibration_next_offset = (uint16_t)(chunk->offset + byte_count);
  if (ml3_calibration_next_offset == ml3_calibration_total)
  {
    uint8_t point_count = ml3_calibration_staging[39U];
    size_t expected_length = ML3_CALIBRATION_FIXED_SIZE
      + ((size_t)point_count * 8U);
    uint32_t stored_crc = ml3_target_read_u32_le(
      &ml3_calibration_staging[ml3_calibration_total - 4U]);
    if ((ml3_target_read_u32_le(&ml3_calibration_staging[0])
          != ML3_CALIBRATION_MAGIC)
        || (ml3_target_read_u16_le(&ml3_calibration_staging[4U])
          != ML3_CALIBRATION_SCHEMA_VERSION)
        || (point_count == 1U)
        || (expected_length != ml3_calibration_total)
        || (stored_crc != ml3_calibration_crc32(ml3_calibration_staging,
          ml3_calibration_total - 4U)))
    {
      ml3_calibration_staging_active = false;
      ml3_calibration_total = 0U;
      ml3_calibration_next_offset = 0U;
      return false;
    }
  }
  /* The complete record is intentionally not committed: Appendix B requires
   * an approved device hash and EEPROM slot map before any persistent write. */
  return true;
}

bool BSP_ML3_CalibrationClear(void)
{
  ml3_calibration_total = 0U;
  ml3_calibration_next_offset = 0U;
  ml3_calibration_staging_active = false;
  /* Clearing a verified slot is blocked until its target address and hash
   * policy are supplied; never erase an unknown EEPROM location. */
  return false;
}

void BSP_sensor_Read( sensor_t *sensor_data, uint8_t message)
{
	if (ml3_mode_selected())
	{
		(void)BSP_ML3_RequestDiagnostic();
		return;
	}

 	#if defined(LoRa_Sensor_Node)

	HW_GetBatteryLevel( );	
	if(message==1)
	{
		PPRINTF("\r\n");
		PPRINTF("Bat:%.3f V\r\n",(batteryLevel_mV/1000.0));
		if(mode==6)
		{
			PPRINTF("PB14_count1:%u\r\n",COUNT);
		}
		else if(mode==7)
		{
			PPRINTF("PB14_status:%d\r\n",HAL_GPIO_ReadPin(GPIO_EXTI14_PORT,GPIO_EXTI14_PIN));
			PPRINTF("PB15_status:%d\r\n",HAL_GPIO_ReadPin(GPIO_EXTI15_PORT,GPIO_EXTI15_PIN));
			PPRINTF("PA4_status:%d\r\n",HAL_GPIO_ReadPin(GPIO_EXTI4_PORT,GPIO_EXTI4_PIN));
		}
		else if(mode==9)
		{
			PPRINTF("PB14_count1:%u\r\n",COUNT);
			PPRINTF("PB15_count2:%u\r\n",COUNT2);
			PPRINTF("PA4_status:%d\r\n",HAL_GPIO_ReadPin(GPIO_EXTI4_PORT,GPIO_EXTI4_PIN));
		}
		else
		{
			PPRINTF("PB14_status:%d\r\n",HAL_GPIO_ReadPin(GPIO_EXTI14_PORT,GPIO_EXTI14_PIN));
		}
	}
	
  IWDG_Refresh();		
	//+3.3V power sensors	
	if(mode!=3)
	{
		sensor_data->temp1=DS18B20_GetTemp_SkipRom(1);
		if(message==1)
		{
			if((sensor_data->temp1>=-55)&&(sensor_data->temp1<=125))
			{
				PPRINTF("DS18B20_temp1:%.1f\r\n",sensor_data->temp1);
			}
			else
			{
				PPRINTF("DS18B20_temp1:null\r\n");
			}
		}
	}
	
  if((mode==1)||(mode==3))
  {		
		#ifdef USE_SHT
		if(flags==0)
		{
			sensor_data->temp_sht=6553.5;
			sensor_data->hum_sht=6553.5;
		} 
		else if(flags==1)
		{
			float temp2,hum1;
			HAL_I2C_MspInit(&I2cHandle1);
			temp2=SHT20_RT();//get temperature
			hum1=SHT20_RH(); //get humidity
			sensor_data->temp_sht=temp2;
			sensor_data->hum_sht=hum1;
			if(message==1)
			{
				PPRINTF("SHT2x_temp:%.1f,SHT2x_hum:%.1f\r\n",sensor_data->temp_sht,sensor_data->hum_sht);
			}
		}
		else if(flags==2)
		{			
			HAL_I2C_MspInit(&I2cHandle2);			
			tran_SHT31data();		
			sensor_data->temp_sht=sht31_tem;
			sensor_data->hum_sht=sht31_hum;
			if(message==1)
			{
				PPRINTF("SHT3x_temp:%.1f,SHT3x_hum:%.1f\r\n",sensor_data->temp_sht,sensor_data->hum_sht);
			}
		}
		else if(flags==3)
		{		
			bh1750flags=1;		
			I2C_IoInit();
			sensor_data->illuminance=bh1750_read();
			I2C_DoInit();		
			if(message==1)
			{			
				PPRINTF("BH1750_lum:%d\r\n",sensor_data->illuminance);		
			}				
		}		
		#endif
	}
  else if((mode==4)||(mode==9))
  {
		sensor_data->temp2=DS18B20_GetTemp_SkipRom(2);
		if(message==1)
		{			
			if((sensor_data->temp2>=-55)&&(sensor_data->temp2<=125))
			{
				PPRINTF("DS18B20_temp2:%.1f\r\n",sensor_data->temp2);
			}
			else
			{
				PPRINTF("DS18B20_temp2:null\r\n");
			}
		}
		sensor_data->temp3=DS18B20_GetTemp_SkipRom(3);
		if(message==1)
		{
			if((sensor_data->temp3>=-55)&&(sensor_data->temp3<=125))
			{
				PPRINTF("DS18B20_temp3:%.1f\r\n",sensor_data->temp3);
			}
			else
			{
				PPRINTF("DS18B20_temp3:null\r\n");
			}
		}
	}		
	
  //+5V power sensors	
	uint16_t adcdata[3][6];
	
	if(power_time!=0)
	{
		HAL_GPIO_WritePin(PWR_OUT_PORT,PWR_OUT_PIN,GPIO_PIN_RESET);//Enable 5v power supply
		for(uint16_t i=0;i<(uint16_t)(power_time/100);i++)
		{
			 HAL_Delay(100);
       if((i%99==0)&&(i!=0))
			 {
					IWDG_Refresh();		 
			 }				 
		}
	}
	
	if((mode!=3)&&(mode!=8)&&(mode!=9))
	{
		BSP_oil_float_Init();
		for(uint8_t q=0;q<6;q++)
		{
			adcdata[0][q] = HW_AdcReadChannel( ADC_Channel_Oil );//PA0
			HAL_Delay(10);
		}
		HAL_GPIO_WritePin(OIL_CONTROL_PORT,OIL_CONTROL_PIN,GPIO_PIN_SET);
		AD_code1=ADC_Average(adcdata[0]);
		sensor_data->oil=AD_code1*batteryLevel_mV/4095;
		if(message==1)
		{				
			PPRINTF("ADC_PA0:%.3f V\r\n",(sensor_data->oil/1000.0));
		}
	}
	
	if(mode==2)
	{
	  if(mode2_flag==1)
		{
			HAL_I2C_MspInit(&I2cHandle3);		
			LidarLite_init();			 
			sensor_data->distance_mm=LidarLite();	
			if(message==1)
			{				
				PPRINTF("lidar_lite_distance:%d cm\r\n",(sensor_data->distance_mm/10));	
			}				
		}
		else if(mode2_flag==2)
		{
			GPIO_ULT_INPUT_Init();
			GPIO_ULT_OUTPUT_Init();		
			HAL_NVIC_EnableIRQ(TIM3_IRQn);			
			sensor_data->distance_mm=ULT_test();
			HAL_NVIC_DisableIRQ(TIM3_IRQn);
			GPIO_ULT_INPUT_DeInit();
			GPIO_ULT_OUTPUT_DeInit();	
			if(message==1)
			{	
				PPRINTF("ULT_distance:%.1f cm\r\n",(sensor_data->distance_mm)/10.0);
			}
		}
		else if(mode2_flag==3)
	  {
			tfsensor_read_distance(&reading_t);	
		  sensor_data->distance_mm = reading_t.distance_mm;		
			sensor_data->distance_signal_strengh = reading_t.distance_signal_strengh;		
			if(message==1)
			{	
				PPRINTF("TF_distance:%d cm,TF_strength:%d\r\n",(sensor_data->distance_mm/10),sensor_data->distance_signal_strengh);	
			}				
		}
		else
		{
			sensor_data->distance_mm = 65535;		
			sensor_data->distance_signal_strengh = 65535;			
		}			 
	} 
	else if((mode==3)||(mode==8))
	{	
		 BSP_oil_float_Init();
		 for(uint8_t w=0;w<6;w++)
		 {
			 adcdata[0][w] = HW_AdcReadChannel( ADC_Channel_Oil );//PA0			 
			 HAL_Delay(10);				 
		 }
     AD_code1=ADC_Average(adcdata[0]);		 
	   sensor_data->oil=AD_code1*batteryLevel_mV/4095;				 
		 
		 HAL_Delay(50);	
		 for(uint8_t y=0;y<6;y++)
		 {
			 adcdata[1][y] = HW_AdcReadChannel( ADC_Channel_IN1 );//PA1
			 HAL_Delay(10);				 
		 }
     AD_code2=ADC_Average(adcdata[1]);		 
	   sensor_data->ADC_1=AD_code2*batteryLevel_mV/4095;		 
		 
		 HAL_Delay(50);	
		 for(uint8_t z=0;z<6;z++)
		 {
			 adcdata[2][z] = HW_AdcReadChannel( ADC_Channel_IN4 );//PA4	
			 HAL_Delay(10);				 
		 }		 
		 AD_code3=ADC_Average(adcdata[2]);		 
	   sensor_data->ADC_2=AD_code3*batteryLevel_mV/4095;  
		 HAL_GPIO_WritePin(OIL_CONTROL_PORT,OIL_CONTROL_PIN,GPIO_PIN_SET); 	

		 if(message==1)
		 {	
			 PPRINTF("ADC_PA0:%.3f V\r\n",(sensor_data->oil/1000.0));
			 PPRINTF("ADC_PA1:%.3f V\r\n",(sensor_data->ADC_1/1000.0));
			 PPRINTF("ADC_PA4:%.3f V\r\n",(sensor_data->ADC_2/1000.0));
		 }
	}	 
	else if(mode==5)
  {
		WEIGHT_SCK_Init();
		WEIGHT_DOUT_Init();		 
		sensor_data->Weight=Get_Weight();		
		WEIGHT_SCK_DeInit();
		WEIGHT_DOUT_DeInit();			
		if(message==1)
		{	
			PPRINTF("HX711_Weight:%d g\r\n",sensor_data->Weight);
		}
	}	

	GPIO_INPUT_IoInit();
  HAL_Delay(5);	
	sensor_data->in1=HAL_GPIO_ReadPin(GPIO_INPUT_PORT,GPIO_INPUT_PIN1);
	GPIO_INPUT_DeIoInit();
	if(message==1)
	{	
		PPRINTF("PA12_status:%d\r\n",sensor_data->in1);
	}
	
	HAL_GPIO_WritePin(PWR_OUT_PORT,PWR_OUT_PIN,GPIO_PIN_SET);//Disable 5v power supply 
	#endif
	
	if(message==1)
	{	
		HAL_Delay(500);
	}
}

void Device_status( device_t *device_data)
{
	uint8_t freq_band;
	uint16_t version;

	#if defined( REGION_EU868 )
		freq_band=0x01;
	#elif defined( REGION_US915 )
		freq_band=0x02;
	#elif defined( REGION_IN865 )
		freq_band=0x03;
	#elif defined( REGION_AU915 )
		freq_band=0x04;
	#elif defined( REGION_KZ865 )
		freq_band=0x05;
	#elif defined( REGION_RU864 )
		freq_band=0x06;
	#elif defined( REGION_AS923 )
	  #if defined AS923_1
		freq_band=0x08;
	  #elif defined AS923_2
	  freq_band=0x09;
	  #elif defined AS923_3
	  freq_band=0x0A;
	  #elif defined AS923_4
	  freq_band=0x0F;
		#else
		freq_band=0x07;
		#endif
	#elif defined( REGION_CN470 )
		freq_band=0x0B;
	#elif defined( REGION_EU433 )
	  freq_band=0x0C;
	#elif defined( REGION_KR920 )	
	  freq_band=0x0D;
	#elif defined( REGION_MA869 )	
	  freq_band=0x0E;
	#else
    freq_band=0x00;
	#endif
	device_data->freq_band = freq_band;
	
	#if defined( REGION_US915 )	|| defined( REGION_AU915 ) || defined( REGION_CN470 )
	device_data->sub_band = customize_set8channel_get();
	#else
	device_data->sub_band = 0xff;
	#endif
	
	if(fire_version>100)
	{
		version=(fire_version/100)<<8|(fire_version/10%10)<<4|(fire_version%10);
	}
	else
	{
		version=(fire_version/10)<<8|(fire_version%10)<<4;		
	}
	device_data->firm_ver = version;
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
  GPIO_InitTypeDef  GPIO_InitStruct;
  RCC_PeriphCLKInitTypeDef  RCC_PeriphCLKInitStruct;
  
  /*##-1- Configure the I2C clock source. The clock is derived from the SYSCLK #*/
  RCC_PeriphCLKInitStruct.PeriphClockSelection = RCC_PERIPHCLK_I2Cx;
  RCC_PeriphCLKInitStruct.I2c1ClockSelection = RCC_I2CxCLKSOURCE_SYSCLK;
  HAL_RCCEx_PeriphCLKConfig(&RCC_PeriphCLKInitStruct);

  /*##-2- Enable peripherals and GPIO Clocks #################################*/
  /* Enable GPIO TX/RX clock */
  I2Cx_SCL_GPIO_CLK_ENABLE();
  I2Cx_SDA_GPIO_CLK_ENABLE();
  /* Enable I2Cx clock */
  I2Cx_CLK_ENABLE();
  
  /*##-3- Configure peripheral GPIO ##########################################*/  
  /* I2C TX GPIO pin configuration  */
  GPIO_InitStruct.Pin       = I2Cx_SCL_PIN;
  GPIO_InitStruct.Mode      = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull      = GPIO_NOPULL;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = I2Cx_SCL_SDA_AF;
  HAL_GPIO_Init(I2Cx_SCL_GPIO_PORT, &GPIO_InitStruct);
    
  /* I2C RX GPIO pin configuration  */
  GPIO_InitStruct.Pin       = I2Cx_SDA_PIN;
  GPIO_InitStruct.Alternate = I2Cx_SCL_SDA_AF;
  HAL_GPIO_Init(I2Cx_SDA_GPIO_PORT, &GPIO_InitStruct);
	
}
/**
  * @brief I2C MSP De-Initialization 
  *        This function frees the hardware resources used in this example:
  *          - Disable the Peripheral's clock
  *          - Revert GPIO, DMA and NVIC configuration to their default state
  * @param hi2c: I2C handle pointer
  * @retval None
  */
void HAL_I2C_MspDeInit(I2C_HandleTypeDef *hi2c)
{
  /*##-1- Reset peripherals ##################################################*/
  I2Cx_FORCE_RESET();
  I2Cx_RELEASE_RESET();

  /*##-2- Disable peripherals and GPIO Clocks #################################*/
  /* Configure I2C Tx as alternate function  */
  HAL_GPIO_DeInit(I2Cx_SCL_GPIO_PORT, I2Cx_SCL_PIN);
  /* Configure I2C Rx as alternate function  */
  HAL_GPIO_DeInit(I2Cx_SDA_GPIO_PORT, I2Cx_SDA_PIN);
}

void  BSP_sensor_Init( void  )
{
  #if defined(LoRa_Sensor_Node)
	
	 pwr_control_IoInit();		
	if (ml3_mode_selected())
	{
		BSP_ML3_Init();
		return;
	}
	
	if((mode==1)||(mode==3))
	{	 
	 #ifdef USE_SHT
	 uint8_t txdata1[1]={0xE7},txdata2[2]={0xF3,0x2D};
	 
	 BSP_sht20_Init();

	 uint32_t currentTime = TimerGetCurrentTime();	 
	 while(HAL_I2C_Master_Transmit(&I2cHandle1,0x80,txdata1,1,1000) != HAL_OK)
	 {
			if(TimerGetElapsedTime(currentTime) >= 500)
			{
			 flags=0;
			 break;
		 }
	 }
	 if(HAL_I2C_Master_Transmit(&I2cHandle1,0x80,txdata1,1,1000) == HAL_OK)
	 {
		 flags=1;
	   PRINTF("\n\rUse Sensor is STH2x\r\n");
	 }
	 
	 if(flags==0)
	 {	 
		 HAL_I2C_MspDeInit(&I2cHandle1);	 
		 BSP_sht31_Init();

		 currentTime = TimerGetCurrentTime();		 
	 	 while(HAL_I2C_Master_Transmit(&I2cHandle2,0x88,txdata2,2,1000) != HAL_OK) 
		 {
			if(TimerGetElapsedTime(currentTime) >= 500)
			{
			 flags=0;
			 break;
			}
		 }
	 if(HAL_I2C_Master_Transmit(&I2cHandle2,0x88,txdata2,2,1000) == HAL_OK)
	 {
		 flags=2;
		 PRINTF("\n\rUse Sensor is STH3x\r\n");
	 }	 
   }
	 
	 if(flags==0)
	 {
		 float luxtemp;
		 HAL_I2C_MspDeInit(&I2cHandle2);
		 I2C_IoInit();
		 luxtemp=bh1750_read();
		 I2C_DoInit();
		 if(luxtemp!=0)
		 {
			flags=3;
			PRINTF("\n\rUse Sensor is BH1750\r\n");			 
		 }
	 }
	 
	 if(flags==0)
	 {
		 PRINTF("\n\rNo I2C device detected\r\n");
	 }
	 #endif
   }
	 
	else if(mode==2)
	{	
	  uint8_t dataByte[1]={0x00};		
	  HAL_GPIO_WritePin(PWR_OUT_PORT,PWR_OUT_PIN,GPIO_PIN_RESET);//Enable 5v power supply	
    BSP_lidar_Init();
    waitbusy(); 	
    HAL_I2C_Mem_Write(&I2cHandle3,0xc4,0x00,1,dataByte,1,1000);	
	  if(waitbusy()<9999)
	  {
     mode2_flag=1;					
		 PRINTF("\n\rUse Sensor is LIDAR_Lite_v3HP\r\n");
	  }		
	  else
	  {		 
		 HAL_I2C_MspDeInit(&I2cHandle3);	 		
		 GPIO_ULT_INPUT_Init();
		 GPIO_ULT_OUTPUT_Init();		
		 if(HAL_GPIO_ReadPin(ULT_Echo_PORT, ULT_Echo_PIN)==RESET)
	   {  
			mode2_flag=2;	  
			TIM3_Init();
			PRINTF("\n\rUse Sensor is ultrasonic distance measurement\r\n");				 
		 }	 
		 GPIO_ULT_INPUT_DeInit();
		 GPIO_ULT_OUTPUT_DeInit();
		 
		 if(mode2_flag==0)
		 {	 
			if(check_deceive()==1)
			{
				mode2_flag=3;
				PRINTF("\n\rUse Sensor is TF-series sensor\r\n");	
			}	
			else
			{	
		    PRINTF("\n\rNo distance measurement device detected\r\n");					
			}					
		 }
	  }
		HAL_GPIO_WritePin(PWR_OUT_PORT,PWR_OUT_PIN,GPIO_PIN_SET);//Disable 5v power supply		
	}
	else if(mode==5)
	{
	  HAL_GPIO_WritePin(PWR_OUT_PORT,PWR_OUT_PIN,GPIO_PIN_RESET);//Enable 5v power supply	
		WEIGHT_SCK_Init();
		WEIGHT_DOUT_Init();
		Get_Maopi();
    HAL_Delay(500);
		Get_Maopi();	
		WEIGHT_SCK_DeInit();
		WEIGHT_DOUT_DeInit();				
		HAL_GPIO_WritePin(PWR_OUT_PORT,PWR_OUT_PIN,GPIO_PIN_SET);//Disable 5v power supply		
		PPRINTF("\n\rUse Sensor is HX711\r\n");			
	}
	else if((mode==7)||(mode==9))
	{
		GPIO_EXTI15_IoInit(inmode2);
	  GPIO_EXTI4_IoInit(inmode3);
		if(mode==9)
		{
			BSP_oil_float_DeInit();
		}
	}
	
	GPIO_EXTI14_IoInit(inmode);
	GPIO_INPUT_IoInit();
	
	#endif
}
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
