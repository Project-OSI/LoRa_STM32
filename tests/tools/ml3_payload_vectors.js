#!/usr/bin/env node
'use strict';

const fs = require('node:fs');
const path = require('node:path');

const ROUTINE_LENGTH = 25;
const DIAGNOSTIC_HEADER_LENGTH = 30;
const CYCLES_PER_PART = 4;
const MAX_CYCLES = 8;
const RAW_MAX = 65520;
const CORE_ADC_FAULT_MASK = 0x000f;
const FIXED_INVALIDATING_MASK = 0x027f;
const CAL_INVALID_FLAG = 0x2000;
const THERM_FAULT_FLAG = 0x8000;
const QUALITY = Object.freeze({VALID: 0, DEGRADED: 1, INVALID: 2});
const FLAG_NAMES = Object.freeze([
  'ML3_ADC_INIT',
  'ML3_ADC_CAL',
  'ML3_ADC_TIMEOUT',
  'ML3_ADC_OVERRUN',
  'ML3_HI_OVER',
  'ML3_LOW_RAIL_CLIPPED',
  'ML3_DIFF_RANGE',
  'ML3_CM_RANGE',
  'ML3_VREF_DRIFT',
  'ML3_V5_LOW',
  'ML3_V5_HIGH',
  'ML3_NOISE_HIGH',
  'ML3_WARMUP_DRIFT',
  'ML3_CAL_INVALID',
  'ML3_TEMP_RANGE',
  'ML3_THERM_FAULT'
]);

function fail(message) {
  throw new Error(message);
}

function isObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function requireObject(value, context) {
  if (!isObject(value)) {
    fail(`${context}: expected object`);
  }
  return value;
}

function requireArray(value, context) {
  if (!Array.isArray(value)) {
    fail(`${context}: expected array`);
  }
  return value;
}

function requireString(value, context) {
  if (typeof value !== 'string' || value.length === 0) {
    fail(`${context}: expected non-empty string`);
  }
  return value;
}

function requireInteger(value, minimum, maximum, context) {
  if (!Number.isSafeInteger(value) || value < minimum || value > maximum) {
    fail(`${context}: expected safe integer in ${minimum}..${maximum}`);
  }
  return value;
}

function requireNullableInteger(value, minimum, maximum, context) {
  return value === null ? null : requireInteger(value, minimum, maximum, context);
}

function requireKeys(object, required, allowed, context) {
  for (const key of required) {
    if (!Object.hasOwn(object, key)) {
      fail(`${context}: missing ${key}`);
    }
  }
  for (const key of Object.keys(object)) {
    if (!allowed.includes(key)) {
      fail(`${context}: unknown field ${key}`);
    }
  }
}

function parseHex(hex, context) {
  requireString(hex, context);
  if ((hex.length & 1) !== 0 || !/^[0-9a-f]+$/.test(hex)) {
    fail(`${context}: expected canonical even-length hexadecimal`);
  }
  return Uint8Array.from(Buffer.from(hex, 'hex'));
}

function bytesToHex(bytes) {
  return Buffer.from(bytes).toString('hex');
}

function writeU16(bytes, offset, value) {
  bytes[offset] = (value >>> 8) & 0xff;
  bytes[offset + 1] = value & 0xff;
}

function writeI16(bytes, offset, value) {
  writeU16(bytes, offset, value & 0xffff);
}

function writeU32(bytes, offset, value) {
  bytes[offset] = (value >>> 24) & 0xff;
  bytes[offset + 1] = (value >>> 16) & 0xff;
  bytes[offset + 2] = (value >>> 8) & 0xff;
  bytes[offset + 3] = value & 0xff;
}

function readU16(bytes, offset) {
  return (bytes[offset] * 256) + bytes[offset + 1];
}

function readI16(bytes, offset) {
  const value = readU16(bytes, offset);
  return value >= 0x8000 ? value - 0x10000 : value;
}

function readU32(bytes, offset) {
  return ((bytes[offset] * 0x1000000) +
    (bytes[offset + 1] * 0x10000) +
    (bytes[offset + 2] * 0x100) +
    bytes[offset + 3]);
}

function trunc(value, divisor) {
  return Math.trunc(value / divisor);
}

function encodeSigned(value, divisor, sentinel, context) {
  if (value === null) {
    return sentinel;
  }
  const encoded = trunc(value, divisor);
  if (encoded < -32768 || encoded > 32767 || (encoded & 0xffff) === sentinel) {
    fail(`${context}: signed value is not encodable without sentinel collision`);
  }
  return encoded;
}

function encodeUnsigned(value, divisor, context) {
  if (value === null) {
    return 0xffff;
  }
  if (value < 0) {
    fail(`${context}: unsigned source cannot be negative`);
  }
  const encoded = trunc(value, divisor);
  if (encoded < 0 || encoded >= 0xffff) {
    fail(`${context}: unsigned value is not encodable`);
  }
  return encoded;
}

function qualityByte(state, cycles) {
  return (QUALITY[state] << 6) | cycles;
}

