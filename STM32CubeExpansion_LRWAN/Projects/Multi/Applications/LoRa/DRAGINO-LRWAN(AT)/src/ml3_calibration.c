#include "ml3_calibration.h"

#include <limits.h>
#include <string.h>

enum {
  ML3_CALIBRATION_SCHEMA_OFFSET = 4,
  ML3_CALIBRATION_LENGTH_OFFSET = 6,
  ML3_CALIBRATION_OFFSET_OFFSET = 8,
  ML3_CALIBRATION_OFFSET_TEMPCO_OFFSET = 12,
  ML3_CALIBRATION_GAIN_OFFSET = 16,
  ML3_CALIBRATION_GAIN_TEMPCO_OFFSET = 20,
  ML3_CALIBRATION_COMMON_MODE_OFFSET = 24,
  ML3_CALIBRATION_CM_REF_OFFSET = 28,
  ML3_CALIBRATION_V5_DIVIDER_OFFSET = 32,
  ML3_CALIBRATION_REFERENCE_TEMP_OFFSET = 36,
  ML3_CALIBRATION_ID_OFFSET = 38,
  ML3_CALIBRATION_POINT_COUNT_OFFSET = 39,
  ML3_CALIBRATION_POINTS_OFFSET = 40,
  ML3_CALIBRATION_TRAILER_SIZE = 12,
  ML3_CALIBRATION_CRC_SIZE = 4,
  ML3_CALIBRATION_SUM_TERM_COUNT = 6
};

typedef struct {
  bool valid;
  size_t length;
  uint8_t point_count;
  uint32_t sequence;
  uint8_t crc[ML3_CALIBRATION_CRC_SIZE];
} ml3_calibration_record_info_t;

