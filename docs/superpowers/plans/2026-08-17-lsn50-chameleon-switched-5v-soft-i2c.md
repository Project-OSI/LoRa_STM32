# LSN50 Chameleon switched-5 V software-I2C implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build one EU868 LSN50v2 image that powers the VIA Chameleon reader
from the stock PB5-switched +5 V output and communicates through software I2C
on PB13/PB12 without added interface components.

**Architecture:** Keep `via_chameleon.c` as the protocol authority. Add a
portable open-drain software-I2C transport, bind it to PB13 SCL and PB12 SDA
with a session-local, software-extended 16-bit TIM2 microsecond clock, and let
`chameleon_lsn50_hw.c` own power, deadlines, one configurable cold retry,
watchdog service, and cleanup. Restore PB14 to the vendor digital-input path
because it is no longer part of the Chameleon bus.

**Tech stack:** C99 host tests, STM32L072 HAL/CMSIS, TIM2 at 1 MHz, shell ARM
GNU build, EU868 LoRaWAN firmware.

**Spec:**
`docs/superpowers/specs/2026-08-17-lsn50-chameleon-switched-5v-soft-i2c-design.md`

## Global constraints

- Work only on `feature/chameleon-v1.7-switched-5v-soft-i2c`. Do not modify
  the v1.5 or v1.6 branches or their existing artifacts.
- Produce only the `chameleon-soft-i2c-5v` target and the
  `LSN50-chameleon-soft-i2c-5v` artifact family on this branch.
- Keep `via_chameleon.c`, `chameleon_payload.c`, their public data structures,
  the 44-byte Chameleon V1 payload, and the existing decoder contract unchanged.
- Keep the image fixed to MOD3. HX711, ultrasonic, and other sensor modes are
  outside this image.
- Leave PA1 as the stock ADC input; this design has no external P-FET gate.
- Use PB13 as SCL, PB12 as SDA, and PB5 as the stock active-low +5 V enable.
  PB6/PB7 and I2C1/I2C2 are not Chameleon resources in this image.
- Configure PB12/PB13 as open-drain/no-pull while active and analog/no-pull
  while inactive. Never drive either bus pin push-pull high.
- Reject `DEBUG + USE_CHAMELEON` at compile time because vendor `DBG_Init()`
  drives PB12-PB15 push-pull low.
- Start with a nominal 50 kHz bus. The measured bus rate must not exceed
  100 kHz.
- Bound one SCL-high wait to 2 ms, one byte to 10 ms, one transaction to 50 ms,
  startup probing to 400 ms, VIA readiness to 2000 ms, and the full acquisition
  to 12 s.
- Treat TIM2 as the STM32L072's 16-bit timer. Reset its software extension at
  each session and never leave it unobserved for its 65.536 ms wrap period.
- Initial bench constants are 100 ms reader startup and 1000 ms cold-off retry.
  Bench discharge and settling measurements may change those constants before
  field release under the rules in the spec.
- Cleanup order is transport shutdown, PB12/PB13 analog isolation, then +5 V
  OFF. Use the same idempotent cleanup path after success, every failure, boot
  preparation, and sleep preparation.
- Do not claim field readiness from compilation or host tests. The electrical,
  endurance, fault-injection, and four-week gates remain hardware work.

---

### Task 1: Add the portable software-I2C transport

**Files:**

- Create:
  `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/chameleon_soft_i2c.h`
- Create:
  `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/chameleon_soft_i2c.c`
- Create: `tests/test_chameleon_soft_i2c.c`
- Modify: `tests/Makefile`
- Reference only:
  `/home/phil/kDrive/OSI OS/Hardware/Chameleon/VIAChameleonI2CMaster/VIAChameleonI2C.cpp`

**Interfaces:**

- Consumes: `chameleon_i2c_status_t` from `via_chameleon.h` and injected line,
  delay, and microsecond-clock operations.
- Produces:

```c
#define CHAMELEON_SOFT_I2C_HALF_PERIOD_US       10U
#define CHAMELEON_SOFT_I2C_SCL_HIGH_TIMEOUT_US 2000U
#define CHAMELEON_SOFT_I2C_BYTE_TIMEOUT_US    10000U
#define CHAMELEON_SOFT_I2C_TXN_TIMEOUT_US     50000U

typedef struct {
    void *context;
    void (*scl_low)(void *context);
    void (*scl_release)(void *context);
    int  (*scl_read)(void *context);
    void (*sda_low)(void *context);
    void (*sda_release)(void *context);
    int  (*sda_read)(void *context);
    void (*delay_us)(void *context, uint32_t us);
    uint32_t (*micros)(void *context);
} chameleon_soft_i2c_ops_t;

typedef struct {
    chameleon_soft_i2c_ops_t ops;
    uint8_t active;
} chameleon_soft_i2c_t;

int chameleon_soft_i2c_init(chameleon_soft_i2c_t *bus,
                            const chameleon_soft_i2c_ops_t *ops);
void chameleon_soft_i2c_shutdown(chameleon_soft_i2c_t *bus);
chameleon_i2c_status_t chameleon_soft_i2c_write(
    chameleon_soft_i2c_t *bus, uint8_t addr7,
    const uint8_t *data, size_t len);
chameleon_i2c_status_t chameleon_soft_i2c_write_read(
    chameleon_soft_i2c_t *bus, uint8_t addr7,
    const uint8_t *wdata, size_t wlen,
    uint8_t *rdata, size_t rlen);
chameleon_i2c_status_t chameleon_soft_i2c_bus_clear(
    chameleon_soft_i2c_t *bus);
```

- [ ] **Step 1: Register a focused host test target**

Add `chameleon_soft_i2c` to `TESTS` and compile the test against the production
transport:

```make
$(OBJDIR)/test_chameleon_soft_i2c: test_chameleon_soft_i2c.c \
		$(PROJ_BASE)/src/chameleon_soft_i2c.c | $(OBJDIR)
	$(CC) $(CFLAGS) "-I$(PROJ_BASE)/inc" -o $@ \
		test_chameleon_soft_i2c.c \
		"$(PROJ_BASE)/src/chameleon_soft_i2c.c"
```

- [ ] **Step 2: Write the failing wired-AND transport tests**

Put the fake bus in `test_chameleon_soft_i2c.c`. Its callbacks must track the
master's low/released state separately from scripted slave-low state, advance a
32-bit microsecond counter in `delay_us`, record START/STOP and SCL rising
edges, queue SDA samples for ACK and read bits, and optionally hold SCL low.
Use assertions with these cases:

