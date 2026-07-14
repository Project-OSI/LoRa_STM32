#include "ml3_thermistor.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

static ml3_thermistor_status_t ml3_thermistor_table_direction(
  const ml3_thermistor_point_t* table,
  size_t table_count,
  int* direction) {
  size_t index;

  if ((table == NULL) || (direction == NULL)) {
    return ML3_THERMISTOR_INVALID_ARGUMENT;
  }
  if ((table_count < 2U) || (table[0U].resistance_ohm == 0U)) {
    return ML3_THERMISTOR_INVALID_TABLE;
  }

  if (table[1U].resistance_ohm > table[0U].resistance_ohm) {
    *direction = 1;
  } else if (table[1U].resistance_ohm < table[0U].resistance_ohm) {
    *direction = -1;
  } else {
    return ML3_THERMISTOR_INVALID_TABLE;
  }

  for (index = 1U; index < table_count; ++index) {
    if (table[index].resistance_ohm == 0U) {
      return ML3_THERMISTOR_INVALID_TABLE;
    }
    if ((*direction > 0)
      && (table[index].resistance_ohm <= table[index - 1U].resistance_ohm)) {
      return ML3_THERMISTOR_INVALID_TABLE;
    }
    if ((*direction < 0)
      && (table[index].resistance_ohm >= table[index - 1U].resistance_ohm)) {
      return ML3_THERMISTOR_INVALID_TABLE;
    }
  }

  return ML3_THERMISTOR_OK;
}

static ml3_thermistor_status_t ml3_thermistor_validate_config(
  const ml3_thermistor_config_t* config) {
  int direction;

  if (config == NULL) {
    return ML3_THERMISTOR_INVALID_ARGUMENT;
  }
  if ((!config->reference_resistance_ready)
    || (!config->rail_guard_ready)
    || (!config->settle_time_ready)
    || (!config->table_ready)
    || (config->reference_resistance_ohm == 0U)
    || (config->rail_guard_code == 0U)
    || (config->rail_guard_code >= (ML3_THERMISTOR_FULL_SCALE_CODE / 2U))
    || (config->settle_time_ms == 0U)
    || (config->table == NULL)) {
    return ML3_THERMISTOR_CONFIG_NOT_READY;
  }

  return ml3_thermistor_table_direction(
    config->table,
    config->table_count,
    &direction);
}

static ml3_thermistor_status_t ml3_thermistor_interpolate_segment(
  const ml3_thermistor_point_t* first,
  const ml3_thermistor_point_t* second,
  uint32_t resistance_ohm,
  int32_t* temperature_centic) {
  const ml3_thermistor_point_t* lower;
  const ml3_thermistor_point_t* upper;
  uint64_t resistance_offset;
  uint64_t resistance_span;
  int64_t temperature_delta;
  uint64_t temperature_magnitude;
  uint64_t interpolated_magnitude;
  int64_t interpolated_delta;
  int64_t interpolated_temperature;

  if (first->resistance_ohm < second->resistance_ohm) {
    lower = first;
    upper = second;
  } else {
    lower = second;
    upper = first;
  }

  resistance_offset = (uint64_t)(resistance_ohm - lower->resistance_ohm);
  resistance_span = (uint64_t)(upper->resistance_ohm - lower->resistance_ohm);

  temperature_delta = (int64_t)upper->temperature_centic
    - (int64_t)lower->temperature_centic;
  if (temperature_delta < 0) {
    temperature_magnitude = (uint64_t)(-temperature_delta);
  } else {
    temperature_magnitude = (uint64_t)temperature_delta;
  }
  interpolated_magnitude = (temperature_magnitude * resistance_offset)
    / resistance_span;
  if (temperature_delta < 0) {
    interpolated_delta = -(int64_t)interpolated_magnitude;
  } else {
    interpolated_delta = (int64_t)interpolated_magnitude;
  }
  interpolated_temperature = (int64_t)lower->temperature_centic
    + interpolated_delta;
  if ((interpolated_temperature < (int64_t)INT32_MIN)
    || (interpolated_temperature > (int64_t)INT32_MAX)) {
    return ML3_THERMISTOR_OVERFLOW;
  }

  *temperature_centic = (int32_t)interpolated_temperature;
  return ML3_THERMISTOR_OK;
}