function validateQuality(flags, state, cycles, context) {
  if (!Object.hasOwn(QUALITY, state)) {
    fail(`${context}: unknown quality state ${state}`);
  }
  requireInteger(cycles, 0, MAX_CYCLES, `${context}.valid_cycle_count`);
  if (((flags & FIXED_INVALIDATING_MASK) !== 0 || cycles < 3) && state !== 'INVALID') {
    fail(`${context}: invalidating condition requires INVALID quality`);
  }
  if (state === 'VALID' && flags !== 0) {
    fail(`${context}: VALID quality cannot carry flags`);
  }
}

function validateRoutineInput(input, context) {
  requireObject(input, context);
  const keys = [
    'status_flags', 'sequence', 'corrected_diff_uv',
    'mean_hi_uncalibrated_uv', 'mean_lo_uncalibrated_uv', 'vdda_uv', 'v5_uv',
    'noise_uv', 'die_temperature_millic', 'soil_temperature_millic',
    'quality_state', 'valid_cycle_count', 'calibration_id'
  ];
  requireKeys(input, keys, keys, context);
  requireInteger(input.status_flags, 0, 0xffff, `${context}.status_flags`);
  requireInteger(input.sequence, 0, 0xffff, `${context}.sequence`);
  for (const field of [
    'corrected_diff_uv', 'mean_hi_uncalibrated_uv', 'mean_lo_uncalibrated_uv',
    'vdda_uv', 'v5_uv', 'noise_uv', 'die_temperature_millic',
    'soil_temperature_millic'
  ]) {
    requireNullableInteger(
      input[field],
      Number.MIN_SAFE_INTEGER,
      Number.MAX_SAFE_INTEGER,
      `${context}.${field}`);
  }
  requireString(input.quality_state, `${context}.quality_state`);
  requireInteger(input.calibration_id, 0, 0xff, `${context}.calibration_id`);
  validateQuality(
    input.status_flags,
    input.quality_state,
    input.valid_cycle_count,
    context);
}

function effectiveRoutineInput(input) {
  const effective = {...input};
  if ((input.status_flags & CORE_ADC_FAULT_MASK) !== 0) {
    effective.corrected_diff_uv = null;
    effective.mean_hi_uncalibrated_uv = null;
    effective.mean_lo_uncalibrated_uv = null;
    effective.vdda_uv = null;
    effective.v5_uv = null;
    effective.noise_uv = null;
    effective.die_temperature_millic = null;
    effective.soil_temperature_millic = null;
  }
  if (input.quality_state === 'INVALID' ||
      (input.status_flags & CAL_INVALID_FLAG) !== 0) {
    effective.corrected_diff_uv = null;
  }
  if ((input.status_flags & THERM_FAULT_FLAG) !== 0) {
    effective.soil_temperature_millic = null;
  }
  return effective;
}

function buildRoutine(input) {
  validateRoutineInput(input, 'routine input');
  const value = effectiveRoutineInput(input);
  const bytes = new Uint8Array(ROUTINE_LENGTH);
  bytes[0] = 1;
  bytes[1] = 0;
  writeU16(bytes, 2, value.status_flags);
  writeU16(bytes, 4, value.sequence);
  writeI16(bytes, 6, encodeSigned(value.corrected_diff_uv, 100, 0x7fff, 'corrected_diff_uv'));
  writeU16(bytes, 8, encodeUnsigned(value.mean_hi_uncalibrated_uv, 100, 'mean_hi_uncalibrated_uv'));
  writeU16(bytes, 10, encodeUnsigned(value.mean_lo_uncalibrated_uv, 100, 'mean_lo_uncalibrated_uv'));
  writeU16(bytes, 12, encodeUnsigned(value.vdda_uv, 1000, 'vdda_uv'));
  writeU16(bytes, 14, encodeUnsigned(value.v5_uv, 1000, 'v5_uv'));
  writeU16(bytes, 16, encodeUnsigned(value.noise_uv, 1, 'noise_uv'));
  writeI16(bytes, 18, encodeSigned(value.die_temperature_millic, 10, 0x7fff, 'die_temperature_millic'));
  writeI16(bytes, 20, encodeSigned(value.soil_temperature_millic, 10, 0x7fff, 'soil_temperature_millic'));
  bytes[22] = qualityByte(value.quality_state, value.valid_cycle_count);
  bytes[23] = value.calibration_id;
  bytes[24] = 0;
  return bytes;
}

function nullableSigned(bytes, offset, sentinel) {
  return readU16(bytes, offset) === sentinel ? null : readI16(bytes, offset);
}

function nullableUnsigned(bytes, offset, sentinel = 0xffff) {
  const value = readU16(bytes, offset);
  return value === sentinel ? null : value;
}

function decodeNamedFlags(flags) {
  return Object.fromEntries(FLAG_NAMES.map((name, bit) =>
    [name, (flags & (1 << bit)) !== 0]));
}

