# OV7251 illuminator experiment preparation

Date: 2026-09-20. The standalone builder is source/build preparation only. The
normal source installer invokes it and manages the resulting `ov7251.ko`; the
installer does not load the module, enable the illuminator, or perform capture.

## Provenance

The work starts at commit `e7c699276b23661d8d5bea25ba5b974dff9b36fa`.
The pinned Linux source is:

- `/var/tmp/ov7251-surface-v6.19.8.c`
- SHA-256 `3588a52e0a3a4dfe23dd3425db95388d93af17c6f8eebe5b004ee8d1eea5aee9`
- linux-surface `surface/v6.19.8`, source commit
  `57d61aff0b53b089227f5a794363fec829114fc5`

The candidate patch is
`patches/ov7251-illuminator-experiment.patch`. The reproducible staging
helper applies, in order:

1. `ov7251-stream-diagnostics.patch`
2. `ov7251-pll-mipi-readback.patch`
3. `ov7251-mipi-state-readback.patch`
4. `ov7251-illuminator-experiment.patch`

The helper applies all patches with `--fuzz=0`, runs the static validator and
mocked-I/O lifecycle test, and builds an out-of-tree OV7251 module against
`/lib/modules/6.19.8-3.surface.fc43.x86_64/build`. The prepared artifact was
rebuilt after the runtime-PM failure-path correction with kernel
`6.19.8-3.surface.fc43.x86_64`, vermagic matching that kernel, and SHA-256:

```
2daacb2fea6176955ef15ac887fad5d20c2b7bf57904bd6b45030376f05f6f75
```

The four-patch staged sensor source hash after applying the reproducible stack
is `7469cd8c8d00a663e91299fec5c7c0233abe7c5500424b5d4f6466def6114aab`.

The build emitted the expected compiler-version warning: the running kernel
was built with GCC 15.2.1 and this build used GCC 16.2.1. No
`modules_install`, `depmod`, `modprobe`, service restart, capture, or
hardware write was performed. The currently installed compressed module remains
the unrelated distribution artifact
`00cfa05cbdf46a8d6c55b077d7729fa419a3a072d88bb343b5985d4e3ad4deac`;
the installed module has no source link or `srcversion`, so source-to-object
identity is not proven.

The Windows comparison binary remains attributable by SHA-256
`201d52f2a1d9441ca20b0a7801ec450276e9161a9aa5617fcc165941c74f0d39`.
Register meanings and specification page references are recorded in
[the Windows control analysis](ov7251-windows-illuminator-control-analysis.md),
using OV7750/OV7251 Product Specification rev. 1F, version 2.12.

## Candidate fields and lifecycle

The only experimental writes are read-modify-write operations on:

| Register | Mask | Current Linux mode value | Candidate value | Operation |
| --- | ---: | ---: | ---: | --- |
| `0x3005` | `BIT(3)` | `0x00` | `0x08` | STROBE output direction |
| `0x3b96` | `BIT(7)` | `0x40` | `0xc0` | frame-PWM enable, retaining bit 6 |

The mode arrays, exposure/gain controls, PWM/divisor/duty values, pattern,
span, line timing, CSI and PLL configuration are not changed. The candidate
does not write `0x3027`, `0x3009`, or any other member of the diagnostic
range.

The driver exposes the lifecycle internally as two methods:
`ov7251_ir_illuminator_on()` enables the frame-PWM permission and then the
STROBE output gate; `ov7251_ir_illuminator_off()` clears the gate and then the
frame-PWM permission. They are intentionally driver-internal at this stage;
the read-only module option controls whether the stream lifecycle invokes
them.

The exact sensor lifecycle is:

```
runtime resume
  -> PLL configuration
  -> selected mode table
  -> V4L2 control setup (exposure/gain/VBLANK)
  -> optional bounded readback
  -> if experimental_strobe_output:
       RMW 0x3b96[7] = 1, read back
       RMW 0x3005[3] = 1, read back
  -> existing 0x0100 = 1
```

This placement is deliberate. It leaves the existing mode values and exposure
calculation intact, and prepares the output while the sensor is still in
standby. Frame-triggered behavior begins only when the existing stream-on write
runs. A separate start-time callback is not required for the hypothesis:
initialization may already configure a stream-triggered output.

Normal stop first clears `0x3005[3]`, then clears `0x3b96[7]`, verifies both
readbacks and only then issues the existing `0x0100 = 0` and
`pm_runtime_put()`. If either cleanup write/readback fails, the primary
start/stop result is preserved and the cleanup fault is recorded separately.
The runtime power-off callback retries cleanup while the sensor is still
powered, then completes the existing clock/GPIO/regulator shutdown and
returns `0` for that successful PM transition. It never performs I2C cleanup
after `power_on` becomes false and does not claim independent optical
shutdown. A failed start takes the same cleanup path before releasing runtime
PM. The output gate is cleared before the frame-PWM permission so a failed
stop cannot intentionally leave a gated output enabled.

The driver keeps three distinct states: `power_on` means register I/O is
currently valid; `strobe_cleanup_needed` means a register cleanup remains
pending; and `strobe_cleanup_error` retains the first cleanup/verification
fault. Any cleanup fault also sets `strobe_recovery_required`, so a later
experimental enable fails closed instead of silently reusing an unresolved
state. Recovery is to keep the experimental option disabled, use the normal
power/reprobe path, verify the bounded diagnostic state, and only then create
a fresh experimental driver instance; this patch does not clear a recorded
fault automatically. Removal first barriers pending runtime-PM work, disables
runtime PM, and performs at most one powered shutdown before destroying driver
state. If PM already reports suspended, removal skips register cleanup rather
than issuing I2C to an unpowered sensor.

