#include "ml3_at_commands.h"

#include <limits.h>
#include <string.h>

static int ml3_at_equals(const uint8_t* input, size_t input_length,
  const char* expected, size_t expected_length) {
  return input_length == expected_length
    && memcmp(input, expected, expected_length) == 0;
}

static int ml3_at_starts_with(const uint8_t* input, size_t input_length,
  const char* prefix, size_t prefix_length) {
  return input_length >= prefix_length
    && memcmp(input, prefix, prefix_length) == 0;
}

static int ml3_at_is_edge_whitespace(uint8_t byte) {
  return byte == (uint8_t)' ' || byte == (uint8_t)'\t'
    || byte == (uint8_t)'\v' || byte == (uint8_t)'\f'
    || byte == (uint8_t)'\r' || byte == (uint8_t)'\n';
}

static ml3_at_status_t ml3_at_validate_framing(const uint8_t* input,
  size_t input_length) {
  size_t index;

  if (ml3_at_is_edge_whitespace(input[0])
      || ml3_at_is_edge_whitespace(input[input_length - 1U])) {
    return ML3_AT_STATUS_INVALID_SYNTAX;
  }
  for (index = 0U; index < input_length; ++index) {
    if (input[index] == (uint8_t)'\r' || input[index] == (uint8_t)'\n') {
      return ML3_AT_STATUS_INVALID_SYNTAX;
    }
  }
  return ML3_AT_STATUS_OK;
}

static ml3_at_status_t ml3_at_parse_uint32(const uint8_t* bytes, size_t length,
  uint32_t* value) {
  uint32_t parsed = 0U;
  size_t index;

  if (length == 0U) {
    return ML3_AT_STATUS_EMPTY_VALUE;
  }
  for (index = 0U; index < length; ++index) {
    uint32_t digit;

    if (bytes[index] < (uint8_t)'0' || bytes[index] > (uint8_t)'9') {
      return ML3_AT_STATUS_INVALID_VALUE;
    }
    digit = (uint32_t)(bytes[index] - (uint8_t)'0');
    if (parsed > (UINT32_MAX - digit) / UINT32_C(10)) {
      return ML3_AT_STATUS_NUMERIC_OVERFLOW;
    }
    parsed = (parsed * UINT32_C(10)) + digit;
  }
  *value = parsed;
  return ML3_AT_STATUS_OK;
}

static ml3_at_status_t ml3_at_parse_bounded_uint32(const uint8_t* bytes,
  size_t length, uint32_t minimum, uint32_t maximum, uint32_t* value) {
  uint32_t parsed;
  ml3_at_status_t status = ml3_at_parse_uint32(bytes, length, &parsed);

  if (status != ML3_AT_STATUS_OK) {
    return status;
  }
  if (parsed < minimum || parsed > maximum) {
    return ML3_AT_STATUS_VALUE_OUT_OF_RANGE;
  }
  *value = parsed;
  return ML3_AT_STATUS_OK;
}

static int ml3_at_is_hex(uint8_t byte) {
  return (byte >= (uint8_t)'0' && byte <= (uint8_t)'9')
    || (byte >= (uint8_t)'A' && byte <= (uint8_t)'F')
    || (byte >= (uint8_t)'a' && byte <= (uint8_t)'f');
}

static ml3_at_status_t ml3_at_parse_calibration_chunk(const uint8_t* input,
  size_t input_length, size_t prefix_length, ml3_at_command_t* parsed) {
  size_t cursor = prefix_length;
  size_t comma;
  uint32_t offset;
  uint32_t total_length;
  size_t hex_length;
  size_t index;
  ml3_at_status_t status;

  if (cursor == input_length) {
    return ML3_AT_STATUS_EMPTY_RECORD;
  }
  comma = cursor;
  while (comma < input_length && input[comma] != (uint8_t)',') {
    ++comma;
  }
  if (comma == input_length) {
    return ML3_AT_STATUS_INVALID_SYNTAX;
  }
  status = ml3_at_parse_uint32(&input[cursor], comma - cursor, &offset);
  if (status != ML3_AT_STATUS_OK) {
    return status;
  }
  cursor = comma + 1U;
  comma = cursor;
  while (comma < input_length && input[comma] != (uint8_t)',') {
    ++comma;
  }
  if (comma == input_length) {
    return ML3_AT_STATUS_INVALID_SYNTAX;
  }
  status = ml3_at_parse_bounded_uint32(&input[cursor], comma - cursor,
    ML3_AT_CALIBRATION_MIN_RECORD_LENGTH,
    ML3_AT_CALIBRATION_MAX_RECORD_LENGTH, &total_length);
  if (status != ML3_AT_STATUS_OK) {
    return status;
  }
  cursor = comma + 1U;
  hex_length = input_length - cursor;
  if (hex_length == 0U) {
    return ML3_AT_STATUS_EMPTY_RECORD;
  }
  if ((hex_length & 1U) != 0U
      || hex_length > ML3_AT_CALIBRATION_CHUNK_MAX_HEX) {
    return ML3_AT_STATUS_INVALID_VALUE;
  }
  if (offset > total_length
      || (uint32_t)(hex_length / 2U) > total_length - offset) {
    return ML3_AT_STATUS_VALUE_OUT_OF_RANGE;
  }
  for (index = cursor; index < input_length; ++index) {
    if (!ml3_at_is_hex(input[index])) {
      return ML3_AT_STATUS_INVALID_VALUE;
    }
  }
  parsed->operation = ML3_AT_OPERATION_SET_CALIBRATION;
  parsed->argument.calibration_chunk.offset = (uint16_t)offset;
  parsed->argument.calibration_chunk.total_length = (uint16_t)total_length;
  parsed->argument.calibration_chunk.hex = &input[cursor];
  parsed->argument.calibration_chunk.hex_length = hex_length;
  return ML3_AT_STATUS_OK;
}