function decodeRoutine(bytes, context) {
  if (bytes.length !== ROUTINE_LENGTH) {
    fail(`${context}: routine hex length must be ${ROUTINE_LENGTH} bytes`);
  }
  const quality = bytes[22];
  const statusFlags = readU16(bytes, 2);
  const qualityCode = (quality >>> 6) & 0x03;
  const state = Object.keys(QUALITY).find((key) => QUALITY[key] === qualityCode);
  return {
    protocol_version: bytes[0],
    payload_type: bytes[1],
    status_flags: statusFlags,
    status_flags_named: decodeNamedFlags(statusFlags),
    sequence: readU16(bytes, 4),
    corrected_diff_decimv: nullableSigned(bytes, 6, 0x7fff),
    mean_hi_decimv: nullableUnsigned(bytes, 8),
    mean_lo_decimv: nullableUnsigned(bytes, 10),
    vdda_mv: nullableUnsigned(bytes, 12),
    v5_mv: nullableUnsigned(bytes, 14),
    noise_uv: nullableUnsigned(bytes, 16),
    die_temperature_centic: nullableSigned(bytes, 18, 0x7fff),
    soil_temperature_centic: nullableSigned(bytes, 20, 0x7fff),
    quality_state: state === undefined ? `RESERVED_${qualityCode}` : state,
    valid_cycle_count: quality & 0x0f,
    quality_reserved: (quality >>> 4) & 0x03,
    calibration_id: bytes[23],
    reserved: bytes[24]
  };
}

function routineSemanticStatus(bytes) {
  if (bytes.length !== ROUTINE_LENGTH || bytes[0] !== 1 || bytes[1] !== 0 ||
      bytes[24] !== 0) {
    return 'value_out_of_range';
  }
  const decoded = decodeRoutine(bytes, 'routine frame');
  if (decoded.quality_reserved !== 0 ||
      !Object.hasOwn(QUALITY, decoded.quality_state) ||
      decoded.valid_cycle_count > MAX_CYCLES) {
    return 'value_out_of_range';
  }
  const invalidating = (decoded.status_flags & FIXED_INVALIDATING_MASK) !== 0 ||
    decoded.valid_cycle_count < 3;
  if ((invalidating && decoded.quality_state !== 'INVALID') ||
      (decoded.quality_state === 'VALID' && decoded.status_flags !== 0) ||
      ((decoded.quality_state === 'INVALID' ||
        (decoded.status_flags & CAL_INVALID_FLAG) !== 0) &&
       decoded.corrected_diff_decimv !== null) ||
      ((decoded.status_flags & CORE_ADC_FAULT_MASK) !== 0 && [
        decoded.corrected_diff_decimv,
        decoded.mean_hi_decimv,
        decoded.mean_lo_decimv,
        decoded.vdda_mv,
        decoded.v5_mv,
        decoded.noise_uv,
        decoded.die_temperature_centic,
        decoded.soil_temperature_centic
      ].some((field) => field !== null)) ||
      ((decoded.status_flags & THERM_FAULT_FLAG) !== 0 &&
       decoded.soil_temperature_centic !== null)) {
    return 'semantic_inconsistency';
  }
  return 'ok';
}

function validateDiagnosticInput(input, context) {
  requireObject(input, context);
  const keys = [
    'sequence', 'status_flags', 'quality_state', 'valid_cycle_count',
    'reset_cause', 'warmup_ms', 'hardware_revision', 'firmware_build_id',
    'calibration_schema', 'calibration_id', 'vrefint_pre_raw',
    'vrefint_post_raw', 'pa4_pre_raw', 'pa4_post_raw',
    'thermistor_raw_ratio', 'cycles'
  ];
  requireKeys(input, keys, keys, context);
  requireInteger(input.sequence, 0, 0xffff, `${context}.sequence`);
  requireInteger(input.status_flags, 0, 0xffff, `${context}.status_flags`);
  requireString(input.quality_state, `${context}.quality_state`);
  requireInteger(input.reset_cause, 0, 0xffffffff, `${context}.reset_cause`);
  requireInteger(input.warmup_ms, 0, 0xffff, `${context}.warmup_ms`);
  requireInteger(input.hardware_revision, 0, 0xff, `${context}.hardware_revision`);
  requireInteger(input.firmware_build_id, 0, 0xffff, `${context}.firmware_build_id`);
  requireInteger(input.calibration_schema, 0, 0xffff, `${context}.calibration_schema`);
  requireInteger(input.calibration_id, 0, 0xff, `${context}.calibration_id`);
  for (const field of [
    'vrefint_pre_raw', 'vrefint_post_raw', 'pa4_pre_raw', 'pa4_post_raw',
    'thermistor_raw_ratio'
  ]) {
    requireNullableInteger(input[field], 0, RAW_MAX, `${context}.${field}`);
  }
  const cycles = requireArray(input.cycles, `${context}.cycles`);
  if (cycles.length > MAX_CYCLES) {
    fail(`${context}.cycles: at most ${MAX_CYCLES} cycles are allowed`);
  }
  cycles.forEach((cycle, index) => {
    requireObject(cycle, `${context}.cycles[${index}]`);
    requireKeys(cycle, ['h1', 'l1', 'l2', 'h2'], ['h1', 'l1', 'l2', 'h2'], `${context}.cycles[${index}]`);
    for (const field of ['h1', 'l1', 'l2', 'h2']) {
      requireInteger(cycle[field], 0, RAW_MAX, `${context}.cycles[${index}].${field}`);
    }
  });
  validateQuality(
    input.status_flags,
    input.quality_state,
    input.valid_cycle_count,
    context);
  if (input.valid_cycle_count > cycles.length) {
    fail(`${context}: valid cycle count exceeds captured cycles`);
  }
}