```c
static void test_probe_sends_shifted_write_address(void);
static void test_address_nack_is_reported_and_lines_release(void);
static void test_write_is_msb_first(void);
static void test_write_read_uses_repeated_start(void);
static void test_read_acks_intermediate_and_nacks_final_byte(void);
static void test_clock_stretch_completes_before_two_ms(void);
static void test_scl_held_low_times_out_and_releases_lines(void);
static void test_sda_held_low_is_bus_fault(void);
static void test_sda_forced_low_during_transmitted_high_is_bus_fault(void);
static void test_transaction_deadline_is_fifty_ms(void);
static void test_deadlines_wrap_across_uint32_max(void);
static void test_start_stop_timing_meets_standard_mode(void);
static void test_bus_clear_clocks_nine_times_then_stops(void);
```

The probe case calls
`chameleon_soft_i2c_write(&bus, 0x08U, NULL, 0U)` and asserts that the first
transmitted byte is `0x10`. The two-byte read case scripts `0xA5, 0x3C` and
asserts the master sends ACK after `0xA5`, NACK after `0x3C`, and emits one
repeated START without STOP between the register write and address-read phase.
Every failure case asserts both master line states are released on return.

- [ ] **Step 3: Run the focused test and confirm the red state**

Run:

```bash
make -C tests build/test_chameleon_soft_i2c
```

Expected: compilation fails because `chameleon_soft_i2c.h` and its production
implementation do not exist.

- [ ] **Step 4: Add the public transport header**

Create `chameleon_soft_i2c.h` with the exact constants, structures, and
function signatures in the Interfaces block. Include `<stddef.h>`,
`<stdint.h>`, and `via_chameleon.h`; expose no STM32 or HAL type. `init()`
validates and copies every callback, marks the bus active, and releases both
lines. It does not reject a physically low SDA/SCL at this stage; the first
transaction must report that condition so lifecycle recovery can run bus clear.

- [ ] **Step 5: Implement open-drain primitives and deadlines**

In `chameleon_soft_i2c.c`, use unsigned subtraction for wrap-safe elapsed time:

```c
static int expired(uint32_t start, uint32_t now, uint32_t limit_us)
{
    return (uint32_t)(now - start) >= limit_us;
}
```

Implement `release_scl_and_wait()` so it releases SCL, polls the physical line,
and returns `CHAMELEON_I2C_ERR_TIMEOUT` after 2 ms or when the enclosing byte or
transaction deadline expires. The polling loop must call `delay_us(..., 1U)`
between samples so target code does not busy-spin and the fake clock advances
in held-low tests. A logical high always calls the release callback; only a
logical low calls the drive-low callback.

- [ ] **Step 6: Implement START, repeated START, STOP, and byte transfers**

Use these sequences:

```text
START:          release SDA -> release/wait SCL -> half delay -> SDA low
                -> half delay -> SCL low
repeated START: release SDA -> release/wait SCL -> half delay -> SDA low
                -> half delay -> SCL low
STOP:           SDA low -> half delay -> release/wait SCL -> half delay
                -> release SDA -> half delay
write bit:      set/release SDA -> half delay -> release/wait SCL
                -> half delay -> SCL low
read bit:       release SDA -> half delay -> release/wait SCL
                -> sample SDA -> half delay -> SCL low
```

Write bytes most-significant bit first, release SDA for the ninth clock, and
return `CHAMELEON_I2C_ERR_NACK` when that sample is high. Read eight bits, then
drive SDA low for ACK except after the final byte, where SDA remains released
for NACK. Apply the 10 ms byte deadline to address, data, and received bytes.
When transmitting a logical one, sample SDA while SCL is high and return BUS if
the physical line is low. This detects a short or another active driver without
claiming a NACK.

The condition delays provide tSU;STA, tHD;STA, tSU;STO, and tBUF margin at
Standard-mode speed. After each SCL falling edge, change SDA without an added
half-period delay, then give the new data a full half-period before releasing
SCL. UM10204 permits zero data-hold time for I2C devices and limits tVD;DAT to
3.45 us, so a 10 us low-to-data delay would be non-compliant. The timing test
must inspect recorded edge timestamps, not only event order.

- [ ] **Step 7: Implement public transactions and bus clear**

`write()` must validate `addr7 <= 0x7f`, allow `len == 0` only when `data` may be
NULL, send `(addr7 << 1)`, and STOP on every exit. `write_read()` must reject
zero-length write or read buffers, send address-write, all write bytes, a
repeated START, address-read, and all read bytes under one 50 ms deadline.

Before the first START, verify released SDA is physically high; return BUS if it
is held low. `bus_clear()` releases SDA, sends at most nine SCL pulses, stops early if SDA
reads high, then attempts STOP. It returns BUS if SDA is still low, TIMEOUT if
SCL cannot rise, otherwise OK. `shutdown()` releases both lines and clears
`active` even after a failed transaction.

- [ ] **Step 8: Run transport and full host suites**

Run:

```bash
make -C tests build/test_chameleon_soft_i2c
tests/build/test_chameleon_soft_i2c
make -C tests clean test
```

Expected: the focused test prints `test_chameleon_soft_i2c OK`; the final
command prints `all host tests passed`.

- [ ] **Step 9: Commit the transport**

```bash
git add tests/Makefile tests/test_chameleon_soft_i2c.c \
  'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/chameleon_soft_i2c.h' \
  'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/chameleon_soft_i2c.c'
git commit -m "feat: add bounded Chameleon software I2C"
```

### Task 2: Make the acquisition lifecycle watchdog-safe and configurable

**Files:**

- Modify:
  `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/chameleon_lsn50_hw.h`
- Modify:
  `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/chameleon_lsn50_hw.c`
- Modify: `tests/test_chameleon_lifecycle.c`

**Interfaces:**

- Consumes: the existing protocol calls `via_chameleon_probe()`,
  `via_chameleon_wait_ready()`, and `via_chameleon_measure()`.
- Produces two additions to `chameleon_lsn50_ops_t`:

```c
chameleon_i2c_status_t (*bus_clear)(void *context);
void (*watchdog_refresh)(void *context);
```

- Retains: `chameleon_lsn50_run()`, `chameleon_lsn50_acquire()`,
  `chameleon_lsn50_prepare_sleep()`, `chameleon_lsn50_last_attempts()`, and
  existing result/flag mappings.

- [ ] **Step 1: Rewrite lifecycle expectations before production code**

