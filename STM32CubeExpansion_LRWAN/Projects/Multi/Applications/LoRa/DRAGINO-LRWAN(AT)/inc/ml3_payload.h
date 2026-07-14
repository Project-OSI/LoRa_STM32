/* SPDX-License-Identifier: MIT */
#ifndef ML3_PAYLOAD_H
#define ML3_PAYLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ml3_config.h"
#include "ml3_quality.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ML3_PAYLOAD_PROTOCOL_VERSION UINT8_C(1)
#define ML3_PAYLOAD_TYPE_ROUTINE UINT8_C(0)
#define ML3_PAYLOAD_TYPE_DIAGNOSTIC UINT8_C(1)
#define ML3_PAYLOAD_ROUTINE_LENGTH 25U
#define ML3_PAYLOAD_ROUTINE_VERSION_OFFSET 0U
#define ML3_PAYLOAD_ROUTINE_TYPE_OFFSET 1U
#define ML3_PAYLOAD_ROUTINE_FLAGS_OFFSET 2U
#define ML3_PAYLOAD_ROUTINE_SEQUENCE_OFFSET 4U
#define ML3_PAYLOAD_ROUTINE_CORRECTED_OFFSET 6U
#define ML3_PAYLOAD_ROUTINE_MEAN_HI_OFFSET 8U
#define ML3_PAYLOAD_ROUTINE_MEAN_LO_OFFSET 10U
#define ML3_PAYLOAD_ROUTINE_VDDA_OFFSET 12U
#define ML3_PAYLOAD_ROUTINE_V5_OFFSET 14U
#define ML3_PAYLOAD_ROUTINE_NOISE_OFFSET 16U
#define ML3_PAYLOAD_ROUTINE_DIE_TEMP_OFFSET 18U
#define ML3_PAYLOAD_ROUTINE_SOIL_TEMP_OFFSET 20U
#define ML3_PAYLOAD_ROUTINE_QUALITY_OFFSET 22U
#define ML3_PAYLOAD_ROUTINE_CALIBRATION_ID_OFFSET 23U
#define ML3_PAYLOAD_ROUTINE_RESERVED_OFFSET 24U
#define ML3_PAYLOAD_UV_PER_DECI_MV 100U
#define ML3_PAYLOAD_UV_PER_MV 1000U
#define ML3_PAYLOAD_MILLIC_PER_CENTIC 10U
#define ML3_PAYLOAD_DIAGNOSTIC_COMMON_PREFIX_LENGTH 3U
#define ML3_PAYLOAD_DIAGNOSTIC_HEADER_LENGTH 30U
#define ML3_PAYLOAD_DIAGNOSTIC_VERSION_OFFSET 0U
#define ML3_PAYLOAD_DIAGNOSTIC_TYPE_OFFSET 1U
#define ML3_PAYLOAD_DIAGNOSTIC_PART_OFFSET 2U
#define ML3_PAYLOAD_DIAGNOSTIC_SEQUENCE_OFFSET 3U
#define ML3_PAYLOAD_DIAGNOSTIC_FLAGS_OFFSET 5U
#define ML3_PAYLOAD_DIAGNOSTIC_QUALITY_OFFSET 7U
#define ML3_PAYLOAD_DIAGNOSTIC_RESET_CAUSE_OFFSET 8U
#define ML3_PAYLOAD_DIAGNOSTIC_WARMUP_OFFSET 12U
#define ML3_PAYLOAD_DIAGNOSTIC_HARDWARE_REV_OFFSET 14U
#define ML3_PAYLOAD_DIAGNOSTIC_FIRMWARE_BUILD_OFFSET 15U
#define ML3_PAYLOAD_DIAGNOSTIC_CAL_SCHEMA_OFFSET 17U
#define ML3_PAYLOAD_DIAGNOSTIC_CAL_ID_OFFSET 19U
#define ML3_PAYLOAD_DIAGNOSTIC_VREF_PRE_OFFSET 20U
#define ML3_PAYLOAD_DIAGNOSTIC_VREF_POST_OFFSET 22U
#define ML3_PAYLOAD_DIAGNOSTIC_PA4_PRE_OFFSET 24U
#define ML3_PAYLOAD_DIAGNOSTIC_PA4_POST_OFFSET 26U
#define ML3_PAYLOAD_DIAGNOSTIC_THERMISTOR_RAW_OFFSET 28U
#define ML3_PAYLOAD_DIAGNOSTIC_PART_INDEX_SHIFT 4U
#define ML3_PAYLOAD_DIAGNOSTIC_PART_COUNT_MASK UINT8_C(0x0F)
#define ML3_PAYLOAD_DIAGNOSTIC_CYCLES_PER_PART 4U
#define ML3_PAYLOAD_DIAGNOSTIC_CYCLE_LENGTH 8U
#define ML3_PAYLOAD_DIAGNOSTIC_CYCLE_PART_MAX_LENGTH 35U
#define ML3_PAYLOAD_DIAGNOSTIC_MAX_PARTS 3U
#define ML3_PAYLOAD_CORRECTED_SENTINEL UINT16_C(0x7FFF)
#define ML3_PAYLOAD_UNSIGNED_SENTINEL UINT16_C(0xFFFF)
#define ML3_PAYLOAD_TEMPERATURE_SENTINEL UINT16_C(0x7FFF)
#define ML3_PAYLOAD_RAW_SENTINEL UINT16_C(0xFFFF)
#define ML3_PAYLOAD_RAW_CODE_MAX UINT16_C(65520)
#define ML3_PAYLOAD_AUTO_DIAG_INTERVAL_MS UINT32_C(21600000)
#define ML3_PAYLOAD_QUALITY_STATE_SHIFT 6U
#define ML3_PAYLOAD_QUALITY_STATE_MASK UINT8_C(0xC0)
#define ML3_PAYLOAD_QUALITY_CYCLE_MASK UINT8_C(0x0F)
#define ML3_PAYLOAD_MIN_VALID_CYCLES 3U
#define ML3_PAYLOAD_MAX_ABBA_CYCLES ML3_QUALITY_MAX_BURST_CYCLES

