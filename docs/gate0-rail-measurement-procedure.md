# Rail monitor and field-validation note

The owner-observation values already configure Phase 2: 4.5–5.5 V for the
ML3 rail, a 500000 ppm PA4 divider ratio, and a 500 mV / 2000 ms discharge
criterion. They are owner hardware observations, not bench measurements. The
manual rail-switching sequence previously kept in this file is retired; this
document does not provide a replacement command or method to force the rail.

The remaining rail work is optional field-installation validation. It does not
block the firmware build. When the controlled Task 10B engine and bring-up path
are available, observe the rail-on voltage under the connected probe's load and
the voltage decay after the engine releases the rail. Record whether the PA4
monitor follows those observations. The engine owns the power transition, so
the validation does not modify vendor radio or power timing settings.

## PA4 monitor divider

The PA4 monitor requires two fitted equal resistors: one from `+5V` terminal
14 to `PA4` terminal 26, and one from `PA4` terminal 26 to `GND` terminal 15.
The configured 500000 ppm ratio assumes that divider. Without it, PA4 floats
and the firmware reads the supply as failed.

The terminal numbers follow the Dragino right-hand JP4 block, numbered 14 at
the top through 26 at the bottom. A pair of equal 1 kΩ resistors is the fitted
configuration used by the owner observation. The routine's PA4 reading may be
compared with a multimeter reading during controlled bring-up; `AT+ML3ADC` is
also available in the bench image for ADC diagnostics.

## Brownout prerequisite

Brownout remains a field-installation prerequisite. Before installing a node,
let its main supply decay through the brownout region and confirm that the 5 V
output does not activate on its own. This safety check is separate from the
optional rail validation and does not block the firmware build.
