#include "ml3_at_commands.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define EXPECT_TRUE(value, label) \
  do { \
    if (!(value)) { \
      (void)fprintf(stderr, "FAIL: %s\n", (label)); \
      ++failures; \
    } \
  } while (0)

#define EXPECT_STATUS(expected, actual, label) \
  do { \
    ml3_at_status_t expected_ = (expected); \
    ml3_at_status_t actual_ = (actual); \
    if (expected_ != actual_) { \
      (void)fprintf(stderr, "FAIL: %s: expected=%d actual=%d\n", \
        (label), (int)expected_, (int)actual_); \
      ++failures; \
    } \
  } while (0)

typedef struct {
  const char* text;
  ml3_at_operation_t operation;
} exact_command_case_t;

static ml3_at_status_t parse_text(const char* text, ml3_at_command_t* output) {
  return ml3_at_parse((const uint8_t*)text, strlen(text), output);
}

static void expect_error_preserves_output(const uint8_t* input, size_t length,
  ml3_at_status_t expected, const char* label) {
  ml3_at_command_t output;
  ml3_at_command_t before;

  (void)memset(&output, 0xA5, sizeof(output));
  (void)memcpy(&before, &output, sizeof(before));
  EXPECT_STATUS(expected, ml3_at_parse(input, length, &output), label);
  EXPECT_TRUE(memcmp(&output, &before, sizeof(output)) == 0,
    "parse error preserves every output byte");
}

static void expect_text_error_preserves_output(const char* text,
  ml3_at_status_t expected, const char* label) {
  expect_error_preserves_output((const uint8_t*)text, strlen(text), expected,
    label);
}

static void test_exact_commands(void) {
  static const exact_command_case_t cases[] = {
    { "AT+ML3?", ML3_AT_OPERATION_QUERY_SETTINGS },
    { "AT+ML3TEST", ML3_AT_OPERATION_TEST },
    { "AT+ML3CAL?", ML3_AT_OPERATION_QUERY_CALIBRATION },
    { "AT+ML3CALCLR", ML3_AT_OPERATION_CLEAR_CALIBRATION },
    { "AT+ML3VER?", ML3_AT_OPERATION_QUERY_VERSION }
  };
  size_t index;

  for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
    ml3_at_command_t output;

    (void)memset(&output, 0xA5, sizeof(output));
    EXPECT_STATUS(ML3_AT_STATUS_OK, parse_text(cases[index].text, &output),
      cases[index].text);
    EXPECT_TRUE(output.operation == cases[index].operation,
      "exact command operation");
  }
}

static void test_warmup_values(void) {
  ml3_at_command_t output;

  EXPECT_STATUS(ML3_AT_STATUS_OK, parse_text("AT+ML3WARM=500", &output),
    "warmup lower endpoint");
  EXPECT_TRUE(output.operation == ML3_AT_OPERATION_SET_WARMUP_MS,
    "warmup operation");
  EXPECT_TRUE(output.argument.warmup_ms == 500U, "warmup lower value");

  EXPECT_STATUS(ML3_AT_STATUS_OK, parse_text("AT+ML3WARM=3000", &output),
    "warmup upper endpoint");
  EXPECT_TRUE(output.argument.warmup_ms == 3000U, "warmup upper value");

  EXPECT_STATUS(ML3_AT_STATUS_OK, parse_text("AT+ML3WARM=000500", &output),
    "warmup leading zeroes");
  EXPECT_TRUE(output.argument.warmup_ms == 500U, "warmup leading zero value");

  expect_text_error_preserves_output("AT+ML3WARM=499",
    ML3_AT_STATUS_VALUE_OUT_OF_RANGE, "warmup below range");
  expect_text_error_preserves_output("AT+ML3WARM=3001",
    ML3_AT_STATUS_VALUE_OUT_OF_RANGE, "warmup above range");
  expect_text_error_preserves_output("AT+ML3WARM=",
    ML3_AT_STATUS_EMPTY_VALUE, "warmup empty");
  expect_text_error_preserves_output("AT+ML3WARM=-500",
    ML3_AT_STATUS_INVALID_VALUE, "warmup sign rejected");
  expect_text_error_preserves_output("AT+ML3WARM=+500",
    ML3_AT_STATUS_INVALID_VALUE, "warmup plus rejected");
  expect_text_error_preserves_output("AT+ML3WARM=x500",
    ML3_AT_STATUS_INVALID_VALUE, "warmup leading junk rejected");
  expect_text_error_preserves_output("AT+ML3WARM=500x",
    ML3_AT_STATUS_INVALID_VALUE, "warmup suffix rejected");
  expect_text_error_preserves_output("AT+ML3WARM=4294967296",
    ML3_AT_STATUS_NUMERIC_OVERFLOW, "warmup uint32 overflow");
  expect_text_error_preserves_output("AT+ML3WARM=4294967295",
    ML3_AT_STATUS_VALUE_OUT_OF_RANGE, "warmup uint32 maximum parses");
  expect_text_error_preserves_output("AT+ML3WARM=999999999999999999999999999",
    ML3_AT_STATUS_NUMERIC_OVERFLOW, "warmup huge overflow");
}

