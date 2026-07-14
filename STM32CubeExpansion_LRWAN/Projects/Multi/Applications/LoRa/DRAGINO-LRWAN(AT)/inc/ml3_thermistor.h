#ifndef ML3_THERMISTOR_H
#define ML3_THERMISTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ML3_THERMISTOR_FULL_SCALE_CODE UINT16_C(65520)

typedef enum {
  ML3_THERMISTOR_OK = 0,
  ML3_THERMISTOR_INVALID_ARGUMENT,
  ML3_THERMISTOR_CODE_OUT_OF_RANGE,
  ML3_THERMISTOR_RESISTANCE_OUT_OF_RANGE,
  ML3_THERMISTOR_INVALID_TABLE,
  ML3_THERMISTOR_CONFIG_NOT_READY,
  ML3_THERMISTOR_RAIL_FAULT,
  ML3_THERMISTOR_OVERFLOW
} ml3_thermistor_status_t;

typedef struct {
  uint32_t resistance_ohm;
  int32_t temperature_centic;
} ml3_thermistor_point_t;

typedef struct {
  uint32_t reference_resistance_ohm;
  uint16_t rail_guard_code;
  uint32_t settle_time_ms;
  const ml3_thermistor_point_t* table;
  size_t table_count;
  bool reference_resistance_ready;
  bool rail_guard_ready;
  bool settle_time_ready;
  bool table_ready;
} ml3_thermistor_config_t;

typedef struct {
  uint32_t resistance_ohm;
  int32_t temperature_centic;
} ml3_thermistor_result_t;

ml3_thermistor_status_t ml3_thermistor_code_to_resistance(
  uint16_t code,
  uint32_t reference_resistance_ohm,
  uint32_t* resistance_ohm);

/* Resistance knots must be strictly monotonic; either table order is valid. */
ml3_thermistor_status_t ml3_thermistor_interpolate(
  const ml3_thermistor_point_t* table,
  size_t table_count,
  uint32_t resistance_ohm,
  int32_t* temperature_centic);

bool ml3_thermistor_config_is_ready(const ml3_thermistor_config_t* config);

ml3_thermistor_status_t ml3_thermistor_convert(
  const ml3_thermistor_config_t* config,
  uint16_t code,
  ml3_thermistor_result_t* result);

#ifdef __cplusplus
}
#endif

#endif /* ML3_THERMISTOR_H */
