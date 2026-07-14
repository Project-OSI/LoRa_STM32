#include "ml3_calibration.h"

#include <inttypes.h>
#include <limits.h>
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
    ml3_calibration_status_t expected_ = (expected); \
    ml3_calibration_status_t actual_ = (actual); \
    if (expected_ != actual_) { \
      (void)fprintf(stderr, "FAIL: %s: expected=%d actual=%d\n", \
        (label), (int)expected_, (int)actual_); \
      ++failures; \
    } \
  } while (0)

#define EXPECT_I64(expected, actual, label) \
  do { \
    int64_t expected_ = (expected); \
    int64_t actual_ = (actual); \
    if (expected_ != actual_) { \
      (void)fprintf(stderr, "FAIL: %s: expected=%" PRId64 " actual=%" PRId64 "\n", \
        (label), expected_, actual_); \
      ++failures; \
    } \
  } while (0)

#define EXPECT_U32(expected, actual, label) \
  do { \
    uint32_t expected_ = (expected); \
    uint32_t actual_ = (actual); \
    if (expected_ != actual_) { \
      (void)fprintf(stderr, "FAIL: %s: expected=%" PRIu32 " actual=%" PRIu32 "\n", \
        (label), expected_, actual_); \
      ++failures; \
    } \
  } while (0)

static ml3_calibration_model_t valid_model(void) {
  ml3_calibration_model_t model;
  (void)memset(&model, 0, sizeof(model));
  model.calibration_id = 7U;
  return model;
}

static void test_evaluation_oracles(void) {
  ml3_calibration_model_t model = valid_model();
  ml3_calibration_point_t constant_points[2] = {
    { INT32_MIN, 500 }, { INT32_MAX, 500 }
  };
  ml3_calibration_point_t keyed_points[2] = {
    { 0, 0 }, { 200, 200 }
  };
  ml3_calibration_point_t trunc_points[2] = {
    { 0, 0 }, { 3, -2 }
  };
  int64_t output = INT64_C(0x1122334455667788);

  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, -20000, 0, 0, &output), "identity status");
  EXPECT_I64(-20000, output, "identity keeps negative raw");

  model.offset_uV = 1000;
  model.offset_tempco_uV_per_C = 20;
  model.gain_ppm = 10000;
  model.gain_tempco_ppm_per_C = 40;
  model.common_mode_ppm = 2000;
  model.cm_ref_uV = 500000;
  model.reference_temp_centiC = 2500;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, 100000, 600000, 3000, &output), "all terms status");
  EXPECT_I64(102320, output, "all terms exact staged result");

  model.points = constant_points;
  model.point_count = 2U;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, 100000, 600000, 3000, &output), "constant residual status");
  EXPECT_I64(102820, output, "piecewise residual follows parametric model");

  model = valid_model();
  model.gain_ppm = 1000;
  model.gain_tempco_ppm_per_C = 333;
  model.reference_temp_centiC = 0;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, 1000000, 0, -150, &output), "staged tempco status");
  EXPECT_I64(1000501, output, "gain tempco truncates before gain application");

  model = valid_model();
  model.gain_ppm = 1000000;
  model.points = keyed_points;
  model.point_count = 2U;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, 100, 0, 0, &output), "raw-keyed table status");
  EXPECT_I64(300, output, "table is keyed by raw rather than linear value");

  model = valid_model();
  model.points = trunc_points;
  model.point_count = 2U;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, 1, 0, 0, &output), "interpolation raw one");
  EXPECT_I64(1, output, "interpolation truncates zero at raw one");
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, 2, 0, 0, &output), "interpolation raw two");
  EXPECT_I64(1, output, "interpolation correction minus one at raw two");
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, 3, 0, 0, &output), "interpolation endpoint");
  EXPECT_I64(1, output, "interpolation correction minus two at endpoint");
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, 6, 0, 0, &output), "interpolation upper hold");
  EXPECT_I64(4, output, "upper endpoint correction is held");
}

static void test_model_validation_and_extremes(void) {
  ml3_calibration_model_t model = valid_model();
  ml3_calibration_point_t points[ML3_CALIBRATION_MAX_POINTS];
  ml3_calibration_point_t interpolation_extremes[2] = {
    { INT32_MIN, INT32_MIN }, { INT32_MAX, INT32_MAX }
  };
  ml3_calibration_point_t bad_effective[2] = { { 0, 0 }, { 1, -2 } };
  ml3_calibration_point_t flat_effective[2] = { { 0, 1 }, { 1, 0 } };
  int64_t output;
  size_t i;

  for (i = 0U; i < ML3_CALIBRATION_MAX_POINTS; ++i) {
    points[i].input_uV = (int32_t)i;
    points[i].correction_uV = 0;
  }
  model.points = points;
  model.point_count = ML3_CALIBRATION_MAX_POINTS;
  output = 0;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, 0, 0, 0, &output),
    "count 255 is valid");
  EXPECT_TRUE(ml3_calibration_record_size(ML3_CALIBRATION_MAX_POINTS) == 2092U,
    "count 255 record size");

  model.point_count = 1U;
  EXPECT_STATUS(ML3_CALIBRATION_INVALID_MODEL,
    ml3_calibration_apply(&model, 0, 0, 0, &output),
    "count one is invalid");
  model.point_count = 2U;
  model.points = NULL;
  EXPECT_STATUS(ML3_CALIBRATION_INVALID_MODEL,
    ml3_calibration_apply(&model, 0, 0, 0, &output),
    "null nonzero table is invalid");
  model.points = points;
  points[1].input_uV = points[0].input_uV;
  EXPECT_STATUS(ML3_CALIBRATION_INVALID_MODEL,
    ml3_calibration_apply(&model, 0, 0, 0, &output),
    "duplicate input is invalid");
  points[1].input_uV = points[0].input_uV - 1;
  EXPECT_STATUS(ML3_CALIBRATION_INVALID_MODEL,
    ml3_calibration_apply(&model, 0, 0, 0, &output),
    "descending input is invalid");
  model.points = bad_effective;
  EXPECT_STATUS(ML3_CALIBRATION_INVALID_MODEL,
    ml3_calibration_apply(&model, 0, 0, 0, &output),
    "decreasing effective knots are invalid");
  model.points = flat_effective;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, -10, 0, 0, &output),
    "flat effective segment and lower hold accepted");
  EXPECT_I64(-9, output, "lower endpoint correction is held");

  model = valid_model();
  model.v5_divider_ppm = INT32_MAX;
  output = 1;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, INT64_MIN, INT64_MAX, INT32_MIN, &output),
    "identity accepts minimum raw");
  EXPECT_I64(INT64_MIN, output, "minimum raw identity result");
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, INT64_MAX, INT64_MIN, INT32_MAX, &output),
    "zero coefficients short-circuit irrelevant extremes");
  EXPECT_I64(INT64_MAX, output, "maximum raw identity result");

  model = valid_model();
  model.gain_ppm = -1;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, INT64_MIN, 0, 0, &output),
    "minimum raw multiply-divide quadrant");
  EXPECT_I64(INT64_MIN + INT64_C(9223372036854), output,
    "minimum raw negative gain correction");

  model.gain_ppm = -1;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, INT64_MAX, 0, 0, &output),
    "overflowing raw product has representable quotient");
  EXPECT_I64(INT64_MAX - INT64_C(9223372036854), output,
    "full-domain multiply divide remains exact");

  model = valid_model();
  model.reference_temp_centiC = INT16_MIN;
  model.gain_tempco_ppm_per_C = INT32_MAX;
  output = 76;
  EXPECT_STATUS(ML3_CALIBRATION_OVERFLOW,
    ml3_calibration_apply(&model, INT64_MAX, 0, INT32_MAX, &output),
    "multiply-divide quotient overflow");
  EXPECT_I64(76, output, "quotient overflow preserves output");

  model = valid_model();
  model.offset_tempco_uV_per_C = -1;
  model.reference_temp_centiC = 0;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, 10, 0, 150, &output),
    "negative offset tempco truncation");
  EXPECT_I64(9, output, "negative offset tempco truncates toward zero");
  model = valid_model();
  model.common_mode_ppm = -1;
  model.cm_ref_uV = 0;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, 10, 1500000, 0, &output),
    "negative common-mode truncation");
  EXPECT_I64(9, output, "negative common-mode division truncates toward zero");

  model = valid_model();
  model.offset_uV = 1;
  output = 77;
  EXPECT_STATUS(ML3_CALIBRATION_OVERFLOW,
    ml3_calibration_apply(&model, INT64_MAX, 0, 0, &output), "final add overflow");
  EXPECT_I64(77, output, "overflow preserves output");

  model = valid_model();
  model.common_mode_ppm = 1;
  model.cm_ref_uV = INT32_MIN;
  output = 78;
  EXPECT_STATUS(ML3_CALIBRATION_OVERFLOW,
    ml3_calibration_apply(&model, 0, INT64_MAX, 0, &output), "common mode subtraction overflow");
  EXPECT_I64(78, output, "common mode overflow preserves output");
  model.common_mode_ppm = 0;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, 0, INT64_MAX, 0, &output),
    "zero common-mode coefficient skips overflowing subtraction");
  EXPECT_I64(0, output, "zero common-mode coefficient remains identity");

  model = valid_model();
  model.points = interpolation_extremes;
  model.point_count = 2U;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, 0, 0, 0, &output),
    "interpolation supports overflowing signed intermediate product");
  EXPECT_I64(0, output, "extreme interpolation exact midpoint");

  model.calibration_id = 0U;
  output = 79;
  EXPECT_STATUS(ML3_CALIBRATION_INVALID_MODEL,
    ml3_calibration_apply(&model, 0, 0, 0, &output), "calibration id zero is invalid");
  EXPECT_I64(79, output, "invalid model preserves output");
}