static void test_cycles_values(void) {
  ml3_at_command_t output;

  EXPECT_STATUS(ML3_AT_STATUS_OK, parse_text("AT+ML3CYCLES=3", &output),
    "cycles lower endpoint");
  EXPECT_TRUE(output.operation == ML3_AT_OPERATION_SET_CYCLES,
    "cycles operation");
  EXPECT_TRUE(output.argument.cycles == 3U, "cycles lower value");

  EXPECT_STATUS(ML3_AT_STATUS_OK, parse_text("AT+ML3CYCLES=8", &output),
    "cycles upper endpoint");
  EXPECT_TRUE(output.argument.cycles == 8U, "cycles upper value");

  expect_text_error_preserves_output("AT+ML3CYCLES=1",
    ML3_AT_STATUS_VALUE_OUT_OF_RANGE, "cycles below range");
  expect_text_error_preserves_output("AT+ML3CYCLES=2",
    ML3_AT_STATUS_VALUE_OUT_OF_RANGE,
    "cycles at former floor now rejected (min raised to 3)");
  expect_text_error_preserves_output("AT+ML3CYCLES=9",
    ML3_AT_STATUS_VALUE_OUT_OF_RANGE, "cycles above range");
  expect_text_error_preserves_output("AT+ML3CYCLES=",
    ML3_AT_STATUS_EMPTY_VALUE, "cycles empty");
  expect_text_error_preserves_output("AT+ML3CYCLES=2x",
    ML3_AT_STATUS_INVALID_VALUE, "cycles suffix rejected");
  expect_text_error_preserves_output("AT+ML3CYCLES=4294967296",
    ML3_AT_STATUS_NUMERIC_OVERFLOW, "cycles overflow");
}

static void test_raw_values(void) {
  ml3_at_command_t output;

  EXPECT_STATUS(ML3_AT_STATUS_OK, parse_text("AT+ML3RAW=0", &output),
    "raw disabled");
  EXPECT_TRUE(output.operation == ML3_AT_OPERATION_SET_RAW,
    "raw operation");
  EXPECT_TRUE(output.argument.raw_enabled == 0U, "raw disabled value");

  EXPECT_STATUS(ML3_AT_STATUS_OK, parse_text("AT+ML3RAW=1", &output),
    "raw enabled");
  EXPECT_TRUE(output.argument.raw_enabled == 1U, "raw enabled value");

  expect_text_error_preserves_output("AT+ML3RAW=2",
    ML3_AT_STATUS_VALUE_OUT_OF_RANGE, "raw outside range");
  expect_text_error_preserves_output("AT+ML3RAW=",
    ML3_AT_STATUS_EMPTY_VALUE, "raw empty");
  expect_text_error_preserves_output("AT+ML3RAW=01x",
    ML3_AT_STATUS_INVALID_VALUE, "raw suffix rejected");
  expect_text_error_preserves_output("AT+ML3RAW=4294967296",
    ML3_AT_STATUS_NUMERIC_OVERFLOW, "raw overflow");
}

