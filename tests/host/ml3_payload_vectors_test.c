#include "ml3_payload.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  const char* id;
  ml3_payload_routine_t input;
  const uint8_t* expected;
  size_t expected_length;
  uint16_t expected_flags;
  uint16_t expected_sequence;
  ml3_quality_state_t expected_quality_state;
  uint8_t expected_valid_cycles;
} ml3_vector_routine_case_t;

typedef struct {
  uint8_t part_index;
  uint8_t part_count;
  const uint8_t* expected;
  size_t expected_length;
} ml3_vector_expected_part_t;

typedef struct {
  const char* id;
  ml3_payload_diagnostic_t input;
  const ml3_vector_expected_part_t* parts;
  size_t part_count;
} ml3_vector_diagnostic_case_t;

typedef struct {
  const char* id;
  const uint8_t* frame;
  size_t frame_length;
  ml3_payload_status_t expected_status;
} ml3_vector_malformed_case_t;

#include "ml3_payload_vectors.generated.inc"

static int failures;

static uint16_t read_u16_be(const uint8_t* input)
{
  return (uint16_t)(((uint16_t)input[0] << 8U) | input[1]);
}

static void check_case(
  bool condition,
  const char* vector_id,
  const char* assertion)
{
  if (!condition) {
    (void)fprintf(stderr, "FAIL %s: %s\n", vector_id, assertion);
    failures += 1;
  }
}

static void test_routine_vectors(void)
{
  uint8_t output[ML3_PAYLOAD_DIAGNOSTIC_CYCLE_PART_MAX_LENGTH];
  size_t index;

  for (index = 0U;
       index < (sizeof(ml3_vector_routine_cases) /
                sizeof(ml3_vector_routine_cases[0]));
       index += 1U) {
    const ml3_vector_routine_case_t* vector =
      &ml3_vector_routine_cases[index];
    size_t output_length = 0U;
    ml3_payload_status_t status = ml3_payload_build_routine(
      &vector->input,
      output,
      sizeof(output),
      sizeof(output),
      &output_length);

    check_case(status == ML3_PAYLOAD_OK, vector->id, "builder status");
    if (status != ML3_PAYLOAD_OK) {
      continue;
    }
    check_case(
      output_length == vector->expected_length,
      vector->id,
      "routine length");
    check_case(
      memcmp(output, vector->expected, vector->expected_length) == 0,
      vector->id,
      "routine bytes from production builder");
    check_case(
      read_u16_be(&output[ML3_PAYLOAD_ROUTINE_FLAGS_OFFSET]) ==
        vector->expected_flags,
      vector->id,
      "status-flag metadata");
    check_case(
      read_u16_be(&output[ML3_PAYLOAD_ROUTINE_SEQUENCE_OFFSET]) ==
        vector->expected_sequence,
      vector->id,
      "sequence metadata");
    check_case(
      (output[ML3_PAYLOAD_ROUTINE_QUALITY_OFFSET] >>
       ML3_PAYLOAD_QUALITY_STATE_SHIFT) ==
        (uint8_t)vector->expected_quality_state,
      vector->id,
      "quality-state metadata");
    check_case(
      (output[ML3_PAYLOAD_ROUTINE_QUALITY_OFFSET] &
       ML3_PAYLOAD_QUALITY_CYCLE_MASK) == vector->expected_valid_cycles,
      vector->id,
      "valid-cycle metadata");
    check_case(
      ml3_payload_validate_routine_frame(output, output_length) ==
        ML3_PAYLOAD_OK,
      vector->id,
      "semantic frame validation");
  }
}

static void test_diagnostic_vectors(void)
{
  uint8_t output[ML3_PAYLOAD_DIAGNOSTIC_CYCLE_PART_MAX_LENGTH];
  size_t vector_index;

  for (vector_index = 0U;
       vector_index < (sizeof(ml3_vector_diagnostic_cases) /
                       sizeof(ml3_vector_diagnostic_cases[0]));
       vector_index += 1U) {
    const ml3_vector_diagnostic_case_t* vector =
      &ml3_vector_diagnostic_cases[vector_index];
    uint8_t production_part_count = 0U;
    size_t part_index;

    check_case(
      ml3_payload_diagnostic_part_count(
        vector->input.cycle_count,
        &production_part_count) == ML3_PAYLOAD_OK,
      vector->id,
      "part-count status");
    check_case(
      (size_t)production_part_count == vector->part_count,
      vector->id,
      "part-count metadata");
    for (part_index = 0U; part_index < vector->part_count; part_index += 1U) {
      const ml3_vector_expected_part_t* expected = &vector->parts[part_index];
      size_t output_length = 0U;
      ml3_payload_status_t status = ml3_payload_build_diagnostic_part(
        &vector->input,
        (uint8_t)part_index,
        output,
        sizeof(output),
        sizeof(output),
        &output_length);

      check_case(status == ML3_PAYLOAD_OK, vector->id, "diagnostic builder status");
      if (status != ML3_PAYLOAD_OK) {
        continue;
      }
      check_case(
        output_length == expected->expected_length,
        vector->id,
        "diagnostic part length");
      check_case(
        memcmp(output, expected->expected, expected->expected_length) == 0,
        vector->id,
        "diagnostic bytes from production builder");
      check_case(
        (output[ML3_PAYLOAD_DIAGNOSTIC_PART_OFFSET] >>
         ML3_PAYLOAD_DIAGNOSTIC_PART_INDEX_SHIFT) == expected->part_index,
        vector->id,
        "zero-based diagnostic part index");
      check_case(
        (output[ML3_PAYLOAD_DIAGNOSTIC_PART_OFFSET] &
         ML3_PAYLOAD_DIAGNOSTIC_PART_COUNT_MASK) == expected->part_count,
        vector->id,
        "diagnostic part count nibble");
    }
  }
}

static void test_malformed_vectors(void)
{
  size_t index;

  for (index = 0U;
       index < (sizeof(ml3_vector_malformed_cases) /
                sizeof(ml3_vector_malformed_cases[0]));
       index += 1U) {
    const ml3_vector_malformed_case_t* vector =
      &ml3_vector_malformed_cases[index];

    check_case(
      ml3_payload_validate_routine_frame(
        vector->frame,
        vector->frame_length) == vector->expected_status,
      vector->id,
      "malformed semantic status");
  }
}

int main(void)
{
  test_routine_vectors();
  test_diagnostic_vectors();
  test_malformed_vectors();

  if (failures != 0) {
    (void)fprintf(stderr, "ml3 payload vectors: %d failure(s)\n", failures);
    return 1;
  }
  (void)printf("ml3 payload vectors: OK\n");
  return 0;
}