static uint16_t ml3_read_u16_le(const uint8_t* bytes) {
  return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t ml3_read_u32_le(const uint8_t* bytes) {
  return (uint32_t)bytes[0]
    | ((uint32_t)bytes[1] << 8)
    | ((uint32_t)bytes[2] << 16)
    | ((uint32_t)bytes[3] << 24);
}

static int16_t ml3_read_i16_le(const uint8_t* bytes) {
  uint16_t value = ml3_read_u16_le(bytes);
  uint16_t magnitude;
  if (value <= (uint16_t)INT16_MAX) {
    return (int16_t)value;
  }
  magnitude = (uint16_t)(~value) + UINT16_C(1);
  if (magnitude == UINT16_C(0x8000)) {
    return INT16_MIN;
  }
  return (int16_t)(-(int16_t)magnitude);
}

static int32_t ml3_read_i32_le(const uint8_t* bytes) {
  uint32_t value = ml3_read_u32_le(bytes);
  uint32_t magnitude;
  if (value <= (uint32_t)INT32_MAX) {
    return (int32_t)value;
  }
  magnitude = ~value + UINT32_C(1);
  if (magnitude == UINT32_C(0x80000000)) {
    return INT32_MIN;
  }
  return (int32_t)(-(int32_t)magnitude);
}

static void ml3_write_u16_le(uint8_t* bytes, uint16_t value) {
  bytes[0] = (uint8_t)(value & UINT16_C(0x00FF));
  bytes[1] = (uint8_t)(value >> 8);
}

static void ml3_write_u32_le(uint8_t* bytes, uint32_t value) {
  bytes[0] = (uint8_t)(value & UINT32_C(0x000000FF));
  bytes[1] = (uint8_t)((value >> 8) & UINT32_C(0x000000FF));
  bytes[2] = (uint8_t)((value >> 16) & UINT32_C(0x000000FF));
  bytes[3] = (uint8_t)(value >> 24);
}

static void ml3_write_i16_le(uint8_t* bytes, int16_t value) {
  ml3_write_u16_le(bytes, (uint16_t)value);
}

static void ml3_write_i32_le(uint8_t* bytes, int32_t value) {
  ml3_write_u32_le(bytes, (uint32_t)value);
}

static bool ml3_add_i64(int64_t left, int64_t right, int64_t* result) {
  if ((right > 0 && left > INT64_MAX - right)
      || (right < 0 && left < INT64_MIN - right)) {
    return false;
  }
  *result = left + right;
  return true;
}

static bool ml3_sub_i64(int64_t left, int64_t right, int64_t* result) {
  if ((right > 0 && left < INT64_MIN + right)
      || (right < 0 && left > INT64_MAX + right)) {
    return false;
  }
  *result = left - right;
  return true;
}

static bool ml3_add_u64(uint64_t left, uint64_t right, uint64_t* result) {
  if (left > UINT64_MAX - right) {
    return false;
  }
  *result = left + right;
  return true;
}

static bool ml3_mul_u64(uint64_t left, uint64_t right, uint64_t* result) {
  if (left != 0U && right > UINT64_MAX / left) {
    return false;
  }
  *result = left * right;
  return true;
}

static uint64_t ml3_abs_i64(int64_t value) {
  if (value >= 0) {
    return (uint64_t)value;
  }
  return (uint64_t)(-(value + 1)) + UINT64_C(1);
}

static bool ml3_signed_from_magnitude(uint64_t magnitude, bool negative,
                                      int64_t* result) {
  if (!negative) {
    if (magnitude > (uint64_t)INT64_MAX) {
      return false;
    }
    *result = (int64_t)magnitude;
    return true;
  }
  if (magnitude > (uint64_t)INT64_MAX + UINT64_C(1)) {
    return false;
  }
  if (magnitude == (uint64_t)INT64_MAX + UINT64_C(1)) {
    *result = INT64_MIN;
    return true;
  }
  *result = -(int64_t)magnitude;
  return true;
}

static bool ml3_sum_terms_i64(
  const int64_t terms[ML3_CALIBRATION_SUM_TERM_COUNT], int64_t* result) {
  uint64_t positive[ML3_CALIBRATION_SUM_TERM_COUNT] = { 0U };
  uint64_t negative[ML3_CALIBRATION_SUM_TERM_COUNT] = { 0U };
  uint64_t magnitude = 0U;
  size_t positive_index;
  size_t negative_index;
  bool has_positive = false;
  bool has_negative = false;

  for (positive_index = 0U;
       positive_index < ML3_CALIBRATION_SUM_TERM_COUNT;
       ++positive_index) {
    if (terms[positive_index] >= 0) {
      positive[positive_index] = (uint64_t)terms[positive_index];
    } else {
      negative[positive_index] = ml3_abs_i64(terms[positive_index]);
    }
  }

  /* Cancel opposite signs before accumulating either magnitude. */
  for (positive_index = 0U;
       positive_index < ML3_CALIBRATION_SUM_TERM_COUNT;
       ++positive_index) {
    for (negative_index = 0U;
         negative_index < ML3_CALIBRATION_SUM_TERM_COUNT;
         ++negative_index) {
      uint64_t cancellation = positive[positive_index] < negative[negative_index]
        ? positive[positive_index] : negative[negative_index];
      positive[positive_index] -= cancellation;
      negative[negative_index] -= cancellation;
    }
  }

  for (positive_index = 0U;
       positive_index < ML3_CALIBRATION_SUM_TERM_COUNT;
       ++positive_index) {
    if (positive[positive_index] != 0U) {
      has_positive = true;
      if (!ml3_add_u64(magnitude, positive[positive_index], &magnitude)) {
        return false;
      }
    }
    if (negative[positive_index] != 0U) {
      has_negative = true;
      if (!ml3_add_u64(magnitude, negative[positive_index], &magnitude)) {
        return false;
      }
    }
  }
  if (has_positive && has_negative) {
    return false;
  }
  return ml3_signed_from_magnitude(magnitude, has_negative, result);
}

static bool ml3_mul_div_trunc_i64(int64_t left, int64_t right,
                                  uint64_t divisor, int64_t* result) {
  uint64_t left_magnitude;
  uint64_t right_magnitude;
  uint64_t left_quotient;
  uint64_t left_remainder;
  uint64_t right_quotient;
  uint64_t right_remainder;
  uint64_t quotient_term;
  uint64_t remainder_term;
  uint64_t low_product;
  uint64_t magnitude;
  bool negative;

  if (divisor == 0U || divisor > UINT32_MAX || result == NULL) {
    return false;
  }
  if (left == 0 || right == 0) {
    *result = 0;
    return true;
  }

  negative = (left < 0) != (right < 0);
  left_magnitude = ml3_abs_i64(left);
  right_magnitude = ml3_abs_i64(right);
  left_quotient = left_magnitude / divisor;
  left_remainder = left_magnitude % divisor;
  right_quotient = right_magnitude / divisor;
  right_remainder = right_magnitude % divisor;

  if (!ml3_mul_u64(left_quotient, right_magnitude, &quotient_term)
      || !ml3_mul_u64(left_remainder, right_quotient, &remainder_term)
      || !ml3_mul_u64(left_remainder, right_remainder, &low_product)
      || !ml3_add_u64(quotient_term, remainder_term, &magnitude)
      || !ml3_add_u64(magnitude, low_product / divisor, &magnitude)) {
    return false;
  }
  return ml3_signed_from_magnitude(magnitude, negative, result);
}

static ml3_calibration_status_t ml3_validate_model(
  const ml3_calibration_model_t* model) {
  size_t index;
  int64_t previous_effective;

  if (model == NULL) {
    return ML3_CALIBRATION_INVALID_ARGUMENT;
  }
  if (model->calibration_id == 0U || model->point_count == 1U) {
    return ML3_CALIBRATION_INVALID_MODEL;
  }
  if (model->point_count == 0U) {
    return ML3_CALIBRATION_OK;
  }
  if (model->points == NULL) {
    return ML3_CALIBRATION_INVALID_MODEL;
  }

  previous_effective = (int64_t)model->points[0].input_uV
    + (int64_t)model->points[0].correction_uV;
  for (index = 1U; index < model->point_count; ++index) {
    int64_t effective;
    if (model->points[index].input_uV <= model->points[index - 1U].input_uV) {
      return ML3_CALIBRATION_INVALID_MODEL;
    }
    effective = (int64_t)model->points[index].input_uV
      + (int64_t)model->points[index].correction_uV;
    if (effective < previous_effective) {
      return ML3_CALIBRATION_INVALID_MODEL;
    }
    previous_effective = effective;
  }
  return ML3_CALIBRATION_OK;
}

static ml3_calibration_status_t ml3_piecewise_residual(
  const ml3_calibration_model_t* model, int64_t raw_uV, int64_t* residual_uV) {
  size_t index;

  if (model->point_count == 0U) {
    *residual_uV = 0;
    return ML3_CALIBRATION_OK;
  }
  if (raw_uV <= (int64_t)model->points[0].input_uV) {
    *residual_uV = (int64_t)model->points[0].correction_uV;
    return ML3_CALIBRATION_OK;
  }
  if (raw_uV >= (int64_t)model->points[model->point_count - 1U].input_uV) {
    *residual_uV = (int64_t)model->points[model->point_count - 1U].correction_uV;
    return ML3_CALIBRATION_OK;
  }

  for (index = 1U; index < model->point_count; ++index) {
    const ml3_calibration_point_t* lower = &model->points[index - 1U];
    const ml3_calibration_point_t* upper = &model->points[index];
    if (raw_uV < (int64_t)upper->input_uV) {
      int64_t input_offset;
      int64_t correction_span;
      int64_t interpolated_span;
      int64_t correction;
      uint64_t input_span = (uint64_t)((int64_t)upper->input_uV
        - (int64_t)lower->input_uV);
      input_offset = raw_uV - (int64_t)lower->input_uV;
      correction_span = (int64_t)upper->correction_uV
        - (int64_t)lower->correction_uV;
      if (!ml3_mul_div_trunc_i64(input_offset, correction_span, input_span,
          &interpolated_span)
          || !ml3_add_i64((int64_t)lower->correction_uV, interpolated_span,
            &correction)) {
        return ML3_CALIBRATION_OVERFLOW;
      }
      *residual_uV = correction;
      return ML3_CALIBRATION_OK;
    }
  }
  return ML3_CALIBRATION_INVALID_MODEL;
}

size_t ml3_calibration_record_size(uint8_t point_count) {
  return ML3_CALIBRATION_FIXED_SIZE + ((size_t)point_count * (size_t)8U);
}

uint32_t ml3_calibration_crc32(const uint8_t* bytes, size_t length) {
  uint32_t crc = UINT32_MAX;
  size_t index;

  if (bytes == NULL && length != 0U) {
    return 0U;
  }
  for (index = 0U; index < length; ++index) {
    uint8_t bit;
    crc ^= (uint32_t)bytes[index];
    for (bit = 0U; bit < 8U; ++bit) {
      uint32_t mask = (uint32_t)(0U - (crc & UINT32_C(1)));
      crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & mask);
    }
  }
  return crc ^ UINT32_MAX;
}