static void test_cancellation_aware_final_sum(void) {
  ml3_calibration_model_t model = valid_model();
  int64_t output = 0;

  model.offset_uV = 1;
  model.common_mode_ppm = -1;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, INT64_MAX, 1000000, 0, &output),
    "positive extreme offset and CM cancellation");
  EXPECT_I64(INT64_MAX, output,
    "positive extreme offset and CM cancellation result");

  model = valid_model();
  model.offset_uV = -1;
  model.common_mode_ppm = 1;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, INT64_MIN, 1000000, 0, &output),
    "negative extreme offset and CM cancellation");
  EXPECT_I64(INT64_MIN, output,
    "negative extreme offset and CM cancellation result");

  model = valid_model();
  model.offset_uV = -1;
  model.common_mode_ppm = 1;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, INT64_MAX, 1000000, 0, &output),
    "positive extreme reversed offset and CM cancellation");
  EXPECT_I64(INT64_MAX, output,
    "positive extreme reversed offset and CM cancellation result");

  model = valid_model();
  model.offset_uV = 1;
  model.common_mode_ppm = -1;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, INT64_MIN, 1000000, 0, &output),
    "negative extreme reversed offset and CM cancellation");
  EXPECT_I64(INT64_MIN, output,
    "negative extreme reversed offset and CM cancellation result");

  model = valid_model();
  model.gain_ppm = 1;
  model.common_mode_ppm = -1;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, INT64_MAX, INT64_MAX, 0, &output),
    "positive extreme gain and later CM cancellation");
  EXPECT_I64(INT64_MAX, output,
    "positive extreme gain and later CM cancellation result");

  model = valid_model();
  model.gain_ppm = 1;
  model.common_mode_ppm = 1;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_apply(&model, INT64_MIN, INT64_MAX, 0, &output),
    "negative extreme gain and later CM cancellation");
  EXPECT_I64(INT64_MIN, output,
    "negative extreme gain and later CM cancellation result");

  model = valid_model();
  model.gain_ppm = 1;
  model.common_mode_ppm = -1;
  output = INT64_C(0x123456789ABCDEF);
  EXPECT_STATUS(ML3_CALIBRATION_OVERFLOW,
    ml3_calibration_apply(&model, INT64_MAX, INT64_MAX - INT64_C(1000000),
      0, &output), "true positive final-sum overflow");
  EXPECT_I64(INT64_C(0x123456789ABCDEF), output,
    "true positive overflow preserves output");

  model = valid_model();
  model.gain_ppm = 1;
  model.common_mode_ppm = 1;
  output = -INT64_C(0x123456789ABCDEF);
  EXPECT_STATUS(ML3_CALIBRATION_OVERFLOW,
    ml3_calibration_apply(&model, INT64_MIN, INT64_MAX - INT64_C(1000000),
      0, &output), "true negative final-sum overflow");
  EXPECT_I64(-INT64_C(0x123456789ABCDEF), output,
    "true negative overflow preserves output");
}

static const uint8_t zero_point_fixture[] = {
  0x43,0x33,0x4c,0x4d,0x02,0x00,0x34,0x00,
  0x18,0xfc,0xff,0xff,0x19,0x00,0x00,0x00,
  0xd2,0x04,0x00,0x00,0xf4,0xff,0xff,0xff,
  0x59,0x01,0x00,0x00,0x50,0xc3,0x00,0x00,
  0x5a,0xfd,0xff,0xff,0xc4,0x09,0x07,0x00,
  0xef,0xcd,0xab,0x89,0xfe,0xff,0xff,0xff,
  0x64,0x23,0x7a,0x0e
};

static const uint8_t two_point_fixture[] = {
  0x43,0x33,0x4c,0x4d,0x02,0x00,0x44,0x00,
  0x18,0xfc,0xff,0xff,0x19,0x00,0x00,0x00,
  0xd2,0x04,0x00,0x00,0xf4,0xff,0xff,0xff,
  0x59,0x01,0x00,0x00,0x50,0xc3,0x00,0x00,
  0x5a,0xfd,0xff,0xff,0xc4,0x09,0x07,0x02,
  0xe0,0xb1,0xff,0xff,0x64,0x00,0x00,0x00,
  0x90,0x05,0x10,0x00,0x06,0xff,0xff,0xff,
  0xef,0xcd,0xab,0x89,0x00,0x00,0x00,0x00,
  0x7f,0xe1,0x6d,0x2f
};

static ml3_calibration_model_t fixture_model(const ml3_calibration_point_t* points,
                                              uint8_t point_count) {
  ml3_calibration_model_t model = valid_model();
  model.offset_uV = -1000;
  model.offset_tempco_uV_per_C = 25;
  model.gain_ppm = 1234;
  model.gain_tempco_ppm_per_C = -12;
  model.common_mode_ppm = 345;
  model.cm_ref_uV = 50000;
  model.v5_divider_ppm = -678;
  model.reference_temp_centiC = 2500;
  model.points = points;
  model.point_count = point_count;
  return model;
}

static void expect_model_equals(const ml3_calibration_model_t* expected,
                                const ml3_calibration_model_t* actual,
                                const char* label) {
  size_t i;
  EXPECT_TRUE(expected->offset_uV == actual->offset_uV, label);
  EXPECT_TRUE(expected->offset_tempco_uV_per_C == actual->offset_tempco_uV_per_C, label);
  EXPECT_TRUE(expected->gain_ppm == actual->gain_ppm, label);
  EXPECT_TRUE(expected->gain_tempco_ppm_per_C == actual->gain_tempco_ppm_per_C, label);
  EXPECT_TRUE(expected->common_mode_ppm == actual->common_mode_ppm, label);
  EXPECT_TRUE(expected->cm_ref_uV == actual->cm_ref_uV, label);
  EXPECT_TRUE(expected->v5_divider_ppm == actual->v5_divider_ppm, label);
  EXPECT_TRUE(expected->reference_temp_centiC == actual->reference_temp_centiC, label);
  EXPECT_TRUE(expected->calibration_id == actual->calibration_id, label);
  EXPECT_TRUE(expected->point_count == actual->point_count, label);
  for (i = 0U; i < expected->point_count; ++i) {
    EXPECT_TRUE(expected->points[i].input_uV == actual->points[i].input_uV, label);
    EXPECT_TRUE(expected->points[i].correction_uV == actual->points[i].correction_uV, label);
  }
}

