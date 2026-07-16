/* SPDX-License-Identifier: MIT */
#include "ml3_payload.h"
#include "ml3_quality.h"

#include <limits.h>
#include <string.h>

static void ml3_payload_write_u16_be(uint8_t* output, uint16_t value)
{
  output[0] = (uint8_t)(value >> 8U);
  output[1] = (uint8_t)value;
}

static void ml3_payload_write_u32_be(uint8_t* output, uint32_t value)
{
  output[0] = (uint8_t)(value >> 24U);
  output[1] = (uint8_t)(value >> 16U);
  output[2] = (uint8_t)(value >> 8U);
  output[3] = (uint8_t)value;
}

static uint16_t ml3_payload_read_u16_be(const uint8_t* input)
{
  return (uint16_t)(((uint16_t)input[0] << 8U) | input[1]);
}

static bool ml3_payload_encode_signed(
  bool available,
  int64_t value,
  int64_t divisor,
  uint16_t sentinel,
  uint16_t* encoded)
{
  int64_t quantized;

  if (!available) {
    *encoded = sentinel;
    return true;
  }
  quantized = value / divisor;
  if ((quantized < INT16_MIN) || (quantized > INT16_MAX)) {
    return false;
  }
  *encoded = (uint16_t)(int16_t)quantized;
  return *encoded != sentinel;
}

static bool ml3_payload_encode_unsigned(
  bool available,
  int64_t value,
  int64_t divisor,
  uint16_t* encoded)
{
  int64_t quantized;

  if (!available) {
    *encoded = ML3_PAYLOAD_UNSIGNED_SENTINEL;
    return true;
  }
  if (value < 0) {
    return false;
  }
  quantized = value / divisor;
  if ((quantized < 0) || (quantized >= (int64_t)ML3_PAYLOAD_UNSIGNED_SENTINEL)) {
    return false;
  }
  *encoded = (uint16_t)quantized;
  return true;
}

static uint8_t ml3_payload_pack_quality(
  ml3_quality_state_t quality_state,
  uint8_t valid_cycle_count)
{
  return (uint8_t)(
    ((uint8_t)quality_state << ML3_PAYLOAD_QUALITY_STATE_SHIFT) |
    valid_cycle_count);
}

static bool ml3_payload_quality_is_valid(
  ml3_quality_state_t quality_state,
  uint8_t valid_cycle_count)
{
  return ((unsigned int)quality_state <=
          (unsigned int)ML3_QUALITY_STATE_INVALID) &&
    (valid_cycle_count <= ML3_PAYLOAD_MAX_ABBA_CYCLES);
}

static bool ml3_payload_quality_is_consistent(
  uint16_t status_flags,
  ml3_quality_state_t quality_state,
  uint8_t valid_cycle_count)
{
  bool fixed_invalidating =
    (status_flags & ML3_QUALITY_INVALIDATING_MASK) != 0U;
  bool too_few_cycles = valid_cycle_count < ML3_PAYLOAD_MIN_VALID_CYCLES;

  if ((fixed_invalidating || too_few_cycles) &&
      (quality_state != ML3_QUALITY_STATE_INVALID)) {
    return false;
  }
  if ((quality_state == ML3_QUALITY_STATE_VALID) &&
      (status_flags != 0U)) {
    return false;
  }
  /*
   * The quality module can never emit DEGRADED with zero status flags:
   * nonzero flags imply DEGRADED and zero flags imply VALID. Reject the
   * unreachable tuple instead of treating it as consistent.
   */
  if ((quality_state == ML3_QUALITY_STATE_DEGRADED) &&
      (status_flags == 0U)) {
    return false;
  }
  return true;
}