ml3_calibration_status_t ml3_calibration_apply(
  const ml3_calibration_model_t* model,
  int64_t raw_uV,
  int64_t common_mode_uV,
  int32_t die_temp_centiC,
  int64_t* corrected_uV) {
  ml3_calibration_status_t status;
  int64_t temperature_delta;
  int64_t offset_temperature = 0;
  int64_t gain_temperature = 0;
  int64_t gain_deviation;
  int64_t gain_correction = 0;
  int64_t common_mode_delta;
  int64_t common_mode_correction = 0;
  int64_t residual;
  int64_t result;
  int64_t terms[ML3_CALIBRATION_SUM_TERM_COUNT];

  if (corrected_uV == NULL) {
    return ML3_CALIBRATION_INVALID_ARGUMENT;
  }
  status = ml3_validate_model(model);
  if (status != ML3_CALIBRATION_OK) {
    return status;
  }

  temperature_delta = (int64_t)die_temp_centiC
    - (int64_t)model->reference_temp_centiC;
  if (model->offset_tempco_uV_per_C != 0
      && !ml3_mul_div_trunc_i64((int64_t)model->offset_tempco_uV_per_C,
        temperature_delta, UINT64_C(100), &offset_temperature)) {
    return ML3_CALIBRATION_OVERFLOW;
  }
  if (model->gain_tempco_ppm_per_C != 0
      && !ml3_mul_div_trunc_i64((int64_t)model->gain_tempco_ppm_per_C,
        temperature_delta, UINT64_C(100), &gain_temperature)) {
    return ML3_CALIBRATION_OVERFLOW;
  }
  if (!ml3_add_i64((int64_t)model->gain_ppm, gain_temperature,
      &gain_deviation)) {
    return ML3_CALIBRATION_OVERFLOW;
  }
  if (gain_deviation != 0
      && !ml3_mul_div_trunc_i64(raw_uV, gain_deviation, UINT64_C(1000000),
        &gain_correction)) {
    return ML3_CALIBRATION_OVERFLOW;
  }
  if (model->common_mode_ppm != 0) {
    if (!ml3_sub_i64(common_mode_uV, (int64_t)model->cm_ref_uV,
        &common_mode_delta)
        || !ml3_mul_div_trunc_i64((int64_t)model->common_mode_ppm,
          common_mode_delta, UINT64_C(1000000), &common_mode_correction)) {
      return ML3_CALIBRATION_OVERFLOW;
    }
  }

  status = ml3_piecewise_residual(model, raw_uV, &residual);
  if (status != ML3_CALIBRATION_OK) {
    return status;
  }
  terms[0] = raw_uV;
  terms[1] = (int64_t)model->offset_uV;
  terms[2] = offset_temperature;
  terms[3] = gain_correction;
  terms[4] = common_mode_correction;
  terms[5] = residual;
  if (!ml3_sum_terms_i64(terms, &result)) {
    return ML3_CALIBRATION_OVERFLOW;
  }
  *corrected_uV = result;
  return ML3_CALIBRATION_OK;
}

