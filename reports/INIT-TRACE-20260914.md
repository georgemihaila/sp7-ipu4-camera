# Initialization trace — 2026-09-14

## Baseline before loading the rebuilt modules

The active kernel is `6.19.8-sp7cam-test1`. The currently loaded module
hashes differ from the newly built instrumented modules, so the read-back
messages below will only appear after the staged modules are loaded.

The uninstrumented comparison is still useful:

| Camera | Result | Evidence |
|---|---|---|
| Rear OV8865 / CSI-0 | 3 frames, 47,941,632 bytes | `/captures-instrument-baseline/rear.raw` |
| Front OV5693 / CSI-2 | timeout, 0 bytes | `/captures-instrument-baseline/front.raw` |

The front attempt reaches stream start but repeatedly reports D-PHY
non-recoverable synchronization and frame-sync errors. It exhausts all 30
sensor stream bounces. The rear stream therefore provides the control case:
the capture path, firmware, and general IPU stack can deliver valid RAW10
frames on this boot.

## What the rebuilt trace will distinguish

- `trace power ...`: logical reset/powerdown GPIO states, 19.2 MHz clock
  rate, and `avdd/dovdd/dvdd` regulator enable read-back.
- `trace sensor ...`: OV5693 stream/reset/chip-ID, mode dimensions, timing,
  and MIPI timing registers after initialization and stream-on.
- `trace phy ...`: requested and read-back PHY building-block values,
  including BB10 used by the front camera.
- `trace csi ...`: CSI receiver enable, lane count, LP/HS status, and clock
  and data settle registers before and after stream-on.
- `trace isys ...`: IPU runtime power-island resume/suspend boundaries and
  PHY setup ordering.

The rebuilt modules are staged at:

`/home/george/repos/sp7-camera/work/instrumented-modules-20260914/`

Run `./trace-capture.sh` after loading that stage to produce a complete
rear-versus-front trace bundle under `reports/init-trace-*`.

## Captured result after reboot

The instrumented sensor module was loaded with `pkexec` (including its
`v4l2-cci` dependency), then `./trace-capture.sh` was run. The paired bundle
is:

`reports/init-trace-20260914-221619/`

The rear still delivered 47,941,632 bytes. The front again timed out with a
zero-byte payload. A focused run with OV5693 dynamic debug enabled is in:

`reports/front-stream-debug-20260914-2220/`

The front-side sequence is now observable end to end:

- `xvclk=19200000Hz`; reset is asserted and released; AVDD enables. DOVDD
  and DVDD are reported as dummy regulators, matching the ACPI-described
  board resources rather than a failed regulator operation.
- The sensor answers as `id=0x5690`, and the initialized mode reads back as
  2592x1944, HTS 0x0a80, VTS 0x07b8, with MIPI registers 0x481f=0x30 and
  0x4837=0x0a.
- Stream-on writes to register `0x0100` succeed and read back as `0x01`.
- CSI-2 port 2 is configured for source 7, two lanes, link frequency
  419.2 MHz, `csettle=684`, and `dsettle=661`. BB10 writes read back as
  `cphy_dll=0x1001b`, `dphy_dll=0x41`, and `AFE=0x40000015`.
- Despite that, the receiver remains at `status=0`, `lp=0`, `hs=0x301` and
  repeatedly reports D-PHY non-recoverable synchronization and frame-sync
  errors (`receiver_errors=0x603/0x683/0x8683`). All 30 sensor bounces fail.

This separates basic initialization from transmission: there is no evidence
of a missing Linux power, clock, reset, stream-on, source mapping, or BB10
write. Since this same front camera is known to work under Windows, the
hardware should be treated as good. The remaining fault boundary is the
Linux-side IPU4P SIP1 receiver configuration/firmware interaction: likely a
missing electrical-correction or source-7 sideband/start-sequence operation,
not a broken sensor or cable. The repo's existing timing/PHY sweeps narrow
that further but do not yet identify the required IPU4P sequence.