static void test_crc_and_records(void) {
  static const uint8_t check[] = { '1','2','3','4','5','6','7','8','9' };
  ml3_calibration_point_t points[2] = { { -20000, 100 }, { 1050000, -250 } };
  ml3_calibration_point_t decoded_points[2] = { { 11, 12 }, { 13, 14 } };
  ml3_calibration_model_t model = fixture_model(NULL, 0U);
  ml3_calibration_model_t decoded;
  uint8_t bytes[ML3_CALIBRATION_MAX_RECORD_SIZE];
  uint8_t mutated[sizeof(two_point_fixture)];
  size_t length = 0U;
  uint32_t sequence = 0U;
  size_t i;

  EXPECT_U32(UINT32_C(0xCBF43926), ml3_calibration_crc32(check, sizeof(check)),
    "CRC-32 ISO-HDLC check value");
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_pack(&model, UINT32_C(0x89ABCDEF), UINT32_C(0xFFFFFFFE),
      bytes, sizeof(bytes), &length), "zero point pack");
  EXPECT_TRUE(length == sizeof(zero_point_fixture), "zero point length 52");
  EXPECT_TRUE(memcmp(bytes, zero_point_fixture, sizeof(zero_point_fixture)) == 0,
    "zero point fixture byte exact");

  model = fixture_model(points, 2U);
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_pack(&model, UINT32_C(0x89ABCDEF), 0U,
      bytes, sizeof(bytes), &length), "two point pack");
  EXPECT_TRUE(length == sizeof(two_point_fixture), "two point length 68");
  EXPECT_TRUE(memcmp(bytes, two_point_fixture, sizeof(two_point_fixture)) == 0,
    "two point fixture byte exact");

  (void)memset(&decoded, 0, sizeof(decoded));
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_unpack(two_point_fixture, sizeof(two_point_fixture),
      ML3_CALIBRATION_MAGIC, UINT32_C(0x89ABCDEF), &decoded,
      decoded_points, 2U, &sequence), "two point unpack");
  expect_model_equals(&model, &decoded, "two point decoded model");
  EXPECT_U32(0U, sequence, "two point decoded sequence");

  for (i = 0U; i < 5U; ++i) {
    ml3_calibration_model_t preserved;
    ml3_calibration_point_t preserved_points[2] = { { 11, 12 }, { 13, 14 } };
    uint32_t preserved_sequence = UINT32_C(0xA5A5A5A5);
    ml3_calibration_status_t expected_status = ML3_CALIBRATION_INVALID_RECORD;
    size_t offset = 8U;

    (void)memcpy(mutated, two_point_fixture, sizeof(mutated));
    (void)memset(&preserved, 0x5A, sizeof(preserved));
    if (i == 0U) { offset = 0U; }
    if (i == 1U) { offset = 4U; }
    if (i == 2U) { offset = 6U; }
    if (i == 3U) { offset = 38U; }
    if (i == 4U) { offset = 40U; }
    mutated[offset] ^= 1U;
    EXPECT_STATUS(expected_status,
      ml3_calibration_unpack(mutated, sizeof(mutated), ML3_CALIBRATION_MAGIC,
        UINT32_C(0x89ABCDEF), &preserved, preserved_points, 2U,
        &preserved_sequence), "mutated record rejected");
    EXPECT_TRUE(((const uint8_t*)&preserved)[0] == 0x5AU, "failed unpack preserves model");
    EXPECT_TRUE(preserved_points[0].input_uV == 11, "failed unpack preserves points");
    EXPECT_U32(UINT32_C(0xA5A5A5A5), preserved_sequence,
      "failed unpack preserves sequence");
  }

  EXPECT_STATUS(ML3_CALIBRATION_INVALID_ARGUMENT,
    ml3_calibration_unpack(two_point_fixture, sizeof(two_point_fixture), 0U,
      UINT32_C(0x89ABCDEF), &decoded, decoded_points, 2U, &sequence),
    "zero expected magic rejected");
  EXPECT_STATUS(ML3_CALIBRATION_INVALID_ARGUMENT,
    ml3_calibration_unpack(two_point_fixture, sizeof(two_point_fixture),
      ML3_CALIBRATION_MAGIC, 0U, &decoded, decoded_points, 2U, &sequence),
    "zero expected device hash rejected");
  EXPECT_STATUS(ML3_CALIBRATION_INVALID_RECORD,
    ml3_calibration_unpack(two_point_fixture, sizeof(two_point_fixture),
      ML3_CALIBRATION_MAGIC, UINT32_C(0x89ABCDEE), &decoded,
      decoded_points, 2U, &sequence), "device hash mismatch rejected");
  EXPECT_STATUS(ML3_CALIBRATION_CAPACITY,
    ml3_calibration_unpack(two_point_fixture, sizeof(two_point_fixture),
      ML3_CALIBRATION_MAGIC, UINT32_C(0x89ABCDEF), &decoded,
      decoded_points, 1U, &sequence), "point capacity enforced");

  {
    ml3_calibration_model_t invalid = model;
    uint8_t preserved_bytes[sizeof(two_point_fixture)];
    size_t preserved_length = 123U;
    (void)memset(preserved_bytes, 0xA5, sizeof(preserved_bytes));
    invalid.calibration_id = 0U;
    EXPECT_STATUS(ML3_CALIBRATION_INVALID_MODEL,
      ml3_calibration_pack(&invalid, UINT32_C(0x89ABCDEF), 0U,
        preserved_bytes, sizeof(preserved_bytes), &preserved_length),
      "invalid pack prevalidation");
    for (i = 0U; i < sizeof(preserved_bytes); ++i) {
      EXPECT_TRUE(preserved_bytes[i] == 0xA5U, "invalid pack preserves destination");
    }
    EXPECT_TRUE(preserved_length == 123U, "invalid pack preserves length");

    invalid = model;
    EXPECT_STATUS(ML3_CALIBRATION_CAPACITY,
      ml3_calibration_pack(&invalid, UINT32_C(0x89ABCDEF), 0U,
        preserved_bytes, sizeof(preserved_bytes) - 1U, &preserved_length),
      "pack capacity prevalidation");
    for (i = 0U; i < sizeof(preserved_bytes); ++i) {
      EXPECT_TRUE(preserved_bytes[i] == 0xA5U, "capacity preserves destination");
    }
    EXPECT_TRUE(preserved_length == 123U, "capacity preserves length");
  }
}

static void rewrite_fixture_crc(uint8_t* record, size_t length) {
  uint32_t crc = ml3_calibration_crc32(record, length - 4U);
  record[length - 4U] = (uint8_t)(crc & UINT32_C(0xFF));
  record[length - 3U] = (uint8_t)((crc >> 8) & UINT32_C(0xFF));
  record[length - 2U] = (uint8_t)((crc >> 16) & UINT32_C(0xFF));
  record[length - 1U] = (uint8_t)(crc >> 24);
}

