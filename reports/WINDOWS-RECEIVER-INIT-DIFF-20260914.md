# Windows source-7 receiver initialization difference

Date: 2026-09-14. Static comparison only; no hardware state was changed by
this report.

## New concrete difference

The Windows `CCsi::Prepare` routine at `iacamera64.sys` VA `0x140049210`
performs more GPREG initialization than the current Linux IPU4P path.

On the first CSI prepare, after the source-dependent reset pulse, Windows
conditionally writes two clock-related fields to **both** CSI GPREG banks:

| Operation | Legacy bank | Combo bank |
|---|---:|---:|
| HPLL frequency | `0x66808` | `0x6e808` |
| ISCLK ratio | `0x6680c` | `0x6e80c` |

The values come from the prepared CCsi object fields `+0x78` and `+0x7c`.
Windows logs them as `GPREG_FB_HPLL_FREQ` and `GPREG_ISCLK_RATIO`. The writes
are skipped only when either field is zero. The same routine then writes the
port-config fields at `0x66814` and `0x6e814`, retrying until readback agrees.

For source 7, the same routine selects the combo reset bank and pulses:

```text
0x6e800: 0x00 -> 0x80 -> 0x00
```

For working source 3 it selects the legacy bank and uses its source-specific
reset bit:

```text
0x66800: 0x00 -> 0x200 -> 0x00
```

The reset pulse and port-config values were already tested independently in
earlier experiments. The HPLL/ISCLK writes have not been tested as a matched
Windows initialization sequence.

## Linux comparison

The current Linux IPU4P setup in `ipu4-isys.c` does this at ISYS setup:

1. writes CSI2 port config at GPREG `+0x14` in both banks;
2. writes the BSCAN exclude value;
3. configures BB10 DLL/AFE registers;
4. enables the receiver later and writes receiver timing counters.

It does not write GPREG `+0x8` or `+0xc`, and the IPU4P CSI stream path has no
equivalent of Windows `CCsi::Prepare` for these fields. This is a real
software initialization omission, although it is not yet proven to be the
front-camera fix because the Windows values are not yet known.

## Source-3/source-7 comparison

The Windows receiver block-building routine takes the same main path for
source 3 (`0..3`) and source 7 (`6..9`); the special `SP_IF=1000` write is for
ports `4,5,10,11` and does not apply to either camera. The source-specific
differences are therefore the GPREG bank/reset selection and the configured
receiver object, not an extra source-7 SP_IF write.

The current Linux rear success does not disprove the omission: the rear is on
the legacy/source-3 path, while the failing front uses the combo/source-7
path. The next safe step is to capture/log the current values of both GPREG
clock fields, then resolve the Windows values from the CSI configuration input
before making one source-7-only test.

## Linux instrumentation added

`ipu4-isys.c` now logs the four GPREG clock words during IPU4P ISYS setup.
It also has disabled-by-default module parameters:

```text
csi_gpreg_hpll_freq=-1
csi_gpreg_isclk_ratio=-1
```

When both are explicitly supplied, Linux writes the pair to both banks before
the existing port-config writes. The loader accepts the corresponding
environment variables `CSI_GPREG_HPLL_FREQ` and `CSI_GPREG_ISCLK_RATIO`, so a
candidate can be tested without editing the driver. The instrumented modules
built successfully against `6.19.8-sp7cam-test1` and are staged under
`/home/george/repos/sp7-camera/work/instrumented-gpreg-20260914`.

The currently loaded kernel module predates this instrumentation; a reboot is
required before the new read-only log appears. No GPREG override has been
enabled and no speculative MMIO write has been made.

## Important limitation

This finding does not justify copying the IPU4 CSE event-correction routine.
It is a separate, direct GPREG initialization path in Windows. The IPU4P
event-correction function may still be unrelated or may require platform data,
but that question should be tested separately.

## Post-reboot instrumented trace

The instrumented stack was loaded after a clean reboot with `pkexec`, with the
clock override disabled. The module parameters remained `-1`, and all four
readbacks were zero:

```text
trace isys gpreg clocks: legacy hpll=0x0 isclk=0x0 combo hpll=0x0 isclk=0x0
```

The bounded capture trace then reproduced the split result:

| Path | Result |
|---|---|
| Rear / source 3 (`ov8865`) | 3 complete frames, 47,941,632 bytes |
| Front / source 7 (`ov5693`) | timeout, 0 bytes |

During the front attempt the kernel recorded repeated `DPHY non-recoverable
synchronization error` and `Frame sync error` messages, followed by 30 sensor
bounce retries. This is receiver synchronization failure, not merely a
userspace capture timeout.

## Static data-flow follow-up

Further disassembly of `CCsi::Prepare` confirms that the HPLL and ISCLK values
are copied into the prepared object from runtime platform/configuration data;
the Prepare routine itself contains no source-7 constants from which the exact
Windows values can be recovered. No separate, source-7-only MMIO or sideband
writer was identified in the receiver call path. The earlier port-config,
reset, timing, CRC/DRC, and AFE differences have already been tested without
resolving source 7.

The local upstream IPU3 driver provides `(HPLL, ISCLK) = (2, 0xc)` as a
reference tuple, but that is not proof of the IPU4P/Surface Pro 7 values. It is
therefore only a labeled candidate for the next one-variable-at-a-time test,
not a confirmed fix.

## No-reboot reload attempt

The module parameters are writable at runtime, but writing them alone does not
perform the GPREG writes. A fresh runtime-resume log was obtained without a
reboot by unloading and reloading the ISYS module together with its unused
CSS/VB2 dependencies. The candidate was then confirmed at both banks:

```text
trace isys gpreg clock override: hpll=0x2 isclk=0xc
trace isys gpreg clocks: legacy hpll=0x2 isclk=0xc combo hpll=0x2 isclk=0xc
```

However, unloading the live camera driver while desktop camera enumeration was
active caused PipeWire format-query warnings, followed by a stream-release
timeout and:

```text
isys power cycle required
```

The subsequent capture is not a valid candidate result: the rear stream timed
out and the front device failed to open. A normal reboot is therefore the safe
way to restore the media graph before any further candidate test. The no-reboot
route is technically possible, but it is not a safe operational test path on
this running desktop.

## Kernel recovery change

The driver now treats `reset_needed` as recoverable. On the next video-node
open, it serializes recovery, resumes the ISYS MMU parent if necessary, forces
the existing IPU bus runtime-PM suspend path (which powers the buttress off),
waits briefly, and forces the matching resume path. Only after both operations
succeed does it clear `reset_needed`; an FLR in progress is still rejected.

This is deliberately a real IPU power-island cycle, not a register write or a
module-parameter change. The recovery-enabled, GPREG-instrumented modules were
built against `6.19.8-sp7cam-test1` and staged at:

```text
/home/george/repos/sp7-camera/work/recovery-gpreg-20260914
```

The target loader now selects that stage by default. The code compiled and the
full kernel/module build completed; hardware validation still requires one
clean reboot because the earlier live module unload left this boot's media
graph incomplete. After reboot, a front-camera failure should be followed by a
new open attempt so the kernel can exercise the recovery path and log either
the power-down/up sequence or the exact PM error.

The rebuilt driver also exposes a guarded manual trigger at
`/sys/bus/intel-ipu4-bus/devices/intel-ipu60/force_power_cycle`. Writing `1`
is rejected while a video or stream handle is open; otherwise it runs the same
MMU/buttress cycle and returns an error if either PM transition fails. This
provides a direct way to replay receiver setup after changing the GPREG test
parameters without unloading the driver.

## Controlled BB10 tests and receiver write ordering

After the clean reboot, the manual trigger was used to replay setup without
rebooting. The Windows BB10 tuple and its two single-field variants were then
tested with a fresh IPU power cycle before each capture:

| BB10 setting | Result |
|---|---|
| `DRC=8`, `AFE=0x4f3cf008` (Windows tuple) | `hs=0`, no frames, 0 bytes |
| `DRC=8`, `AFE=0x15` (DRC only) | `hs=0x301`, DPHY/frame-sync errors, 0 bytes |
| `DRC=32`, `AFE=0x4f3cf008` (AFE only) | `hs=0`, no frames, 0 bytes |

The community defaults (`DRC=32`, `AFE=0x15`) were restored and replayed after
the tests. These results rule out either BB10 field, alone or together, as the
missing source-7 fix. The GPREG candidate `HPLL=2, ISCLK=0xc` was held
constant for these comparisons.

Static disassembly also resolved a source-7 ordering difference. In Windows
`CCsiRx::ConfigBlockBuilding` (`0x14004e6a0`), the source-7 branch writes the
combo receiver base `0x6c100 + 0x4` with the two-lane count, then writes
`0x6c100 + 0x8` with receiver config `3`; the subsequent enable path writes
`0x6c100 + 0x0` with `1`. Linux had written config before lane count. The
IPU4P stream path is now patched to use Windows' lane-count, config, enable
ordering while preserving the existing values. The full kernel/module build
completed successfully and the rebuilt module is staged at
`/home/george/repos/sp7-camera/work/recovery-gpreg-20260914`.

The first live unload/reload attempt was not usable for validation: after
ISYS/PSYS removal, the replacement PSYS module became stuck in kernel async
module initialization (`async_synchronize_cookie_domain`). The parent driver
was left resident; a subsequent child-module insertion did complete, allowing
the ordering test below without a hard reset.

## Follow-up: Windows `ConfigSkewCaliTimer` ordering

The Windows source-7 branch also calls `ConfigSkewCaliTimer` between the lane
count write and receiver config. Its helper resolves to a read/modify/write of
the buttress `CSI_BSCAN_EXCLUDE` register at `0x100d8`, with the resulting
value `0x08040200`. Linux already writes that exact value during ISYS setup.

To isolate ordering, the rebuilt ISYS module gained an opt-in
`windows_bscan_late=Y` parameter that rewrites the same value immediately after
the lane-count write. The readback was already `0x08040200` before the replay.
The front capture still produced 0 bytes and the same `hs=0x303` /
`receiver_errors=0x683` D-PHY/frame-sync failures. A color-bar sensor test had
the same result. The parameter was restored to `N`; the rear control still
captured 47,941,632 bytes.

This ruled out the known Windows receiver writes and their ordering as the
missing source-7 initialization, but did not test the distinct timing values
computed by Windows `ConfigMipiClk`; that candidate is tested below.

## Follow-up: literal Windows source-7 `ConfigMipiClk` timing

The Windows source-7 object carries a receiver frequency field of
`350000000`. Static x86 arithmetic in `CCsiRx::ConfigMipiClk` produces
`1155` for the clock/first-data timing value and `1269` for the data timing
value. The earlier `1368/1322` test did not exercise these values; it used a
different Linux-derived candidate.

An opt-in `windows_source7_mipi_timing=Y` path was added to the IPU4P driver.
On source 7 it writes the literal Windows raw receiver slots:

```text
0x30 = 0
0x34 = 1155
0x38 = 0
0x3c = 1269
```

After a clean reboot, the new module was loaded and the parameter enabled
without `pkexec` using non-interactive `sudo`. The front capture then
succeeded:

```text
front.raw  30233088 bytes
sha256     22a43a81da71304fdf051448d82269774c2cec18739beadb11306dbd2310b127
```

The rear control capture remained successful:

```text
rear.raw   47941632 bytes
sha256     ac9b26114a722effa1e56f4c9218addc05f0a274cf40a2b1b9124ff44810c80e
```

The kernel trace confirms `config=0x3`, `lanes=2`, the literal raw timing
readbacks, and no recurring DPHY/frame-sync failure. This isolates the missing
initialization to the source-7 MIPI receiver timing calculation: Linux was
using the 419.2-MHz sensor link-frequency-derived timing, while Windows uses
the source-7 receiver field that evaluates to `1155/1269`.
