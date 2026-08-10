# Task 10B Phase 2 configuration plan

**Goal:** Record the Task 10B electrical configuration values without enabling acquisition, service wiring, thermistor support, or transmission.

**Scope:** Change `inc/ml3_config.h` and the existing target integration contract only. Do not change vendor code, the seven protected ML3 modules, BSP service wiring, readiness expressions, or the rail procedure.

## Inputs and decisions

- `ML3_CONFIG_PB5_ACTIVE_LOW` is `1U`, from the active-low vendor control identified in Task 10B brief line 52. Its readiness flag remains zero until Phase 4.
- The zero-ambiguity guard is `2U` mV, from the Task 10B brief line 70 and the Gate 0 §4 macro feed. Its readiness flag remains zero.
- The LO common-mode observation is 5.0–7.2 mV. Apply a stated 2 mV margin: 3–10 mV. This is a derivation from Task 10B brief line 71, not a fresh measurement. Its readiness flag remains zero.
- Warm-up is `1500U` ms, provisional from Task 10B brief line 73. Its readiness flag remains zero.
- The 4.5–5.5 V supply limits, 0.5 divider ratio, and discharge threshold/time are the owner hardware observations in Task 10B brief lines 74–82. They are not bench measurements. The divider comment must say that the fitted equal 1 kΩ divider is required: a floating PA4 appears as a failed rail. Their readiness flags remain zero.

## Deliberately pending

- The PB5 reset/ISP/brownout safety gate remains pending. Reset and ISP are owner-confirmed, while the brief defers brownout to the pre-field-installation checklist.
- Region, data rate, payload limit, and airtime remain pending because the brief requires the gateway's actual configuration rather than a guessed profile.
- There is no configuration seam for the observed 1110 mV signal ceiling. The protected quality module currently hard-codes a 1100 mV limit. Do not change it in this phase; carry the conflict into the Phase 4 activation review.
- All thermistor macros remain zero, as required by the reduced scope.

## Execution

1. Extend `tests/host/ml3_target_integration_contract.sh` with exact expected values for this phase and assertions that the related readiness flags and `ML3_CONFIG_ACQUISITION_READY` remain zero. Run it first and observe RED against the existing all-zero configuration.
2. Update `inc/ml3_config.h` with the cited values and pending/readiness comments above. Do not add an unused signal-ceiling macro.
3. Run the target integration contract, full host suite, default GCC build, BENCH GCC build, and `git diff --check`. Commit the phase with a conventional `feat:` subject and do not push.

## Host-contract boundary

The original host contract classifies every Gate 0 base value as build-only zero. That conflicts with Task 10B Phase 2, which requires the base values while retaining every readiness, acquisition, and deployment gate at zero. The Phase 2 target integration contract pins both parts of the state, and `ml3_contract_test.c` now expects the ten mandated values. `ml3_readiness_cohesion_contract.sh` expects its canonical divider value of `500000U` while preserving the zero readiness check. These contract updates resolve the brief-versus-baseline contradiction without changing the protected measurement, quality, payload, or thermistor logic.