static void test_record_metadata_and_maximum_count(void) {
  ml3_calibration_point_t decoded_points[ML3_CALIBRATION_MAX_POINTS];
  ml3_calibration_point_t max_points[ML3_CALIBRATION_MAX_POINTS];
  ml3_calibration_model_t decoded;
  ml3_calibration_model_t model;
  uint8_t record[ML3_CALIBRATION_MAX_RECORD_SIZE];
  uint8_t mutation[sizeof(two_point_fixture)];
  uint8_t trailing_record[sizeof(two_point_fixture) + 1U];
  uint32_t sequence = UINT32_C(0xA5A5A5A5);
  size_t length = 0U;
  size_t index;

  for (index = 0U; index < ML3_CALIBRATION_MAX_POINTS; ++index) {
    max_points[index].input_uV = (int32_t)index - 127;
    max_points[index].correction_uV = 0;
  }
  model = fixture_model(max_points, ML3_CALIBRATION_MAX_POINTS);
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_pack(&model, UINT32_C(0x89ABCDEF), UINT32_C(0x10203040),
      record, sizeof(record), &length), "maximum point pack");
  EXPECT_TRUE(length == sizeof(record), "maximum record fills 2092 bytes");
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_unpack(record, sizeof(record), ML3_CALIBRATION_MAGIC,
      UINT32_C(0x89ABCDEF), &decoded, decoded_points,
      ML3_CALIBRATION_MAX_POINTS, &sequence), "maximum point unpack");
  EXPECT_U32(UINT32_C(0x10203040), sequence, "maximum point sequence");
  expect_model_equals(&model, &decoded, "maximum point round trip");

  for (index = 0U; index < 6U; ++index) {
    ml3_calibration_model_t preserved;
    ml3_calibration_model_t preserved_snapshot;
    ml3_calibration_point_t preserved_points[2] = { { 11, 12 }, { 13, 14 } };
    ml3_calibration_point_t points_snapshot[2];
    uint32_t preserved_sequence = UINT32_C(0xA5A5A5A5);
    (void)memcpy(mutation, two_point_fixture, sizeof(mutation));
    (void)memset(&preserved, 0x5A, sizeof(preserved));
    preserved_snapshot = preserved;
    (void)memcpy(points_snapshot, preserved_points, sizeof(points_snapshot));
    if (index == 0U) { mutation[0] ^= 1U; }
    if (index == 1U) { mutation[4] = 3U; }
    if (index == 2U) { mutation[6] = 0x43U; }
    if (index == 3U) { mutation[38] = 0U; }
    if (index == 4U) {
      (void)memcpy(&mutation[48], &mutation[40], 4U);
    }
    if (index == 5U) {
      mutation[48] = 0xE1U;
      mutation[49] = 0xB1U;
      mutation[50] = 0xFFU;
      mutation[51] = 0xFFU;
      mutation[52] = 0x9CU;
      mutation[53] = 0xFFU;
      mutation[54] = 0xFFU;
      mutation[55] = 0xFFU;
    }
    rewrite_fixture_crc(mutation, sizeof(mutation));
    EXPECT_STATUS(ML3_CALIBRATION_INVALID_RECORD,
      ml3_calibration_unpack(mutation, sizeof(mutation), ML3_CALIBRATION_MAGIC,
        UINT32_C(0x89ABCDEF), &preserved, preserved_points, 2U,
        &preserved_sequence), "CRC-valid metadata mutation rejected");
    EXPECT_TRUE(memcmp(&preserved, &preserved_snapshot, sizeof(preserved)) == 0,
      "metadata failure preserves entire model");
    EXPECT_TRUE(memcmp(preserved_points, points_snapshot, sizeof(points_snapshot)) == 0,
      "metadata failure preserves all point storage");
    EXPECT_U32(UINT32_C(0xA5A5A5A5), preserved_sequence,
      "metadata failure preserves sequence");
  }

  EXPECT_STATUS(ML3_CALIBRATION_INVALID_ARGUMENT,
    ml3_calibration_unpack(two_point_fixture, sizeof(two_point_fixture),
      UINT32_C(0x4D4C3342), UINT32_C(0x89ABCDEF), &decoded,
      decoded_points, 2U, &sequence), "wrong expected magic rejected");

  (void)memcpy(trailing_record, two_point_fixture, sizeof(two_point_fixture));
  trailing_record[sizeof(two_point_fixture)] = 0U;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_unpack(trailing_record, sizeof(trailing_record),
      ML3_CALIBRATION_MAGIC, UINT32_C(0x89ABCDEF), &decoded,
      decoded_points, 2U, &sequence), "unpack accepts larger containing capacity");
}

typedef enum { OP_READ, OP_WRITE } operation_kind_t;
typedef struct {
  operation_kind_t kind;
  uint8_t slot;
  size_t offset;
  size_t length;
  uint8_t first_bytes[4];
} storage_operation_t;

typedef struct {
  uint8_t slots[2][128];
  storage_operation_t operations[128];
  size_t operation_count;
  size_t read_calls;
  size_t write_calls;
  size_t fail_read_call;
  size_t fail_write_call;
  int apply_failed_write;
  size_t corrupt_write_call;
  size_t partial_write_call;
  size_t partial_write_length;
} fake_storage_t;

static bool fake_read(void* context, uint8_t slot, size_t offset,
                      uint8_t* destination, size_t length) {
  fake_storage_t* fake = (fake_storage_t*)context;
  storage_operation_t* operation;
  if (fake->operation_count >= 128U) {
    ++failures;
    return false;
  }
  operation = &fake->operations[fake->operation_count++];
  operation->kind = OP_READ;
  operation->slot = slot;
  operation->offset = offset;
  operation->length = length;
  ++fake->read_calls;
  if (fake->read_calls == fake->fail_read_call) {
    return false;
  }
  (void)memcpy(destination, &fake->slots[slot][offset], length);
  return true;
}

static bool fake_write(void* context, uint8_t slot, size_t offset,
                       const uint8_t* source, size_t length) {
  fake_storage_t* fake = (fake_storage_t*)context;
  storage_operation_t* operation;
  int fail;
  size_t applied_length = length;
  size_t byte_index;
  if (fake->operation_count >= 128U) {
    ++failures;
    return false;
  }
  operation = &fake->operations[fake->operation_count++];
  operation->kind = OP_WRITE;
  operation->slot = slot;
  operation->offset = offset;
  operation->length = length;
  (void)memset(operation->first_bytes, 0, sizeof(operation->first_bytes));
  for (byte_index = 0U; byte_index < length && byte_index < 4U; ++byte_index) {
    operation->first_bytes[byte_index] = source[byte_index];
  }
  ++fake->write_calls;
  fail = fake->write_calls == fake->fail_write_call;
  if (fake->write_calls == fake->partial_write_call) {
    applied_length = fake->partial_write_length < length
      ? fake->partial_write_length : length;
    fail = 1;
  }
  if (!fail || fake->apply_failed_write) {
    (void)memcpy(&fake->slots[slot][offset], source, applied_length);
    if (fake->write_calls == fake->corrupt_write_call && applied_length > 0U) {
      fake->slots[slot][offset] ^= 0x80U;
    }
  }
  return fail ? false : true;
}

static ml3_calibration_storage_port_t fake_port(fake_storage_t* fake) {
  ml3_calibration_storage_port_t port;
  port.context = fake;
  port.slot_capacity = sizeof(fake->slots[0]);
  port.read = fake_read;
  port.write = fake_write;
  return port;
}

static void fake_init(fake_storage_t* fake) {
  (void)memset(fake, 0, sizeof(*fake));
  (void)memset(fake->slots, 0xFF, sizeof(fake->slots));
}

static void put_record(fake_storage_t* fake, uint8_t slot,
                       const ml3_calibration_model_t* model, uint32_t sequence) {
  size_t length = 0U;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_pack(model, UINT32_C(0x89ABCDEF), sequence,
      fake->slots[slot], sizeof(fake->slots[slot]), &length), "fixture slot pack");
}

static ml3_calibration_status_t load_latest(fake_storage_t* fake,
                                             ml3_calibration_model_t* model,
                                             ml3_calibration_point_t* points,
                                             size_t point_capacity,
                                             uint32_t* sequence,
                                             uint8_t* slot) {
  ml3_calibration_storage_port_t port = fake_port(fake);
  uint8_t scratch[256];
  return ml3_calibration_load_latest(&port, ML3_CALIBRATION_MAGIC,
    UINT32_C(0x89ABCDEF), scratch, sizeof(scratch), model, points,
    point_capacity, sequence, slot);
}