static bool ml3_payload_has_core_adc_failure(uint16_t status_flags)
{
  const uint16_t core_adc_failure_mask =
    (uint16_t)((uint16_t)ML3_QUALITY_FLAG_ADC_INIT |
               (uint16_t)ML3_QUALITY_FLAG_ADC_CAL |
               (uint16_t)ML3_QUALITY_FLAG_ADC_TIMEOUT |
               (uint16_t)ML3_QUALITY_FLAG_ADC_OVERRUN);

  return (status_flags & core_adc_failure_mask) != 0U;
}

ml3_payload_status_t ml3_payload_build_routine(
  const ml3_payload_routine_t* input,
  uint8_t* output,
  size_t output_capacity,
  size_t max_frmpayload_bytes,
  size_t* output_length)
{
  uint8_t frame[ML3_PAYLOAD_ROUTINE_LENGTH];
  uint16_t corrected;
  uint16_t mean_hi;
  uint16_t mean_lo;
  uint16_t vdda;
  uint16_t v5;
  uint16_t noise;
  uint16_t die_temperature;
  uint16_t soil_temperature;
  bool corrected_available;
  bool acquisition_available;
  bool soil_temperature_available;

  if ((input == NULL) || (output == NULL) || (output_length == NULL)) {
    return ML3_PAYLOAD_ERR_INVALID_ARGUMENT;
  }
  *output_length = 0U;
  if (max_frmpayload_bytes == 0U) {
    return ML3_PAYLOAD_ERR_MAX_FRMPAYLOAD_NOT_READY;
  }
  if (ML3_PAYLOAD_ROUTINE_LENGTH > max_frmpayload_bytes) {
    return ML3_PAYLOAD_ERR_FRAME_TOO_LARGE;
  }
  if (output_capacity < ML3_PAYLOAD_ROUTINE_LENGTH) {
    return ML3_PAYLOAD_ERR_OUTPUT_TOO_SMALL;
  }
  if (!ml3_payload_quality_is_valid(
        input->quality_state,
        input->valid_cycle_count)) {
    return ML3_PAYLOAD_ERR_VALUE_OUT_OF_RANGE;
  }
  if (!ml3_payload_quality_is_consistent(
        input->status_flags,
        input->quality_state,
        input->valid_cycle_count)) {
    return ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY;
  }

  acquisition_available = !ml3_payload_has_core_adc_failure(input->status_flags);
  soil_temperature_available = acquisition_available &&
    ((input->status_flags & (uint16_t)ML3_QUALITY_FLAG_THERM_FAULT) == 0U) &&
    input->soil_temperature_available;
  corrected_available = acquisition_available &&
    input->corrected_diff_available &&
    (input->quality_state != ML3_QUALITY_STATE_INVALID) &&
    ((input->status_flags & (uint16_t)ML3_QUALITY_FLAG_CAL_INVALID) == 0U);
  if (!ml3_payload_encode_signed(
        corrected_available,
        input->corrected_diff_uv,
        (int64_t)ML3_PAYLOAD_UV_PER_DECI_MV,
        ML3_PAYLOAD_CORRECTED_SENTINEL,
        &corrected) ||
      !ml3_payload_encode_unsigned(
        acquisition_available && input->mean_hi_available,
        input->mean_hi_uncalibrated_uv,
        (int64_t)ML3_PAYLOAD_UV_PER_DECI_MV,
        &mean_hi) ||
      !ml3_payload_encode_unsigned(
        acquisition_available && input->mean_lo_available,
        input->mean_lo_uncalibrated_uv,
        (int64_t)ML3_PAYLOAD_UV_PER_DECI_MV,
        &mean_lo) ||
      !ml3_payload_encode_unsigned(
        acquisition_available && input->vdda_available,
        input->vdda_uv,
        (int64_t)ML3_PAYLOAD_UV_PER_MV,
        &vdda) ||
      !ml3_payload_encode_unsigned(
        acquisition_available && input->v5_available,
        input->v5_uv,
        (int64_t)ML3_PAYLOAD_UV_PER_MV,
        &v5) ||
      !ml3_payload_encode_unsigned(
        acquisition_available && input->noise_available,
        input->noise_uv,
        INT64_C(1),
        &noise) ||
      !ml3_payload_encode_signed(
        acquisition_available && input->die_temperature_available,
        input->die_temperature_millic,
        (int64_t)ML3_PAYLOAD_MILLIC_PER_CENTIC,
        ML3_PAYLOAD_TEMPERATURE_SENTINEL,
        &die_temperature) ||
      !ml3_payload_encode_signed(
        soil_temperature_available,
        input->soil_temperature_millic,
        (int64_t)ML3_PAYLOAD_MILLIC_PER_CENTIC,
        ML3_PAYLOAD_TEMPERATURE_SENTINEL,
        &soil_temperature)) {
    return ML3_PAYLOAD_ERR_VALUE_OUT_OF_RANGE;
  }

  frame[ML3_PAYLOAD_ROUTINE_VERSION_OFFSET] = ML3_PAYLOAD_PROTOCOL_VERSION;
  frame[ML3_PAYLOAD_ROUTINE_TYPE_OFFSET] = ML3_PAYLOAD_TYPE_ROUTINE;
  ml3_payload_write_u16_be(
    &frame[ML3_PAYLOAD_ROUTINE_FLAGS_OFFSET],
    input->status_flags);
  ml3_payload_write_u16_be(
    &frame[ML3_PAYLOAD_ROUTINE_SEQUENCE_OFFSET],
    input->sequence);
  ml3_payload_write_u16_be(
    &frame[ML3_PAYLOAD_ROUTINE_CORRECTED_OFFSET],
    corrected);
  ml3_payload_write_u16_be(&frame[ML3_PAYLOAD_ROUTINE_MEAN_HI_OFFSET], mean_hi);
  ml3_payload_write_u16_be(&frame[ML3_PAYLOAD_ROUTINE_MEAN_LO_OFFSET], mean_lo);
  ml3_payload_write_u16_be(&frame[ML3_PAYLOAD_ROUTINE_VDDA_OFFSET], vdda);
  ml3_payload_write_u16_be(&frame[ML3_PAYLOAD_ROUTINE_V5_OFFSET], v5);
  ml3_payload_write_u16_be(&frame[ML3_PAYLOAD_ROUTINE_NOISE_OFFSET], noise);
  ml3_payload_write_u16_be(
    &frame[ML3_PAYLOAD_ROUTINE_DIE_TEMP_OFFSET],
    die_temperature);
  ml3_payload_write_u16_be(
    &frame[ML3_PAYLOAD_ROUTINE_SOIL_TEMP_OFFSET],
    soil_temperature);
  frame[ML3_PAYLOAD_ROUTINE_QUALITY_OFFSET] = ml3_payload_pack_quality(
    input->quality_state,
    input->valid_cycle_count);
  frame[ML3_PAYLOAD_ROUTINE_CALIBRATION_ID_OFFSET] = input->calibration_id;
  frame[ML3_PAYLOAD_ROUTINE_RESERVED_OFFSET] = 0U;

  (void)memcpy(output, frame, sizeof(frame));
  *output_length = sizeof(frame);
  return ML3_PAYLOAD_OK;
}