#if ML3_CONFIG_MAX_FRMPAYLOAD_READY
#if (ML3_CONFIG_MAX_FRMPAYLOAD_BYTES < ML3_PAYLOAD_ROUTINE_LENGTH) || \
    (ML3_CONFIG_MAX_FRMPAYLOAD_BYTES < ML3_PAYLOAD_DIAGNOSTIC_HEADER_LENGTH) || \
    (ML3_CONFIG_MAX_FRMPAYLOAD_BYTES < \
     ML3_PAYLOAD_DIAGNOSTIC_CYCLE_PART_MAX_LENGTH)
#error "ML3_CONFIG_MAX_FRMPAYLOAD_BYTES is too small for ML3 payload frames"
#endif
#endif

typedef enum {
  ML3_PAYLOAD_OK = 0,
  ML3_PAYLOAD_ERR_INVALID_ARGUMENT = 1,
  ML3_PAYLOAD_ERR_VALUE_OUT_OF_RANGE = 2,
  ML3_PAYLOAD_ERR_OUTPUT_TOO_SMALL = 3,
  ML3_PAYLOAD_ERR_MAX_FRMPAYLOAD_NOT_READY = 4,
  ML3_PAYLOAD_ERR_FRAME_TOO_LARGE = 5,
  ML3_PAYLOAD_ERR_RATE_LIMIT_CAPACITY = 6,
  ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY = 7
} ml3_payload_status_t;

typedef struct {
  uint16_t status_flags;
  uint16_t sequence;
  bool corrected_diff_available;
  int64_t corrected_diff_uv;
  bool mean_hi_available;
  int64_t mean_hi_uncalibrated_uv;
  bool mean_lo_available;
  int64_t mean_lo_uncalibrated_uv;
  bool vdda_available;
  int64_t vdda_uv;
  bool v5_available;
  int64_t v5_uv;
  bool noise_available;
  int64_t noise_uv;
  bool die_temperature_available;
  int64_t die_temperature_millic;
  bool soil_temperature_available;
  int64_t soil_temperature_millic;
  ml3_quality_state_t quality_state;
  uint8_t valid_cycle_count;
  uint8_t calibration_id;
} ml3_payload_routine_t;

typedef struct {
  uint16_t h1;
  uint16_t l1;
  uint16_t l2;
  uint16_t h2;
} ml3_payload_raw_cycle_t;

typedef struct {
  uint16_t sequence;
  uint16_t status_flags;
  ml3_quality_state_t quality_state;
  uint8_t valid_cycle_count;
  uint32_t reset_cause;
  uint16_t warmup_ms;
  uint8_t hardware_revision;
  uint16_t firmware_build_id;
  uint16_t calibration_schema;
  uint8_t calibration_id;
  bool vrefint_pre_available;
  uint16_t vrefint_pre_raw;
  bool vrefint_post_available;
  uint16_t vrefint_post_raw;
  bool pa4_pre_available;
  uint16_t pa4_pre_raw;
  bool pa4_post_available;
  uint16_t pa4_post_raw;
  bool thermistor_raw_ratio_available;
  uint16_t thermistor_raw_ratio;
  uint8_t cycle_count;
  ml3_payload_raw_cycle_t cycles[ML3_PAYLOAD_MAX_ABBA_CYCLES];
} ml3_payload_diagnostic_t;

typedef struct {
  uint32_t signature;
  uint32_t last_queued_ms;
  bool occupied;
} ml3_payload_diag_rate_entry_t;

ml3_payload_status_t ml3_payload_build_routine(
  const ml3_payload_routine_t* input,
  uint8_t* output,
  size_t output_capacity,
  size_t max_frmpayload_bytes,
  size_t* output_length);

ml3_payload_status_t ml3_payload_validate_routine_frame(
  const uint8_t* frame,
  size_t frame_length);

ml3_payload_status_t ml3_payload_diagnostic_part_count(
  uint8_t cycle_count,
  uint8_t* part_count);

ml3_payload_status_t ml3_payload_build_diagnostic_part(
  const ml3_payload_diagnostic_t* input,
  uint8_t part_index,
  uint8_t* output,
  size_t output_capacity,
  size_t max_frmpayload_bytes,
  size_t* output_length);

/*
 * Storage belongs to the caller. Eligibility never mutates it. Call
 * ml3_payload_auto_diag_mark_queued only after the radio queue accepts every
 * diagnostic part. A target persistence adapter is intentionally deferred to
 * Task 10; this module does not read or write nonvolatile storage.
 */
bool ml3_payload_auto_diag_is_eligible(
  const ml3_payload_diag_rate_entry_t* entries,
  size_t entry_capacity,
  uint32_t invalidating_signature,
  uint32_t now_ms);

ml3_payload_status_t ml3_payload_auto_diag_mark_queued(
  ml3_payload_diag_rate_entry_t* entries,
  size_t entry_capacity,
  uint32_t invalidating_signature,
  uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* ML3_PAYLOAD_H */