function diagnosticPartCount(cycleCount) {
  return 1 + Math.ceil(cycleCount / CYCLES_PER_PART);
}

function rawOrSentinel(value) {
  return value === null ? 0xffff : value;
}

function buildDiagnosticParts(input) {
  validateDiagnosticInput(input, 'diagnostic input');
  const count = diagnosticPartCount(input.cycles.length);
  const parts = [];
  const header = new Uint8Array(DIAGNOSTIC_HEADER_LENGTH);
  header[0] = 1;
  header[1] = 1;
  header[2] = count;
  writeU16(header, 3, input.sequence);
  writeU16(header, 5, input.status_flags);
  header[7] = qualityByte(input.quality_state, input.valid_cycle_count);
  writeU32(header, 8, input.reset_cause);
  writeU16(header, 12, input.warmup_ms);
  header[14] = input.hardware_revision;
  writeU16(header, 15, input.firmware_build_id);
  writeU16(header, 17, input.calibration_schema);
  header[19] = input.calibration_id;
  writeU16(header, 20, rawOrSentinel(input.vrefint_pre_raw));
  writeU16(header, 22, rawOrSentinel(input.vrefint_post_raw));
  writeU16(header, 24, rawOrSentinel(input.pa4_pre_raw));
  writeU16(header, 26, rawOrSentinel(input.pa4_post_raw));
  writeU16(header, 28, rawOrSentinel(input.thermistor_raw_ratio));
  parts.push(header);
  for (let partIndex = 1; partIndex < count; partIndex += 1) {
    const firstCycle = (partIndex - 1) * CYCLES_PER_PART;
    const cycles = input.cycles.slice(firstCycle, firstCycle + CYCLES_PER_PART);
    const part = new Uint8Array(3 + (cycles.length * 8));
    part[0] = 1;
    part[1] = 1;
    part[2] = (partIndex << 4) | count;
    cycles.forEach((cycle, cycleIndex) => {
      const offset = 3 + (cycleIndex * 8);
      writeU16(part, offset, cycle.h1);
      writeU16(part, offset + 2, cycle.l1);
      writeU16(part, offset + 4, cycle.l2);
      writeU16(part, offset + 6, cycle.h2);
    });
    parts.push(part);
  }
  return parts;
}

function decodeDiagnosticPart(bytes, context) {
  if (bytes.length < 3) {
    fail(`${context}: diagnostic part is shorter than its prefix`);
  }
  const partIndex = bytes[2] >>> 4;
  const partCount = bytes[2] & 0x0f;
  const common = {
    protocol_version: bytes[0],
    payload_type: bytes[1],
    part_index: partIndex,
    part_count: partCount
  };
  if (partIndex === 0) {
    if (bytes.length !== DIAGNOSTIC_HEADER_LENGTH) {
      fail(`${context}: diagnostic header length must be ${DIAGNOSTIC_HEADER_LENGTH}`);
    }
    const quality = bytes[7];
    const statusFlags = readU16(bytes, 5);
    const qualityCode = (quality >>> 6) & 0x03;
    const state = Object.keys(QUALITY).find((key) => QUALITY[key] === qualityCode);
    return {
      ...common,
      sequence: readU16(bytes, 3),
      status_flags: statusFlags,
      status_flags_named: decodeNamedFlags(statusFlags),
      quality_state: state === undefined ? `RESERVED_${qualityCode}` : state,
      valid_cycle_count: quality & 0x0f,
      quality_reserved: (quality >>> 4) & 0x03,
      reset_cause: readU32(bytes, 8),
      warmup_ms: readU16(bytes, 12),
      hardware_revision: bytes[14],
      firmware_build_id: readU16(bytes, 15),
      calibration_schema: readU16(bytes, 17),
      calibration_id: bytes[19],
      vrefint_pre_raw: nullableUnsigned(bytes, 20),
      vrefint_post_raw: nullableUnsigned(bytes, 22),
      pa4_pre_raw: nullableUnsigned(bytes, 24),
      pa4_post_raw: nullableUnsigned(bytes, 26),
      thermistor_raw_ratio: nullableUnsigned(bytes, 28)
    };
  }
  if ((bytes.length - 3) % 8 !== 0 || bytes.length > 35) {
    fail(`${context}: diagnostic cycle part has invalid length`);
  }
  const cycles = [];
  for (let offset = 3; offset < bytes.length; offset += 8) {
    cycles.push({
      h1: readU16(bytes, offset),
      l1: readU16(bytes, offset + 2),
      l2: readU16(bytes, offset + 4),
      h2: readU16(bytes, offset + 6)
    });
  }
  return {...common, cycles};
}