ml3_payload_status_t ml3_payload_diagnostic_part_count(
  uint8_t cycle_count,
  uint8_t* part_count)
{
  if (part_count == NULL) {
    return ML3_PAYLOAD_ERR_INVALID_ARGUMENT;
  }
  if (cycle_count > ML3_PAYLOAD_MAX_ABBA_CYCLES) {
    return ML3_PAYLOAD_ERR_VALUE_OUT_OF_RANGE;
  }
  *part_count = (uint8_t)(1U +
    (((unsigned int)cycle_count +
      (ML3_PAYLOAD_DIAGNOSTIC_CYCLES_PER_PART - 1U)) /
     ML3_PAYLOAD_DIAGNOSTIC_CYCLES_PER_PART));
  return ML3_PAYLOAD_OK;
}

static bool ml3_payload_raw_is_valid(bool available, uint16_t raw)
{
  return !available || (raw <= ML3_PAYLOAD_RAW_CODE_MAX);
}

static uint16_t ml3_payload_raw_or_sentinel(bool available, uint16_t raw)
{
  return available ? raw : ML3_PAYLOAD_RAW_SENTINEL;
}

static bool ml3_payload_diagnostic_raw_is_valid(
  const ml3_payload_diagnostic_t* input)
{
  size_t index;

  if (!ml3_payload_raw_is_valid(
        input->vrefint_pre_available,
        input->vrefint_pre_raw) ||
      !ml3_payload_raw_is_valid(
        input->vrefint_post_available,
        input->vrefint_post_raw) ||
      !ml3_payload_raw_is_valid(input->pa4_pre_available, input->pa4_pre_raw) ||
      !ml3_payload_raw_is_valid(input->pa4_post_available, input->pa4_post_raw) ||
      !ml3_payload_raw_is_valid(
        input->thermistor_raw_ratio_available,
        input->thermistor_raw_ratio)) {
    return false;
  }
  for (index = 0U; index < input->cycle_count; index += 1U) {
    if ((input->cycles[index].h1 > ML3_PAYLOAD_RAW_CODE_MAX) ||
        (input->cycles[index].l1 > ML3_PAYLOAD_RAW_CODE_MAX) ||
        (input->cycles[index].l2 > ML3_PAYLOAD_RAW_CODE_MAX) ||
        (input->cycles[index].h2 > ML3_PAYLOAD_RAW_CODE_MAX)) {
      return false;
    }
  }
  return true;
}

