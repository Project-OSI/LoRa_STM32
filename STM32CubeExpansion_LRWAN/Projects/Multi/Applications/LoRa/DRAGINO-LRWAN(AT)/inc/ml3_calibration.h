#ifndef ML3_CALIBRATION_H
#define ML3_CALIBRATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ML3_CALIBRATION_MAGIC UINT32_C(0x4D4C3343)
#define ML3_CALIBRATION_SCHEMA_VERSION UINT16_C(2)
#define ML3_CALIBRATION_MAX_POINTS UINT8_MAX
#define ML3_CALIBRATION_FIXED_SIZE ((size_t)52U)
#define ML3_CALIBRATION_MAX_RECORD_SIZE ((size_t)2092U)
#define ML3_CALIBRATION_SLOT_COUNT ((uint8_t)2U)

typedef enum {
  ML3_CALIBRATION_OK = 0,
  ML3_CALIBRATION_INVALID_ARGUMENT,
  ML3_CALIBRATION_CAPACITY,
  ML3_CALIBRATION_INVALID_MODEL,
  ML3_CALIBRATION_INVALID_RECORD,
  ML3_CALIBRATION_NO_VALID_RECORD,
  ML3_CALIBRATION_AMBIGUOUS,
  ML3_CALIBRATION_IO,
  ML3_CALIBRATION_OVERFLOW,
  ML3_CALIBRATION_VERIFY_FAILED
} ml3_calibration_status_t;

typedef struct {
  int32_t input_uV;
  int32_t correction_uV;
} ml3_calibration_point_t;

typedef struct {
  int32_t offset_uV;
  int32_t offset_tempco_uV_per_C;
  int32_t gain_ppm;
  int32_t gain_tempco_ppm_per_C;
  int32_t common_mode_ppm;
  int32_t cm_ref_uV;
  int32_t v5_divider_ppm;
  int16_t reference_temp_centiC;
  uint8_t calibration_id;
  uint8_t point_count;
  const ml3_calibration_point_t* points;
} ml3_calibration_model_t;

typedef bool (*ml3_calibration_storage_read_fn)(
  void* context,
  uint8_t slot,
  size_t offset,
  uint8_t* destination,
  size_t length);

typedef bool (*ml3_calibration_storage_write_fn)(
  void* context,
  uint8_t slot,
  size_t offset,
  const uint8_t* source,
  size_t length);

typedef struct {
  void* context;
  size_t slot_capacity;
  ml3_calibration_storage_read_fn read;
  ml3_calibration_storage_write_fn write;
} ml3_calibration_storage_port_t;

size_t ml3_calibration_record_size(uint8_t point_count);

uint32_t ml3_calibration_crc32(const uint8_t* bytes, size_t length);

ml3_calibration_status_t ml3_calibration_apply(
  const ml3_calibration_model_t* model,
  int64_t raw_uV,
  int64_t common_mode_uV,
  int32_t die_temp_centiC,
  int64_t* corrected_uV);

ml3_calibration_status_t ml3_calibration_pack(
  const ml3_calibration_model_t* model,
  uint32_t device_hash,
  uint32_t sequence,
  uint8_t* destination,
  size_t destination_capacity,
  size_t* record_length);

ml3_calibration_status_t ml3_calibration_unpack(
  const uint8_t* record,
  size_t record_capacity,
  uint32_t expected_magic,
  uint32_t expected_device_hash,
  ml3_calibration_model_t* model,
  ml3_calibration_point_t* point_storage,
  size_t point_capacity,
  uint32_t* sequence);

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
  uint8_t* selected_slot);

ml3_calibration_status_t ml3_calibration_store(
  const ml3_calibration_storage_port_t* port,
  uint32_t expected_magic,
  uint32_t expected_device_hash,
  const ml3_calibration_model_t* candidate,
  uint8_t* scratch,
  size_t scratch_capacity,
  uint32_t* committed_sequence,
  uint8_t* committed_slot);

#ifdef __cplusplus
}
#endif

#endif /* ML3_CALIBRATION_H */
