#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const childProcess = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const vectorTool = require('../tools/ml3_payload_vectors.js');

const ROOT_DIR = path.resolve(__dirname, '..', '..');
const VECTOR_PATH = path.join(ROOT_DIR, 'tests', 'vectors', 'ml3_payload_vectors.json');
const TOOL_PATH = path.join(ROOT_DIR, 'tests', 'tools', 'ml3_payload_vectors.js');

function expectValidationError(document, pattern) {
  assert.throws(() => vectorTool.validateDocument(document), pattern);
}

function loadVectors() {
  return JSON.parse(fs.readFileSync(VECTOR_PATH, 'utf8'));
}

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

function testValidDocumentAndDeterministicOutputs() {
  const document = loadVectors();
  const validated = vectorTool.validateDocument(document);
  const first = vectorTool.renderDecoderFixtures(validated);
  const second = vectorTool.renderDecoderFixtures(
    vectorTool.validateDocument(loadVectors()));
  const generated = vectorTool.renderCInclude(validated);

  assert.equal(first, second);
  assert.match(first, /"routine_valid_nominal"/);
  assert.match(first, /"diagnostic_8_cycles_part_0"/);
  assert.match(first, /"ML3_ADC_TIMEOUT": true/);
  assert.match(first, /"ML3_THERM_FAULT": true/);
  assert.match(generated, /ml3_vector_routine_cases/);
  assert.match(generated, /ml3_vector_diagnostic_cases/);
}

function testRejectsDuplicateIdsAndMalformedHex() {
  const duplicate = loadVectors();
  duplicate.routine[1].id = duplicate.routine[0].id;
  expectValidationError(duplicate, /duplicate vector id/);

  const malformed = loadVectors();
  malformed.routine[0].expected.hex = '010';
  expectValidationError(malformed, /even-length hexadecimal/);

  const wrongLength = loadVectors();
  wrongLength.routine[0].expected.hex =
    wrongLength.routine[0].expected.hex.slice(0, -2);
  expectValidationError(wrongLength, /hex length/);
}

function testRejectsSchemaAndUnsafeIntegers() {
  const schema = loadVectors();
  schema.schema_version = 2;
  expectValidationError(schema, /schema_version/);

  const unsafe = loadVectors();
  unsafe.routine[0].input.corrected_diff_uv = Number.MAX_SAFE_INTEGER + 1;
  expectValidationError(unsafe, /safe integer/);
}

function testRejectsByteAndFieldMutations() {
  const byteMutation = loadVectors();
  const hex = byteMutation.routine[0].expected.hex;
  byteMutation.routine[0].expected.hex =
    `${hex.slice(0, 8)}ff${hex.slice(10)}`;
  expectValidationError(byteMutation, /field-vs-byte mismatch|bytes do not match input/);

  const fieldMutation = loadVectors();
  fieldMutation.routine[0].expected.decoded.sequence += 1;
  expectValidationError(fieldMutation, /field-vs-byte mismatch/);
}

function testRejectsBadDiagnosticPartMetadata() {
  const document = loadVectors();
  document.diagnostic[3].parts[0].expected.decoded.part_count += 1;
  expectValidationError(document, /part metadata|field-vs-byte mismatch/);
}

function testMalformedStatusCannotBlessContradictoryFrame() {
  const document = loadVectors();
  document.malformed[0].expected_status = 'ok';
  expectValidationError(document, /expected malformed status/);
}

function testCoreAdcFaultVectorsResistNarrowedOracleMask() {
  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'ml3-vector-mutant-'));
  const mutantPath = path.join(tempDir, 'ml3_payload_vectors.js');
  const source = fs.readFileSync(TOOL_PATH, 'utf8');
  const mutant = source.replace(
    'const CORE_ADC_FAULT_MASK = 0x000f;',
    'const CORE_ADC_FAULT_MASK = 0x0004;');

  assert.notEqual(mutant, source, 'core ADC mask mutation was not applied');
  try {
    fs.writeFileSync(mutantPath, mutant);
    const result = childProcess.spawnSync(
      process.execPath,
      [mutantPath, '--check', VECTOR_PATH],
      {encoding: 'utf8'});
    assert.equal(result.status, 1, result.stdout + result.stderr);
    assert.match(
      result.stderr,
      /bytes do not match input|expected malformed status/);
  } finally {
    fs.rmSync(tempDir, {recursive: true, force: true});
  }
}

