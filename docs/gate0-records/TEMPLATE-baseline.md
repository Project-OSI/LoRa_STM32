# Gate 0 baseline sheet — node ________ / ML3 ________

Copy this file to `docs/gate0-records/<ml3-serial>-<node-eui>/baseline.md` and fill every field. "n/a" needs a reason. Runbook: `docs/ml3-gate0-bench-runbook.md` §2; plan §2.1 governs on conflict.

## Identity

| Field | Value |
|---|---|
| Date / operator | |
| LSN50v2 PCB revision (silkscreen) | |
| Board photos taken (front/back) | yes / no |
| STM32 top marking (lot/date) | |
| STM32 REV_ID via ST-Link (`STM32_Programmer_CLI -c port=SWD`) | |
| Flashed image: branch commit | |
| Flashed image: `lora.bin` sha256 | |
| `AT+ML3VER=?` output (verbatim) | |
| Dragino upstream baseline commit | `aaa4b50` |
| Node DevEUI | |
| ML3 serial number | |
| ML3 cable length / calculated loop resistance (state gauge) | |

## LoRaWAN (fills `ML3_CONFIG_LORA_*`)

| Field | Value |
|---|---|
| Region | |
| ADR on? | |
| DR/SF observed on bench | |
| Worst assignable DR (floor) | |
| Max FRMPayload at that floor | |

## Pin audit (resolves D3; fills `ML3_CONFIG_THERMISTOR_*`)

| Terminal | MCU pin | Current use | Free? |
|---|---|---|---|
| | PA0 (10) | | |
| | PA1 (11) | | |
| | PA4 (14) | | |
| | PB5 (41) | | |
| Spare ADC pin chosen for thermistor: | | | |
| Spare GPIO chosen for excitation: | | | |

Continuity-verified terminal→pin? yes / no

## PB5 / +5 V rail safety (§2.8 — pass/fail; fills `ML3_CONFIG_PB5_ACTIVE_LOW`, `_PB5_RESET_ISP_BROWNOUT_SAFE`)

Sensors DISCONNECTED for this whole block.

| Test | Method used | Result | Scope capture file |
|---|---|---|---|
| Polarity (trace) | | active-low / active-high | |
| Polarity (live poll observation) | | matches trace? | |
| Pull on enable node (value + direction, or NONE) | | | |
| Reset hold ≥10 s: rail stays off | | pass / FAIL | |
| ISP/bootloader ≥2 min: rail stays off | | pass / FAIL | |
| Brownout ramp down+up through BOR: rail stays off | | pass / FAIL | |
| Board modification fitted (pull), if any | | | |
| Re-test after modification: all three | | pass / FAIL / n-a | |

## Sign-off

Baseline complete, all fields traceable: ________ (date, initials)