function assertDeepEqual(actual, expected, context) {
  if (JSON.stringify(actual) !== JSON.stringify(expected)) {
    fail(`${context}: field-vs-byte mismatch; expected ${JSON.stringify(expected)}, decoded ${JSON.stringify(actual)}`);
  }
}

function validateProtocol(protocol) {
  requireObject(protocol, 'protocol');
  const expected = {
    version: 1,
    byte_order: 'big-endian',
    routine_length: ROUTINE_LENGTH,
    diagnostic_header_length: DIAGNOSTIC_HEADER_LENGTH,
    diagnostic_cycles_per_part: CYCLES_PER_PART,
    max_cycles: MAX_CYCLES,
    raw_sentinel: 'ffff',
    signed_sentinel: '7fff'
  };
  requireKeys(protocol, Object.keys(expected), Object.keys(expected), 'protocol');
  assertDeepEqual(protocol, expected, 'protocol');
}

function validateRoutineVector(vector, ids, context) {
  requireObject(vector, context);
  requireKeys(vector, ['id', 'input', 'expected'], ['id', 'input', 'expected'], context);
  const id = requireString(vector.id, `${context}.id`);
  if (ids.has(id)) {
    fail(`${context}: duplicate vector id ${id}`);
  }
  ids.add(id);
  validateRoutineInput(vector.input, `${context}.input`);
  requireObject(vector.expected, `${context}.expected`);
  requireKeys(vector.expected, ['hex', 'decoded'], ['hex', 'decoded'], `${context}.expected`);
  const bytes = parseHex(vector.expected.hex, `${context}.expected.hex`);
  if (bytes.length !== ROUTINE_LENGTH) {
    fail(`${context}.expected.hex: hex length must be ${ROUTINE_LENGTH} bytes`);
  }
  const built = buildRoutine(vector.input);
  if (bytesToHex(bytes) !== bytesToHex(built)) {
    fail(`${context}: expected bytes do not match input; built ${bytesToHex(built)}`);
  }
  assertDeepEqual(
    decodeRoutine(bytes, `${context}.expected.hex`),
    vector.expected.decoded,
    `${context}.expected.decoded`);
  if (routineSemanticStatus(bytes) !== 'ok') {
    fail(`${context}: valid routine vector is semantically contradictory`);
  }
}

function validateDiagnosticVector(vector, ids, context) {
  requireObject(vector, context);
  requireKeys(vector, ['id', 'input', 'parts'], ['id', 'input', 'parts'], context);
  const id = requireString(vector.id, `${context}.id`);
  if (ids.has(id)) {
    fail(`${context}: duplicate vector id ${id}`);
  }
  ids.add(id);
  validateDiagnosticInput(vector.input, `${context}.input`);
  const expectedParts = buildDiagnosticParts(vector.input);
  const parts = requireArray(vector.parts, `${context}.parts`);
  if (parts.length !== expectedParts.length) {
    fail(`${context}: bad part metadata; expected ${expectedParts.length} parts`);
  }
  parts.forEach((part, index) => {
    requireObject(part, `${context}.parts[${index}]`);
    requireKeys(part, ['expected'], ['expected'], `${context}.parts[${index}]`);
    requireObject(part.expected, `${context}.parts[${index}].expected`);
    requireKeys(part.expected, ['hex', 'decoded'], ['hex', 'decoded'], `${context}.parts[${index}].expected`);
    const bytes = parseHex(part.expected.hex, `${context}.parts[${index}].expected.hex`);
    if (bytes.length !== expectedParts[index].length) {
      fail(`${context}.parts[${index}].expected.hex: hex length must be ${expectedParts[index].length} bytes`);
    }
    if (bytesToHex(bytes) !== bytesToHex(expectedParts[index])) {
      fail(`${context}.parts[${index}]: expected bytes do not match input; built ${bytesToHex(expectedParts[index])}`);
    }
    const decoded = decodeDiagnosticPart(bytes, `${context}.parts[${index}]`);
    if (decoded.part_index !== index || decoded.part_count !== parts.length) {
      fail(`${context}.parts[${index}]: bad part metadata`);
    }
    assertDeepEqual(decoded, part.expected.decoded, `${context}.parts[${index}].expected.decoded`);
  });
}

function validateMalformedVector(vector, ids, context) {
  requireObject(vector, context);
  requireKeys(vector, ['id', 'kind', 'hex', 'decoded', 'expected_status'], ['id', 'kind', 'hex', 'decoded', 'expected_status'], context);
  const id = requireString(vector.id, `${context}.id`);
  if (ids.has(id)) {
    fail(`${context}: duplicate vector id ${id}`);
  }
  ids.add(id);
  if (vector.kind !== 'routine') {
    fail(`${context}.kind: only routine malformed vectors are currently defined`);
  }
  const bytes = parseHex(vector.hex, `${context}.hex`);
  const decoded = decodeRoutine(bytes, `${context}.hex`);
  assertDeepEqual(decoded, vector.decoded, `${context}.decoded`);
  const status = routineSemanticStatus(bytes);
  if (status !== vector.expected_status || status === 'ok') {
    fail(`${context}: expected malformed status ${vector.expected_status}, got ${status}`);
  }
}