static void test_slot_selection(void) {
  fake_storage_t fake;
  ml3_calibration_model_t model = fixture_model(NULL, 0U);
  ml3_calibration_model_t loaded;
  ml3_calibration_point_t points[2];
  uint32_t sequence = 0U;
  uint8_t slot = 9U;

  fake_init(&fake);
  {
    ml3_calibration_storage_port_t port = fake_port(&fake);
    uint8_t scratch[128];
    ml3_calibration_model_t preserved;
    ml3_calibration_model_t snapshot;
    ml3_calibration_point_t preserved_points[2] = { { 1, 2 }, { 3, 4 } };
    ml3_calibration_point_t point_snapshot[2];
    (void)memset(&preserved, 0x3C, sizeof(preserved));
    snapshot = preserved;
    (void)memcpy(point_snapshot, preserved_points, sizeof(point_snapshot));
    sequence = UINT32_C(0xDEADBEEF);
    slot = 9U;
    EXPECT_STATUS(ML3_CALIBRATION_NO_VALID_RECORD,
      ml3_calibration_load_latest(&port, ML3_CALIBRATION_MAGIC,
        UINT32_C(0x89ABCDEF), scratch, sizeof(scratch), &preserved,
        preserved_points, 2U, &sequence, &slot), "both invalid slots are no-valid");
    EXPECT_TRUE(memcmp(&preserved, &snapshot, sizeof(preserved)) == 0,
      "no-valid preserves model");
    EXPECT_TRUE(memcmp(preserved_points, point_snapshot, sizeof(point_snapshot)) == 0,
      "no-valid preserves points");
    EXPECT_U32(UINT32_C(0xDEADBEEF), sequence, "no-valid preserves sequence");
    EXPECT_U32(9U, slot, "no-valid preserves selected slot");

    fake.fail_read_call = 1U;
    fake.read_calls = 0U;
    EXPECT_STATUS(ML3_CALIBRATION_IO,
      ml3_calibration_load_latest(&port, ML3_CALIBRATION_MAGIC,
        UINT32_C(0x89ABCDEF), scratch, sizeof(scratch), &preserved,
        preserved_points, 2U, &sequence, &slot), "slot read I/O is distinct from no-valid");
    EXPECT_TRUE(memcmp(&preserved, &snapshot, sizeof(preserved)) == 0,
      "load I/O preserves model");
    EXPECT_TRUE(memcmp(preserved_points, point_snapshot, sizeof(point_snapshot)) == 0,
      "load I/O preserves points");
    fake.fail_read_call = 0U;
    port.slot_capacity = 129U;
    EXPECT_STATUS(ML3_CALIBRATION_CAPACITY,
      ml3_calibration_load_latest(&port, ML3_CALIBRATION_MAGIC,
        UINT32_C(0x89ABCDEF), scratch, sizeof(scratch), &preserved,
        preserved_points, 2U, &sequence, &slot), "load scratch capacity enforced");
  }
  put_record(&fake, 0U, &model, 10U);
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    load_latest(&fake, &loaded, points, 2U, &sequence, &slot), "one valid slot wins");
  EXPECT_U32(10U, sequence, "one valid sequence");
  EXPECT_U32(0U, slot, "one valid slot index");

  put_record(&fake, 1U, &model, 9U);
  fake.operation_count = 0U;
  fake.read_calls = 0U;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    load_latest(&fake, &loaded, points, 2U, &sequence, &slot), "strictly newer slot wins");
  EXPECT_U32(10U, sequence, "numeric neighboring sequence select");
  EXPECT_U32(0U, slot, "newer slot zero selected");

  put_record(&fake, 0U, &model, UINT32_MAX);
  put_record(&fake, 1U, &model, 0U);
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    load_latest(&fake, &loaded, points, 2U, &sequence, &slot), "rollover select");
  EXPECT_U32(0U, sequence, "zero is newer than max");
  EXPECT_U32(1U, slot, "rollover slot selected");

  put_record(&fake, 0U, &model, 7U);
  put_record(&fake, 1U, &model, 7U);
  EXPECT_STATUS(ML3_CALIBRATION_AMBIGUOUS,
    load_latest(&fake, &loaded, points, 2U, &sequence, &slot),
    "equal sequences fail closed");
  put_record(&fake, 1U, &model, UINT32_C(0x80000007));
  EXPECT_STATUS(ML3_CALIBRATION_AMBIGUOUS,
    load_latest(&fake, &loaded, points, 2U, &sequence, &slot),
    "half-range difference fails closed");
}

static void expect_operation(const fake_storage_t* fake, size_t index,
                             operation_kind_t kind, uint8_t slot,
                             size_t offset, size_t length, const char* label) {
  EXPECT_TRUE(index < fake->operation_count, label);
  if (index < fake->operation_count) {
    EXPECT_TRUE(fake->operations[index].kind == kind, label);
    EXPECT_TRUE(fake->operations[index].slot == slot, label);
    EXPECT_TRUE(fake->operations[index].offset == offset, label);
    EXPECT_TRUE(fake->operations[index].length == length, label);
  }
}

static void test_store_order_and_failure_modes(void) {
  fake_storage_t fake;
  ml3_calibration_model_t model = fixture_model(NULL, 0U);
  ml3_calibration_model_t newer = model;
  ml3_calibration_model_t loaded;
  ml3_calibration_point_t points[2];
  ml3_calibration_storage_port_t port;
  uint8_t scratch[256];
  uint32_t sequence = 99U;
  uint8_t slot = 9U;

  newer.offset_uV = -999;
  fake_init(&fake);
  port = fake_port(&fake);
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC, UINT32_C(0x89ABCDEF),
      &newer, scratch, sizeof(scratch), &sequence, &slot), "first store succeeds");
  EXPECT_U32(0U, sequence, "first sequence is zero");
  EXPECT_U32(0U, slot, "first store targets slot zero");
  EXPECT_TRUE(fake.operation_count >= 8U, "first store records commit operations");
  expect_operation(&fake, 0U, OP_READ, 0U, 0U, 128U, "read slot zero first");
  expect_operation(&fake, 1U, OP_READ, 1U, 0U, 128U, "read slot one second");
  expect_operation(&fake, 2U, OP_WRITE, 0U, 48U, 4U, "invalidate candidate CRC");
  expect_operation(&fake, 3U, OP_READ, 0U, 48U, 4U, "verify invalid CRC marker");
  expect_operation(&fake, 4U, OP_WRITE, 0U, 0U, 48U, "write body after marker verify");
  expect_operation(&fake, 5U, OP_READ, 0U, 0U, 48U, "read back body");
  expect_operation(&fake, 6U, OP_WRITE, 0U, 48U, 4U, "write CRC last");
  expect_operation(&fake, 7U, OP_READ, 0U, 0U, 52U, "read back full record");

  fake.operation_count = 0U;
  fake.read_calls = 0U;
  fake.write_calls = 0U;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    load_latest(&fake, &loaded, points, 2U, &sequence, &slot), "load first stored record");
  EXPECT_U32(0U, sequence, "stored sequence reload");
  EXPECT_TRUE(loaded.offset_uV == -999, "stored candidate reload");

  model.offset_uV = -998;
  port = fake_port(&fake);
  fake.operation_count = 0U;
  fake.read_calls = 0U;
  fake.write_calls = 0U;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC, UINT32_C(0x89ABCDEF),
      &model, scratch, sizeof(scratch), &sequence, &slot), "second store succeeds");
  EXPECT_U32(1U, sequence, "second sequence increments");
  EXPECT_U32(1U, slot, "invalid slot targeted, newest preserved");

  fake_init(&fake);
  put_record(&fake, 0U, &model, 10U);
  put_record(&fake, 1U, &model, 9U);
  port = fake_port(&fake);
  fake.fail_read_call = 2U;
  sequence = 99U;
  slot = 9U;
  EXPECT_STATUS(ML3_CALIBRATION_IO,
    ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC, UINT32_C(0x89ABCDEF),
      &newer, scratch, sizeof(scratch), &sequence, &slot), "read error refuses write");
  EXPECT_TRUE(fake.write_calls == 0U, "read error performs no writes");
  EXPECT_U32(99U, sequence, "read error preserves store sequence output");
  EXPECT_U32(9U, slot, "read error preserves store slot output");

  fake_init(&fake);
  put_record(&fake, 0U, &model, 10U);
  port = fake_port(&fake);
  fake.corrupt_write_call = 2U;
  EXPECT_STATUS(ML3_CALIBRATION_VERIFY_FAILED,
    ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC, UINT32_C(0x89ABCDEF),
      &newer, scratch, sizeof(scratch), &sequence, &slot),
    "silent body corruption rejected before commit");
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    load_latest(&fake, &loaded, points, 2U, &sequence, &slot),
    "previous newest survives body corruption");
  EXPECT_U32(10U, sequence, "old sequence survives corruption");

  fake_init(&fake);
  put_record(&fake, 0U, &model, 10U);
  port = fake_port(&fake);
  fake.fail_write_call = 3U;
  fake.apply_failed_write = 1;
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC, UINT32_C(0x89ABCDEF),
      &newer, scratch, sizeof(scratch), &sequence, &slot),
    "failed CRC callback may be proved successful by readback");
  EXPECT_U32(11U, sequence, "proved CRC write advances sequence");

  fake_init(&fake);
  put_record(&fake, 0U, &model, 10U);
  port = fake_port(&fake);
  fake.fail_write_call = 3U;
  EXPECT_STATUS(ML3_CALIBRATION_IO,
    ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC, UINT32_C(0x89ABCDEF),
      &newer, scratch, sizeof(scratch), &sequence, &slot),
    "failed unapplied CRC callback is an error");
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    load_latest(&fake, &loaded, points, 2U, &sequence, &slot),
    "previous newest survives failed CRC callback");
  EXPECT_U32(10U, sequence, "old sequence survives failed CRC callback");

  fake_init(&fake);
  put_record(&fake, 0U, &model, 10U);
  port = fake_port(&fake);
  fake.corrupt_write_call = 3U;
  EXPECT_STATUS(ML3_CALIBRATION_VERIFY_FAILED,
    ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC, UINT32_C(0x89ABCDEF),
      &newer, scratch, sizeof(scratch), &sequence, &slot),
    "silent final CRC corruption fails verification");
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    load_latest(&fake, &loaded, points, 2U, &sequence, &slot),
    "old record selected after silent CRC corruption");
  EXPECT_U32(10U, sequence, "silent CRC corruption cannot commit");
}