Extend `fake_hw_t` with `bus_clear_calls`, `watchdog_calls`, and a configurable
millisecond cost for probe/wait/measure. Add callbacks to `make_ops()` and use
these constants in the header:

```c
#define CHAMELEON_POWER_STABILIZE_MS   100U
#define CHAMELEON_PROBE_TIMEOUT_MS     400U
#define CHAMELEON_PROBE_INTERVAL_MS     50U
#define CHAMELEON_COLD_RETRY_OFF_MS   1000U
#define CHAMELEON_COLD_RETRY_ENABLED      1U
#define CHAMELEON_RETRY_SESSION_RESERVE_MS 5200U
#define CHAMELEON_ACQUIRE_TIMEOUT_MS  12000U
#define CHAMELEON_WATCHDOG_SLICE_MS    1000U
#define CHAMELEON_LIFECYCLE_CONTROL_MARGIN_MS 50U
```

Update the success trace to begin
`deinit,isolate,off,on,stabilize,init` and end
`deinit,isolate,off`. Add assertions that:

- initialization failure still calls the same complete cleanup;
- NO_DEVICE, trigger, status-I/O, read, and partial-sample failures call bus
  clear before cleanup;
- readiness timeout cleans up without an unnecessary bus clear;
- a first failed session waits exactly 1000 ms and starts at most one second
  session;
- a retry is skipped unless the remaining global budget covers the cold-off
  delay plus the 5200 ms worst-case second session and control margin;
- long waits refresh the watchdog at intervals no greater than 1000 ms;
- simulated slow protocol operations stop at the 12 s acquisition deadline;
- success and sentinel-bearing valid samples do not retry.

- [ ] **Step 2: Run the lifecycle test and verify expected failure**

```bash
make -C tests build/test_chameleon_lifecycle
tests/build/test_chameleon_lifecycle
```

Expected: assertions fail on the current off/isolate order, 25 ms startup,
1500 ms probe window, 200 ms retry, missing bus clear, or missing watchdog
callback.

- [ ] **Step 3: Make cleanup unconditional and idempotent**

Replace the `initialized` branch with one path:

```c
static void clean_session(const chameleon_lsn50_ops_t *ops)
{
    ops->i2c_deinit(ops->context);
    ops->bus_isolate(ops->context);
    ops->rail_off(ops->context);
}
```

Require `i2c_deinit`, `bus_clear`, and `watchdog_refresh` in `ops_valid()`.
Start each session by calling `clean_session(ops)`, then `rail_on`; this gives
the same isolate-before-OFF ordering at boot and between attempts.

- [ ] **Step 4: Add watchdog-serviced bounded delay**

Use one helper for startup and cold-off waits:

```c
static void bounded_delay(const chameleon_lsn50_ops_t *ops, uint32_t delay_ms)
{
    while (delay_ms != 0U) {
        uint32_t slice = delay_ms > CHAMELEON_WATCHDOG_SLICE_MS
                       ? CHAMELEON_WATCHDOG_SLICE_MS : delay_ms;
        ops->delay_ms(ops->context, slice);
        ops->watchdog_refresh(ops->context);
        delay_ms -= slice;
    }
}
```

Refresh immediately before and after each opaque protocol call. Individual VIA
waits remain at or below 2 s, under the 5 s watchdog-service requirement.

- [ ] **Step 5: Enforce the probe and global acquisition deadlines**

Record `acquire_started = ops->millis(...)` once. Before probe, readiness, and
measurement calls, compute remaining time by unsigned subtraction. Reserve
100 ms around readiness: 50 ms for the final status transaction and 50 ms for
lifecycle control and cleanup. Reserve 550 ms around
`via_chameleon_measure()`: 500 ms for one trigger transaction, one possible
final status transaction, and eight register reads, plus the same 50 ms
lifecycle margin. Pass these budgets:

```c
static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

ready_timeout = remaining_ms > 100U
              ? min_u32(requested_timeout_ms, remaining_ms - 100U) : 0U;
measure_timeout = remaining_ms > 550U
                ? min_u32(requested_timeout_ms, remaining_ms - 550U) : 0U;
```

Return `CHAMELEON_RESULT_MEASUREMENT_TIMEOUT` instead of starting the call when
its timeout is zero. The reserves above include a 50 ms lifecycle control
margin for watchdog calls, GPIO transitions, timer shutdown, and final cleanup. Opaque
synchronous callbacks must honor the timeout or single-transaction contract
documented by `chameleon_lsn50_ops_t`. Tests must charge nonzero time for those
control operations instead of treating them as free. These reservations keep
the complete return path inside the 12 s wall-clock limit when callbacks honor
their contracts. The nominal two-session estimate is not the safety mechanism.
Every phase must clamp itself to the runtime remaining budget so later constant
changes cannot exceed the cap.

Make `bounded_probe()` stop at 400 ms or the remaining global budget, whichever
comes first. It may issue an immediate first probe, then wait no more than 50 ms
between attempts, but it must not start a probe unless one 50 ms transaction
and the 50 ms lifecycle control margin fit both enclosing deadlines.

- [ ] **Step 6: Clear the bus before cleanup after transport failures**

Use an explicit predicate:

```c
static int needs_bus_clear(chameleon_result_t result)
{
    return result == CHAMELEON_RESULT_NO_DEVICE
        || result == CHAMELEON_RESULT_TRIGGER_FAILED
        || result == CHAMELEON_RESULT_STATUS_IO_FAILED
        || result == CHAMELEON_RESULT_READ_FAILED
        || result == CHAMELEON_RESULT_PARTIAL_SAMPLE;
}
```

Call `ops->bus_clear()` once while the bus and reader are still active only when
one 50 ms transaction plus the 50 ms lifecycle control margin remains. Ignore
its result for payload mapping, then run unconditional cleanup. Do not clear the
bus for a valid sentinel sample or reader-busy timeout.

- [ ] **Step 7: Keep one compile-time cold retry**

Run a maximum of `1U + CHAMELEON_COLD_RETRY_ENABLED` sessions. Retry only when
the remaining global budget is at least the configured cold-off delay plus
`CHAMELEON_RETRY_SESSION_RESERVE_MS`. The initial 5200 ms reserve covers 100 ms
startup, a probe with its final transaction, two readiness phases with their
final transactions, trigger, register reads, failure-path bus clear, and the
50 ms lifecycle control margin. Call
the bounded cold-off delay, then let the second session enforce the same
remaining-time checks. Before field release, gate any constant change on the
measured discharge curve and recompute the reserve when its constituent limits
change; do not introduce an AT setting or EEPROM field.

- [ ] **Step 8: Run lifecycle and regression tests**

