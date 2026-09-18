# OV7251 Windows binary and Linux source-6 correlation

Date: 2026-09-18
Target: Surface Pro 7, IPU4P, OV7251 on firmware source 6

## Inputs

This was a read-only static comparison. No module, device register, service,
or boot state was changed.

| Input | SHA-256 |
| --- | --- |
| `/home/george/repos/sp7-camera/work/microsoft-sp7/extracted/SurfaceUpdate/camera/iacamera64.sys` | `3a92f0c648d1c8f7be3890c47c7b509a300405e22b64b519668ffbfd0d474f9` |
| `/home/george/repos/sp7-camera/work/microsoft-sp7/extracted/SurfaceUpdate/ov7251/ov7251.sys` | `201d52f2a1d9441ca20b0a7801ec450276e9161a9aa5617fcc165941c74f0d39` |

## Correlations demonstrated from the current Linux source

- Linux compact CSI-2 index 1 maps to firmware source 6.  The mapping is the
  inverse of the firmware-port conversion in `ipu-isys.c`, and the source is
  assigned in `ipu-isys-csi2.c`; it is not an accidental use of source 7.
- The source-6 receiver base is `0x6c000` relative to ISYS, while source 7 is
  `0x6c100`.  Runtime initialization binds those bases to the compact CSI
  entries.
- The endpoint supplies one lane to Linux, and the CSI-2 start trace records
  source 6 with one lane before the sensor `s_stream(1)` callback.
- Linux programs the same BSCAN exclusion value recovered from the Windows
  binary: `0x08040200`.  The Linux register is buttress offset `0x100d8`.
- The Linux IPU4P PHY definitions match the Windows common register families:
  CPHY DLL override, CPHY RX control, DPHY DLL override, DPHY RX control, and
  BB AFE configuration.  Linux configures BB 4, 6, 12, and 14 globally, with
  an existing Surface Pro 7 source-7 BB 10 quirk.

## What the Windows binary exposes

Static disassembly of `iacamera64.sys` identifies common receiver code using
the register offsets corresponding to `0x10100`, `0x1014c`, and `0x10174`.
It also contains strings for HPLL frequency, ISCLK ratio, CSI-2 port
configuration, MIPI clock configuration, and CRC/DRC/BB-AFE setup.  The
Windows BSCAN code writes the value `0x08040200` while preserving selected
existing bits.

The only exact MIPI-rate literal recovered is `0x14dc9380`, which represents
350 MHz in the source-7 `ConfigMipiClk` path.  It is not evidence for the
OV7251 source-6 rate.  The binary passes source-6 receiver values through
runtime/configuration objects; no source-6-only HPLL, ISCLK, port, PHY, BB,
or receiver-timing constant was recovered.

The separate `ov7251.sys` image contains reset-related diagnostics, but no
source-6 receiver table or attributable sensor-to-CSI timing sequence.

## Consequence

This analysis confirms that the Linux source-6 selection and BSCAN mapping are
not the obvious mismatch, while leaving the source-6 runtime PHY/clock values
unknown.  It does not justify another guessed port, timing, or PHY mutation.

The required next evidence is one of:

1. a known-good Windows trace that records source-6 receiver/PHY state during
   the same start sequence; or
2. a narrowly instrumented Linux start that logs source-6 BB/AFE, HPLL/ISCLK,
   port, receiver timing, OV7251 power/reset/clock, `0x0100`, and lane state
   together.

The current Linux trace already covers HPLL/ISCLK/override/port and BSCAN;
the current host does not permit safe userspace BAR reads to add BB/AFE
values.  A diagnostic module change would require loading a new kernel
module; it should be deferred until the missing evidence justifies the
necessary restart.
