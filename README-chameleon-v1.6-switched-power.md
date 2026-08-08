# LSN50V2 + VIA Chameleon v1.x: switched-power wiring

This branch is for the current Chameleon board revision, which has no dedicated sleep pin. The Chameleon board is fully power-cycled for every measurement using a P-channel MOSFET.

## LSN50V2 connections

| LSN50V2 signal | Terminal | Connection |
|---|---:|---|
| VDD | 13 (or 1) | P-channel MOSFET source |
| PB12 | 20 | P-channel MOSFET gate control |
| PB13 | 21 | Chameleon SCL (I2C2) |
| PB14 | 18 | Chameleon SDA (I2C2) |
| GND | 15 (or 12) | Chameleon GND |

P-channel MOSFET drain connects to Chameleon VCC.

Use an external gate-to-source pull-up (100 kOhm is a suitable default). PB12 is configured open-drain: LOW turns the sensor on; released/high-impedance lets the resistor pull the gate to VDD and turns the sensor off.

SDA and SCL need pull-up resistors to the **switched Chameleon VCC on the MOSFET drain side**. 4.7 kOhm is the default if the Chameleon carrier does not already provide suitable pull-ups. Do not pull SDA/SCL to the always-powered LSN50 VDD or to +5 V.

Do not use PB6/PB7 for this revision. They are the stock LSN50V2 I2C1 pair and the board-level pull-up arrangement can leave an unpowered Chameleon partly powered through SDA/SCL.

## Firmware sequence

For each measurement cycle:

1. Mask the stock PB14 EXTI function.
2. Configure PB12 as open-drain and pull it LOW to switch the Chameleon on.
3. Wait 100 ms for cold start.
4. Configure PB13/PB14 as I2C2 SCL/SDA at 400 kHz, open-drain, no MCU pull-ups.
5. Probe address 0x08.
6. Trigger a fresh reading (0x40), poll status (0x41), and read temperature, compensated/raw resistance and sensor ID using the existing Chameleon protocol.
7. Disable I2C2.
8. Put PB13/PB14 into analog/no-pull state so the MCU cannot back-power the slave.
9. Release PB12. The external gate pull-up switches the MOSFET off.
10. Continue with the normal LSN50 low-power cycle.

The existing 2 s measurement timeout, 50 ms status polling, post-ready settle and compensation retry logic are retained.

## Bench validation before field use

First test with a multimeter before connecting the Chameleon:

- Sensor OFF: MOSFET drain should be near 0 V.
- Sensor ON: MOSFET drain should be approximately LSN50 VDD.

Then connect the Chameleon and verify:

- Sensor OFF: Chameleon VCC, SDA and SCL should all fall close to 0 V. A persistent ~1-2 V level means back-powering is still present.
- Sensor ON: Chameleon VCC and I2C HIGH levels should be approximately VDD.
- Confirm repeated wake/read/power-off cycles without `I2C missing` payloads.
- Run at least several hundred cycles before returning the node to the field.