```bash
make -C tests build/test_chameleon_lifecycle
tests/build/test_chameleon_lifecycle
make -C tests clean test
```

Expected: exact lifecycle tests and all existing VIA/payload tests pass.

- [ ] **Step 9: Commit lifecycle policy**

```bash
git add tests/test_chameleon_lifecycle.c \
  'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/chameleon_lsn50_hw.h' \
  'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/chameleon_lsn50_hw.c'
git commit -m "fix: bound Chameleon power lifecycle"
```

### Task 3: Bind software I2C to PB13/PB12 and stock PB5 power

**Files:**

- Modify:
  `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/chameleon_lsn50_hw.c`
- Modify:
  `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/chameleon_lsn50_hw.h`
- Create: `tests/test_chameleon_hw_binding.c`
- Modify: `tests/Makefile`

**Interfaces:**

- Consumes: all `chameleon_soft_i2c_*` functions from Task 1 and the lifecycle
  operation table from Task 2.
- Produces the unchanged VIA board callbacks
  `chameleon_board_i2c_write()` and `chameleon_board_i2c_write_read()` backed by
  one static `chameleon_soft_i2c_t`.
- Produces a small host-testable `chameleon_tim2_clock_t` helper in
  `chameleon_lsn50_hw.h` that extends successive 16-bit TIM2 samples into
  session-local 32-bit elapsed microseconds.
- Owns TIM2 only while a Chameleon acquisition session is active. TIM21 remains
  owned by the watchdog LSI measurement.

- [ ] **Step 1: Add a red source-contract test**

Create `test_chameleon_hw_binding.c` using the same `read_source`,
`require_text`, and `forbid_text` pattern as the existing integration guards.
Include `<assert.h>` and `chameleon_lsn50_hw.h`, then add this functional test
before the source checks:

```c
static void test_tim2_clock_extends_ffff_wrap(void)
{
    chameleon_tim2_clock_t clock;

    chameleon_tim2_clock_reset(&clock, 0xfff0U);
    assert(chameleon_tim2_clock_update(&clock, 0xfffeU) == 14U);
    assert(chameleon_tim2_clock_update(&clock, 0x0008U) == 24U);
}
```

The test calls the production inline helper; do not duplicate its arithmetic
inside the test. Assert the final source also contains:

```text
#define CHAMELEON_SCL_PIN GPIO_PIN_13
#define CHAMELEON_SDA_PIN GPIO_PIN_12
GPIO_MODE_OUTPUT_OD
GPIO_NOPULL
GPIO_MODE_ANALOG
TIM2->PSC
TIM2->CNT
TIM2->ARR = 0xffffU;
chameleon_soft_i2c_write(
chameleon_soft_i2c_write_read(
#if defined(DEBUG) && defined(USE_CHAMELEON)
```

Forbid `I2C2`, `HAL_I2C_`, `GPIO_AF5_I2C2`, `GPIO_PIN_14`,
`TIM2->ARR = 0xffffffffU`, `CHAMELEON_POWER_EXTERNAL_PMOS`, and
`GPIO_MODE_OUTPUT_PP` in
`chameleon_lsn50_hw.c`. Register and run the test.

Read `stm32l0xx_hw.c` in the same test and require
`RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;`, the clock assumption used
to derive TIM2's 1 MHz prescaler from PCLK1.

```bash
make -C tests build/test_chameleon_hw_binding
tests/build/test_chameleon_hw_binding
```

Expected: it fails against the current I2C2/PB14 implementation.

- [ ] **Step 2: Add compile-time ownership guards**

At the production boundary in `chameleon_lsn50_hw.c`, add:

```c
#if defined(DEBUG) && defined(USE_CHAMELEON)
#error "DEBUG drives PB12/PB13 push-pull and is incompatible with Chameleon"
#endif
#if !defined(CHAMELEON_POWER_LSN50_5V)
#error "This branch requires the stock PB5-switched +5 V backend"
#endif
#if !defined(CHAMELEON_SOFT_I2C_PB12_PB13)
#error "This branch requires PB12/PB13 software I2C"
#endif
#if (CHAMELEON_POLL_INTERVAL_MS * 1000U) >= 65536U
#error "VIA poll interval can skip a complete TIM2 wrap"
#endif
#if (CHAMELEON_PROBE_INTERVAL_MS * 1000U) >= 65536U
#error "Probe interval can skip a complete TIM2 wrap"
#endif
```

Remove the external-PMOS selection and all private I2C2 handle, timing-word,
HAL transaction, and I2C2 diagnostic code.

- [ ] **Step 3: Configure PB5 with stock electrical semantics**

Keep PB5 OFF as output open-drain with pull-up and a preloaded SET latch. LOW
enables +5 V:

```c
HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
gpio.Pin = GPIO_PIN_5;
gpio.Mode = GPIO_MODE_OUTPUT_OD;
gpio.Pull = GPIO_PULLUP;
gpio.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOB, &gpio);
```

`stm32_rail_on()` writes RESET; `stm32_rail_off()` writes SET. Do not replace
the vendor's open-drain behavior with push-pull.

- [ ] **Step 4: Configure and extend the session-local 16-bit TIM2 clock**

Add the testable extension to `chameleon_lsn50_hw.h`:

```c
typedef struct {
    uint16_t last_count;
    uint32_t elapsed_us;
} chameleon_tim2_clock_t;

static inline void chameleon_tim2_clock_reset(
    chameleon_tim2_clock_t *clock, uint16_t count)
{
    clock->last_count = count;
    clock->elapsed_us = 0U;
}

static inline uint32_t chameleon_tim2_clock_update(
    chameleon_tim2_clock_t *clock, uint16_t count)
{
    clock->elapsed_us += (uint16_t)(count - clock->last_count);
    clock->last_count = count;
    return clock->elapsed_us;
}
```

Implement `stm32_timer_start()` with direct CMSIS registers so it has no IRQ or
MSP dependency:

```c
uint32_t pclk_hz = HAL_RCC_GetPCLK1Freq();
if (pclk_hz < 1000000U || (pclk_hz % 1000000U) != 0U) return 0;
__HAL_RCC_TIM2_CLK_ENABLE();
__HAL_RCC_TIM2_FORCE_RESET();
__HAL_RCC_TIM2_RELEASE_RESET();
TIM2->PSC = (pclk_hz / 1000000U) - 1U;
TIM2->ARR = 0xffffU;
TIM2->EGR = TIM_EGR_UG;
TIM2->CNT = 0U;
chameleon_tim2_clock_reset(&tim2_clock, 0U);
TIM2->CR1 = TIM_CR1_CEN;
```

