# IR illuminator: OV7251 STROBE/frame-PWM control path

This page documents the candidate OV7251 illuminator implementation and its
hardware boundary. It is not part of the default installed OV7251 module: the
candidate is built from `patches/ov7251-illuminator-experiment.patch` into a
temporary, kernel-specific module. The option defaults off, no GPIO is guessed,
and no optical illumination result is claimed.

## What is being controlled

The OV7251's STROBE output is a sensor timing output. The candidate changes two
bits only:

| Register | Mask | Meaning used by the candidate | Baseline / candidate example |
|---|---:|---|---:|
| `0x3005` | `BIT(3)` = `0x08` | STROBE I/O direction: input/high-impedance to output | `0x00 -> 0x08` |
| `0x3b96` | `BIT(7)` = `0x80` | frame-PWM enable | `0x40 -> 0xc0` |

The lower six bits of `0x3b96` are preserved. In particular, the candidate
does not overwrite the mode's existing bit 6, polarity bit, step-pixel bit, or
start-option fields. It also does not write `0x3027` (STROBE source select),
`0x3009` (manual STROBE value), the pattern register `0x3b81`, the divisor or
duty registers `0x3b82..0x3b87`, or the pulse timing fields
`0x3b88..0x3b95`.

This is important: enabling a sensor output bit is not proof that an external
IR LED, transistor, current limiter, or board trace is connected to that pin.
The repository contains no independent measurement of LED current or emitted
IR power.

## Module controls and default state

The candidate adds two read-only module parameters to the external OV7251
driver:

```c
static bool experimental_strobe_output;
module_param_named(experimental_strobe_output,
                   experimental_strobe_output, bool, 0444);

static bool strobe_diagnostics;
module_param_named(strobe_diagnostics, strobe_diagnostics, bool, 0444);
```

`experimental_strobe_output=0` is the safe default. It leaves the existing
sensor mode tables and the normal `0x0100` stream sequence unchanged.
`strobe_diagnostics=1` enables bounded register readback only; it does not
enable the output.

The reproducible builder accepts a source file whose SHA-256 is the pinned
linux-surface OV7251 source and emits a temporary `.ko`:

```sh
./scripts/build-ov7251-illuminator-experiment.sh \
  /var/tmp/ov7251-surface-v6.19.8.c \
  6.19.8-3.surface.fc43.x86_64 \
  /var/tmp/ov7251-illuminator-experiment.ko
```

The builder applies the stream diagnostics, PLL/MIPI readback, MIPI state, and
illuminator patches with zero fuzz, runs the static validator and mocked I/O
failure-path test, and builds against the exact prepared kernel tree. It does
not install or load the result.

## Exact I2C transactions

The OV7251 driver uses a 7-bit I2C client address of `0x60`. The register
payloads below exclude the I2C address byte. `ov7251_write_reg()` sends a
three-byte transfer `[register_hi, register_lo, value]`; `ov7251_read_reg()`
sends the two-byte register pointer and then receives one byte.

Read and write examples are therefore:

```text
read  0x3005: I2C write [0x30, 0x05], then I2C read [before]
write 0x3005: I2C write [0x30, 0x05, before | 0x08]

read  0x3b96: I2C write [0x3b, 0x96], then I2C read [before]
write 0x3b96: I2C write [0x3b, 0x96, before | 0x80]
```

The candidate never uses a blind constant write. Its helper is equivalent to:

```c
static int ov7251_update_strobe_bit(struct ov7251 *sensor,
                                    u16 reg, u8 mask, bool enable)
{
    u8 before, after, verify;

    if (ov7251_read_reg(sensor, reg, &before))
        return -EIO;
    after = enable ? before | mask : before & (u8)~mask;
    if (ov7251_write_reg(sensor, reg, after))
        return -EIO;
    if (ov7251_read_reg(sensor, reg, &verify))
        return -EIO;
    if ((verify & mask) != (after & mask))
        return -EIO;
    if ((verify ^ before) & (u8)~mask)
        return -EIO; /* another field changed: fail closed */
    return 0;
}
```

