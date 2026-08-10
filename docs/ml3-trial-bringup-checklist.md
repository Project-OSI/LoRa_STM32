# ML3 trial firmware — bench bring-up checklist

Operator procedure for the first flash of firmware that can actually measure. Work through it in order and **stop at the first step whose result disagrees with what is written here.**

Terminal numbers follow the Dragino pinout diagram: left block **1–13** (`PA0` = 2, `PA1` = 3, `GND` = 12), right block **14–26** (`+5V` = 14, `GND` = 15, `PA4` = 26).

## Before you start

- Image: `gcc/build/lora.hex`, built from branch `feature/ml3-precision-adc`. Flash with STM32CubeProgrammer's GUI.
- Console: `picocom -b 9600 /dev/ttyUSB0`, exit with `Ctrl-A` then `Ctrl-X`.
- **Never issue `AT+5VT`.** It corrupts the stack and resets the node — a pre-existing vendor defect, unrelated to this work.
- Have the multimeter ready. The probe stays **disconnected** until step 4.

## What this firmware does differently

This is the first build where mode 10 actually measures. It powers the +5 V rail from the board, warms the probe for 1.5 seconds, runs a burst of readings, and transmits on FPort 13. In every other mode it behaves exactly like stock firmware.

It also **transmits when things go wrong** — a partly failed or wholly failed measurement still produces a frame carrying fault flags and sentinel values. A silent node means a genuine problem, not a bad reading.

---

## Step 1 — Flash and confirm identity

Flash `lora.hex`, then on the console:

```
AT+ML3VER?
```
(no `=` — that syntax errors)

**Expect:** a `+ML3VER:` line reporting protocol, mode 10 and FPort 13.
**Stop if:** the command is unrecognised. The flash didn't take.

## Step 2 — Confirm it behaves like stock

Leave it in its normal mode. Watch the console for a few minutes.

**Expect:** boot banner, network join, ordinary uplinks — indistinguishable from before.
**Stop if:** it resets repeatedly, or fails to join when it did previously.

## Step 3 — Confirm the rail is off, with nothing attached

Meter red on **+5V (14)**, black on **GND (15)**.

**Expect:** essentially 0 V, apart from brief pulses if the stock mode polls sensors.
**Stop if:** the rail sits energised continuously. Nothing should hold it on, and a stuck rail would power a probe permanently once one is attached.

## Step 4 — Attach the probe

Rail confirmed off, then wire:

| ML3 wire | Terminal |
|---|---|
| White | **+5V (14)** |
| Brown | **GND (15)** |
| Green (shield) | **GND (15)** |
| Blue (signal HI) | **PA0 (2)** |
| Black (signal LO) | **PA1 (3)** |
| Grey (thermistor) | tape it off — unused |

Check white before powering: reversed polarity can damage the probe.

## Step 5 — The backpowering check

Probe connected, rail off. Meter **PA0 (2)** then **PA1 (3)**, each against ground.

**Expect:** both essentially zero.
**Stop if:** either sits at a meaningful voltage with the rail off. That would mean something is feeding the inputs while unpowered, and we investigate before going further.

*(This also fills the one gap left in the §3 envelope record.)*

## Step 6 — Enter ML3 mode

```
AT+MOD=10
```

**Expect:** accepted. The node continues running; acquisition happens on its own cycle.
**Stop if:** rejected. That means the readiness gate is still closed and the build isn't the one you think it is.

## Step 7 — Watch the first acquisition

Keep the meter on **+5V (14)** and watch the console.

**Expect:** the rail rises to about 5 V, holds for roughly two seconds, then falls back to zero. Shortly after, a transmission.
**Stop if:** the rail comes on and **stays** on. Power the node down and tell me — that's the one failure mode that can harm the probe.

## Step 8 — Confirm the frame

In ChirpStack, find the uplink.

**Expect:** a frame on **FPort 13**, around 25 bytes.
**Note:** it will look like meaningless bytes. That is correct — the frame carries raw microvolts, and conversion to soil moisture happens in the backend, which doesn't decode this port yet.

## Step 9 — Sanity-check the values

Compare against the meter while a measurement runs: **PA0** should read close to the probe's signal for its condition — a few millivolts in air, near a volt in water. Take a couple of readings in air, then with the rods in water.

**Expect:** the two conditions differ clearly.
**Stop if:** both read the same. The probe or its wiring isn't reaching the chip.

## Step 10 — Return it to a safe state

```
AT+MOD=1
```

Confirm the rail returns to its stock behaviour and stays off between polls.

---

## When you're done

Tell me: which steps passed, any numbers you noted at steps 5, 7 and 9, and whether the FPort 13 frame arrived. That completes bench bring-up.

## Before any node goes into the ground

Two items are deliberately outstanding and are **not** bench blockers:

- **The brownout rail check.** Reset and ISP behaviour were confirmed on 2026-08-09; brownout was not. Since the trial now powers the probe from this rail, a dying battery must not be able to switch it on. Discharge a large capacitor through the node so the supply falls slowly through its brown-out region while watching the rail stay off.
- **Soil-specific calibration** (a₀ and a₁ from Delta-T's Appendix 1 procedure) is what takes accuracy from approximate to specified. The firmware doesn't need it — the backend applies it — but the data is only as good as those two numbers.