static ml3_calibration_status_t ml3_validate_record(
  const uint8_t* record,
  size_t record_capacity,
  uint32_t expected_magic,
  uint32_t expected_device_hash,
  ml3_calibration_record_info_t* info) {
  size_t expected_length;
  size_t point_index;
  size_t device_hash_offset;
  size_t sequence_offset;
  size_t crc_offset;
  uint8_t point_count;
  int64_t previous_effective = 0;

  if (record == NULL || info == NULL || expected_magic == 0U
      || expected_device_hash == 0U || expected_magic != ML3_CALIBRATION_MAGIC) {
    return ML3_CALIBRATION_INVALID_ARGUMENT;
  }
  info->valid = false;
  if (record_capacity < ML3_CALIBRATION_FIXED_SIZE) {
    return ML3_CALIBRATION_INVALID_RECORD;
  }
  point_count = record[ML3_CALIBRATION_POINT_COUNT_OFFSET];
  expected_length = ml3_calibration_record_size(point_count);
  if (ml3_read_u32_le(&record[0]) != expected_magic
      || ml3_read_u16_le(&record[ML3_CALIBRATION_SCHEMA_OFFSET])
        != ML3_CALIBRATION_SCHEMA_VERSION
      || (size_t)ml3_read_u16_le(&record[ML3_CALIBRATION_LENGTH_OFFSET])
        != expected_length
      || expected_length > record_capacity
      || point_count == 1U
      || record[ML3_CALIBRATION_ID_OFFSET] == 0U) {
    return ML3_CALIBRATION_INVALID_RECORD;
  }

  device_hash_offset = ML3_CALIBRATION_POINTS_OFFSET + ((size_t)point_count * 8U);
  sequence_offset = device_hash_offset + 4U;
  crc_offset = sequence_offset + 4U;
  if (ml3_read_u32_le(&record[device_hash_offset]) != expected_device_hash
      || ml3_read_u32_le(&record[crc_offset])
        != ml3_calibration_crc32(record, crc_offset)) {
    return ML3_CALIBRATION_INVALID_RECORD;
  }

  for (point_index = 0U; point_index < point_count; ++point_index) {
    size_t point_offset = ML3_CALIBRATION_POINTS_OFFSET + (point_index * 8U);
    int32_t input = ml3_read_i32_le(&record[point_offset]);
    int32_t correction = ml3_read_i32_le(&record[point_offset + 4U]);
    int64_t effective = (int64_t)input + (int64_t)correction;
    if (point_index != 0U) {
      size_t previous_offset = point_offset - 8U;
      int32_t previous_input = ml3_read_i32_le(&record[previous_offset]);
      if (input <= previous_input || effective < previous_effective) {
        return ML3_CALIBRATION_INVALID_RECORD;
      }
    }
    previous_effective = effective;
  }

  info->valid = true;
  info->length = expected_length;
  info->point_count = point_count;
  info->sequence = ml3_read_u32_le(&record[sequence_offset]);
  (void)memcpy(info->crc, &record[crc_offset], ML3_CALIBRATION_CRC_SIZE);
  return ML3_CALIBRATION_OK;
}