With the option disabled (the static default), the on/off methods are not
called and the normal register sequence is unchanged. The separate
read-only `strobe_diagnostics=1` parameter enables bounded reads only; it
does not enable the output. The existing single-byte I2C helpers now reject
short positive transfers as `-EIO`, while successful transfers are unchanged.

## Bounded diagnostics and validation

When `strobe_diagnostics=1`, the owning driver reads the fixed set
`0x3005`, `0x3027`, `0x3009`, and `0x3b80..0x3b96` after control setup,
after a candidate enable, before stop, and after the disable attempt. This is
per stream lifecycle, never per frame, and reports failed reads as
`value=invalid`. No userspace I2C access is added.

The reproducible validator is
`scripts/ir/check-ov7251-illuminator-experiment.py`. It checks:

- both controls are default-off/read-only module parameters;
- both fields use read-before/write/readback RMW and reject preserved-bit changes;
- the enable is after V4L2 controls and before `0x0100=1`;
- normal-stop cleanup precedes standby and runtime-PM release;
- failed-start cleanup precedes runtime-PM release;
- runtime power-off retries cleanup before clock/regulator shutdown, returns
  PM success after that shutdown, and retains cleanup faults;
- removal does not duplicate resource shutdown or perform cleanup I2C while
  PM reports the sensor suspended, and barriers pending runtime-PM work first;
- the full bounded diagnostic register set is present; and
- the candidate helper does not write diagnostic-only registers.

The executable mocked-I/O check is
`scripts/ir/test-ov7251-illuminator-failure-path.py`. It checks default-off
no-write behavior and successful bit-preserving RMW, then injects one-shot
cleanup read-before, write, and readback failures independently at both
candidate registers. It checks that emergency power-off still returns PM
success; confirms no later I2C or duplicate resource shutdown while unpowered;
blocks experimental reopen after an unresolved fault; exercises suspended and
active removal; and preserves separate primary start/stop and cleanup errors.
It is a faithful lifecycle-contract model, not a kernel callback or
board/optical test. The build helper runs both checks and then builds the
prepared module. These are static/build results only; they do not prove that
the SP7 board connects STROBE to an emitter.

## Activation-gated live test

Do not activate this candidate until the patch review and rollback path are
approved. When activation is authorized, use the existing temporary module
selection workflow and keep the distribution module untouched.

For each run, hold the same scene, pixel format, mode, image processing,
exposure and analogue gain. Record control readbacks and use an independent
IR-sensitive detector at the suspected emitter aperture; an indicator LED or
auto-exposed image is not sufficient.

1. Baseline: `experimental_strobe_output=0`, diagnostics enabled if desired.
   Start the already-qualified OV7251 capture, record detector baseline,
   fixed exposure/gain, frame validity and the diagnostic register state.
2. Candidate: `experimental_strobe_output=1` and diagnostics enabled. On
   start, require readback of `0x3005[3]=1` and `0x3b96[7]=1`, with every
   other bit of those two bytes equal to its pre-RMW value. Keep the fixed
   exposure/gain and image processing identical.
3. Stop and require both bits to read zero before runtime power release; the
   detector must return to baseline. Repeat start/stop for three bounded
   cycles and retain all logs.

Expected observations:

- Detector response during candidate streaming, unchanged fixed controls and
  clean post-stop readback support the sensor-output hypothesis.
- Correct register readback but no detector response is inconclusive: first
  distinguish absent board connection/detector sensitivity from configuration
  failure. Do not infer LED current or add timing/current writes.
- Any readback mismatch, invalid read, register write error, cleanup error,
  output persisting after stop, or loss of the already-working capture path is
  an abort condition.

Rollback is immediate stop, confirm the cleanup error/readback state, return to
the untouched distribution OV7251 module through the existing temporary-module
rollback procedure, and recheck the qualified IR and both RGB paths. The
candidate must never be left continuously enabled. Three cycles are only the
initial gate; the final qualification requirement remains the documented
ten-cycle shutdown test.

## Remaining activation uncertainties

1. The installed OV7251 object is not source-identifiable; only the pinned
   source/build artifact is attributable.
2. The datasheet names the two candidate fields, but does not establish the
   SP7 board connection, external driver, LED polarity, safe current, or
   detector geometry.
3. `0x3b96[7:6]` interaction with the existing `0x3b80` trigger fields is
   not fully observable statically. The candidate intentionally leaves those
   timing fields unchanged.
4. The Linux mode keeps span `0x0000001a`; Windows dynamically derives
   `0x3b8c..0x3b8f` from exposure and clamps it to `0x34..0x308`. This
   experiment does not copy that Windows timing behavior, so illumination
   width across exposure changes remains unresolved.
5. Sensor readback proves register state, not optical output. If runtime
   evidence is needed after this test, the narrow observation is the
   candidate driver's start/stop readback and cleanup return value, correlated
   with the independent detector—not an undefined full Windows trace.
6. The known Windows `0x1400086d0` exposure/span producer and
   `0x1400089a0` property callback remain static evidence; their parent
   property-to-callback mapping and any external controller ownership are not
   needed to prepare this two-field candidate but remain unresolved for a
   production control path.