function validateDocument(document) {
  requireObject(document, 'document');
  requireKeys(
    document,
    ['schema_version', 'protocol', 'routine', 'diagnostic', 'malformed'],
    ['schema_version', 'protocol', 'routine', 'diagnostic', 'malformed'],
    'document');
  requireInteger(document.schema_version, 1, 1, 'schema_version');
  validateProtocol(document.protocol);
  const ids = new Set();
  requireArray(document.routine, 'routine').forEach((vector, index) =>
    validateRoutineVector(vector, ids, `routine[${index}]`));
  requireArray(document.diagnostic, 'diagnostic').forEach((vector, index) =>
    validateDiagnosticVector(vector, ids, `diagnostic[${index}]`));
  requireArray(document.malformed, 'malformed').forEach((vector, index) =>
    validateMalformedVector(vector, ids, `malformed[${index}]`));
  return document;
}

function loadAndValidate(vectorPath) {
  let document;
  try {
    document = JSON.parse(fs.readFileSync(vectorPath, 'utf8'));
  } catch (error) {
    fail(`${vectorPath}: ${error.message}`);
  }
  return validateDocument(document);
}

function renderDecoderFixtures(document) {
  const fixtures = {
    schema_version: document.schema_version,
    routine: document.routine.map((vector) => ({
      id: vector.id,
      hex: vector.expected.hex,
      decoded: vector.expected.decoded,
      expected_status: 'ok'
    })),
    diagnostic: document.diagnostic.flatMap((vector) =>
      vector.parts.map((part, index) => ({
        id: `${vector.id}_part_${index}`,
        hex: part.expected.hex,
        decoded: part.expected.decoded,
        expected_status: 'ok'
      }))),
    malformed: document.malformed.map((vector) => ({
      id: vector.id,
      hex: vector.hex,
      decoded: vector.decoded,
      expected_status: vector.expected_status
    }))
  };
  return `${JSON.stringify(fixtures, null, 2)}\n`;
}

function cString(value) {
  return JSON.stringify(value);
}

function cInt64(value) {
  return value === null ? 'INT64_C(0)' : `INT64_C(${value})`;
}

function cBool(value) {
  return value === null ? 'false' : 'true';
}

function cQuality(value) {
  return `ML3_QUALITY_STATE_${value}`;
}

function cBytes(hex) {
  const bytes = parseHex(hex, 'generated hex');
  return Array.from(bytes, (value) => `UINT8_C(0x${value.toString(16).padStart(2, '0')})`).join(', ');
}

function renderRoutineInput(input) {
  return `{ .status_flags = UINT16_C(${input.status_flags}), ` +
    `.sequence = UINT16_C(${input.sequence}), ` +
    `.corrected_diff_available = ${cBool(input.corrected_diff_uv)}, ` +
    `.corrected_diff_uv = ${cInt64(input.corrected_diff_uv)}, ` +
    `.mean_hi_available = ${cBool(input.mean_hi_uncalibrated_uv)}, ` +
    `.mean_hi_uncalibrated_uv = ${cInt64(input.mean_hi_uncalibrated_uv)}, ` +
    `.mean_lo_available = ${cBool(input.mean_lo_uncalibrated_uv)}, ` +
    `.mean_lo_uncalibrated_uv = ${cInt64(input.mean_lo_uncalibrated_uv)}, ` +
    `.vdda_available = ${cBool(input.vdda_uv)}, ` +
    `.vdda_uv = ${cInt64(input.vdda_uv)}, ` +
    `.v5_available = ${cBool(input.v5_uv)}, .v5_uv = ${cInt64(input.v5_uv)}, ` +
    `.noise_available = ${cBool(input.noise_uv)}, ` +
    `.noise_uv = ${cInt64(input.noise_uv)}, ` +
    `.die_temperature_available = ${cBool(input.die_temperature_millic)}, ` +
    `.die_temperature_millic = ${cInt64(input.die_temperature_millic)}, ` +
    `.soil_temperature_available = ${cBool(input.soil_temperature_millic)}, ` +
    `.soil_temperature_millic = ${cInt64(input.soil_temperature_millic)}, ` +
    `.quality_state = ${cQuality(input.quality_state)}, ` +
    `.valid_cycle_count = UINT8_C(${input.valid_cycle_count}), ` +
    `.calibration_id = UINT8_C(${input.calibration_id}) }`;
}