ml3_payload_status_t ml3_payload_build_diagnostic_part(
  const ml3_payload_diagnostic_t* input,
  uint8_t part_index,
  uint8_t* output,
  size_t output_capacity,
  size_t max_frmpayload_bytes,
  size_t* output_length)
{
  uint8_t frame[ML3_PAYLOAD_DIAGNOSTIC_CYCLE_PART_MAX_LENGTH];
  uint8_t part_count;
  size_t frame_length;
  size_t first_cycle = 0U;
  size_t cycles_in_part = 0U;
  size_t index;
  size_t offset;
  ml3_payload_status_t status;

  if ((input == NULL) || (output == NULL) || (output_length == NULL)) {
    return ML3_PAYLOAD_ERR_INVALID_ARGUMENT;
  }
  *output_length = 0U;
  status = ml3_payload_diagnostic_part_count(input->cycle_count, &part_count);
  if (status != ML3_PAYLOAD_OK) {
    return status;
  }
  /*
   * part_index and part_count are packed into the frame's high/low
   * nibbles unmasked (see the ML3_PAYLOAD_DIAGNOSTIC_PART_OFFSET write
   * below). A value past the 4-bit nibble range would silently corrupt
   * that byte instead of being caught, so reject it explicitly. Safe
   * today only because burst cycles <= ML3_MEASUREMENT_MAX_ABBA_CYCLES
   * bounds part_count <= 3, but this guards the invariant directly.
   */
  if ((part_index > ML3_PAYLOAD_DIAGNOSTIC_PART_COUNT_MASK) ||
      (part_count > ML3_PAYLOAD_DIAGNOSTIC_PART_COUNT_MASK)) {
    return ML3_PAYLOAD_ERR_INVALID_ARGUMENT;
  }
  if ((part_index >= part_count) ||
      !ml3_payload_quality_is_valid(
        input->quality_state,
        input->valid_cycle_count) ||
      !ml3_payload_diagnostic_raw_is_valid(input)) {
    return ML3_PAYLOAD_ERR_VALUE_OUT_OF_RANGE;
  }
  if (!ml3_payload_quality_is_consistent(
        input->status_flags,
        input->quality_state,
        input->valid_cycle_count) ||
      (input->valid_cycle_count > input->cycle_count)) {
    return ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY;
  }

  if (part_index == 0U) {
    frame_length = ML3_PAYLOAD_DIAGNOSTIC_HEADER_LENGTH;
  } else {
    first_cycle =
      ((size_t)part_index - 1U) * ML3_PAYLOAD_DIAGNOSTIC_CYCLES_PER_PART;
    cycles_in_part = (size_t)input->cycle_count - first_cycle;
    if (cycles_in_part > ML3_PAYLOAD_DIAGNOSTIC_CYCLES_PER_PART) {
      cycles_in_part = ML3_PAYLOAD_DIAGNOSTIC_CYCLES_PER_PART;
    }
    frame_length = ML3_PAYLOAD_DIAGNOSTIC_COMMON_PREFIX_LENGTH +
      (cycles_in_part * ML3_PAYLOAD_DIAGNOSTIC_CYCLE_LENGTH);
  }
  if (max_frmpayload_bytes == 0U) {
    return ML3_PAYLOAD_ERR_MAX_FRMPAYLOAD_NOT_READY;
  }
  if (frame_length > max_frmpayload_bytes) {
    return ML3_PAYLOAD_ERR_FRAME_TOO_LARGE;
  }
  if (frame_length > output_capacity) {
    return ML3_PAYLOAD_ERR_OUTPUT_TOO_SMALL;
  }

  frame[ML3_PAYLOAD_DIAGNOSTIC_VERSION_OFFSET] = ML3_PAYLOAD_PROTOCOL_VERSION;
  frame[ML3_PAYLOAD_DIAGNOSTIC_TYPE_OFFSET] = ML3_PAYLOAD_TYPE_DIAGNOSTIC;
  frame[ML3_PAYLOAD_DIAGNOSTIC_PART_OFFSET] = (uint8_t)(
    (part_index << ML3_PAYLOAD_DIAGNOSTIC_PART_INDEX_SHIFT) | part_count);
  if (part_index == 0U) {
    ml3_payload_write_u16_be(
      &frame[ML3_PAYLOAD_DIAGNOSTIC_SEQUENCE_OFFSET],
      input->sequence);
    ml3_payload_write_u16_be(
      &frame[ML3_PAYLOAD_DIAGNOSTIC_FLAGS_OFFSET],
      input->status_flags);
    frame[ML3_PAYLOAD_DIAGNOSTIC_QUALITY_OFFSET] = ml3_payload_pack_quality(
      input->quality_state,
      input->valid_cycle_count);
    ml3_payload_write_u32_be(
      &frame[ML3_PAYLOAD_DIAGNOSTIC_RESET_CAUSE_OFFSET],
      input->reset_cause);
    ml3_payload_write_u16_be(
      &frame[ML3_PAYLOAD_DIAGNOSTIC_WARMUP_OFFSET],
      input->warmup_ms);
    frame[ML3_PAYLOAD_DIAGNOSTIC_HARDWARE_REV_OFFSET] =
      input->hardware_revision;
    ml3_payload_write_u16_be(
      &frame[ML3_PAYLOAD_DIAGNOSTIC_FIRMWARE_BUILD_OFFSET],
      input->firmware_build_id);
    ml3_payload_write_u16_be(
      &frame[ML3_PAYLOAD_DIAGNOSTIC_CAL_SCHEMA_OFFSET],
      input->calibration_schema);
    frame[ML3_PAYLOAD_DIAGNOSTIC_CAL_ID_OFFSET] = input->calibration_id;
    ml3_payload_write_u16_be(
      &frame[ML3_PAYLOAD_DIAGNOSTIC_VREF_PRE_OFFSET],
      ml3_payload_raw_or_sentinel(
        input->vrefint_pre_available,
        input->vrefint_pre_raw));
    ml3_payload_write_u16_be(
      &frame[ML3_PAYLOAD_DIAGNOSTIC_VREF_POST_OFFSET],
      ml3_payload_raw_or_sentinel(
        input->vrefint_post_available,
        input->vrefint_post_raw));
    ml3_payload_write_u16_be(
      &frame[ML3_PAYLOAD_DIAGNOSTIC_PA4_PRE_OFFSET],
      ml3_payload_raw_or_sentinel(
        input->pa4_pre_available,
        input->pa4_pre_raw));
    ml3_payload_write_u16_be(
      &frame[ML3_PAYLOAD_DIAGNOSTIC_PA4_POST_OFFSET],
      ml3_payload_raw_or_sentinel(
        input->pa4_post_available,
        input->pa4_post_raw));
    ml3_payload_write_u16_be(
      &frame[ML3_PAYLOAD_DIAGNOSTIC_THERMISTOR_RAW_OFFSET],
      ml3_payload_raw_or_sentinel(
        input->thermistor_raw_ratio_available,
        input->thermistor_raw_ratio));
  } else {
    offset = ML3_PAYLOAD_DIAGNOSTIC_COMMON_PREFIX_LENGTH;
    for (index = 0U; index < cycles_in_part; index += 1U) {
      const ml3_payload_raw_cycle_t* cycle =
        &input->cycles[first_cycle + index];
      ml3_payload_write_u16_be(&frame[offset], cycle->h1);
      ml3_payload_write_u16_be(&frame[offset + 2U], cycle->l1);
      ml3_payload_write_u16_be(&frame[offset + 4U], cycle->l2);
      ml3_payload_write_u16_be(&frame[offset + 6U], cycle->h2);
      offset += ML3_PAYLOAD_DIAGNOSTIC_CYCLE_LENGTH;
    }
  }

  (void)memcpy(output, frame, frame_length);
  *output_length = frame_length;
  return ML3_PAYLOAD_OK;
}

