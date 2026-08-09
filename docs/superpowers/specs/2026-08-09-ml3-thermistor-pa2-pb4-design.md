# ML3 thermistor PA2/PB4 design

## Goal

Record the owner-approved wiring for the ML3 grey thermistor conductor without enabling an acquisition on the LSN50v2.

## Pin assignment

PA2 (`ADC_IN2`, LSN50v2 terminal 4) is the thermistor ADC input. PB4 (terminal 9) is the excitation GPIO. In ML3 mode, PB4 drives high only during the thermistor read through the 10.0 kΩ reference resistor. The ML3 grey conductor joins PA2 at the resistor junction; the brown conductor remains at node ground. The 100 nF capacitor connects from PA2 to ground.

PA0 and PA1 remain the ML3 moisture inputs, and PA4 remains the switched-5 V monitor. PA5 is unavailable because the radio uses it as SPI clock. PA2 is LPUART1 TX in stock firmware. The future adapter must wait for UART transmit and DMA completion, switch PA2 alone to analog during a sample, then restore LPUART1 TX. PA3 remains untouched.

PB4 is the legacy oil-float control. ML3 mode excludes the legacy oil reader; this selection does not imply that PB4 is safe for legacy mode after the thermistor wiring is fitted.

## Physical condition before activation

Dragino documents a pull-up on PA2. It would put a second path to VDD across the reference divider and can leave the thermistor excited while the firmware requests it off. The board-level PA2 pull must therefore be absent or disconnected before the thermistor acquisition is enabled. Verify that physical condition, then follow the unpowered-probe sequence before a thermistor read: PB5 off, PA4 confirms discharge, PB4 high, PA2 sample, PB4 low. An attached serial console must not drive PA2 during a sample; the future adapter restores LPUART1 TX after the sample without using `vcom_IoDeInit`, because that helper also changes PA3.

This document does not claim that the modification has been made or checked on any node.

## Firmware boundary

The configuration names PA2 and PB4 so the production adapter has one defined target. Their two readiness macros remain zero. `ML3_CONFIG_ACQUISITION_READY` and `ML3_CONFIG_DEPLOYABLE` must therefore remain false, PB5 must not be enabled by this decision, and no calibration record may be written.

The STM32 internal temperature sensor (`ADC_IN18`) continues to supply the temperature used by the electrical calibration model. The PA2 circuit supplies soil temperature from the ML3 thermistor, after the probe is unpowered and PA4 has verified discharge. Its raw reading is ratiometric and must not receive VREFINT compensation before `ml3_thermistor_convert`.

The ML3 manual lookup table needs an independent transcription check before activation.

## Verification

The host configuration contract must prove the selected values and both zero readiness gates. The target integration contract, full host suite, and default and BENCH GCC builds must still pass. A future hardware activation requires confirmation that PA2 has no parallel pull-up, the serial source is quiet during a sample, and bath verification covers at least five points spanning 0–40 °C with a maximum error of 1 °C.
