#!/usr/bin/env node
'use strict';

const fs = require('node:fs');
const path = require('node:path');

const ROOT_DIR = path.resolve(__dirname, '..', '..');
const RUNNER_PATH = path.join(ROOT_DIR, 'tests', 'host', 'run_ml3_host_tests.sh');

const categories = [
  {
    name: 'ADC scaling and bounds',
    file: 'ml3_adc_precision_test.c',
    evidence: ['test_compute_vdda_and_channel_uv_bounds', 'test_read_uV_end_to_end'],
    runnerSource: 'PRECISION_TEST',
    runnerBinary: 'ml3_adc_precision_test'
  },
  {
    name: 'ABBA discard, median, and variance',
    file: 'ml3_measurement_task3_test.c',
    evidence: [
      'test_full_state_machine_abba_oracle',
      'test_abba_raw_evidence_counts_and_excludes_discards',
      'test_compute_stats_oracles'
    ],
    runnerSource: 'TASK3_TEST',
    runnerBinary: 'ml3_measurement_task3_test'
  },
  {
    name: 'calibration, CRC, and storage cuts',
    file: 'ml3_calibration_test.c',
    evidence: [
      'test_evaluation_oracles',
      'test_crc_and_records',
      'test_slot_selection',
      'test_two_marker_power_cut_matrix'
    ],
    runnerSource: 'CALIBRATION_TEST',
    runnerBinary: 'ml3_calibration_test'
  },
  {
    name: 'quality boundaries',
    file: 'ml3_quality_test.c',
    evidence: [
      'test_cycle_count_boundaries',
      'test_noise_thresholds_use_strict_boundaries',
      'test_vdda_drift_uses_exact_strict_cross_products'
    ],
    runnerSource: 'QUALITY_TEST',
    runnerBinary: 'ml3_quality_test'
  },
  {
    name: 'thermistor ratio and interpolation',
    file: 'ml3_thermistor_test.c',
    evidence: [
      'test_ratiometric_resistance_conversion',
      'test_table_interpolation_in_both_orders',
      'test_configuration_readiness_fails_closed'
    ],
    runnerSource: 'THERMISTOR_TEST',
    runnerBinary: 'ml3_thermistor_test'
  },
  {
    name: 'payload semantics and length gates',
    file: 'ml3_payload_test.c',
    evidence: [
      'test_routine_sentinels_and_quality_states',
      'test_diagnostic_part_boundaries_and_gates',
      'test_automatic_diagnostic_rate_limit'
    ],
    runnerSource: 'PAYLOAD_TEST',
    runnerBinary: 'ml3_payload_test'
  },
  {
    name: 'shared payload vectors',
    file: 'ml3_payload_vectors_test.c',
    evidence: ['test_routine_vectors', 'test_diagnostic_vectors', 'test_malformed_vectors'],
    runnerSource: 'PAYLOAD_VECTORS_TEST',
    runnerBinary: 'ml3_payload_vectors_test'
  },
  {
    name: 'AT command parser bounds',
    file: 'ml3_at_commands_test.c',
    evidence: ['test_exact_commands', 'test_calibration_record_span', 'test_invalid_arguments'],
    runnerSource: 'AT_COMMANDS_TEST',
    runnerBinary: 'ml3_at_commands_test'
  }
];

function fail(message) {
  process.stderr.write(`ml3 coverage contract: ${message}\n`);
  process.exitCode = 1;
}

function main() {
  const runner = fs.readFileSync(RUNNER_PATH, 'utf8');

  for (const category of categories) {
    const testPath = path.join(ROOT_DIR, 'tests', 'host', category.file);
    if (!fs.existsSync(testPath)) {
      fail(`${category.name}: missing ${category.file}`);
      continue;
    }
    const source = fs.readFileSync(testPath, 'utf8');
    for (const token of category.evidence) {
      const occurrences = source.split(token).length - 1;
      if (occurrences < 2) {
        fail(`${category.name}: evidence ${token} is not defined and invoked`);
      }
    }
    if (!runner.includes(`${category.runnerSource}=`)) {
      fail(`${category.name}: runner does not declare ${category.runnerSource}`);
    }
    if (!runner.includes(
      `ml3_run_isolated_command "$BUILD_DIR/${category.runnerBinary}"`)) {
      fail(`${category.name}: runner does not execute ${category.runnerBinary}`);
    }
  }
  if (process.exitCode === undefined) {
    process.stdout.write(`ml3 coverage contract: ${categories.length} categories OK\n`);
  }
}

main();
