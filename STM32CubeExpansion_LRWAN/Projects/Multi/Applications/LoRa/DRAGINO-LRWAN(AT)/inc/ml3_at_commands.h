#ifndef ML3_AT_COMMANDS_H
#define ML3_AT_COMMANDS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ML3_AT_STATUS_OK = 0,
  ML3_AT_STATUS_INVALID_ARGUMENT,
  ML3_AT_STATUS_UNKNOWN_COMMAND,
  ML3_AT_STATUS_INVALID_SYNTAX,
  ML3_AT_STATUS_EMPTY_VALUE,
  ML3_AT_STATUS_INVALID_VALUE,
  ML3_AT_STATUS_VALUE_OUT_OF_RANGE,
  ML3_AT_STATUS_NUMERIC_OVERFLOW,
  ML3_AT_STATUS_EMPTY_RECORD,
  ML3_AT_STATUS_RECORD_CONTAINS_NUL
} ml3_at_status_t;

#define ML3_AT_CALIBRATION_MIN_RECORD_LENGTH 52U
#define ML3_AT_CALIBRATION_MAX_RECORD_LENGTH 2092U
#define ML3_AT_CALIBRATION_CHUNK_MAX_BYTES 48U
#define ML3_AT_CALIBRATION_CHUNK_MAX_HEX \
  (ML3_AT_CALIBRATION_CHUNK_MAX_BYTES * 2U)

/*
 * This module is compiled standalone (see tests/host/run_ml3_host_tests.sh)
 * and cannot include ml3_measurement.h, so these must be kept in lockstep
 * with ML3_MEASUREMENT_MIN_ABBA_CYCLES / ML3_MEASUREMENT_MAX_ABBA_CYCLES
 * there. A configured cycle count below the measurement module's fixed
 * minimum valid-cycle count can never produce a valid reading.
 */
#define ML3_AT_CYCLES_MIN_VALUE 3U
#define ML3_AT_CYCLES_MAX_VALUE 8U

typedef enum {
  ML3_AT_OPERATION_QUERY_SETTINGS = 0,
  ML3_AT_OPERATION_SET_WARMUP_MS,
  ML3_AT_OPERATION_SET_CYCLES,
  ML3_AT_OPERATION_SET_RAW,
  ML3_AT_OPERATION_TEST,
  ML3_AT_OPERATION_QUERY_CALIBRATION,
  ML3_AT_OPERATION_SET_CALIBRATION,
  ML3_AT_OPERATION_CLEAR_CALIBRATION,
  ML3_AT_OPERATION_QUERY_VERSION
} ml3_at_operation_t;

typedef struct {
  const uint8_t* bytes;
  size_t length;
} ml3_at_borrowed_span_t;

typedef struct {
  uint16_t offset;
  uint16_t total_length;
  const uint8_t* hex;
  size_t hex_length;
} ml3_at_calibration_chunk_t;

typedef union {
  uint16_t warmup_ms;
  uint8_t cycles;
  uint8_t raw_enabled;
  ml3_at_borrowed_span_t calibration_record;
  ml3_at_calibration_chunk_t calibration_chunk;
} ml3_at_argument_t;

typedef struct {
  ml3_at_operation_t operation;
  ml3_at_argument_t argument;
} ml3_at_command_t;

/*
 * This parser has no side effects. A successful SET_CALIBRATION result borrows
 * its ASCII hexadecimal chunk from input; the span remains valid only while
 * input does. The caller owns decoding, storage, and verified clear operations.
 * Calibration chunks use AT+ML3CAL=<offset>,<total>,<hex>, with at most 48
 * decoded bytes per command so the existing 128-byte AT line remains bounded.
 */
ml3_at_status_t ml3_at_parse(const uint8_t* input, size_t input_length,
  ml3_at_command_t* output);

#ifdef __cplusplus
}
#endif

#endif /* ML3_AT_COMMANDS_H */
