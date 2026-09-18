# OV7251 firmware resources and probe sequence

Fresh evidence: 2026-09-18, Surface Pro 7, BIOS `24.109.140`, running
`6.19.8-3.surface.fc43.x86_64`. The complete ACPI dump and disassembly are
outside the repository at
`/tmp/ov7251-ir-baseline-20260918/acpi-dump-root/`; no AML method was
executed.

## Driver provenance

The loaded modules are distribution-signed Fedora/linux-surface modules with
the recorded paths, hashes, and vermagic in `docs/ir-baseline.md`. The
matching public linux-surface source reference is tag
`surface/v6.19.8` at commit `57d61aff0b53b089227f5a794363fec829114fc5`.
Relevant retrieved source hashes are:

```text
drivers/platform/x86/intel/int3472/discrete.c
  4a125466ec4cdf5827c40c8bf5375f84509b92336f44936f7b390e3f27493c7e
drivers/media/i2c/ov7251.c
  3588a52e0a3a4dfe23dd3425db95388d93af17c6f8eebe5b004ee8d1eea5aee9
```

The enabled linux-surface repository exposes the matching binary RPM but did
not provide a downloadable SRPM for this installed build. The tag/source
comparison is therefore recorded as the exact versioned source reference,
while the binary module hash remains the attribution for anything loaded on
the target. The source tag's INT3472 code still has the generic
POWER_ENABLE fallback `con_id = "avdd"`; it has no INT347E-specific `vdda`
entry.

The upstream fix is resolved, not inferred from the mailing-list announcement:

```text
media.git commit 59cab094b8b50d7d4483bb04143294e1fb66aacc
platform/x86: int3472: map the ov7251 power enable GPIO to "vdda"
author: D. Manresa; committed by Sakari Ailus on 2026-09-02
```

It adds an INT347E-specific POWER_ENABLE map to `vdda`. The source tag used by
the running kernel predates that commit. The patch is a small backport to the
older `.hid` table shape; the current upstream `.hids` form must not be copied
blindly into this target.

## Probe sequence and exact failure boundary

The matching `ov7251.c` does the following during `ov7251_probe()`:

1. Validate firmware endpoint data and acquire the external clock.
2. Require a supported clock rate: 19.2 MHz or 24 MHz.
3. Acquire `vdddo`, `vddd`, and `vdda` regulators, then request the `enable`
   GPIO.
4. Enable I/O, analog, and core regulators in that order; enable the clock;
   wait; assert `enable`; wait for 65,536 clock cycles.
5. Write the global-init array beginning with register `0x0103 = 0x01`.
6. Read chip ID registers `0x300a = 0x77` and `0x300b = 0x50`, then read the
   revision and register the media entity.

The current boot log reaches step 5 and fails with I2C `-110`. It also logs
`vdda` as a dummy regulator. This makes the consumer-name mismatch the
highest-value one-variable experiment, but does not by itself prove that the
physical rail is gated correctly.

## Fresh ACPI/resource table

| Resource | Firmware owner/evidence | Linux provider or consumer | Current state | Interpretation |
| --- | --- | --- | --- | --- |
| Sensor | `\_SB.PCI0.I2C3.CAM3`, HID/CID `INT347E`, UID `0`, DDN `OV7251-CRDD` | I2C client `i2c-INT347E:00` on DesignWare bus 2 | No driver symlink; no media entity | Sensor is enumerated but unbound |
| I2C address | CAM3 `_CRS` SerialBus source `\_SB.PCI0.I2C3`, address bytes `0x60 0x00` | `i2c_designware.2` | Boot log reaches the transaction | Transport exists; no broad scan performed |
| Dependency | CAM3 `_DEP` contains `ICL2` | `INT3472:02`, UID `2` | Bound to `int3472-discrete` | Provider is present before sensor probe |
| Power enable | ICL2 `_DSM` returns `0x0100630B`: type `0x0b`, pin `0x63`, sensor-on `1` | `INT3472:02-avdd` regulator from the discrete INT3472 driver | `disabled`, no consumer | Generic source names it `avdd`; OV7251 asks for `vdda` |
| Reset | ICL2 `_DSM` returns `0x01004A00`: type `0x00`, pin `0x4a`, sensor-on `1` | INT3472 reset mapping to OV7251 GPIO consumer `enable` | Mapping exists in the source contract | Preserve this mapping; do not add a guessed GPIO |
| External clock | CAM3/ICL2 firmware clock resources plus SSDB `mclkspeed = 0x0124f800` | IPU bridge software node and OV7251 clock consumer | Not exercised because probe stops earlier | Fresh SSDB value is 19.2 MHz |
| CSI endpoint | CAM3 SSDB bytes decode to CSI link/port `6`, one lane; `ipu-bridge.c` supplies one link frequency `319200000` | IPU bridge generated endpoint properties | No sensor endpoint registered | These are post-probe requirements, not the current I2C blocker |
| Emitter/LED | CAM3/ICL2 expose only the two GPIO resources above in the fresh dump | `/sys/class/leds` has privacy LEDs for RGB `INT33BE`/`INT347A`, none for INT347E | No IR emitter device identified | Built-in illumination remains unverified; do not equate privacy LED with IR flood |

The local historical DSDT in `/home/george/repos/sp7-camera/work/microsoft-sp7/`
is consistent with the fresh dump for the relevant `0x63` power and `0x4a`
reset resources, but the fresh dump is the authoritative evidence for this
boot and firmware revision.

## Working RGB comparison

The same host has working RGB sensors and the bridge owns only the
v4l2loopback endpoints. Their INT3472 privacy LEDs and sensor supply mappings
are distinct from CAM3/ICL2. No RGB module, media link, or bridge configuration
was changed during this investigation.

## Ordered hypotheses and experiments

| Priority | Hypothesis | Evidence | One-variable experiment |
| --- | --- | --- | --- |
| 1 | INT347E POWER_ENABLE is registered as `avdd` but OV7251 requests `vdda`; the physical rail never enables | Fresh logs show `vdda` dummy, `INT3472:02-avdd` disabled/no consumer, and first I2C write `-110`; exact upstream fix matches this boundary | Build and load only the INT3472 backport, retry only `INT347E:00`, and require a real `vdda` consumer plus successful chip access |
| 2 | Reset polarity or resource ownership is wrong | Firmware reports a reset resource and the old source already maps INT347E RESET to `enable`; no missing second GPIO is demonstrated | Do not change yet; inspect the post-fix provider and sensor logs. Change only if the vdda trial proves power but reset access still fails |
| 3 | Clock/CSI endpoint is wrong | Endpoint data is available, but I2C fails before media registration | Test only after a successful chip-ID read; do not change CSI timing in the probe experiment |
| 4 | I2C controller/address/transport is independently defective | DesignWare timeout is observed, but it occurs exactly at the first unpowered-sensor transaction | Compare the same bounded retry after the vdda mapping; no `i2cdetect`, guessed `i2cset`, or forced power |

The Stage 1 decision is therefore to build the minimal INT3472 module first.
No OV7251 source change is justified before this experiment. Hardware access
must remain serialized; the RGB bridge must be stopped only for the bounded
module/binding trial and restored afterward.