ml3_thermistor_status_t ml3_thermistor_code_to_resistance(
  uint16_t code,
  uint32_t reference_resistance_ohm,
  uint32_t* resistance_ohm) {
  uint32_t denominator;
  uint64_t resistance;

  if ((resistance_ohm == NULL) || (reference_resistance_ohm == 0U)) {
    return ML3_THERMISTOR_INVALID_ARGUMENT;
  }
  if (code >= ML3_THERMISTOR_FULL_SCALE_CODE) {
    return ML3_THERMISTOR_CODE_OUT_OF_RANGE;
  }

  denominator = (uint32_t)ML3_THERMISTOR_FULL_SCALE_CODE - (uint32_t)code;
  resistance = ((uint64_t)reference_resistance_ohm * (uint64_t)code)
    / (uint64_t)denominator;
  if (resistance > (uint64_t)UINT32_MAX) {
    return ML3_THERMISTOR_OVERFLOW;
  }

  *resistance_ohm = (uint32_t)resistance;
  return ML3_THERMISTOR_OK;
}

ml3_thermistor_status_t ml3_thermistor_interpolate(
  const ml3_thermistor_point_t* table,
  size_t table_count,
  uint32_t resistance_ohm,
  int32_t* temperature_centic) {
  ml3_thermistor_status_t status;
  int direction;
  size_t index;

  if (temperature_centic == NULL) {
    return ML3_THERMISTOR_INVALID_ARGUMENT;
  }
  status = ml3_thermistor_table_direction(table, table_count, &direction);
  if (status != ML3_THERMISTOR_OK) {
    return status;
  }

  if (direction > 0) {
    if ((resistance_ohm < table[0U].resistance_ohm)
      || (resistance_ohm > table[table_count - 1U].resistance_ohm)) {
      return ML3_THERMISTOR_RESISTANCE_OUT_OF_RANGE;
    }
  } else if ((resistance_ohm > table[0U].resistance_ohm)
    || (resistance_ohm < table[table_count - 1U].resistance_ohm)) {
    return ML3_THERMISTOR_RESISTANCE_OUT_OF_RANGE;
  }

  for (index = 0U; index < (table_count - 1U); ++index) {
    if (((direction > 0)
        && (resistance_ohm >= table[index].resistance_ohm)
        && (resistance_ohm <= table[index + 1U].resistance_ohm))
      || ((direction < 0)
        && (resistance_ohm <= table[index].resistance_ohm)
        && (resistance_ohm >= table[index + 1U].resistance_ohm))) {
      return ml3_thermistor_interpolate_segment(
        &table[index],
        &table[index + 1U],
        resistance_ohm,
        temperature_centic);
    }
  }

  return ML3_THERMISTOR_INVALID_TABLE;
}

bool ml3_thermistor_config_is_ready(const ml3_thermistor_config_t* config) {
  return ml3_thermistor_validate_config(config) == ML3_THERMISTOR_OK;
}

ml3_thermistor_status_t ml3_thermistor_convert(
  const ml3_thermistor_config_t* config,
  uint16_t code,
  ml3_thermistor_result_t* result) {
  ml3_thermistor_status_t status;
  uint32_t resistance_ohm;
  int32_t temperature_centic;
  uint32_t upper_rail_threshold;

  if ((config == NULL) || (result == NULL)) {
    return ML3_THERMISTOR_INVALID_ARGUMENT;
  }
  if (code > ML3_THERMISTOR_FULL_SCALE_CODE) {
    return ML3_THERMISTOR_CODE_OUT_OF_RANGE;
  }

  status = ml3_thermistor_validate_config(config);
  if (status != ML3_THERMISTOR_OK) {
    return status;
  }

  upper_rail_threshold = (uint32_t)ML3_THERMISTOR_FULL_SCALE_CODE
    - (uint32_t)config->rail_guard_code;
  if ((code <= config->rail_guard_code)
    || ((uint32_t)code >= upper_rail_threshold)) {
    return ML3_THERMISTOR_RAIL_FAULT;
  }

  status = ml3_thermistor_code_to_resistance(
    code,
    config->reference_resistance_ohm,
    &resistance_ohm);
  if (status != ML3_THERMISTOR_OK) {
    return status;
  }
  status = ml3_thermistor_interpolate(
    config->table,
    config->table_count,
    resistance_ohm,
    &temperature_centic);
  if (status != ML3_THERMISTOR_OK) {
    return status;
  }

  result->resistance_ohm = resistance_ohm;
  result->temperature_centic = temperature_centic;
  return ML3_THERMISTOR_OK;
}
