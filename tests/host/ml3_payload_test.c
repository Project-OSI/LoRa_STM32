#include "ml3_payload.h"
#include "ml3_quality.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef ML3_PAYLOAD_FIXED_INVALIDATING_MASK
#error "ml3_payload must use ML3_QUALITY_INVALIDATING_MASK"
#endif
#ifdef ML3_PAYLOAD_FLAG_CAL_INVALID
#error "ml3_payload must use ML3_QUALITY_FLAG_CAL_INVALID"
#endif

static int failures;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                          \
      (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      failures += 1;                                                            \
    }                                                                           \
  } while (0)

static uint16_t read_u16_be(const uint8_t* input)
{
  return (uint16_t)(((uint16_t)input[0] << 8U) | input[1]);
}

static void write_u16_be(uint8_t* output, uint16_t value)
{
  output[0] = (uint8_t)(value >> 8U);
  output[1] = (uint8_t)value;
}

static ml3_payload_routine_t routine_input(void)
{
  ml3_payload_routine_t input;

  (void)memset(&input, 0, sizeof(input));
  input.corrected_diff_available = true;
  input.mean_hi_available = true;
  input.mean_lo_available = true;
  input.vdda_available = true;
  input.v5_available = true;
  input.noise_available = true;
  input.die_temperature_available = true;
  input.soil_temperature_available = true;
  input.quality_state = ML3_QUALITY_STATE_VALID;
  input.valid_cycle_count = 4U;
  return input;
}

static void test_public_wire_contract_constants(void)
{
  CHECK(ML3_PAYLOAD_PROTOCOL_VERSION == 1U);
  CHECK(ML3_PAYLOAD_TYPE_ROUTINE == 0U);
  CHECK(ML3_PAYLOAD_TYPE_DIAGNOSTIC == 1U);
  CHECK(ML3_PAYLOAD_ROUTINE_LENGTH == 25U);
  CHECK(ML3_PAYLOAD_ROUTINE_FLAGS_OFFSET == 2U);
  CHECK(ML3_PAYLOAD_ROUTINE_SEQUENCE_OFFSET == 4U);
  CHECK(ML3_PAYLOAD_ROUTINE_CORRECTED_OFFSET == 6U);
  CHECK(ML3_PAYLOAD_ROUTINE_QUALITY_OFFSET == 22U);
  CHECK(ML3_PAYLOAD_ROUTINE_CALIBRATION_ID_OFFSET == 23U);
  CHECK(ML3_PAYLOAD_ROUTINE_RESERVED_OFFSET == 24U);
  CHECK(ML3_PAYLOAD_UV_PER_DECI_MV == 100U);
  CHECK(ML3_PAYLOAD_UV_PER_MV == 1000U);
  CHECK(ML3_PAYLOAD_MILLIC_PER_CENTIC == 10U);
  CHECK(ML3_PAYLOAD_DIAGNOSTIC_PART_OFFSET == 2U);
  CHECK(ML3_PAYLOAD_DIAGNOSTIC_SEQUENCE_OFFSET == 3U);
  CHECK(ML3_PAYLOAD_DIAGNOSTIC_THERMISTOR_RAW_OFFSET == 28U);
  CHECK(ML3_PAYLOAD_DIAGNOSTIC_HEADER_LENGTH == 30U);
  CHECK(ML3_PAYLOAD_DIAGNOSTIC_PART_INDEX_SHIFT == 4U);
  CHECK(ML3_PAYLOAD_DIAGNOSTIC_PART_COUNT_MASK == UINT8_C(0x0F));
  CHECK(ML3_PAYLOAD_DIAGNOSTIC_CYCLES_PER_PART == 4U);
  CHECK(ML3_PAYLOAD_DIAGNOSTIC_MAX_PARTS == 3U);
  CHECK(ML3_PAYLOAD_AUTO_DIAG_INTERVAL_MS == UINT32_C(21600000));
  CHECK(ML3_QUALITY_INVALIDATING_MASK == UINT16_C(0x027F));
}