`stm32_micros()` passes `(uint16_t)TIM2->CNT` through
`chameleon_tim2_clock_update()`. `stm32_delay_us()` polls unsigned elapsed
microseconds. The extension is valid because the 50 ms VIA poll delay is the
longest interval without a timer sample while the session-local timer runs,
below the 65.536 ms wrap. `stm32_timer_stop()` clears CEN, resets TIM2, and
disables its clock. The current firmware fixes APB1 to HCLK/1 at 32 MHz; add a
source guard for that clock assumption rather than generalizing to unused
clock trees.

- [ ] **Step 5: Bind PB12/PB13 as open-drain lines**

Before mode change, preload both output latches SET. Configure both in one HAL
call as output open-drain/no-pull/low-speed. Use these callbacks:

```c
scl_low:     HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET)
scl_release: HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET)
scl_read:    HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_13) == GPIO_PIN_SET
sda_low:     HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET)
sda_release: HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET)
sda_read:    HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) == GPIO_PIN_SET
```

`stm32_bus_isolate()` configures the same two pins as analog/no-pull. It must
not alter EXTI14 because PB14 has returned to the vendor path.

- [ ] **Step 6: Route VIA callbacks through the soft transport**

`stm32_i2c_init()` starts TIM2, configures the lines, and calls
`chameleon_soft_i2c_init()`. On any partial failure it calls the idempotent
deinitializer. `stm32_i2c_deinit()` calls transport shutdown and then stops
TIM2. `stm32_bus_clear()` calls `chameleon_soft_i2c_bus_clear()`;
`stm32_watchdog_refresh()` calls `IWDG_Refresh()`. Include `iwdg.h` explicitly;
do not depend on a transitive declaration.

The VIA board callbacks become direct wrappers:

```c
return chameleon_soft_i2c_write(&chameleon_bus, addr7, data, len);
return chameleon_soft_i2c_write_read(&chameleon_bus, addr7,
                                     wdata, wlen, rdata, rlen);
```

Keep `chameleon_board_delay_ms()`, `chameleon_board_millis()`, and
`chameleon_board_battery_mv()` unchanged.

- [ ] **Step 7: Use the lifecycle cleanup for sleep preparation**

Replace the current rail-off-before-isolation implementation with:

```c
void chameleon_lsn50_prepare_sleep(void)
{
    clean_session(&stm32_ops);
}
```

This is safe because transport shutdown, timer stop, GPIO isolation, and PB5
OFF are idempotent.

- [ ] **Step 8: Run binding and host regression tests**

```bash
make -C tests build/test_chameleon_hw_binding
tests/build/test_chameleon_hw_binding
make -C tests clean test
```

Expected: no production Chameleon source references I2C2 or PB14, and all host
tests pass.

- [ ] **Step 9: Commit the STM32 binding**

```bash
git add tests/Makefile tests/test_chameleon_hw_binding.c \
  'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/inc/chameleon_lsn50_hw.h' \
  'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/chameleon_lsn50_hw.c'
git commit -m "feat: bind Chameleon bus to PB12 and PB13"
```

### Task 4: Restore PB14 vendor behavior and remove I2C2-era diagnostics

**Files:**

- Modify:
  `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/bsp.c`
- Modify:
  `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/main.c`
- Modify:
  `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/at.c`
- Modify:
  `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/command.c`
- Modify:
  `STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/stm32l0xx_it.c`
- Modify: `tests/test_chameleon_integration_guards.c`
- Test unchanged: `tests/test_chameleon_payload.c`
- Vendor comparison: the same paths at `master`

**Interfaces:**

- Consumes: `chameleon_lsn50_acquire()` and the existing 44-byte encoder.
- Produces: normal PB14 EXTI configuration, interrupt handling, downlink
  reconfiguration, and MOD3 status reads alongside the Chameleon extension.
- Retains the `USE_CHAMELEON` MOD3 lock in `at.c`, `command.c`, and `lora.c`.

- [ ] **Step 1: Reverse the old PB14 expectations in the integration guard**

Replace assertions for disabled PB14/I2C2 diagnostics with assertions that:

- `bsp.c` has no I2C2 branch in `HAL_I2C_MspInit` or `MspDeInit`;
- `GPIO_EXTI14_IoInit(inmode);` is present without a neighboring
  `#ifndef USE_CHAMELEON` guard;
- `main.c` has no `PB14_DIGITAL_READ` macro and no Chameleon
  `switch_status=0`; MOD3 reads `HAL_GPIO_ReadPin(GPIO_EXTI14_PORT,
  GPIO_EXTI14_PIN)`;
- the downlink and AT paths call `GPIO_EXTI14_IoInit(inmode)` for this image;
- `stm32l0xx_it.c` handles and clears `GPIO_PIN_14` without a Chameleon guard;
- the source tree has no `[CHAM-DBG` strings, I2C2 timing words, or old
  `[5v-reg]`/`[vcc-pmos]` banners;
- `main.c` prints `Chameleon reset:%s flags:0x%08lx` from `RCC->CSR` once at
  boot and clears the reset flags afterward;
- the existing `USE_CHAMELEON` branch around the vendor +5 V pulse still
  requires `(mode!=3)&&(power_time!=0)`, so the Chameleon lifecycle is the only
  PB5 owner in MOD3;
- the mode-3 lock, payload encoder call, exact result line, and 44-byte payload
  tests remain.

Run the guard and confirm it fails before editing production files.

```bash
make -C tests build/test_chameleon_integration_guards
tests/build/test_chameleon_integration_guards
```

- [ ] **Step 2: Remove Chameleon ownership from the HAL I2C MSP hooks**

Delete only the `if (hi2c->Instance == I2C2)` blocks added in `bsp.c`. Leave
the vendor I2C1 code byte-for-byte aligned with `master`. The software-I2C path
must not call either MSP hook.

- [ ] **Step 3: Restore PB14 initialization and reporting in BSP**

Remove the Chameleon condition around `GPIO_EXTI14_IoInit(inmode)` and delete
the `PB14_status:I2C2_SDA` diagnostic branch. Keep MOD3 acquisition through
`chameleon_lsn50_acquire()` after the three stock ADC readings. Change the boot
line to exactly:

```c
PRINTF("\r\nChameleon acquisition enabled [soft-i2c-5v]\r\n");
```

