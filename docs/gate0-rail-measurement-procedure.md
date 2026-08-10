# Bench procedure — 5 V rail, PA4 monitor, and discharge

Operator procedure. Written 2026-08-09 after the decision to power the ML3 from the board's own switched +5 V output. These four numbers unblock Task 10B Phase 2; the firmware cannot be configured without them, and they must be measured rather than assumed.

Node carries the **bench image** (`AT+ML3ADC` available). Equipment: multimeter, serial console, two resistors, the ML3 probe.

## What we are finding out, and why

| Measurement | Fills | Why it matters |
|---|---|---|
| Rail voltage under the probe's load | `ML3_CONFIG_V5_MINIMUM_MV` / `_MAXIMUM_MV` | The ML3 needs at least 5.0 V. If the board's rail sags below that under an 18 mA load, the probe is out of spec and the whole power arrangement has to change. **This one can fail.** |
| PA4 divider ratio | `ML3_CONFIG_V5_DIVIDER_RATIO_PPM` | The firmware watches the rail through PA4. Without the true ratio it cannot convert what it reads into a real voltage. |
| Discharge threshold and time | `ML3_CONFIG_DISCHARGE_THRESHOLD_MV` / `_TIMEOUT_MS` | The firmware must know when the rail has genuinely fallen away after switch-off. A guessed value either stalls the sequence forever or lets it continue while the probe is still live. |

---

## Step 1 — Fit the PA4 monitor divider

PA4 has nothing connected to it today (it read 2–4 mV floating during the Gate 0 work), so there is no monitor circuit yet. It needs one, and it is two resistors.

The rail is about 5 V and the chip cannot read above roughly 3.6 V, so the voltage must be halved before it reaches the pin.

1. **Resistor A** from the **+5V** terminal (JP4 position 1) to **PA4** (JP4 position 13).
2. **Resistor B** from **PA4** to **GND** (JP4 position 2).

Use two resistors of the same value. Higher values waste less power — if you have 10 kΩ or larger, prefer those; the 1 kΩ pair works fine for this bench session. They do not need to be a matched pair or precise, because we are about to *measure* the actual ratio rather than trust it.

Leave the ML3 disconnected for now.

---

## Step 2 — Get the rail switching on and off

The firmware powers the 5 V output briefly around each transmission. We widen that window so there is time to measure. On the console:

```
AT+5VT=?          note the current value so you can restore it
AT+5VT=30000      hold the rail on for 30 seconds each cycle
AT+TDC=?          note the current value
AT+TDC=60000      one cycle per minute
```

The rail should now be on for roughly half of each minute. Confirm with the meter on **+5V** and **GND**: you should see it rise to about 5 V, hold, then fall back toward zero.

---

## Step 3 — Rail voltage, unloaded and loaded

**Unloaded first.** With the ML3 still disconnected, during an on-window read the voltage at the **+5V** terminal. Write it down.

**Now connect the probe:** white to **+5V**, brown and green to **GND**, blue to **PA0**, black to **PA1**, grey taped off.

**Loaded.** During an on-window, read the **+5V** terminal again with the probe drawing its 18 mA. Write it down.

**This is the pass/fail step.** The ML3's minimum supply is 5.0 V.

- Loaded voltage comfortably at or above 5.0 V → pass, carry on.
- Loaded voltage below 5.0 V → **stop and tell me.** The probe would be running out of specification and we need to solve that before anything else. Do not proceed as though it were fine.

Also note whether the voltage is steady through the window or drifts as the battery reacts.

---

## Step 4 — The PA4 divider ratio

During a single on-window, take both readings as close together as you can:

1. Meter on the **+5V** terminal — the true rail voltage.
2. `AT+ML3ADC` on the console — read the **CH=4** line.

Send me both. The ratio between them is the number the firmware needs, and measuring it removes any dependence on your resistors being accurate.

Repeat once more in a later window so we have two samples and can see whether they agree.

---

## Step 5 — Discharge behaviour

We need to know how quickly the rail collapses once the firmware switches it off.

Watch the meter on **+5V** through the moment the window ends, and note roughly how long it takes to fall to near zero — whether that is a fraction of a second, a few seconds, or longer.

Then, immediately after a window ends, run `AT+ML3ADC` **three or four times in quick succession** and send me the `CH=4` values in order. Those give a rough decay curve.

Expect this to be fast, possibly too fast to catch — the probe's own draw pulls the rail down quickly. **"It collapsed immediately, I couldn't catch it" is a perfectly good result**; it tells us to set a short, safe threshold. Don't spend long fighting for a detailed curve.

---

## When you are done

Restore the settings you noted in Step 2 (`AT+5VT` and `AT+TDC`), so the node is not sitting there powering the probe every minute.

Send me:

1. Rail voltage **unloaded**
2. Rail voltage **loaded** (with the pass/fail verdict against 5.0 V)
3. The paired **meter + CH=4** readings, twice
4. Whatever you saw of the **discharge**
5. Which resistor values you fitted

That completes the configuration inputs, and Task 10B Phase 2 is unblocked.

## Still outstanding, separately

The **brownout check** is now a prerequisite before field installation, since the trial depends on this rail. It is a short test: discharge a large capacitor through the node so the supply falls slowly through its brown-out region, while watching that the 5 V output never rises on its own. Not needed before the firmware work, but needed before a node goes in the ground.