ml3_calibration_status_t ml3_calibration_pack(
  const ml3_calibration_model_t* model,
  uint32_t device_hash,
  uint32_t sequence,
  uint8_t* destination,
  size_t destination_capacity,
  size_t* record_length) {
  ml3_calibration_status_t status;
  size_t length;
  size_t point_index;
  size_t trailer_offset;
  size_t crc_offset;

  if (destination == NULL || record_length == NULL || device_hash == 0U) {
    return ML3_CALIBRATION_INVALID_ARGUMENT;
  }
  status = ml3_validate_model(model);
  if (status != ML3_CALIBRATION_OK) {
    return status;
  }
  length = ml3_calibration_record_size(model->point_count);
  if (destination_capacity < length) {
    return ML3_CALIBRATION_CAPACITY;
  }

  ml3_write_u32_le(&destination[0], ML3_CALIBRATION_MAGIC);
  ml3_write_u16_le(&destination[ML3_CALIBRATION_SCHEMA_OFFSET],
    ML3_CALIBRATION_SCHEMA_VERSION);
  ml3_write_u16_le(&destination[ML3_CALIBRATION_LENGTH_OFFSET], (uint16_t)length);
  ml3_write_i32_le(&destination[ML3_CALIBRATION_OFFSET_OFFSET], model->offset_uV);
  ml3_write_i32_le(&destination[ML3_CALIBRATION_OFFSET_TEMPCO_OFFSET],
    model->offset_tempco_uV_per_C);
  ml3_write_i32_le(&destination[ML3_CALIBRATION_GAIN_OFFSET], model->gain_ppm);
  ml3_write_i32_le(&destination[ML3_CALIBRATION_GAIN_TEMPCO_OFFSET],
    model->gain_tempco_ppm_per_C);
  ml3_write_i32_le(&destination[ML3_CALIBRATION_COMMON_MODE_OFFSET],
    model->common_mode_ppm);
  ml3_write_i32_le(&destination[ML3_CALIBRATION_CM_REF_OFFSET], model->cm_ref_uV);
  ml3_write_i32_le(&destination[ML3_CALIBRATION_V5_DIVIDER_OFFSET],
    model->v5_divider_ppm);
  ml3_write_i16_le(&destination[ML3_CALIBRATION_REFERENCE_TEMP_OFFSET],
    model->reference_temp_centiC);
  destination[ML3_CALIBRATION_ID_OFFSET] = model->calibration_id;
  destination[ML3_CALIBRATION_POINT_COUNT_OFFSET] = model->point_count;
  for (point_index = 0U; point_index < model->point_count; ++point_index) {
    size_t point_offset = ML3_CALIBRATION_POINTS_OFFSET + (point_index * 8U);
    ml3_write_i32_le(&destination[point_offset], model->points[point_index].input_uV);
    ml3_write_i32_le(&destination[point_offset + 4U],
      model->points[point_index].correction_uV);
  }
  trailer_offset = ML3_CALIBRATION_POINTS_OFFSET + ((size_t)model->point_count * 8U);
  ml3_write_u32_le(&destination[trailer_offset], device_hash);
  ml3_write_u32_le(&destination[trailer_offset + 4U], sequence);
  crc_offset = trailer_offset + 8U;
  ml3_write_u32_le(&destination[crc_offset],
    ml3_calibration_crc32(destination, crc_offset));
  *record_length = length;
  return ML3_CALIBRATION_OK;
}