Before `iwdg_init()`, retain one compact reset report outside
`CHAMELEON_FIELD_DEBUG`. Read `RCC->CSR` once, classify IWDG, WWDG, software,
low-power, POR/PDR, pin, option-byte, and firewall resets in that priority
order, print the classification and raw flags, then call
`__HAL_RCC_CLEAR_RESET_FLAGS()`. Use this format:

```c
PPRINTF("Chameleon reset:%s flags:0x%08lx\r\n",
        chameleon_reset_cause(chameleon_reset_flags),
        (unsigned long)chameleon_reset_flags);
```

The raw flags resolve combinations that the one-word classification cannot.

- [ ] **Step 4: Restore PB14 reads and reconfiguration in main/AT/IRQ paths**

Use the vendor expression directly wherever the status is sampled:

```c
switch_status = HAL_GPIO_ReadPin(GPIO_EXTI14_PORT, GPIO_EXTI14_PIN);
```

Remove the `USE_CHAMELEON` guard around downlink and AT
`GPIO_EXTI14_IoInit(inmode)` calls. Remove the guard around the PB14 body in
`EXTI4_15_IRQHandler`; retain its vendor `inmode`, `join_network`, mode, flag,
COUNT, clear, and callback logic.

- [ ] **Step 5: Remove obsolete field-debug instrumentation**

Delete `CHAMELEON_FIELD_DEBUG`-only retained-stage and I2C2-register reporting
from `bsp.c`, `main.c`, `at.c`, `command.c`, `stm32l0xx_it.c`, and the Chameleon
hardware header/source. These probes described the failed PB13/PB14 HAL path
and have no valid register meaning for software I2C. Do not remove the normal
one-line `Chameleon result:%s attempts:%u flags:0x%02x` report or the new
reset-flags boot line.

- [ ] **Step 6: Preserve the dedicated MOD3 behavior**

Keep these behaviors and tests:

```text
EEPROM_Read_Config forces mode=3 under USE_CHAMELEON.
AT+MOD accepts 3 and rejects 1, 2, and 4-9.
The MOD command skips EEPROM storage in the dedicated image.
Downlink mode selection accepts only mode 3.
The vendor +5 V pulse remains suppressed when `mode==3`; only
`chameleon_lsn50_hw.c` owns PB5 during a Chameleon acquisition.
```

Do not change `via_chameleon.c`, `chameleon_payload.c`, or payload field order.

- [ ] **Step 7: Run host tests and decoder fixture**

```bash
make -C tests clean test
node /home/phil/Repos/osi-os/scripts/verify-lsn50-chameleon-codec.js
git diff -- 'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/main.c' \
  'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/at.c' \
  'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/stm32l0xx_it.c'
```

Expected: all host tests and codec fixtures pass. The diff restores PB14 while
retaining only the functional MOD3/Chameleon integration around vendor code.

- [ ] **Step 8: Commit the vendor-path restoration**

```bash
git add tests/test_chameleon_integration_guards.c \
  'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/bsp.c' \
  'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/main.c' \
  'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/at.c' \
  'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/command.c' \
  'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/stm32l0xx_it.c'
git commit -m "fix: restore PB14 vendor input behavior"
```

### Task 5: Bound watchdog LSI startup measurement

**Files:**

- Modify: `STM32CubeExpansion_LRWAN/Drivers/BSP/Components/iwdg/iwdg.c`
- Modify: `STM32CubeExpansion_LRWAN/Drivers/BSP/Components/iwdg/iwdg.h`
- Create: `tests/test_iwdg_guard.c`
- Modify: `tests/Makefile`
- Reference: commit `7153161` from `feature/chameleon-i2c-reader`

**Interfaces:**

- Consumes: `TimerGetCurrentTime()` and `TimerGetElapsedTime()` after the RTC
  timebase initialization already present on this branch.
- Produces: a private, bounded `GetLSIFrequency()` with a 37 kHz fallback after
  100 ms.

- [ ] **Step 1: Add the existing unbounded-wait regression test**

Port `tests/test_iwdg_guard.c` from commit `7153161` and add `iwdg_guard` to the
host `TESTS`. It must require:

```text
#define LSI_FALLBACK_HZ          37000U
#define LSI_CAPTURE_TIMEOUT_MS   100U
TimerGetCurrentTime()
TimerGetElapsedTime(captureStart)
return LSI_FALLBACK_HZ;
```

It must forbid the empty `while(uwCaptureNumber != 2)` body and the private
`GetLSIFrequency` declaration in `iwdg.h`.

- [ ] **Step 2: Run the focused test and verify failure**

```bash
make -C tests build/test_iwdg_guard
tests/build/test_iwdg_guard
```

Expected: failure because the current vendor loop can wait forever before the
watchdog is active.

- [ ] **Step 3: Port only the source fix from `7153161`**

Add `#include "timeServer.h"`, move the static function prototype into
`iwdg.c`, define the two constants above, and change the capture wait to:

```c
uint32_t captureStart = TimerGetCurrentTime();
while (uwCaptureNumber != 2U) {
    if (TimerGetElapsedTime(captureStart) >= LSI_CAPTURE_TIMEOUT_MS) {
        HAL_TIM_IC_Stop_IT(&Input_Handle, TIM_CHANNEL_1);
        HAL_TIM_IC_DeInit(&Input_Handle);
        uwCaptureNumber = 0U;
        return LSI_FALLBACK_HZ;
    }
}
```

Do not cherry-pick the commit because it also contains obsolete firmware
artifacts and tests from the older branch.

- [ ] **Step 4: Run focused and full suites**

```bash
make -C tests build/test_iwdg_guard
tests/build/test_iwdg_guard
make -C tests clean test
```

Expected: the guard prints `test_iwdg_guard OK`; all host tests pass.

- [ ] **Step 5: Commit the boot fix separately**

```bash
git add tests/Makefile tests/test_iwdg_guard.c \
  STM32CubeExpansion_LRWAN/Drivers/BSP/Components/iwdg/iwdg.c \
  STM32CubeExpansion_LRWAN/Drivers/BSP/Components/iwdg/iwdg.h
git commit -m "fix: bound watchdog LSI startup wait"
```

### Task 6: Isolate the v1.7 build and produce bench artifacts

**Files:**

- Modify: `build/build.sh`
- Modify: `build/.gitignore`
- Modify: `tests/test_chameleon_build_script.c`
- Create by build: `build/LSN50-chameleon-soft-i2c-5v.bin`
- Create by build: `build/LSN50-chameleon-soft-i2c-5v.hex`
- Generate but do not track:
  `build/LSN50-chameleon-soft-i2c-5v.elf`