static void test_store_selection_and_prevalidation(void) {
  fake_storage_t fake;
  ml3_calibration_point_t candidate_points[2] = { { -20000, 100 }, { 1050000, -250 } };
  ml3_calibration_model_t zero_model = fixture_model(NULL, 0U);
  ml3_calibration_model_t two_model = fixture_model(candidate_points, 2U);
  ml3_calibration_model_t loaded;
  ml3_calibration_point_t loaded_points[2];
  ml3_calibration_storage_port_t port;
  uint8_t scratch[256];
  uint32_t sequence = UINT32_C(0xCAFEBABE);
  uint8_t slot = 8U;

  fake_init(&fake);
  put_record(&fake, 0U, &two_model, 10U);
  put_record(&fake, 1U, &zero_model, 9U);
  port = fake_port(&fake);
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC, UINT32_C(0x89ABCDEF),
      &two_model, scratch, sizeof(scratch), &sequence, &slot),
    "both-valid store succeeds");
  EXPECT_U32(11U, sequence, "both-valid store increments newest");
  EXPECT_U32(1U, slot, "both-valid store overwrites only older slot");
  EXPECT_TRUE(fake.operations[2].offset == 48U && fake.operations[2].length == 4U,
    "different old length CRC invalidated");
  EXPECT_TRUE(fake.operations[4].offset == 64U && fake.operations[4].length == 4U,
    "different candidate length CRC invalidated");
  EXPECT_TRUE(fake.operations[6].offset == 0U && fake.operations[6].length == 64U,
    "body follows both CRC invalidations");

  fake_init(&fake);
  put_record(&fake, 0U, &zero_model, 7U);
  put_record(&fake, 1U, &zero_model, 7U);
  port = fake_port(&fake);
  sequence = UINT32_C(0xCAFEBABE);
  slot = 8U;
  EXPECT_STATUS(ML3_CALIBRATION_AMBIGUOUS,
    ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC, UINT32_C(0x89ABCDEF),
      &zero_model, scratch, sizeof(scratch), &sequence, &slot),
    "ambiguous store fails closed");
  EXPECT_TRUE(fake.write_calls == 0U, "ambiguous store performs zero writes");
  EXPECT_U32(UINT32_C(0xCAFEBABE), sequence, "ambiguous store preserves sequence");
  EXPECT_U32(8U, slot, "ambiguous store preserves slot");

  fake_init(&fake);
  put_record(&fake, 0U, &zero_model, UINT32_MAX);
  put_record(&fake, 1U, &zero_model, UINT32_MAX - 1U);
  port = fake_port(&fake);
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC, UINT32_C(0x89ABCDEF),
      &zero_model, scratch, sizeof(scratch), &sequence, &slot),
    "store sequence rollover");
  EXPECT_U32(0U, sequence, "store rolls max sequence to zero");
  EXPECT_U32(1U, slot, "rollover replaces older slot");
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    load_latest(&fake, &loaded, loaded_points, 2U, &sequence, &slot),
    "rollover store reload");
  EXPECT_U32(0U, sequence, "rollover record selected as newest");

  fake_init(&fake);
  port = fake_port(&fake);
  zero_model.calibration_id = 0U;
  sequence = UINT32_C(0xCAFEBABE);
  slot = 8U;
  EXPECT_STATUS(ML3_CALIBRATION_INVALID_MODEL,
    ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC, UINT32_C(0x89ABCDEF),
      &zero_model, scratch, sizeof(scratch), &sequence, &slot),
    "invalid candidate rejected before storage I/O");
  EXPECT_TRUE(fake.read_calls == 0U && fake.write_calls == 0U,
    "invalid candidate causes no storage I/O");
  EXPECT_U32(UINT32_C(0xCAFEBABE), sequence, "invalid candidate preserves sequence");
  EXPECT_U32(8U, slot, "invalid candidate preserves slot");

  zero_model = fixture_model(NULL, 0U);
  EXPECT_STATUS(ML3_CALIBRATION_CAPACITY,
    ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC, UINT32_C(0x89ABCDEF),
      &zero_model, scratch, sizeof(scratch) - 1U, &sequence, &slot),
    "store scratch capacity enforced");
}

static void test_partial_write_recovery(void) {
  size_t crc_prefix;
  for (crc_prefix = 1U; crc_prefix <= 3U; ++crc_prefix) {
    fake_storage_t fake;
    ml3_calibration_model_t old_model = fixture_model(NULL, 0U);
    ml3_calibration_model_t new_model = old_model;
    ml3_calibration_model_t loaded;
    ml3_calibration_point_t points[2];
    ml3_calibration_storage_port_t port;
    uint8_t scratch[256];
    uint32_t sequence = 0U;
    uint8_t slot = 0U;
    new_model.offset_uV = -700;
    fake_init(&fake);
    put_record(&fake, 0U, &old_model, 30U);
    port = fake_port(&fake);
    fake.partial_write_call = 3U;
    fake.partial_write_length = crc_prefix;
    fake.apply_failed_write = 1;
    EXPECT_STATUS(ML3_CALIBRATION_IO,
      ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC,
        UINT32_C(0x89ABCDEF), &new_model, scratch, sizeof(scratch),
        &sequence, &slot), "partial CRC write rejected");
    EXPECT_STATUS(ML3_CALIBRATION_OK,
      load_latest(&fake, &loaded, points, 2U, &sequence, &slot),
      "reload after partial CRC write");
    EXPECT_U32(30U, sequence, "old newest survives partial CRC write");
  }

  {
    fake_storage_t fake;
    ml3_calibration_model_t old_model = fixture_model(NULL, 0U);
    ml3_calibration_model_t new_model = old_model;
    ml3_calibration_model_t loaded;
    ml3_calibration_point_t points[2];
    ml3_calibration_storage_port_t port;
    uint8_t scratch[256];
    uint32_t sequence = 0U;
    uint8_t slot = 0U;
    new_model.offset_uV = -701;
    fake_init(&fake);
    put_record(&fake, 0U, &old_model, 30U);
    port = fake_port(&fake);
    fake.partial_write_call = 2U;
    fake.partial_write_length = 17U;
    fake.apply_failed_write = 1;
    EXPECT_STATUS(ML3_CALIBRATION_IO,
      ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC,
        UINT32_C(0x89ABCDEF), &new_model, scratch, sizeof(scratch),
        &sequence, &slot), "partial body write rejected");
    EXPECT_STATUS(ML3_CALIBRATION_OK,
      load_latest(&fake, &loaded, points, 2U, &sequence, &slot),
      "reload after partial body write");
    EXPECT_U32(30U, sequence, "old newest survives partial body write");
  }
}