ml3_calibration_status_t ml3_calibration_unpack(
  const uint8_t* record,
  size_t record_capacity,
  uint32_t expected_magic,
  uint32_t expected_device_hash,
  ml3_calibration_model_t* model,
  ml3_calibration_point_t* point_storage,
  size_t point_capacity,
  uint32_t* sequence) {
  ml3_calibration_record_info_t info;
  ml3_calibration_model_t decoded;
  ml3_calibration_status_t status;
  size_t point_index;

  if (model == NULL || sequence == NULL) {
    return ML3_CALIBRATION_INVALID_ARGUMENT;
  }
  status = ml3_validate_record(record, record_capacity, expected_magic,
    expected_device_hash, &info);
  if (status != ML3_CALIBRATION_OK) {
    return status;
  }
  if ((size_t)info.point_count > point_capacity
      || (info.point_count != 0U && point_storage == NULL)) {
    return ML3_CALIBRATION_CAPACITY;
  }

  decoded.offset_uV = ml3_read_i32_le(&record[ML3_CALIBRATION_OFFSET_OFFSET]);
  decoded.offset_tempco_uV_per_C =
    ml3_read_i32_le(&record[ML3_CALIBRATION_OFFSET_TEMPCO_OFFSET]);
  decoded.gain_ppm = ml3_read_i32_le(&record[ML3_CALIBRATION_GAIN_OFFSET]);
  decoded.gain_tempco_ppm_per_C =
    ml3_read_i32_le(&record[ML3_CALIBRATION_GAIN_TEMPCO_OFFSET]);
  decoded.common_mode_ppm =
    ml3_read_i32_le(&record[ML3_CALIBRATION_COMMON_MODE_OFFSET]);
  decoded.cm_ref_uV = ml3_read_i32_le(&record[ML3_CALIBRATION_CM_REF_OFFSET]);
  decoded.v5_divider_ppm =
    ml3_read_i32_le(&record[ML3_CALIBRATION_V5_DIVIDER_OFFSET]);
  decoded.reference_temp_centiC =
    ml3_read_i16_le(&record[ML3_CALIBRATION_REFERENCE_TEMP_OFFSET]);
  decoded.calibration_id = record[ML3_CALIBRATION_ID_OFFSET];
  decoded.point_count = info.point_count;
  decoded.points = info.point_count == 0U ? NULL : point_storage;
  for (point_index = 0U; point_index < info.point_count; ++point_index) {
    size_t point_offset = ML3_CALIBRATION_POINTS_OFFSET + (point_index * 8U);
    point_storage[point_index].input_uV = ml3_read_i32_le(&record[point_offset]);
    point_storage[point_index].correction_uV =
      ml3_read_i32_le(&record[point_offset + 4U]);
  }
  *model = decoded;
  *sequence = info.sequence;
  return ML3_CALIBRATION_OK;
}

static bool ml3_valid_port(const ml3_calibration_storage_port_t* port,
                           bool require_write) {
  return port != NULL
    && port->slot_capacity >= ML3_CALIBRATION_FIXED_SIZE
    && port->read != NULL
    && (!require_write || port->write != NULL);
}

static ml3_calibration_status_t ml3_compare_sequences(uint32_t first,
                                                       uint32_t second,
                                                       bool* first_is_newer) {
  uint32_t delta = first - second;
  if (delta == 0U || delta == UINT32_C(0x80000000)) {
    return ML3_CALIBRATION_AMBIGUOUS;
  }
  *first_is_newer = delta < UINT32_C(0x80000000);
  return ML3_CALIBRATION_OK;
}

static void ml3_build_crc_marker(const uint8_t* existing_crc,
                                 const uint8_t* candidate_crc,
                                 uint8_t* marker) {
  size_t byte_index;
  for (byte_index = 0U; byte_index < ML3_CALIBRATION_CRC_SIZE; ++byte_index) {
    uint16_t trial;
    for (trial = 0U; trial <= UINT8_MAX; ++trial) {
      uint8_t value = (uint8_t)trial;
      if (value != existing_crc[byte_index] && value != candidate_crc[byte_index]) {
        marker[byte_index] = value;
        break;
      }
    }
  }
}

static ml3_calibration_status_t ml3_write_verified_crc_marker(
  const ml3_calibration_storage_port_t* port,
  uint8_t slot,
  size_t crc_offset,
  const uint8_t* existing_crc,
  const uint8_t* candidate_crc,
  uint8_t* readback) {
  uint8_t marker[ML3_CALIBRATION_CRC_SIZE];
  ml3_build_crc_marker(existing_crc, candidate_crc, marker);
  if (!port->write(port->context, slot, crc_offset, marker, sizeof(marker))) {
    return ML3_CALIBRATION_IO;
  }
  if (!port->read(port->context, slot, crc_offset, readback, sizeof(marker))) {
    return ML3_CALIBRATION_IO;
  }
  if (memcmp(marker, readback, sizeof(marker)) != 0) {
    return ML3_CALIBRATION_VERIFY_FAILED;
  }
  return ML3_CALIBRATION_OK;
}