- Generate but do not track:
  `build/LSN50-chameleon-soft-i2c-5v.map`

**Interfaces:**

- Consumes: the production sources from Tasks 1-5.
- Produces: target `chameleon-soft-i2c-5v`, its private object directory, the
  exact `[soft-i2c-5v]` boot identity, and a reproducible BIN/HEX pair.

- [ ] **Step 1: Replace the old build-script assertions with v1.7 assertions**

Update `test_chameleon_build_script.c` to require:

```text
chameleon-soft-i2c-5v)
LSN50-chameleon-soft-i2c-5v
-DUSE_CHAMELEON
-DCHAMELEON_POWER_LSN50_5V
-DCHAMELEON_SOFT_I2C_PB12_PB13
src/chameleon_soft_i2c.c
src/chameleon_lsn50_hw.c
OBJDIR="./build/obj/${TARGET_VARIANT}"
```

Forbid the three `chameleon-i2c2-*` case labels,
`CHAMELEON_POWER_EXTERNAL_PMOS`, `CHAMELEON_FIELD_DEBUG`, and absolute
`/home/` paths. Retain the source check that PB5 is open-drain/pull-up and add
checks for the `DEBUG + USE_CHAMELEON` error guard.

- [ ] **Step 2: Run the build-script test and confirm it fails**

```bash
make -C tests build/test_chameleon_build_script
tests/build/test_chameleon_build_script
```

Expected: failure because the script still exposes the v1.6 target family.

- [ ] **Step 3: Reduce the build script to one branch target**

Keep `clean` and replace the target cases with:

```bash
chameleon-soft-i2c-5v)
  TARGET_BASENAME="LSN50-chameleon-soft-i2c-5v"
  EXTRA_CFLAGS=(-UUSE_SHT -DUSE_CHAMELEON \
    -DCHAMELEON_POWER_LSN50_5V \
    -DCHAMELEON_SOFT_I2C_PB12_PB13)
  ;;
```

The clean action removes only this target's object directory and four generated
files. Add compilation of `chameleon_soft_i2c.c` before
`chameleon_lsn50_hw.c`. Do not delete, rebuild, stage, or rename the inherited
v1.6 artifacts.

- [ ] **Step 4: Track only the v1.7 BIN and HEX release files**

Add these exceptions below the existing artifact rules in `build/.gitignore`:

```gitignore
!LSN50-chameleon-soft-i2c-5v.bin
!LSN50-chameleon-soft-i2c-5v.hex
```

Keep ELF, map, and object directories ignored; use them locally for inspection.

- [ ] **Step 5: Prove the forbidden DEBUG combination fails**

Run a direct compile in a disposable directory:

```bash
check_dir="$(mktemp -d)"
trap 'rm -rf "$check_dir"' EXIT
if arm-none-eabi-gcc @build/cflags.rsp -UUSE_SHT -DUSE_CHAMELEON \
    -DCHAMELEON_POWER_LSN50_5V -DCHAMELEON_SOFT_I2C_PB12_PB13 \
    -DDEBUG -c \
    'STM32CubeExpansion_LRWAN/Projects/Multi/Applications/LoRa/DRAGINO-LRWAN(AT)/src/chameleon_lsn50_hw.c' \
    -o "$check_dir/chameleon_lsn50_hw.o"; then
  echo 'FAIL: DEBUG + USE_CHAMELEON compiled' >&2
  exit 1
fi
```

Expected: compiler output contains the explicit incompatibility error.

- [ ] **Step 6: Build from a clean target directory**

```bash
build/build.sh clean
make -C tests clean test
build/build.sh chameleon-soft-i2c-5v
```

Expected: host suite passes; ARM build ends in `BUILD OK` and reports firmware
size without overflowing flash or RAM.

- [ ] **Step 7: Inspect identity, symbols, and outputs**

```bash
test -s build/LSN50-chameleon-soft-i2c-5v.bin
test -s build/LSN50-chameleon-soft-i2c-5v.hex
arm-none-eabi-nm build/LSN50-chameleon-soft-i2c-5v.elf | \
  rg 'chameleon_soft_i2c_(write|write_read|bus_clear)|chameleon_lsn50_acquire'
strings build/LSN50-chameleon-soft-i2c-5v.elf | \
  rg 'Chameleon acquisition enabled \[soft-i2c-5v\]'
if strings build/LSN50-chameleon-soft-i2c-5v.elf | \
    rg 'I2C2 acquisition|\[5v-reg|\[vcc-pmos'; then
  echo 'FAIL: stale v1.6 identity in v1.7 ELF' >&2
  exit 1
fi
arm-none-eabi-size build/LSN50-chameleon-soft-i2c-5v.elf
sha256sum build/LSN50-chameleon-soft-i2c-5v.{bin,hex}
```

Expected: all named soft-I2C and lifecycle symbols exist, only the new boot
identity appears, and both checksums are recorded in the execution report.

- [ ] **Step 8: Confirm inherited artifacts were untouched**

```bash
git diff --exit-code ae3df1a -- \
  build/LSN50-chameleon-i2c2-vcc-pmos.bin \
  build/LSN50-chameleon-i2c2-vcc-pmos.hex \
  build/LSN50-chameleon-i2c2-5v-reg.bin \
  build/LSN50-chameleon-i2c2-5v-reg.hex
```

Expected: exit 0 and no output.

- [ ] **Step 9: Commit the isolated build and bench artifacts**

```bash
git add build/build.sh build/.gitignore tests/test_chameleon_build_script.c \
  build/LSN50-chameleon-soft-i2c-5v.bin \
  build/LSN50-chameleon-soft-i2c-5v.hex
git commit -m "build: produce Chameleon soft-I2C 5V image"
```

### Task 7: Write the bench guide and complete software verification

**Files:**

- Delete: `README-chameleon-v1.6-switched-power.md`
- Create: `README-chameleon-v1.7-soft-i2c-5v.md`
- Review: all files changed since `ae3df1a`

**Interfaces:**

- Consumes: the final artifact names, constants, wiring, and gates from the
  approved spec and implemented code.
- Produces: one unambiguous flash/wiring/test guide for the bench candidate.

- [ ] **Step 1: Replace the obsolete two-variant guide**

Write `README-chameleon-v1.7-soft-i2c-5v.md` with these exact sections:

```text
Image identity
Wiring by terminal and signal
Electrical states and no-pulldown rule
Acquisition and failure behavior
Build and artifact verification
Flash and UART smoke test
Off-state and enable-transient measurements
Protocol endurance and fault injection
Power-consumption logging
Four-week field gate
Known rejected wiring
```