The patched helpers also reject short positive I2C transfers as `-EIO`; a
positive result smaller than the requested three-byte write, two-byte register
pointer, or one-byte read is not accepted as success.

## Stream lifecycle ordering

The candidate keeps `power_on`, `strobe_cleanup_needed`,
`strobe_cleanup_error`, and `strobe_recovery_required` separately. The exact
normal start order is:

```text
runtime resume / power rails / 19.2-MHz clock / enable GPIO
    -> PLL and selected OV7251 mode table
    -> V4L2 control setup (exposure, gain, VBLANK)
    -> optional diagnostics readback
    -> if experimental_strobe_output:
         RMW 0x3b96[7] = 1, verify
         RMW 0x3005[3] = 1, verify
    -> existing 0x0100 = 0x01 (sensor streaming)
```

The output is enabled last so a partially configured sensor cannot expose the
STROBE pin before its frame-PWM permission is present. The normal stop order is
the reverse at the ownership boundary:

```text
RMW 0x3005[3] = 0, verify       # close the output gate first
RMW 0x3b96[7] = 0, verify       # remove frame-PWM permission
0x0100 = 0x00                    # sensor standby
runtime-PM release
```

If normal-stop cleanup fails, the primary stream result is retained but the
first cleanup error is recorded and future experimental enable is refused.
Runtime power-off retries cleanup while `power_on` is still true, marks the
sensor unpowered before disabling clock/GPIO/regulators, and never issues I2C
after the power state becomes false. Driver removal drains pending runtime-PM
work and skips I2C cleanup when PM already reports the sensor suspended.

## Bounded diagnostic reads

With `strobe_diagnostics=1`, the driver reads the fixed set below at stream
boundaries, not once per frame:

```text
0x3005, 0x3027, 0x3009,
0x3b80 0x3b81 0x3b82 0x3b83 0x3b84 0x3b85 0x3b86 0x3b87
0x3b88 0x3b89 0x3b8a 0x3b8b 0x3b8c 0x3b8d 0x3b8e 0x3b8f
0x3b90 0x3b91 0x3b92 0x3b93 0x3b94 0x3b95 0x3b96
```

The log labels each value with a lifecycle phase such as
`before-experiment`, `after-experiment`, `before-stop`, or `after-disable`.
Read failure is logged as `value=invalid`; it is not converted into a guessed
register value.

The Windows comparison established that some mode tables contain
`0x3005=0x08`, `0x3b96=0xc0`, `0x3b81=0xaa`, and a mode-dependent
`0x3b8c..0x3b8f` span. That is evidence for a control/timing hypothesis, not
permission to copy the whole table into Linux. The candidate deliberately
does not alter exposure-derived span, frame pattern, duty, pulse width, CSI,
PLL, or lane configuration.

## Validation and current boundary

The source-only checks are:

```sh
python3 scripts/ir/check-ov7251-illuminator-experiment.py patched-ov7251.c
python3 scripts/ir/test-ov7251-illuminator-failure-path.py
```

They verify read-modify-write preservation, enable/disable ordering, default-
off parameters, powered cleanup, duplicate-shutdown protection, PM/remove
ordering, and failed-start recovery. They do not prove the STROBE pin toggles
on a physical board, an LED emits IR, or the output improves a decoded frame.

The prepared candidate has not been installed or loaded as the default module,
and an enabled source-6 capture was not qualified. A future hardware test must
keep the RGB bridge unchanged, hold exposure/gain fixed, capture a valid
changing IR stream, and use an independent IR-sensitive detector. Do not
replace this with a generic GPIO write or a brightness observation from an RGB
camera.

Related source and evidence:

- [`patches/ov7251-illuminator-experiment.patch`](../patches/ov7251-illuminator-experiment.patch)
- [`docs/ov7251-illuminator-experiment.md`](ov7251-illuminator-experiment.md)
- [`docs/ov7251-windows-illuminator-control-analysis.md`](ov7251-windows-illuminator-control-analysis.md)
- [`docs/ov7251-illuminator-live-20260920.md`](ov7251-illuminator-live-20260920.md)