ml3_calibration_status_t ml3_calibration_load_latest(
  const ml3_calibration_storage_port_t* port,
  uint32_t expected_magic,
  uint32_t expected_device_hash,
  uint8_t* record_scratch,
  size_t scratch_capacity,
  ml3_calibration_model_t* model,
  ml3_calibration_point_t* point_storage,
  size_t point_capacity,
  uint32_t* sequence,
  uint8_t* selected_slot) {
  ml3_calibration_record_info_t slot_info[ML3_CALIBRATION_SLOT_COUNT];
  ml3_calibration_status_t status;
  uint8_t slot;
  uint8_t selection;
  uint32_t decoded_sequence;

  if (!ml3_valid_port(port, false) || record_scratch == NULL || model == NULL
      || sequence == NULL || selected_slot == NULL || expected_magic == 0U
      || expected_device_hash == 0U || expected_magic != ML3_CALIBRATION_MAGIC) {
    return ML3_CALIBRATION_INVALID_ARGUMENT;
  }
  if (scratch_capacity < port->slot_capacity) {
    return ML3_CALIBRATION_CAPACITY;
  }

  for (slot = 0U; slot < ML3_CALIBRATION_SLOT_COUNT; ++slot) {
    slot_info[slot].valid = false;
    if (!port->read(port->context, slot, 0U, record_scratch, port->slot_capacity)) {
      return ML3_CALIBRATION_IO;
    }
    status = ml3_validate_record(record_scratch, port->slot_capacity,
      expected_magic, expected_device_hash, &slot_info[slot]);
    if (status != ML3_CALIBRATION_OK) {
      slot_info[slot].valid = false;
    }
  }

  if (!slot_info[0].valid && !slot_info[1].valid) {
    return ML3_CALIBRATION_NO_VALID_RECORD;
  }
  if (slot_info[0].valid && !slot_info[1].valid) {
    selection = 0U;
  } else if (!slot_info[0].valid && slot_info[1].valid) {
    selection = 1U;
  } else {
    bool first_is_newer;
    status = ml3_compare_sequences(slot_info[0].sequence, slot_info[1].sequence,
      &first_is_newer);
    if (status != ML3_CALIBRATION_OK) {
      return status;
    }
    if (first_is_newer) {
      selection = 0U;
    } else {
      selection = 1U;
    }
  }

  if (selection == 0U) {
    if (!port->read(port->context, selection, 0U, record_scratch,
        port->slot_capacity)) {
      return ML3_CALIBRATION_IO;
    }
  }
  status = ml3_calibration_unpack(record_scratch, slot_info[selection].length,
    expected_magic, expected_device_hash, model, point_storage, point_capacity,
    &decoded_sequence);
  if (status != ML3_CALIBRATION_OK) {
    return status;
  }
  *sequence = decoded_sequence;
  *selected_slot = selection;
  return ML3_CALIBRATION_OK;
}