static void test_marker_verification_failures(void) {
  size_t mode;
  for (mode = 0U; mode < 2U; ++mode) {
    fake_storage_t fake;
    ml3_calibration_model_t old_model = fixture_model(NULL, 0U);
    ml3_calibration_model_t new_model = old_model;
    ml3_calibration_model_t loaded;
    ml3_calibration_point_t points[2];
    ml3_calibration_storage_port_t port;
    uint8_t scratch[256];
    uint32_t sequence = UINT32_C(0xA5A5A5A5);
    uint8_t slot = 9U;

    new_model.offset_uV = -650;
    fake_init(&fake);
    put_record(&fake, 0U, &old_model, 40U);
    port = fake_port(&fake);
    if (mode == 0U) {
      fake.corrupt_write_call = 1U;
      EXPECT_STATUS(ML3_CALIBRATION_VERIFY_FAILED,
        ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC,
          UINT32_C(0x89ABCDEF), &new_model, scratch, sizeof(scratch),
          &sequence, &slot), "silent marker corruption is detected");
    } else {
      fake.fail_read_call = 3U;
      EXPECT_STATUS(ML3_CALIBRATION_IO,
        ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC,
          UINT32_C(0x89ABCDEF), &new_model, scratch, sizeof(scratch),
          &sequence, &slot), "marker readback I/O is reported");
    }
    EXPECT_TRUE(fake.write_calls == 1U,
      "marker verification failure prevents body and commit writes");
    EXPECT_U32(UINT32_C(0xA5A5A5A5), sequence,
      "marker verification failure preserves sequence output");
    EXPECT_U32(9U, slot, "marker verification failure preserves slot output");
    fake.fail_read_call = 0U;
    fake.read_calls = 0U;
    EXPECT_STATUS(ML3_CALIBRATION_OK,
      load_latest(&fake, &loaded, points, 2U, &sequence, &slot),
      "old record reloads after marker verification failure");
    EXPECT_U32(40U, sequence,
      "marker verification failure preserves previous newest");
  }
}

typedef struct {
  const char* label;
  size_t fail_read_call;
  size_t fail_write_call;
  size_t partial_write_call;
  size_t partial_write_length;
  size_t corrupt_write_call;
  int apply_failed_write;
  ml3_calibration_status_t expected_status;
  bool committed;
} two_marker_fault_case_t;

static const storage_operation_t* nth_write_operation(
  const fake_storage_t* fake, size_t ordinal) {
  size_t operation_index;
  size_t write_ordinal = 0U;
  for (operation_index = 0U;
       operation_index < fake->operation_count;
       ++operation_index) {
    if (fake->operations[operation_index].kind == OP_WRITE) {
      ++write_ordinal;
      if (write_ordinal == ordinal) {
        return &fake->operations[operation_index];
      }
    }
  }
  return NULL;
}

static void test_two_marker_power_cut_matrix(void) {
  static const two_marker_fault_case_t cases[] = {
    { "old marker write callback failure", 0U, 1U, 0U, 0U, 0U, 0,
      ML3_CALIBRATION_IO, false },
    { "old marker full write then callback failure", 0U, 1U, 0U, 0U, 0U, 1,
      ML3_CALIBRATION_IO, false },
    { "old marker partial write", 0U, 0U, 1U, 2U, 0U, 1,
      ML3_CALIBRATION_IO, false },
    { "old marker silent corruption", 0U, 0U, 0U, 0U, 1U, 0,
      ML3_CALIBRATION_VERIFY_FAILED, false },
    { "old marker readback failure", 3U, 0U, 0U, 0U, 0U, 0,
      ML3_CALIBRATION_IO, false },
    { "candidate marker write callback failure", 0U, 2U, 0U, 0U, 0U, 0,
      ML3_CALIBRATION_IO, false },
    { "candidate marker full write then callback failure", 0U, 2U, 0U, 0U, 0U, 1,
      ML3_CALIBRATION_IO, false },
    { "candidate marker partial write", 0U, 0U, 2U, 2U, 0U, 1,
      ML3_CALIBRATION_IO, false },
    { "candidate marker silent corruption", 0U, 0U, 0U, 0U, 2U, 0,
      ML3_CALIBRATION_VERIFY_FAILED, false },
    { "candidate marker readback failure", 4U, 0U, 0U, 0U, 0U, 0,
      ML3_CALIBRATION_IO, false },
    { "body write callback failure", 0U, 3U, 0U, 0U, 0U, 0,
      ML3_CALIBRATION_IO, false },
    { "body full write then callback failure", 0U, 3U, 0U, 0U, 0U, 1,
      ML3_CALIBRATION_IO, false },
    { "body partial write", 0U, 0U, 3U, 17U, 0U, 1,
      ML3_CALIBRATION_IO, false },
    { "body silent corruption", 0U, 0U, 0U, 0U, 3U, 0,
      ML3_CALIBRATION_VERIFY_FAILED, false },
    { "body readback failure", 5U, 0U, 0U, 0U, 0U, 0,
      ML3_CALIBRATION_IO, false },
    { "final CRC write callback failure", 0U, 4U, 0U, 0U, 0U, 0,
      ML3_CALIBRATION_IO, false },
    { "final CRC one-byte write", 0U, 0U, 4U, 1U, 0U, 1,
      ML3_CALIBRATION_IO, false },
    { "final CRC two-byte write", 0U, 0U, 4U, 2U, 0U, 1,
      ML3_CALIBRATION_IO, false },
    { "final CRC three-byte write", 0U, 0U, 4U, 3U, 0U, 1,
      ML3_CALIBRATION_IO, false },
    { "final CRC silent corruption", 0U, 0U, 0U, 0U, 4U, 0,
      ML3_CALIBRATION_VERIFY_FAILED, false },
    { "committed CRC final readback failure", 6U, 0U, 0U, 0U, 0U, 0,
      ML3_CALIBRATION_IO, true },
    { "committed CRC callback reports failure", 0U, 4U, 0U, 0U, 0U, 1,
      ML3_CALIBRATION_OK, true },
    { "exact committed CRC", 0U, 0U, 0U, 0U, 0U, 0,
      ML3_CALIBRATION_OK, true }
  };
  ml3_calibration_point_t two_points[2] = {
    { -20000, 100 }, { 1050000, -250 }
  };
  size_t case_index;

  for (case_index = 0U; case_index < sizeof(cases) / sizeof(cases[0]);
       ++case_index) {
    const two_marker_fault_case_t* fault = &cases[case_index];
    fake_storage_t fake;
    ml3_calibration_model_t newest = fixture_model(two_points, 2U);
    ml3_calibration_model_t older = fixture_model(NULL, 0U);
    ml3_calibration_model_t candidate = fixture_model(two_points, 2U);
    ml3_calibration_model_t loaded;
    ml3_calibration_point_t loaded_points[2];
    ml3_calibration_storage_port_t port;
    uint8_t newest_snapshot[128];
    uint8_t scratch[256];
    uint32_t committed_sequence = UINT32_C(0xA5A5A5A5);
    uint32_t loaded_sequence = 0U;
    uint8_t committed_slot = 9U;
    uint8_t loaded_slot = 9U;
    size_t operation_index;
    size_t store_write_calls;
    ml3_calibration_status_t status;
    const storage_operation_t* operation;

    candidate.offset_uV = -900;
    fake_init(&fake);
    put_record(&fake, 0U, &newest, 10U);
    put_record(&fake, 1U, &older, 9U);
    (void)memcpy(newest_snapshot, fake.slots[0], sizeof(newest_snapshot));
    port = fake_port(&fake);
    fake.fail_read_call = fault->fail_read_call;
    fake.fail_write_call = fault->fail_write_call;
    fake.partial_write_call = fault->partial_write_call;
    fake.partial_write_length = fault->partial_write_length;
    fake.corrupt_write_call = fault->corrupt_write_call;
    fake.apply_failed_write = fault->apply_failed_write;

    status = ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC,
      UINT32_C(0x89ABCDEF), &candidate, scratch, sizeof(scratch),
      &committed_sequence, &committed_slot);
    EXPECT_STATUS(fault->expected_status, status, fault->label);
    EXPECT_TRUE(memcmp(newest_snapshot, fake.slots[0], sizeof(newest_snapshot)) == 0,
      fault->label);
    for (operation_index = 0U;
         operation_index < fake.operation_count;
         ++operation_index) {
      if (fake.operations[operation_index].kind == OP_WRITE) {
        EXPECT_TRUE(fake.operations[operation_index].slot == 1U, fault->label);
      }
    }
    store_write_calls = fake.write_calls;
    operation = nth_write_operation(&fake, 1U);
    EXPECT_TRUE(operation != NULL && operation->offset == 48U
      && operation->length == 4U, fault->label);
    if (store_write_calls >= 2U) {
      operation = nth_write_operation(&fake, 2U);
      EXPECT_TRUE(operation != NULL && operation->offset == 64U
        && operation->length == 4U, fault->label);
    }
    if (store_write_calls >= 3U) {
      operation = nth_write_operation(&fake, 3U);
      EXPECT_TRUE(operation != NULL && operation->offset == 0U
        && operation->length == 64U, fault->label);
    }
    if (store_write_calls >= 4U) {
      operation = nth_write_operation(&fake, 4U);
      EXPECT_TRUE(operation != NULL && operation->offset == 64U
        && operation->length == 4U, fault->label);
    }
    if (status == ML3_CALIBRATION_OK) {
      EXPECT_U32(11U, committed_sequence, fault->label);
      EXPECT_U32(1U, committed_slot, fault->label);
    } else {
      EXPECT_U32(UINT32_C(0xA5A5A5A5), committed_sequence, fault->label);
      EXPECT_U32(9U, committed_slot, fault->label);
    }

    fake.fail_read_call = 0U;
    fake.fail_write_call = 0U;
    fake.partial_write_call = 0U;
    fake.corrupt_write_call = 0U;
    fake.apply_failed_write = 0;
    fake.read_calls = 0U;
    EXPECT_STATUS(ML3_CALIBRATION_OK,
      load_latest(&fake, &loaded, loaded_points, 2U,
        &loaded_sequence, &loaded_slot), fault->label);
    if (fault->committed) {
      EXPECT_U32(11U, loaded_sequence, fault->label);
      EXPECT_U32(1U, loaded_slot, fault->label);
      expect_model_equals(&candidate, &loaded, fault->label);
    } else {
      EXPECT_U32(10U, loaded_sequence, fault->label);
      EXPECT_U32(0U, loaded_slot, fault->label);
      expect_model_equals(&newest, &loaded, fault->label);
    }
    EXPECT_TRUE(memcmp(newest_snapshot, fake.slots[0], sizeof(newest_snapshot)) == 0,
      fault->label);
  }
}