ml3_payload_status_t ml3_payload_validate_routine_frame(
  const uint8_t* frame,
  size_t frame_length)
{
  uint16_t flags;
  uint16_t corrected;
  uint8_t quality;
  uint8_t quality_state;
  uint8_t valid_cycles;

  if (frame == NULL) {
    return ML3_PAYLOAD_ERR_INVALID_ARGUMENT;
  }
  if ((frame_length != ML3_PAYLOAD_ROUTINE_LENGTH) ||
      (frame[ML3_PAYLOAD_ROUTINE_VERSION_OFFSET] != ML3_PAYLOAD_PROTOCOL_VERSION) ||
      (frame[ML3_PAYLOAD_ROUTINE_TYPE_OFFSET] != ML3_PAYLOAD_TYPE_ROUTINE) ||
      (frame[ML3_PAYLOAD_ROUTINE_RESERVED_OFFSET] != 0U)) {
    return ML3_PAYLOAD_ERR_VALUE_OUT_OF_RANGE;
  }
  quality = frame[ML3_PAYLOAD_ROUTINE_QUALITY_OFFSET];
  quality_state = (uint8_t)(
    (quality & ML3_PAYLOAD_QUALITY_STATE_MASK) >>
    ML3_PAYLOAD_QUALITY_STATE_SHIFT);
  valid_cycles = (uint8_t)(quality & ML3_PAYLOAD_QUALITY_CYCLE_MASK);
  if (((quality & UINT8_C(0x30)) != 0U) ||
      (quality_state > (uint8_t)ML3_QUALITY_STATE_INVALID) ||
      (valid_cycles > ML3_PAYLOAD_MAX_ABBA_CYCLES)) {
    return ML3_PAYLOAD_ERR_VALUE_OUT_OF_RANGE;
  }

  flags = ml3_payload_read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_FLAGS_OFFSET]);
  corrected = ml3_payload_read_u16_be(
    &frame[ML3_PAYLOAD_ROUTINE_CORRECTED_OFFSET]);
  if (!ml3_payload_quality_is_consistent(
        flags,
        (ml3_quality_state_t)quality_state,
        valid_cycles)) {
    return ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY;
  }
  if ((((flags & (uint16_t)ML3_QUALITY_FLAG_CAL_INVALID) != 0U) ||
       (quality_state == (uint8_t)ML3_QUALITY_STATE_INVALID)) &&
      (corrected != ML3_PAYLOAD_CORRECTED_SENTINEL)) {
    return ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY;
  }
  if (ml3_payload_has_core_adc_failure(flags) &&
      ((corrected != ML3_PAYLOAD_CORRECTED_SENTINEL) ||
       (ml3_payload_read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_MEAN_HI_OFFSET]) !=
        ML3_PAYLOAD_UNSIGNED_SENTINEL) ||
       (ml3_payload_read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_MEAN_LO_OFFSET]) !=
        ML3_PAYLOAD_UNSIGNED_SENTINEL) ||
       (ml3_payload_read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_VDDA_OFFSET]) !=
        ML3_PAYLOAD_UNSIGNED_SENTINEL) ||
       (ml3_payload_read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_V5_OFFSET]) !=
        ML3_PAYLOAD_UNSIGNED_SENTINEL) ||
       (ml3_payload_read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_NOISE_OFFSET]) !=
        ML3_PAYLOAD_UNSIGNED_SENTINEL) ||
       (ml3_payload_read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_DIE_TEMP_OFFSET]) !=
        ML3_PAYLOAD_TEMPERATURE_SENTINEL) ||
       (ml3_payload_read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_SOIL_TEMP_OFFSET]) !=
        ML3_PAYLOAD_TEMPERATURE_SENTINEL))) {
    return ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY;
  }
  if (((flags & (uint16_t)ML3_QUALITY_FLAG_THERM_FAULT) != 0U) &&
      (ml3_payload_read_u16_be(&frame[ML3_PAYLOAD_ROUTINE_SOIL_TEMP_OFFSET]) !=
       ML3_PAYLOAD_TEMPERATURE_SENTINEL)) {
    return ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY;
  }
  return ML3_PAYLOAD_OK;
}