static void test_routine_semantically_consistent_degraded_vector(void)
{
  static const uint8_t expected[ML3_PAYLOAD_ROUTINE_LENGTH] = {
    0x01U, 0x00U, 0x00U, 0x80U, 0x12U, 0x34U, 0xFFU, 0x38U,
    0x30U, 0x39U, 0x31U, 0x01U, 0x0CU, 0xE4U, 0x13U, 0x88U,
    0x01U, 0x41U, 0xFFU, 0x85U, 0x09U, 0xE6U, 0x44U, 0x7EU,
    0x00U
  };
  ml3_payload_routine_t input;
  uint8_t output[ML3_PAYLOAD_ROUTINE_LENGTH];
  size_t output_length = 0U;

  input = routine_input();
  input.status_flags = UINT16_C(0x0080);
  input.sequence = UINT16_C(0x1234);
  input.corrected_diff_available = true;
  input.corrected_diff_uv = INT64_C(-20000);
  input.mean_hi_available = true;
  input.mean_hi_uncalibrated_uv = INT64_C(1234500);
  input.mean_lo_available = true;
  input.mean_lo_uncalibrated_uv = INT64_C(1254500);
  input.vdda_available = true;
  input.vdda_uv = INT64_C(3300000);
  input.v5_available = true;
  input.v5_uv = INT64_C(5000000);
  input.noise_available = true;
  input.noise_uv = INT64_C(321);
  input.die_temperature_available = true;
  input.die_temperature_millic = INT64_C(-1230);
  input.soil_temperature_available = true;
  input.soil_temperature_millic = INT64_C(25340);
  input.quality_state = ML3_QUALITY_STATE_DEGRADED;
  input.valid_cycle_count = 4U;
  input.calibration_id = UINT8_C(0x7E);

  CHECK(ml3_payload_build_routine(
          &input,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_OK);
  CHECK(output_length == ML3_PAYLOAD_ROUTINE_LENGTH);
  CHECK(memcmp(output, expected, sizeof(expected)) == 0);
}

static void test_supplied_layout_oracle_is_semantically_malformed(void)
{
  static const uint8_t supplied_oracle[ML3_PAYLOAD_ROUTINE_LENGTH] = {
    0x01U, 0x00U, 0xA5U, 0x5AU, 0x12U, 0x34U, 0xFFU, 0x38U,
    0x30U, 0x39U, 0x31U, 0x01U, 0x0CU, 0xE4U, 0x13U, 0x88U,
    0x01U, 0x41U, 0xFFU, 0x85U, 0x09U, 0xE6U, 0x44U, 0x7EU,
    0x00U
  };

  CHECK(read_u16_be(&supplied_oracle[2]) == UINT16_C(0xA55A));
  CHECK(read_u16_be(&supplied_oracle[4]) == UINT16_C(0x1234));
  CHECK(read_u16_be(&supplied_oracle[6]) == UINT16_C(0xFF38));
  CHECK(supplied_oracle[22] == UINT8_C(0x44));
  CHECK(ml3_payload_validate_routine_frame(
          supplied_oracle,
          sizeof(supplied_oracle)) == ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY);
}

static void test_routine_sentinels_and_quality_states(void)
{
  ml3_payload_routine_t input = routine_input();
  uint8_t output[ML3_PAYLOAD_ROUTINE_LENGTH];
  size_t output_length = 0U;

  input.corrected_diff_available = false;
  input.mean_hi_available = false;
  input.mean_lo_available = false;
  input.vdda_available = false;
  input.v5_available = false;
  input.noise_available = false;
  input.die_temperature_available = false;
  input.soil_temperature_available = false;
  input.calibration_id = 0U;
  CHECK(ml3_payload_build_routine(
          &input,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_OK);
  CHECK(read_u16_be(&output[6]) == ML3_PAYLOAD_CORRECTED_SENTINEL);
  CHECK(read_u16_be(&output[8]) == ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(read_u16_be(&output[10]) == ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(read_u16_be(&output[12]) == ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(read_u16_be(&output[14]) == ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(read_u16_be(&output[16]) == ML3_PAYLOAD_UNSIGNED_SENTINEL);
  CHECK(read_u16_be(&output[18]) == ML3_PAYLOAD_TEMPERATURE_SENTINEL);
  CHECK(read_u16_be(&output[20]) == ML3_PAYLOAD_TEMPERATURE_SENTINEL);
  CHECK(output[22] == UINT8_C(0x04));
  CHECK(output[23] == 0U);

  input = routine_input();
  input.status_flags = UINT16_C(0x0080);
  input.corrected_diff_uv = INT64_C(-199);
  input.die_temperature_millic = INT64_C(-19);
  input.quality_state = ML3_QUALITY_STATE_DEGRADED;
  CHECK(ml3_payload_build_routine(
          &input,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_OK);
  CHECK(read_u16_be(&output[6]) == UINT16_C(0xFFFF));
  CHECK(read_u16_be(&output[18]) == UINT16_C(0xFFFF));
  CHECK(output[22] == UINT8_C(0x44));

  input = routine_input();
  input.status_flags = UINT16_C(0x0004);
  input.corrected_diff_uv = INT64_C(500000);
  input.quality_state = ML3_QUALITY_STATE_INVALID;
  input.valid_cycle_count = 3U;
  CHECK(ml3_payload_build_routine(
          &input,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_OK);
  CHECK(read_u16_be(&output[6]) == ML3_PAYLOAD_CORRECTED_SENTINEL);
  CHECK(output[22] == UINT8_C(0x83));

  input = routine_input();
  input.status_flags = ML3_QUALITY_FLAG_CAL_INVALID;
  input.corrected_diff_uv = INT64_C(500000);
  input.quality_state = ML3_QUALITY_STATE_DEGRADED;
  CHECK(ml3_payload_build_routine(
          &input,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_OK);
  CHECK(read_u16_be(&output[6]) == ML3_PAYLOAD_CORRECTED_SENTINEL);
}

static void test_routine_boundaries_and_length_gate(void)
{
  ml3_payload_routine_t input = routine_input();
  uint8_t output[ML3_PAYLOAD_ROUTINE_LENGTH];
  uint8_t before[ML3_PAYLOAD_ROUTINE_LENGTH];
  size_t output_length = 99U;

  (void)memset(output, 0xA5, sizeof(output));
  (void)memcpy(before, output, sizeof(before));
  CHECK(ml3_payload_build_routine(
          &input,
          output,
          sizeof(output),
          0U,
          &output_length) == ML3_PAYLOAD_ERR_MAX_FRMPAYLOAD_NOT_READY);
  CHECK(output_length == 0U);
  CHECK(memcmp(output, before, sizeof(output)) == 0);

  output_length = 99U;
  CHECK(ml3_payload_build_routine(
          &input,
          output,
          sizeof(output),
          ML3_PAYLOAD_ROUTINE_LENGTH - 1U,
          &output_length) == ML3_PAYLOAD_ERR_FRAME_TOO_LARGE);
  CHECK(output_length == 0U);
  CHECK(memcmp(output, before, sizeof(output)) == 0);

  output_length = 99U;
  CHECK(ml3_payload_build_routine(
          &input,
          output,
          ML3_PAYLOAD_ROUTINE_LENGTH - 1U,
          ML3_PAYLOAD_ROUTINE_LENGTH,
          &output_length) == ML3_PAYLOAD_ERR_OUTPUT_TOO_SMALL);
  CHECK(output_length == 0U);
  CHECK(memcmp(output, before, sizeof(output)) == 0);

  input.valid_cycle_count = 9U;
  CHECK(ml3_payload_build_routine(
          &input,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_ERR_VALUE_OUT_OF_RANGE);

  input = routine_input();
  input.mean_hi_uncalibrated_uv = INT64_C(6553500);
  CHECK(ml3_payload_build_routine(
          &input,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_ERR_VALUE_OUT_OF_RANGE);

  input = routine_input();
  input.mean_hi_uncalibrated_uv = INT64_C(-1);
  CHECK(ml3_payload_build_routine(
          &input,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_ERR_VALUE_OUT_OF_RANGE);

  input = routine_input();
  input.corrected_diff_uv = INT64_C(3276700);
  CHECK(ml3_payload_build_routine(
          &input,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_ERR_VALUE_OUT_OF_RANGE);
}

static void test_routine_rejects_inconsistent_quality(void)
{
  ml3_payload_routine_t input = routine_input();
  uint8_t output[ML3_PAYLOAD_ROUTINE_LENGTH];
  size_t output_length = 0U;

  input.status_flags = UINT16_C(0x0001);
  input.quality_state = ML3_QUALITY_STATE_DEGRADED;
  CHECK(ml3_payload_build_routine(
          &input,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY);

  input = routine_input();
  input.status_flags = UINT16_C(0x0080);
  CHECK(ml3_payload_build_routine(
          &input,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY);

  input = routine_input();
  input.valid_cycle_count = 2U;
  input.quality_state = ML3_QUALITY_STATE_DEGRADED;
  CHECK(ml3_payload_build_routine(
          &input,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY);
}

static void test_adc_failure_forces_all_numeric_sentinels(void)
{
  static const uint16_t core_adc_failure_flags[] = {
    (uint16_t)ML3_QUALITY_FLAG_ADC_INIT,
    (uint16_t)ML3_QUALITY_FLAG_ADC_CAL,
    (uint16_t)ML3_QUALITY_FLAG_ADC_TIMEOUT,
    (uint16_t)ML3_QUALITY_FLAG_ADC_OVERRUN
  };
  static const size_t offsets[] = {
    ML3_PAYLOAD_ROUTINE_CORRECTED_OFFSET,
    ML3_PAYLOAD_ROUTINE_MEAN_HI_OFFSET,
    ML3_PAYLOAD_ROUTINE_MEAN_LO_OFFSET,
    ML3_PAYLOAD_ROUTINE_VDDA_OFFSET,
    ML3_PAYLOAD_ROUTINE_V5_OFFSET,
    ML3_PAYLOAD_ROUTINE_NOISE_OFFSET,
    ML3_PAYLOAD_ROUTINE_DIE_TEMP_OFFSET,
    ML3_PAYLOAD_ROUTINE_SOIL_TEMP_OFFSET
  };
  static const uint16_t sentinels[] = {
    ML3_PAYLOAD_CORRECTED_SENTINEL,
    ML3_PAYLOAD_UNSIGNED_SENTINEL,
    ML3_PAYLOAD_UNSIGNED_SENTINEL,
    ML3_PAYLOAD_UNSIGNED_SENTINEL,
    ML3_PAYLOAD_UNSIGNED_SENTINEL,
    ML3_PAYLOAD_UNSIGNED_SENTINEL,
    ML3_PAYLOAD_TEMPERATURE_SENTINEL,
    ML3_PAYLOAD_TEMPERATURE_SENTINEL
  };
  ml3_payload_routine_t input;
  uint8_t output[ML3_PAYLOAD_ROUTINE_LENGTH];
  uint8_t mutation[ML3_PAYLOAD_ROUTINE_LENGTH];
  size_t output_length = 0U;
  size_t flag_index;
  size_t index;

  for (flag_index = 0U;
       flag_index < (sizeof(core_adc_failure_flags) /
                     sizeof(core_adc_failure_flags[0]));
       flag_index += 1U) {
    input = routine_input();
    input.status_flags = core_adc_failure_flags[flag_index];
    input.quality_state = ML3_QUALITY_STATE_INVALID;
    CHECK(ml3_payload_build_routine(
            &input,
            output,
            sizeof(output),
            sizeof(output),
            &output_length) == ML3_PAYLOAD_OK);
    for (index = 0U;
         index < (sizeof(offsets) / sizeof(offsets[0]));
         index += 1U) {
      CHECK(read_u16_be(&output[offsets[index]]) == sentinels[index]);
    }
    CHECK(ml3_payload_validate_routine_frame(output, output_length) ==
      ML3_PAYLOAD_OK);

    for (index = 0U;
         index < (sizeof(offsets) / sizeof(offsets[0]));
         index += 1U) {
      (void)memcpy(mutation, output, sizeof(mutation));
      write_u16_be(&mutation[offsets[index]], 0U);
      CHECK(ml3_payload_validate_routine_frame(mutation, sizeof(mutation)) ==
        ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY);
    }
  }
}

static void test_therm_fault_forces_soil_temperature_sentinel(void)
{
  ml3_payload_routine_t input = routine_input();
  uint8_t output[ML3_PAYLOAD_ROUTINE_LENGTH];
  size_t output_length = 0U;

  input.status_flags = (uint16_t)ML3_QUALITY_FLAG_THERM_FAULT;
  input.quality_state = ML3_QUALITY_STATE_DEGRADED;
  CHECK(ml3_payload_build_routine(
          &input,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_OK);
  CHECK(read_u16_be(&output[ML3_PAYLOAD_ROUTINE_CORRECTED_OFFSET]) == 0U);
  CHECK(read_u16_be(&output[ML3_PAYLOAD_ROUTINE_SOIL_TEMP_OFFSET]) ==
    ML3_PAYLOAD_TEMPERATURE_SENTINEL);
  CHECK(ml3_payload_validate_routine_frame(output, output_length) ==
    ML3_PAYLOAD_OK);

  write_u16_be(&output[ML3_PAYLOAD_ROUTINE_SOIL_TEMP_OFFSET], 0U);
  CHECK(ml3_payload_validate_routine_frame(output, output_length) ==
    ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY);
}

static ml3_payload_diagnostic_t diagnostic_input(void)
{
  ml3_payload_diagnostic_t input;
  size_t index;

  (void)memset(&input, 0, sizeof(input));
  input.sequence = UINT16_C(0x1234);
  input.status_flags = UINT16_C(0x0080);
  input.quality_state = ML3_QUALITY_STATE_DEGRADED;
  input.valid_cycle_count = 4U;
  input.reset_cause = UINT32_C(0x89ABCDEF);
  input.warmup_ms = UINT16_C(1000);
  input.hardware_revision = UINT8_C(0x21);
  input.firmware_build_id = UINT16_C(0x4567);
  input.calibration_schema = UINT16_C(2);
  input.calibration_id = UINT8_C(0x7E);
  input.vrefint_pre_available = true;
  input.vrefint_pre_raw = UINT16_C(0x1111);
  input.pa4_pre_available = true;
  input.pa4_pre_raw = UINT16_C(0x2222);
  input.pa4_post_available = true;
  input.pa4_post_raw = UINT16_C(0x3333);
  input.cycle_count = 8U;
  for (index = 0U; index < ML3_PAYLOAD_MAX_ABBA_CYCLES; index += 1U) {
    input.cycles[index].h1 = (uint16_t)(UINT16_C(0x1000) + (uint16_t)(index * 4U));
    input.cycles[index].l1 = (uint16_t)(input.cycles[index].h1 + 1U);
    input.cycles[index].l2 = (uint16_t)(input.cycles[index].h1 + 2U);
    input.cycles[index].h2 = (uint16_t)(input.cycles[index].h1 + 3U);
  }
  return input;
}

static void test_diagnostic_header_and_cycle_vectors(void)
{
  static const uint8_t expected_header[ML3_PAYLOAD_DIAGNOSTIC_HEADER_LENGTH] = {
    0x01U, 0x01U, 0x03U, 0x12U, 0x34U, 0x00U, 0x80U, 0x44U,
    0x89U, 0xABU, 0xCDU, 0xEFU, 0x03U, 0xE8U, 0x21U, 0x45U,
    0x67U, 0x00U, 0x02U, 0x7EU, 0x11U, 0x11U, 0xFFU, 0xFFU,
    0x22U, 0x22U, 0x33U, 0x33U, 0xFFU, 0xFFU
  };
  static const uint8_t expected_part_one[ML3_PAYLOAD_DIAGNOSTIC_CYCLE_PART_MAX_LENGTH] = {
    0x01U, 0x01U, 0x13U,
    0x10U, 0x00U, 0x10U, 0x01U, 0x10U, 0x02U, 0x10U, 0x03U,
    0x10U, 0x04U, 0x10U, 0x05U, 0x10U, 0x06U, 0x10U, 0x07U,
    0x10U, 0x08U, 0x10U, 0x09U, 0x10U, 0x0AU, 0x10U, 0x0BU,
    0x10U, 0x0CU, 0x10U, 0x0DU, 0x10U, 0x0EU, 0x10U, 0x0FU
  };
  static const uint8_t expected_part_two[ML3_PAYLOAD_DIAGNOSTIC_CYCLE_PART_MAX_LENGTH] = {
    0x01U, 0x01U, 0x23U,
    0x10U, 0x10U, 0x10U, 0x11U, 0x10U, 0x12U, 0x10U, 0x13U,
    0x10U, 0x14U, 0x10U, 0x15U, 0x10U, 0x16U, 0x10U, 0x17U,
    0x10U, 0x18U, 0x10U, 0x19U, 0x10U, 0x1AU, 0x10U, 0x1BU,
    0x10U, 0x1CU, 0x10U, 0x1DU, 0x10U, 0x1EU, 0x10U, 0x1FU
  };
  ml3_payload_diagnostic_t input = diagnostic_input();
  uint8_t output[ML3_PAYLOAD_DIAGNOSTIC_CYCLE_PART_MAX_LENGTH];
  size_t output_length = 0U;

  CHECK(ml3_payload_build_diagnostic_part(
          &input,
          0U,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_OK);
  CHECK(output_length == sizeof(expected_header));
  CHECK(memcmp(output, expected_header, sizeof(expected_header)) == 0);

  CHECK(ml3_payload_build_diagnostic_part(
          &input,
          1U,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_OK);
  CHECK(output_length == sizeof(expected_part_one));
  CHECK(memcmp(output, expected_part_one, sizeof(expected_part_one)) == 0);

  CHECK(ml3_payload_build_diagnostic_part(
          &input,
          2U,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_OK);
  CHECK(output_length == sizeof(expected_part_two));
  CHECK(memcmp(output, expected_part_two, sizeof(expected_part_two)) == 0);
}

static void test_diagnostic_part_boundaries_and_gates(void)
{
  static const struct {
    uint8_t cycles;
    uint8_t parts;
    size_t last_length;
  } cases[] = {
    { 0U, 1U, ML3_PAYLOAD_DIAGNOSTIC_HEADER_LENGTH },
    { 1U, 2U, 11U },
    { 4U, 2U, 35U },
    { 5U, 3U, 11U },
    { 8U, 3U, 35U }
  };
  ml3_payload_diagnostic_t input = diagnostic_input();
  uint8_t output[ML3_PAYLOAD_DIAGNOSTIC_CYCLE_PART_MAX_LENGTH];
  uint8_t part_count = 0U;
  size_t output_length = 0U;
  size_t index;

  for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); index += 1U) {
    input.cycle_count = cases[index].cycles;
    input.valid_cycle_count = cases[index].cycles;
    input.quality_state = (cases[index].cycles < 3U) ?
      ML3_QUALITY_STATE_INVALID : ML3_QUALITY_STATE_DEGRADED;
    CHECK(ml3_payload_diagnostic_part_count(input.cycle_count, &part_count) ==
      ML3_PAYLOAD_OK);
    CHECK(part_count == cases[index].parts);
    CHECK(ml3_payload_build_diagnostic_part(
            &input,
            (uint8_t)(part_count - 1U),
            output,
            sizeof(output),
            sizeof(output),
            &output_length) == ML3_PAYLOAD_OK);
    CHECK(output_length == cases[index].last_length);
    CHECK((output[2] >> 4U) == (uint8_t)(part_count - 1U));
    CHECK((output[2] & UINT8_C(0x0F)) == part_count);
  }

  CHECK(ml3_payload_diagnostic_part_count(9U, &part_count) ==
    ML3_PAYLOAD_ERR_VALUE_OUT_OF_RANGE);
  input.cycle_count = 0U;
  CHECK(ml3_payload_build_diagnostic_part(
          &input,
          1U,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_ERR_VALUE_OUT_OF_RANGE);

  input = diagnostic_input();
  input.cycle_count = 4U;
  input.valid_cycle_count = 4U;
  CHECK(ml3_payload_build_diagnostic_part(
          &input,
          1U,
          output,
          sizeof(output),
          34U,
          &output_length) == ML3_PAYLOAD_ERR_FRAME_TOO_LARGE);
  CHECK(ml3_payload_build_diagnostic_part(
          &input,
          1U,
          output,
          34U,
          35U,
          &output_length) == ML3_PAYLOAD_ERR_OUTPUT_TOO_SMALL);
  CHECK(ml3_payload_build_diagnostic_part(
          &input,
          1U,
          output,
          sizeof(output),
          0U,
          &output_length) == ML3_PAYLOAD_ERR_MAX_FRMPAYLOAD_NOT_READY);

  input = diagnostic_input();
  input.cycles[7].h2 = UINT16_C(65521);
  CHECK(ml3_payload_build_diagnostic_part(
          &input,
          2U,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_ERR_VALUE_OUT_OF_RANGE);

  input = diagnostic_input();
  input.status_flags = 0U;
  input.quality_state = ML3_QUALITY_STATE_VALID;
  input.valid_cycle_count = 4U;
  CHECK(ml3_payload_build_diagnostic_part(
          &input,
          0U,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_OK);
  CHECK(output[ML3_PAYLOAD_DIAGNOSTIC_QUALITY_OFFSET] == UINT8_C(0x04));

  input.quality_state = ML3_QUALITY_STATE_INVALID;
  input.valid_cycle_count = 8U;
  CHECK(ml3_payload_build_diagnostic_part(
          &input,
          0U,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_OK);
  CHECK(output[ML3_PAYLOAD_DIAGNOSTIC_QUALITY_OFFSET] == UINT8_C(0x88));

  input.quality_state = (ml3_quality_state_t)3;
  CHECK(ml3_payload_build_diagnostic_part(
          &input,
          0U,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_ERR_VALUE_OUT_OF_RANGE);
}

static void test_diagnostic_rejects_inconsistent_quality(void)
{
  ml3_payload_diagnostic_t input = diagnostic_input();
  uint8_t output[ML3_PAYLOAD_DIAGNOSTIC_CYCLE_PART_MAX_LENGTH];
  size_t output_length = 0U;

  input.status_flags = UINT16_C(0x0002);
  CHECK(ml3_payload_build_diagnostic_part(
          &input,
          0U,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY);

  input = diagnostic_input();
  input.status_flags = UINT16_C(0x0080);
  input.quality_state = ML3_QUALITY_STATE_VALID;
  CHECK(ml3_payload_build_diagnostic_part(
          &input,
          0U,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY);

  input = diagnostic_input();
  input.cycle_count = 3U;
  input.valid_cycle_count = 4U;
  CHECK(ml3_payload_build_diagnostic_part(
          &input,
          0U,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY);

  input = diagnostic_input();
  input.cycle_count = 2U;
  input.valid_cycle_count = 2U;
  input.quality_state = ML3_QUALITY_STATE_DEGRADED;
  CHECK(ml3_payload_build_diagnostic_part(
          &input,
          0U,
          output,
          sizeof(output),
          sizeof(output),
          &output_length) == ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY);
}

static void test_automatic_diagnostic_rate_limit(void)
{
  ml3_payload_diag_rate_entry_t entries[2];
  ml3_payload_diag_rate_entry_t before[2];
  uint32_t wrap_now;

  (void)memset(entries, 0, sizeof(entries));
  (void)memcpy(before, entries, sizeof(before));
  CHECK(ml3_payload_auto_diag_is_eligible(
    entries, 2U, UINT32_C(0x11111111), UINT32_C(1000)));
  CHECK(memcmp(entries, before, sizeof(entries)) == 0);

  CHECK(ml3_payload_auto_diag_mark_queued(
          entries,
          2U,
          UINT32_C(0x11111111),
          UINT32_C(1000)) == ML3_PAYLOAD_OK);
  CHECK(entries[0].occupied);
  CHECK(entries[0].signature == UINT32_C(0x11111111));
  CHECK(entries[0].last_queued_ms == UINT32_C(1000));
  CHECK(!ml3_payload_auto_diag_is_eligible(
    entries,
    2U,
    UINT32_C(0x11111111),
    UINT32_C(1000) + ML3_PAYLOAD_AUTO_DIAG_INTERVAL_MS - UINT32_C(1)));
  CHECK(ml3_payload_auto_diag_is_eligible(
    entries,
    2U,
    UINT32_C(0x11111111),
    UINT32_C(1000) + ML3_PAYLOAD_AUTO_DIAG_INTERVAL_MS));

  CHECK(ml3_payload_auto_diag_is_eligible(
    entries, 2U, UINT32_C(0x22222222), UINT32_C(2000)));
  CHECK(ml3_payload_auto_diag_mark_queued(
          entries,
          2U,
          UINT32_C(0x22222222),
          UINT32_C(2000)) == ML3_PAYLOAD_OK);
  CHECK(!ml3_payload_auto_diag_is_eligible(
    entries, 2U, UINT32_C(0x33333333), UINT32_C(3000)));
  (void)memcpy(before, entries, sizeof(before));
  CHECK(ml3_payload_auto_diag_mark_queued(
          entries,
          2U,
          UINT32_C(0x33333333),
          UINT32_C(3000)) == ML3_PAYLOAD_ERR_RATE_LIMIT_CAPACITY);
  CHECK(memcmp(entries, before, sizeof(entries)) == 0);

  (void)memset(entries, 0, sizeof(entries));
  entries[0].occupied = true;
  entries[0].signature = UINT32_C(0x44444444);
  entries[0].last_queued_ms = UINT32_MAX - UINT32_C(1000);
  wrap_now = entries[0].last_queued_ms + ML3_PAYLOAD_AUTO_DIAG_INTERVAL_MS;
  CHECK(!ml3_payload_auto_diag_is_eligible(
    entries,
    1U,
    UINT32_C(0x44444444),
    wrap_now - UINT32_C(1)));
  CHECK(ml3_payload_auto_diag_is_eligible(
    entries, 1U, UINT32_C(0x44444444), wrap_now));

  CHECK(!ml3_payload_auto_diag_is_eligible(
    entries, 0U, UINT32_C(0x44444444), wrap_now));
  CHECK(!ml3_payload_auto_diag_is_eligible(entries, 1U, 0U, wrap_now));
  CHECK(ml3_payload_auto_diag_mark_queued(entries, 1U, 0U, wrap_now) ==
    ML3_PAYLOAD_ERR_INVALID_ARGUMENT);
}

int main(void)
{
  test_public_wire_contract_constants();
  test_routine_semantically_consistent_degraded_vector();
  test_supplied_layout_oracle_is_semantically_malformed();
  test_routine_sentinels_and_quality_states();
  test_routine_boundaries_and_length_gate();
  test_routine_rejects_inconsistent_quality();
  test_adc_failure_forces_all_numeric_sentinels();
  test_therm_fault_forces_soil_temperature_sentinel();
  test_diagnostic_header_and_cycle_vectors();
  test_diagnostic_part_boundaries_and_gates();
  test_diagnostic_rejects_inconsistent_quality();
  test_automatic_diagnostic_rate_limit();

  if (failures != 0) {
    (void)fprintf(stderr, "ml3 payload: %d failure(s)\n", failures);
    return 1;
  }
  (void)printf("ml3 payload: OK\n");
  return 0;
}
