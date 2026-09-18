# OV7251 Windows register-sequence comparison

Date: 2026-09-18. This is a read-only reverse-engineering result from the
original Surface Pro 7 Windows driver, not a claimed fix.

## Attributed input

The inspected driver is:

```text
/home/george/repos/sp7-camera/work/microsoft-sp7/extracted/SurfaceUpdate/ov7251/ov7251.sys
PE32+ Windows x64
size: 171096 bytes
SHA-256: 201d52f2a1d9441ca20b0a7801ec450276e9161a9aa5617fcc165941c74f0d39
```

The register tables are embedded in the driver's `.rdata` section. The table
interpreter at `0x140004000` consumes 16-byte records containing a register
address and value; the write helper at `0x140003e6c` writes the sensor
register. The selected mode is called from `0x140007c78` after common reset
and standby tables.

## Windows mode contract

The mode descriptors at virtual address `0x140020a10` describe four usable
variants, repeated for several firmware profiles:

| mode | output | nominal rate | lanes | table |
|---|---:|---:|---:|---:|
| 0 | 648x488 | 30 fps | 1 | `0x14001c730` |
| 1 | 648x488 | 60 fps | 1 | `0x14001cff0` |
| 2 | 648x488 | 90 fps | 1 | `0x14001d8b0` |
| 3 | 324x244 | 180 fps | 2 | `0x14001e170` |

The mode records also contain `800000000` as their rate field. Its exact
semantic name in the Windows ABI is not established here; it must not be
blindly equated with the Linux V4L2 link-frequency value.

The 30-fps table includes these relevant values:

```text
0x30b0=0x0a  0x30b1=0x01  0x30b3=0x7d  0x30b4=0x03  0x30b5=0x05
0x3800..0x3807 = 00 00 00 00 02 8f 01 ef
0x3808..0x380f = 02 88 01 e8 0a e0 02 3e
0x4801=0x0f  0x4806=0x0f  0x4837=0x19
```

The 60-fps table changes the clock/timing portion:

```text
0x30b3=0x8f  0x30b4=0x04
0x3808..0x380f = 02 88 01 e8 03 a0 03 5e
0x4837=0x1d
```

The 90-fps table returns to the 240-class PLL values and uses:

```text
0x30b3=0x7d  0x30b4=0x03
0x3808..0x380f = 02 88 01 e8 03 a0 02 3e
0x4837=0x19
```

Before the selected mode table, Windows writes reset/standby and common
sensor values, including `0x0103=0x01`, `0x0100=0x00`, the PLL2 values, and
the common MIPI controls. The driver later enables streaming separately.

## Comparison with the current Linux source

The Linux driver advertises 640x480 modes and programs `0x3808..0x380b` as
`02 80 01 e0`. Its 30-fps mode uses `0x380c..0x380f = 03 a0 06 bc`; its
60-fps mode uses `03 a0 03 5c`; and its 90-fps mode uses `03 a0 02 3c`.
Linux already programs `0x4801=0x0f`, `0x4806=0x0f`, and `0x4837=0x19` in
the relevant tables.

This makes the Windows geometry/timing sequence a concrete experiment rather
than a PHY guess. It does not establish that the Windows mode descriptor's
`800000000` field is the correct Linux receiver link frequency, nor that
648x488 is the only cause of the current no-payload result.

## Evidence boundary

Demonstrated: the original binary contains and executes attributable OV7251
register tables with 648x488 modes and the values above.

Hypothesis: the Linux 640x480 geometry/timing or its associated PLL choice is
the remaining sensor-side mismatch that prevents IPU4P source-6 payload.

Untested: applying this sequence on Linux, changing the IPU4P receiver
metadata, and obtaining a frame. The experiment is isolated to a temporary
module and can be rolled back without rebooting or overwriting the installed
distribution module.