static void test_calibration_record_span(void) {
  uint8_t command[] = "AT+ML3CAL=0,52,01FfA0B1";
  uint8_t invalid_odd_hex[] = "AT+ML3CAL=0,52,01F";
  uint8_t invalid_offset[] = "AT+ML3CAL=1,52,01FfA0B1";
  ml3_at_command_t output;

  EXPECT_STATUS(ML3_AT_STATUS_OK,
    ml3_at_parse(command, sizeof(command) - 1U, &output), "chunk accepted");
  EXPECT_TRUE(output.operation == ML3_AT_OPERATION_SET_CALIBRATION,
    "calibration write operation");
  EXPECT_TRUE(output.argument.calibration_chunk.offset == 0U,
    "calibration chunk offset");
  EXPECT_TRUE(output.argument.calibration_chunk.total_length == 52U,
    "calibration chunk total length");
  EXPECT_TRUE(output.argument.calibration_chunk.hex_length == 8U,
    "calibration chunk hex length");
  EXPECT_TRUE(output.argument.calibration_chunk.hex[2] == 'F',
    "calibration chunk borrows hex span");

  expect_text_error_preserves_output("AT+ML3CAL=", ML3_AT_STATUS_EMPTY_RECORD,
    "empty calibration record");
  expect_text_error_preserves_output("AT+ML3CAL=0,52,01F",
    ML3_AT_STATUS_INVALID_VALUE, "odd calibration hex rejected");
  expect_text_error_preserves_output("AT+ML3CAL=0,52,01FG",
    ML3_AT_STATUS_INVALID_VALUE, "non-hex calibration digit rejected");
  EXPECT_STATUS(ML3_AT_STATUS_OK,
    ml3_at_parse(invalid_offset, sizeof(invalid_offset) - 1U, &output),
    "nonzero continuation chunk accepted by parser");
  expect_error_preserves_output(invalid_odd_hex, sizeof(invalid_odd_hex) - 1U,
    ML3_AT_STATUS_INVALID_VALUE, "odd calibration span rejected");
}

static void test_exact_case_and_framing(void) {
  static const char* unknown_commands[] = {
    "at+ML3?",
    "AT+ml3?",
    "AT+ML3",
    "AT+ML3??",
    "AT+ML3?EXTRA",
    "AT+ML3TESTING",
    "AT+ML3WARMER=500",
    "AT+ML3CAL?=x",
    "AT+ML3CALCLRX",
    "AT+ML3VER"
  };
  static const char* malformed_frames[] = {
    " AT+ML3?",
    "\tAT+ML3?",
    "AT+ML3? ",
    "AT+ML3?\t",
    "AT+ML3?\r",
    "AT+ML3?\n",
    "AT+ML3\r?",
    "AT+ML3\n?",
    "AT+ML3WARM=500\r\n",
    "AT+ML3CAL=A\nB",
    "AT+ML3CAL=A "
  };
  uint8_t fixed_command_with_nul[] = {
    'A', 'T', '+', 'M', 'L', '3', '?', 0U, 'X'
  };
  size_t index;

  for (index = 0U;
       index < sizeof(unknown_commands) / sizeof(unknown_commands[0]);
       ++index) {
    expect_text_error_preserves_output(unknown_commands[index],
      ML3_AT_STATUS_UNKNOWN_COMMAND, unknown_commands[index]);
  }
  for (index = 0U;
       index < sizeof(malformed_frames) / sizeof(malformed_frames[0]);
       ++index) {
    expect_text_error_preserves_output(malformed_frames[index],
      ML3_AT_STATUS_INVALID_SYNTAX, malformed_frames[index]);
  }
  expect_error_preserves_output(fixed_command_with_nul,
    sizeof(fixed_command_with_nul), ML3_AT_STATUS_INVALID_SYNTAX,
    "NUL outside calibration record rejected");
}

static void test_invalid_arguments(void) {
  ml3_at_command_t output;
  ml3_at_command_t before;

  (void)memset(&output, 0x5A, sizeof(output));
  (void)memcpy(&before, &output, sizeof(before));
  EXPECT_STATUS(ML3_AT_STATUS_INVALID_ARGUMENT,
    ml3_at_parse(NULL, 1U, &output), "null input with length");
  EXPECT_TRUE(memcmp(&output, &before, sizeof(output)) == 0,
    "null input preserves output");
  EXPECT_STATUS(ML3_AT_STATUS_INVALID_ARGUMENT,
    ml3_at_parse((const uint8_t*)"", 0U, &output), "empty input");
  EXPECT_TRUE(memcmp(&output, &before, sizeof(output)) == 0,
    "empty input preserves output");
  EXPECT_STATUS(ML3_AT_STATUS_INVALID_ARGUMENT,
    ml3_at_parse((const uint8_t*)"AT+ML3?", 7U, NULL), "null output");
}

int main(void) {
  test_exact_commands();
  test_warmup_values();
  test_cycles_values();
  test_raw_values();
  test_calibration_record_span();
  test_exact_case_and_framing();
  test_invalid_arguments();

  if (failures != 0) {
    (void)fprintf(stderr, "ml3 AT command tests: %d failed\n", failures);
    return 1;
  }
  (void)puts("ml3 AT command parser: OK");
  return 0;
}