function renderDiagnosticInput(input) {
  const cycles = input.cycles.map((cycle) =>
    `{ .h1 = UINT16_C(${cycle.h1}), .l1 = UINT16_C(${cycle.l1}), ` +
    `.l2 = UINT16_C(${cycle.l2}), .h2 = UINT16_C(${cycle.h2}) }`);
  while (cycles.length < MAX_CYCLES) {
    cycles.push('{ .h1 = UINT16_C(0), .l1 = UINT16_C(0), ' +
      '.l2 = UINT16_C(0), .h2 = UINT16_C(0) }');
  }
  const rawFields = [
    ['vrefint_pre_raw', 'vrefint_pre_available'],
    ['vrefint_post_raw', 'vrefint_post_available'],
    ['pa4_pre_raw', 'pa4_pre_available'],
    ['pa4_post_raw', 'pa4_post_available'],
    ['thermistor_raw_ratio', 'thermistor_raw_ratio_available']
  ].map(([field, availableField]) =>
    `.${availableField} = ${cBool(input[field])}, ` +
    `.${field} = UINT16_C(${input[field] ?? 0})`);
  return `{ .sequence = UINT16_C(${input.sequence}), ` +
    `.status_flags = UINT16_C(${input.status_flags}), ` +
    `.quality_state = ${cQuality(input.quality_state)}, ` +
    `.valid_cycle_count = UINT8_C(${input.valid_cycle_count}), ` +
    `.reset_cause = UINT32_C(${input.reset_cause}), ` +
    `.warmup_ms = UINT16_C(${input.warmup_ms}), ` +
    `.hardware_revision = UINT8_C(${input.hardware_revision}), ` +
    `.firmware_build_id = UINT16_C(${input.firmware_build_id}), ` +
    `.calibration_schema = UINT16_C(${input.calibration_schema}), ` +
    `.calibration_id = UINT8_C(${input.calibration_id}), ` +
    `${rawFields.join(', ')}, .cycle_count = UINT8_C(${input.cycles.length}), ` +
    `.cycles = { ${cycles.join(', ')} } }`;
}

function renderCInclude(document) {
  const lines = [
    '/* Generated; do not edit. Source: tests/vectors/ml3_payload_vectors.json. */',
    ''
  ];
  document.routine.forEach((vector, index) => {
    lines.push(`static const uint8_t ml3_vector_routine_bytes_${index}[] = { ${cBytes(vector.expected.hex)} };`);
  });
  lines.push('', 'static const ml3_vector_routine_case_t ml3_vector_routine_cases[] = {');
  document.routine.forEach((vector, index) => {
    lines.push(`  { ${cString(vector.id)}, ${renderRoutineInput(vector.input)}, ml3_vector_routine_bytes_${index}, sizeof(ml3_vector_routine_bytes_${index}), UINT16_C(${vector.expected.decoded.status_flags}), UINT16_C(${vector.expected.decoded.sequence}), ${cQuality(vector.expected.decoded.quality_state)}, UINT8_C(${vector.expected.decoded.valid_cycle_count}) },`);
  });
  lines.push('};', '');

  document.diagnostic.forEach((vector, vectorIndex) => {
    vector.parts.forEach((part, partIndex) => {
      lines.push(`static const uint8_t ml3_vector_diag_${vectorIndex}_part_${partIndex}[] = { ${cBytes(part.expected.hex)} };`);
    });
    lines.push(`static const ml3_vector_expected_part_t ml3_vector_diag_parts_${vectorIndex}[] = {`);
    vector.parts.forEach((part, partIndex) => {
      lines.push(`  { UINT8_C(${part.expected.decoded.part_index}), UINT8_C(${part.expected.decoded.part_count}), ml3_vector_diag_${vectorIndex}_part_${partIndex}, sizeof(ml3_vector_diag_${vectorIndex}_part_${partIndex}) },`);
    });
    lines.push('};');
  });
  lines.push('', 'static const ml3_vector_diagnostic_case_t ml3_vector_diagnostic_cases[] = {');
  document.diagnostic.forEach((vector, index) => {
    lines.push(`  { ${cString(vector.id)}, ${renderDiagnosticInput(vector.input)}, ml3_vector_diag_parts_${index}, sizeof(ml3_vector_diag_parts_${index}) / sizeof(ml3_vector_diag_parts_${index}[0]) },`);
  });
  lines.push('};', '');

  document.malformed.forEach((vector, index) => {
    lines.push(`static const uint8_t ml3_vector_malformed_bytes_${index}[] = { ${cBytes(vector.hex)} };`);
  });
  lines.push('', 'static const ml3_vector_malformed_case_t ml3_vector_malformed_cases[] = {');
  document.malformed.forEach((vector, index) => {
    const status = vector.expected_status === 'semantic_inconsistency' ?
      'ML3_PAYLOAD_ERR_SEMANTIC_INCONSISTENCY' : 'ML3_PAYLOAD_ERR_VALUE_OUT_OF_RANGE';
    lines.push(`  { ${cString(vector.id)}, ml3_vector_malformed_bytes_${index}, sizeof(ml3_vector_malformed_bytes_${index}), ${status} },`);
  });
  lines.push('};', '');
  return `${lines.join('\n')}\n`;
}

function writeCInclude(vectorPath, outputPath) {
  const document = loadAndValidate(vectorPath);
  fs.mkdirSync(path.dirname(outputPath), {recursive: true});
  fs.writeFileSync(outputPath, renderCInclude(document), {encoding: 'utf8', flag: 'w'});
}