static void test_power_cut_boundaries(void) {
  size_t cut;
  for (cut = 1U; cut <= 3U; ++cut) {
    fake_storage_t fake;
    ml3_calibration_model_t old_model = fixture_model(NULL, 0U);
    ml3_calibration_model_t new_model = old_model;
    ml3_calibration_model_t loaded;
    ml3_calibration_point_t points[2];
    ml3_calibration_storage_port_t port;
    uint8_t scratch[256];
    uint32_t sequence = 0U;
    uint8_t slot = 0U;
    ml3_calibration_status_t status;

    new_model.offset_uV = -777;
    fake_init(&fake);
    put_record(&fake, 0U, &old_model, 20U);
    port = fake_port(&fake);
    fake.fail_write_call = cut;
    fake.apply_failed_write = 1;
    status = ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC,
      UINT32_C(0x89ABCDEF), &new_model, scratch, sizeof(scratch), &sequence, &slot);
    if (cut < 3U) {
      EXPECT_TRUE(status != ML3_CALIBRATION_OK, "pre-commit power cut reports failure");
    } else {
      EXPECT_STATUS(ML3_CALIBRATION_OK, status, "committed CRC survives callback failure");
    }
    EXPECT_STATUS(ML3_CALIBRATION_OK,
      load_latest(&fake, &loaded, points, 2U, &sequence, &slot),
      "reload succeeds at write boundary");
    if (cut < 3U) {
      EXPECT_U32(20U, sequence, "previous newest remains selected before CRC commit");
      EXPECT_TRUE(loaded.offset_uV == old_model.offset_uV,
        "previous model remains selected before CRC commit");
    } else {
      EXPECT_U32(21U, sequence, "new record selected after CRC commit");
      EXPECT_TRUE(loaded.offset_uV == new_model.offset_uV,
        "new model selected after CRC commit");
    }
  }
}

static void test_zero_crc_cannot_commit_before_final_write(void) {
  fake_storage_t fake;
  ml3_calibration_model_t model = fixture_model(NULL, 0U);
  ml3_calibration_model_t loaded;
  ml3_calibration_point_t points[2];
  ml3_calibration_storage_port_t port;
  uint8_t candidate_record[52];
  uint8_t scratch[256];
  size_t candidate_length = 0U;
  uint32_t sequence = 0U;
  uint8_t slot = 0U;

  EXPECT_STATUS(ML3_CALIBRATION_OK,
    ml3_calibration_pack(&model, UINT32_C(0x89ABCDEF), UINT32_C(0xD9ED3857),
      candidate_record, sizeof(candidate_record), &candidate_length),
    "zero CRC oracle pack");
  EXPECT_U32(0U, ml3_calibration_crc32(candidate_record, candidate_length - 4U),
    "documented candidate has zero CRC");
  EXPECT_TRUE(candidate_record[48] == 0U && candidate_record[49] == 0U
    && candidate_record[50] == 0U && candidate_record[51] == 0U,
    "documented trailing CRC is all zero");

  fake_init(&fake);
  put_record(&fake, 0U, &model, UINT32_C(0xD9ED3856));
  port = fake_port(&fake);
  fake.fail_write_call = 2U;
  fake.apply_failed_write = 1;
  EXPECT_STATUS(ML3_CALIBRATION_IO,
    ml3_calibration_store(&port, ML3_CALIBRATION_MAGIC, UINT32_C(0x89ABCDEF),
      &model, scratch, sizeof(scratch), &sequence, &slot),
    "power cut after zero-CRC candidate body");
  EXPECT_TRUE(fake.operations[2].kind == OP_WRITE
    && fake.operations[2].offset == 48U,
    "zero-CRC invalidation precedes body");
  EXPECT_TRUE(fake.operations[2].first_bytes[0] != 0U
    && fake.operations[2].first_bytes[1] != 0U
    && fake.operations[2].first_bytes[2] != 0U
    && fake.operations[2].first_bytes[3] != 0U,
    "zero-CRC invalidation differs from every candidate CRC byte");
  EXPECT_TRUE(fake.operations[2].first_bytes[0] != 0xFFU
    && fake.operations[2].first_bytes[1] != 0xFFU
    && fake.operations[2].first_bytes[2] != 0xFFU
    && fake.operations[2].first_bytes[3] != 0xFFU,
    "zero-CRC invalidation differs from erased target bytes");
  EXPECT_TRUE(fake.operations[3].kind == OP_READ
    && fake.operations[3].offset == 48U,
    "zero-CRC invalidation is read back before body");
  EXPECT_STATUS(ML3_CALIBRATION_OK,
    load_latest(&fake, &loaded, points, 2U, &sequence, &slot),
    "reload after zero-CRC body power cut");
  EXPECT_U32(UINT32_C(0xD9ED3856), sequence,
    "zero CRC candidate remains uncommitted before final write");
}

int main(void) {
  test_evaluation_oracles();
  test_model_validation_and_extremes();
  test_cancellation_aware_final_sum();
  test_crc_and_records();
  test_record_metadata_and_maximum_count();
  test_slot_selection();
  test_store_order_and_failure_modes();
  test_store_selection_and_prevalidation();
  test_partial_write_recovery();
  test_marker_verification_failures();
  test_two_marker_power_cut_matrix();
  test_power_cut_boundaries();
  test_zero_crc_cannot_commit_before_final_write();

  if (failures != 0) {
    (void)fprintf(stderr, "%d calibration test(s) failed\n", failures);
    return 1;
  }
  (void)puts("ml3 calibration: OK");
  return 0;
}