The wiring table must say terminal 14/+5 V to reader VCC, terminal 15 or another
verified GND to reader GND, terminal 20/PB12 to SDA, and terminal 21/PB13 to
SCL. State that the USB-I2C adapter, PB6, PB7, and PB14 remain disconnected
from the reader. State that neither internal nor external pull-downs are fitted.
The known-hazards section must note that removing the LSN50 battery while +5 V
is charged is outside the supported sequence because MCU VDD can collapse
before the reader rail. It must also leave an explicit enclosure-level ESD/TVS
decision before permanent outdoor-cable deployment; this is not a bench-build
gate.

- [ ] **Step 2: Document the operator smoke test**

Use the new BIN/HEX names and record these UART checks after flashing:

```text
Boot line contains: Chameleon acquisition enabled [soft-i2c-5v]
AT+MOD=? reports mode 3 using the vendor-supported query form for this image.
AT+GETSENSORVALUE=0 completes without reset and prints the Chameleon result.
One normal uplink retains Work_mode=3ADC+IIC and the V1 Chameleon fields.
```

Do not describe the software build as field-ready. Label it a bench candidate
until every spec gate has evidence.

The guide must include the numerical gates: measured bus rate no more than
100 kHz and rise time no more than 1 us; VCC/SDA/SCL no higher than 5.5 V;
reader VCC no lower than 3.0 V during measurement; and MCU VDD no lower than
2.0 V during the enable transient, including with a cold or passivated cell.
A loaded +5 V rail below 4.5 V triggers diagnosis but is not an automatic
rejection when it remains above 3.0 V and every protocol gate passes. Capture
+5 V, SDA, and SCL at enable on a single-shot scope at 10 MS/s or faster when
available, with 1 MS/s as the minimum. Archive a verified RT9266 datasheet with
the other hardware sources before using converter-specific limits or expected
waveforms in the bench verdict.

Require at least 500 one-minute sessions with zero resets, no acquisition over
12 s, and at least 99% clean samples. Confirm the reset-cause boot line by
causing one pin reset and one watchdog reset under controlled bench conditions.
Run 20 cycles each with SDA open, SCL grounded, SDA grounded, reader VCC
disconnected, reader absent, reader hot-plugged, the maximum intended cable,
and forced pre-sleep cleanup during a session. Log current for at least 24
hours; target added average current is at most 250 uA at a 5-minute interval or
60 uA at a 20-minute interval, with at most 5 uA steady off-state increment.
Any target exceedance needs a battery-life calculation; more than five times
the average target or more than 5 uA off-state increment rejects the build. Two
units then run four weeks with `i2c_missing` below 0.5%, no fault block over
30 minutes, no unexplained frame-counter reset, no sustained battery decline,
stable array IDs, and plausible Chameleon values against the known-good range
or a co-located reference.

- [ ] **Step 3: Copy the adaptive electrical decision table from the spec**

The guide must record natural VCC/SDA/SCL decay through at least 300 s or until
all nodes stay below 0.1 V. To separate stored charge from a live source, it
then uses a temporary current-limited load to bring +5 V below 0.1 V, removes
that load with PB5 off and both bus pins analog, and watches for rebound or
sourced current for another 5 minutes. It must distinguish these outcomes:

| Observation | Action |
|---|---|
| VCC/SDA/SCL cross below 0.1 V soon enough that 150% of the time is at most 5 s | Set the retry delay to that value, with a 1 s minimum |
| Natural decay takes more than 5 s but reaches below 0.1 V before the shortest deployed interval | Disable same-cycle retry; retry at the next scheduled sample |
| Natural decay does not reach below 0.1 V before the shortest deployed interval | Do not field-release the passive-discharge build; specify and validate active discharge or add hardware |
| After temporary discharge below 0.1 V, a node rebounds to at least 0.3 V or sourced current exceeds 5 uA | Stop and investigate leakage/back-power before adding hardware |
| A residual node remains above 0.3 V before forced discharge but continues falling | Treat it as stored charge; continue the natural-decay observation to at least 5 minutes |
| VDD stays at least 2.0 V, rail settles, and no reset/corruption occurs | Keep the stock +5 V design; use measured startup delay with margin |
| VDD falls below 2.0 V, rail does not settle, MCU resets, or data corrupts | Reject direct +5 V for this hardware/cell condition |

- [ ] **Step 4: Run prose and repository checks**

```bash
node /home/phil/Repos/osi-os/.claude/skills/anti-slop-writing/slop-check.js \
  README-chameleon-v1.7-soft-i2c-5v.md \
  docs/superpowers/specs/2026-08-17-lsn50-chameleon-switched-5v-soft-i2c-design.md \
  docs/superpowers/plans/2026-08-17-lsn50-chameleon-switched-5v-soft-i2c.md
git diff --check
```

Expected: `slop-check: PASS (no tier-1 findings)` and no whitespace errors.

- [ ] **Step 5: Run final clean verification**

```bash
make -C tests clean test
build/build.sh clean
build/build.sh chameleon-soft-i2c-5v
node /home/phil/Repos/osi-os/scripts/verify-lsn50-chameleon-codec.js
git diff --check
git status --short --branch
```

Then repeat Task 6's symbol, banner, size, checksum, forbidden-identity, and
unchanged-v1.6-artifact checks. Review that `git diff ae3df1a --
via_chameleon.c chameleon_payload.c` is empty and that no HX711 file changed.

- [ ] **Step 6: Request independent code review**

Use `superpowers:requesting-code-review`. Give the reviewer the spec, this
plan, the full diff since `ae3df1a`, and the verification output. Require
explicit review of open-drain semantics, TIM2 wrap/deadlines, STOP-on-error,
cleanup order, PB14 restoration, watchdog limits, target isolation, and
payload invariance. Fix all blocking findings and rerun Step 5.

- [ ] **Step 7: Commit the guide and any review fixes**

```bash
git add README-chameleon-v1.7-soft-i2c-5v.md \
  README-chameleon-v1.6-switched-power.md
git commit -m "docs: add Chameleon soft-I2C bench guide"
```

- [ ] **Step 8: Hand off the hardware gates**

Report the BIN/HEX paths, SHA-256 hashes, build size, host/codec results, and
the fact that software verification is complete. Stop before field-release
claims. The next work item is the spec's bench sequence: waveform/rate,
stored-charge and back-power separation, enable sag, 500-cycle endurance,
fault injection, 24-hour energy logging, then two units for four weeks.