function walkFiles(directory) {
  if (!fs.existsSync(directory)) {
    return [];
  }
  return fs.readdirSync(directory, {withFileTypes: true})
    .sort((left, right) => left.name.localeCompare(right.name))
    .flatMap((entry) => {
      const entryPath = path.join(directory, entry.name);
      return entry.isDirectory() ? walkFiles(entryPath) : [entryPath];
    });
}

function expectedPayloadHex(rootDir) {
  const vectorPath = path.join(
    rootDir,
    'tests',
    'vectors',
    'ml3_payload_vectors.json');
  const document = loadAndValidate(vectorPath);
  return new Set([
    ...document.routine.map((vector) => vector.expected.hex),
    ...document.diagnostic.flatMap((vector) =>
      vector.parts.map((part) => part.expected.hex)),
    ...document.malformed.map((vector) => vector.hex)
  ]);
}

function hasPayloadAssignmentContext(source, valueOffset) {
  const assignment = source.slice(0, valueOffset).match(
    /\b([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\])?\s*=\s*$/);
  return assignment !== null && /(payload|frame|vector)/i.test(assignment[1]);
}

function validateSourceContract(rootDir) {
  const hostDir = path.join(rootDir, 'tests', 'host');
  const canonicalHex = expectedPayloadHex(rootDir);
  const candidates = walkFiles(hostDir).filter((filePath) => {
    const name = path.basename(filePath);
    return /\.(c|h|js|sh)$/.test(name);
  });

  for (const filePath of candidates) {
    const source = fs.readFileSync(filePath, 'utf8');
    for (const literal of source.matchAll(/["']([0-9a-f]{50,})["']/gi)) {
      const hex = literal[1].toLowerCase();
      if (canonicalHex.has(hex) ||
          hasPayloadAssignmentContext(source, literal.index)) {
        fail(`${filePath}: expected payload bytes outside vector JSON`);
      }
    }
    for (const initializer of source.matchAll(/\{[^{}]*\}/gs)) {
      const byteLiterals = initializer[0].match(/0x[0-9a-fA-F]{2}(?:U)?\b/g) || [];
      const hex = byteLiterals
        .map((literal) => literal.slice(2, 4).toLowerCase())
        .join('');
      if (canonicalHex.has(hex) ||
          (byteLiterals.length >= ROUTINE_LENGTH &&
           hasPayloadAssignmentContext(source, initializer.index))) {
        fail(`${filePath}: expected payload byte array outside vector JSON`);
      }
    }
  }
  const vectorTestPath = path.join(hostDir, 'ml3_payload_vectors_test.c');
  if (!fs.existsSync(vectorTestPath) ||
      !fs.readFileSync(vectorTestPath, 'utf8').includes(
        '#include "ml3_payload_vectors.generated.inc"')) {
    fail(`${vectorTestPath}: generated vector include contract is missing`);
  }
}

function usage(stream) {
  stream.write(
    'Usage:\n' +
    '  node tests/tools/ml3_payload_vectors.js --check VECTOR_JSON\n' +
    '  node tests/tools/ml3_payload_vectors.js --generate-c VECTOR_JSON OUTPUT_INC\n' +
    '  node tests/tools/ml3_payload_vectors.js --decoder-fixtures VECTOR_JSON\n' +
    '  node tests/tools/ml3_payload_vectors.js --check-source-contract ROOT_DIR\n');
}

function main(argv) {
  const [mode, vectorPath, outputPath, ...extra] = argv;
  if (mode === '--check' && vectorPath && outputPath === undefined && extra.length === 0) {
    const document = loadAndValidate(vectorPath);
    process.stdout.write(
      `validated ${document.routine.length} routine, ${document.diagnostic.length} diagnostic, and ${document.malformed.length} malformed vectors\n`);
    return;
  }
  if (mode === '--generate-c' && vectorPath && outputPath && extra.length === 0) {
    writeCInclude(vectorPath, outputPath);
    return;
  }
  if (mode === '--decoder-fixtures' && vectorPath && outputPath === undefined && extra.length === 0) {
    process.stdout.write(renderDecoderFixtures(loadAndValidate(vectorPath)));
    return;
  }
  if (mode === '--check-source-contract' && vectorPath && outputPath === undefined && extra.length === 0) {
    validateSourceContract(vectorPath);
    process.stdout.write('payload vector source contract: OK\n');
    return;
  }
  usage(process.stderr);
  process.exitCode = 2;
}

module.exports = {
  buildDiagnosticParts,
  buildRoutine,
  decodeDiagnosticPart,
  decodeRoutine,
  loadAndValidate,
  renderCInclude,
  renderDecoderFixtures,
  routineSemanticStatus,
  validateSourceContract,
  validateDocument,
  writeCInclude
};

if (require.main === module) {
  try {
    main(process.argv.slice(2));
  } catch (error) {
    process.stderr.write(`ml3 payload vectors: ${error.message}\n`);
    process.exitCode = 1;
  }
}
