# ML3 firmware integration

The LSN50v2 ML3 path is selected by mode `10` and uses LoRaWAN FPort `13`.
Those transport values are authorized, but the image is not deployable: the
Gate 0 electrical measurements and Phase 2 qualification values remain zero
in `inc/ml3_config.h`, so acquisition and transmission stay fail-closed.

## Target seams

- `BSP_sensor_Init` powers the existing sensor-control rail first, then enters
  the ML3 service and returns before stock sensor setup in mode 10.
- `BSP_sensor_Read` and the LoRa battery callback cannot invoke the stock ADC
  in mode 10. The callback returns `0x00FF` until a valid ML3 battery result
  exists.
- `BSP_ML3_Service` is called from the main loop and owns the future
  non-blocking measurement state machine. It remains a fail-closed no-op until
  the verified ADC/timer/power adapter and strict radio-acceptance path are
  added; it does not run while the readiness contract is false.
- ML3 AT commands are dispatched before the legacy prefix-matching AT table,
  so they cannot trigger unrelated EEPROM configuration writes.

## Local calibration transport

Calibration data is sent through bounded ASCII chunks:

```
AT+ML3CAL=<offset>,<total>,<hex>
```

`total` is 52–2092 bytes. Each command carries 1–48 decoded bytes (an even
number of hexadecimal characters). Offset zero starts a transfer; subsequent
chunks must use the exact next offset and the same total. The target currently
stages a complete record in RAM and reports it as pending. Persistent dual-slot
commit and verified clear remain blocked until the Appendix B device-hash
algorithm and STM32 data-EEPROM slot map are approved; no address or hash is
invented by this integration.

## Verification

Run the host and target integration contracts from the repository root:

```sh
tests/host/ml3_target_integration_contract.sh
tests/host/run_ml3_host_suite.sh
```

The host suite covers the pure ADC, measurement, calibration, quality,
thermistor, payload, vector, and AT-parser modules. A target build still needs
the exact Keil/uVision toolchain and a clean Rebuild All; no `.hex` is claimed
until Gate 0, Phase 2, and the radio acceptance path are complete.