static int ml3_at_contains_nul(const uint8_t* input, size_t input_length) {
  size_t index;

  for (index = 0U; index < input_length; ++index) {
    if (input[index] == 0U) {
      return 1;
    }
  }
  return 0;
}

ml3_at_status_t ml3_at_parse(const uint8_t* input, size_t input_length,
  ml3_at_command_t* output) {
  static const char warmup_prefix[] = "AT+ML3WARM=";
  static const char cycles_prefix[] = "AT+ML3CYCLES=";
  static const char raw_prefix[] = "AT+ML3RAW=";
  static const char calibration_prefix[] = "AT+ML3CAL=";
  ml3_at_command_t parsed = { 0 };
  ml3_at_status_t status;
  uint32_t numeric_value;

  if (input == NULL || input_length == 0U || output == NULL) {
    return ML3_AT_STATUS_INVALID_ARGUMENT;
  }
  status = ml3_at_validate_framing(input, input_length);
  if (status != ML3_AT_STATUS_OK) {
    return status;
  }

  if (ml3_at_starts_with(input, input_length, calibration_prefix,
      sizeof(calibration_prefix) - 1U)) {
    status = ml3_at_parse_calibration_chunk(input, input_length,
      sizeof(calibration_prefix) - 1U, &parsed);
    if (status != ML3_AT_STATUS_OK) {
      return status;
    }
    *output = parsed;
    return ML3_AT_STATUS_OK;
  }
  if (ml3_at_contains_nul(input, input_length)) {
    return ML3_AT_STATUS_INVALID_SYNTAX;
  }

  if (ml3_at_equals(input, input_length, "AT+ML3?", sizeof("AT+ML3?") - 1U)) {
    parsed.operation = ML3_AT_OPERATION_QUERY_SETTINGS;
  } else if (ml3_at_equals(input, input_length, "AT+ML3TEST",
      sizeof("AT+ML3TEST") - 1U)) {
    parsed.operation = ML3_AT_OPERATION_TEST;
  } else if (ml3_at_equals(input, input_length, "AT+ML3CAL?",
      sizeof("AT+ML3CAL?") - 1U)) {
    parsed.operation = ML3_AT_OPERATION_QUERY_CALIBRATION;
  } else if (ml3_at_equals(input, input_length, "AT+ML3CALCLR",
      sizeof("AT+ML3CALCLR") - 1U)) {
    parsed.operation = ML3_AT_OPERATION_CLEAR_CALIBRATION;
  } else if (ml3_at_equals(input, input_length, "AT+ML3VER?",
      sizeof("AT+ML3VER?") - 1U)) {
    parsed.operation = ML3_AT_OPERATION_QUERY_VERSION;
  } else if (ml3_at_starts_with(input, input_length, warmup_prefix,
      sizeof(warmup_prefix) - 1U)) {
    status = ml3_at_parse_bounded_uint32(
      &input[sizeof(warmup_prefix) - 1U],
      input_length - (sizeof(warmup_prefix) - 1U), 500U, 3000U,
      &numeric_value);
    if (status != ML3_AT_STATUS_OK) {
      return status;
    }
    parsed.operation = ML3_AT_OPERATION_SET_WARMUP_MS;
    parsed.argument.warmup_ms = (uint16_t)numeric_value;
  } else if (ml3_at_starts_with(input, input_length, cycles_prefix,
      sizeof(cycles_prefix) - 1U)) {
    status = ml3_at_parse_bounded_uint32(
      &input[sizeof(cycles_prefix) - 1U],
      input_length - (sizeof(cycles_prefix) - 1U), ML3_AT_CYCLES_MIN_VALUE,
      ML3_AT_CYCLES_MAX_VALUE, &numeric_value);
    if (status != ML3_AT_STATUS_OK) {
      return status;
    }
    parsed.operation = ML3_AT_OPERATION_SET_CYCLES;
    parsed.argument.cycles = (uint8_t)numeric_value;
  } else if (ml3_at_starts_with(input, input_length, raw_prefix,
      sizeof(raw_prefix) - 1U)) {
    status = ml3_at_parse_bounded_uint32(&input[sizeof(raw_prefix) - 1U],
      input_length - (sizeof(raw_prefix) - 1U), 0U, 1U, &numeric_value);
    if (status != ML3_AT_STATUS_OK) {
      return status;
    }
    parsed.operation = ML3_AT_OPERATION_SET_RAW;
    parsed.argument.raw_enabled = (uint8_t)numeric_value;
  } else {
    return ML3_AT_STATUS_UNKNOWN_COMMAND;
  }

  *output = parsed;
  return ML3_AT_STATUS_OK;
}