function testEachCoreAdcFaultPinsSentinelsAndContradictions() {
  const document = loadVectors();
  const sentinelFields = [
    'corrected_diff_decimv',
    'mean_hi_decimv',
    'mean_lo_decimv',
    'vdda_mv',
    'v5_mv',
    'noise_uv',
    'die_temperature_centic',
    'soil_temperature_centic'
  ];
  const retainedContradictionFields = sentinelFields.slice(1);

  for (const flag of [1, 2, 4, 8]) {
    const routine = document.routine.find(
      (vector) => vector.input.status_flags === flag);
    assert.ok(routine, `missing routine core ADC fixture for flag 0x${flag.toString(16)}`);
    for (const field of sentinelFields) {
      assert.equal(routine.expected.decoded[field], null, `${routine.id}.${field}`);
    }

    const malformed = document.malformed.find(
      (vector) => vector.decoded.status_flags === flag);
    assert.ok(malformed, `missing malformed core ADC fixture for flag 0x${flag.toString(16)}`);
    assert.equal(malformed.expected_status, 'semantic_inconsistency');
    assert.equal(malformed.decoded.corrected_diff_decimv, null);
    for (const field of retainedContradictionFields) {
      assert.notEqual(malformed.decoded[field], null, `${malformed.id}.${field}`);
    }
  }
}

function testWritesOnlyRequestedGeneratedFile() {
  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'ml3-vector-tool-'));
  const outputPath = path.join(tempDir, 'vectors.inc');

  try {
    vectorTool.writeCInclude(VECTOR_PATH, outputPath);
    assert.match(fs.readFileSync(outputPath, 'utf8'), /Generated; do not edit/);
    assert.deepEqual(fs.readdirSync(tempDir), ['vectors.inc']);
  } finally {
    fs.rmSync(tempDir, {recursive: true, force: true});
  }
}

function testExpectedBytesExistOnlyInVectorJson() {
  const document = loadVectors();
  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'ml3-vector-source-'));
  const hostDir = path.join(tempDir, 'tests', 'host');
  const vectorsDir = path.join(tempDir, 'tests', 'vectors');
  const duplicatePath = path.join(hostDir, 'ml3_contract_test.c');
  const unrelatedPath = path.join(hostDir, 'ml3_calibration_like_test.c');

  vectorTool.validateSourceContract(ROOT_DIR);
  try {
    fs.mkdirSync(hostDir, {recursive: true});
    fs.mkdirSync(vectorsDir, {recursive: true});
    fs.copyFileSync(VECTOR_PATH, path.join(vectorsDir, 'ml3_payload_vectors.json'));
    fs.writeFileSync(
      path.join(hostDir, 'ml3_payload_vectors_test.c'),
      '#include "ml3_payload_vectors.generated.inc"\n');
    fs.writeFileSync(
      duplicatePath,
      `const char *expected = "${document.routine[0].expected.hex}";\n`);
    assert.throws(
      () => vectorTool.validateSourceContract(tempDir),
      /expected payload bytes outside vector JSON/);

    fs.rmSync(duplicatePath);
    const continuation = document.diagnostic
      .flatMap((vector) => vector.parts)
      .find((part) => part.expected.decoded.part_index > 0);
    fs.writeFileSync(
      duplicatePath,
      `const unsigned char frame[] = {${continuation.expected.hex.match(/../g)
        .map((byte) => `0x${byte}`).join(',')}};\n`);
    assert.throws(
      () => vectorTool.validateSourceContract(tempDir),
      /expected payload byte array outside vector JSON/);

    fs.rmSync(duplicatePath);
    fs.writeFileSync(
      unrelatedPath,
      `const unsigned char calibration_record[] = {${Array.from(
        {length: 52},
        (_, index) => `0x${index.toString(16).padStart(2, '0')}`).join(',')}};\n`);
    assert.doesNotThrow(() => vectorTool.validateSourceContract(tempDir));
  } finally {
    fs.rmSync(tempDir, {recursive: true, force: true});
  }
}

testValidDocumentAndDeterministicOutputs();
testRejectsDuplicateIdsAndMalformedHex();
testRejectsSchemaAndUnsafeIntegers();
testRejectsByteAndFieldMutations();
testRejectsBadDiagnosticPartMetadata();
testMalformedStatusCannotBlessContradictoryFrame();
testCoreAdcFaultVectorsResistNarrowedOracleMask();
testEachCoreAdcFaultPinsSentinelsAndContradictions();
testWritesOnlyRequestedGeneratedFile();
testExpectedBytesExistOnlyInVectorJson();

process.stdout.write('ml3 payload vector tool: OK\n');