ml3_calibration_status_t ml3_calibration_store(
  const ml3_calibration_storage_port_t* port,
  uint32_t expected_magic,
  uint32_t expected_device_hash,
  const ml3_calibration_model_t* candidate,
  uint8_t* scratch,
  size_t scratch_capacity,
  uint32_t* committed_sequence,
  uint8_t* committed_slot) {
  ml3_calibration_record_info_t slot_info[ML3_CALIBRATION_SLOT_COUNT];
  ml3_calibration_record_info_t committed_info;
  ml3_calibration_status_t status;
  uint8_t* expected_record;
  uint8_t* readback;
  uint8_t slot_candidate_crc[ML3_CALIBRATION_SLOT_COUNT][ML3_CALIBRATION_CRC_SIZE];
  size_t candidate_length;
  size_t candidate_crc_offset;
  size_t old_crc_offset;
  uint32_t next_sequence;
  uint8_t target;
  uint8_t slot;
  bool crc_write_ok;

  if (!ml3_valid_port(port, true) || candidate == NULL || scratch == NULL
      || committed_sequence == NULL || committed_slot == NULL
      || expected_magic == 0U || expected_device_hash == 0U
      || expected_magic != ML3_CALIBRATION_MAGIC
      || port->slot_capacity > SIZE_MAX / 2U) {
    return ML3_CALIBRATION_INVALID_ARGUMENT;
  }
  if (scratch_capacity < port->slot_capacity * 2U) {
    return ML3_CALIBRATION_CAPACITY;
  }

  expected_record = scratch;
  readback = scratch + port->slot_capacity;
  status = ml3_calibration_pack(candidate, expected_device_hash, 0U,
    expected_record, port->slot_capacity, &candidate_length);
  if (status != ML3_CALIBRATION_OK) {
    return status;
  }
  status = ml3_validate_record(expected_record, candidate_length, expected_magic,
    expected_device_hash, &committed_info);
  if (status != ML3_CALIBRATION_OK) {
    return status;
  }
  candidate_crc_offset = candidate_length - ML3_CALIBRATION_CRC_SIZE;

  for (slot = 0U; slot < ML3_CALIBRATION_SLOT_COUNT; ++slot) {
    slot_info[slot].valid = false;
    if (!port->read(port->context, slot, 0U, readback, port->slot_capacity)) {
      return ML3_CALIBRATION_IO;
    }
    (void)memcpy(slot_candidate_crc[slot], &readback[candidate_crc_offset],
      ML3_CALIBRATION_CRC_SIZE);
    status = ml3_validate_record(readback, port->slot_capacity, expected_magic,
      expected_device_hash, &slot_info[slot]);
    if (status != ML3_CALIBRATION_OK) {
      slot_info[slot].valid = false;
    }
  }

  if (!slot_info[0].valid && !slot_info[1].valid) {
    target = 0U;
    next_sequence = 0U;
  } else if (slot_info[0].valid && !slot_info[1].valid) {
    target = 1U;
    next_sequence = slot_info[0].sequence + UINT32_C(1);
  } else if (!slot_info[0].valid && slot_info[1].valid) {
    target = 0U;
    next_sequence = slot_info[1].sequence + UINT32_C(1);
  } else {
    bool first_is_newer;
    status = ml3_compare_sequences(slot_info[0].sequence, slot_info[1].sequence,
      &first_is_newer);
    if (status != ML3_CALIBRATION_OK) {
      return status;
    }
    if (first_is_newer) {
      target = 1U;
      next_sequence = slot_info[0].sequence + UINT32_C(1);
    } else {
      target = 0U;
      next_sequence = slot_info[1].sequence + UINT32_C(1);
    }
  }

  status = ml3_calibration_pack(candidate, expected_device_hash, next_sequence,
    expected_record, port->slot_capacity, &candidate_length);
  if (status != ML3_CALIBRATION_OK) {
    return status;
  }
  candidate_crc_offset = candidate_length - ML3_CALIBRATION_CRC_SIZE;
  if (slot_info[target].valid) {
    old_crc_offset = slot_info[target].length - ML3_CALIBRATION_CRC_SIZE;
    if (old_crc_offset != candidate_crc_offset) {
      status = ml3_write_verified_crc_marker(port, target, old_crc_offset,
        slot_info[target].crc, &expected_record[candidate_crc_offset], readback);
      if (status != ML3_CALIBRATION_OK) {
        return status;
      }
    }
  }
  status = ml3_write_verified_crc_marker(port, target, candidate_crc_offset,
    slot_info[target].valid && old_crc_offset == candidate_crc_offset
      ? slot_info[target].crc : slot_candidate_crc[target],
    &expected_record[candidate_crc_offset], readback);
  if (status != ML3_CALIBRATION_OK) {
    return status;
  }
  if (!port->write(port->context, target, 0U, expected_record,
      candidate_crc_offset)) {
    return ML3_CALIBRATION_IO;
  }
  if (!port->read(port->context, target, 0U, readback, candidate_crc_offset)) {
    return ML3_CALIBRATION_IO;
  }
  if (memcmp(expected_record, readback, candidate_crc_offset) != 0) {
    return ML3_CALIBRATION_VERIFY_FAILED;
  }

  crc_write_ok = port->write(port->context, target, candidate_crc_offset,
    &expected_record[candidate_crc_offset], ML3_CALIBRATION_CRC_SIZE);
  if (!port->read(port->context, target, 0U, readback, candidate_length)) {
    return ML3_CALIBRATION_IO;
  }
  if (memcmp(expected_record, readback, candidate_length) != 0) {
    return crc_write_ok ? ML3_CALIBRATION_VERIFY_FAILED : ML3_CALIBRATION_IO;
  }
  status = ml3_validate_record(readback, candidate_length, expected_magic,
    expected_device_hash, &committed_info);
  if (status != ML3_CALIBRATION_OK) {
    return crc_write_ok ? ML3_CALIBRATION_VERIFY_FAILED : ML3_CALIBRATION_IO;
  }
  *committed_sequence = committed_info.sequence;
  *committed_slot = target;
  return ML3_CALIBRATION_OK;
}