bool ml3_payload_auto_diag_is_eligible(
  const ml3_payload_diag_rate_entry_t* entries,
  size_t entry_capacity,
  uint32_t invalidating_signature,
  uint32_t now_ms)
{
  bool free_entry = false;
  size_t index;

  if ((entries == NULL) || (entry_capacity == 0U) ||
      (invalidating_signature == 0U)) {
    return false;
  }
  for (index = 0U; index < entry_capacity; index += 1U) {
    if (entries[index].occupied) {
      if (entries[index].signature == invalidating_signature) {
        return (uint32_t)(now_ms - entries[index].last_queued_ms) >=
          ML3_PAYLOAD_AUTO_DIAG_INTERVAL_MS;
      }
    } else {
      free_entry = true;
    }
  }
  return free_entry;
}

ml3_payload_status_t ml3_payload_auto_diag_mark_queued(
  ml3_payload_diag_rate_entry_t* entries,
  size_t entry_capacity,
  uint32_t invalidating_signature,
  uint32_t now_ms)
{
  size_t free_index = entry_capacity;
  size_t target_index = entry_capacity;
  size_t index;

  if ((entries == NULL) || (entry_capacity == 0U) ||
      (invalidating_signature == 0U)) {
    return ML3_PAYLOAD_ERR_INVALID_ARGUMENT;
  }
  for (index = 0U; index < entry_capacity; index += 1U) {
    if (entries[index].occupied) {
      if (entries[index].signature == invalidating_signature) {
        target_index = index;
        break;
      }
    } else if (free_index == entry_capacity) {
      free_index = index;
    }
  }
  if (target_index == entry_capacity) {
    target_index = free_index;
  }
  if (target_index == entry_capacity) {
    return ML3_PAYLOAD_ERR_RATE_LIMIT_CAPACITY;
  }

  entries[target_index].signature = invalidating_signature;
  entries[target_index].last_queued_ms = now_ms;
  entries[target_index].occupied = true;
  return ML3_PAYLOAD_OK;
}
