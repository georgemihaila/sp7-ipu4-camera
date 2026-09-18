# OV7251 MIPI-state readback experiment

Date: 2026-09-18. Target kernel: `6.19.8-3.surface.fc43.x86_64`.

This was a read-only sensor diagnostic. It did not change the OV7251 register
sequence, PLL selection, IPU4P receiver configuration, or CSI timing.

## Build and live attribution

The module was built from the pinned `surface/v6.19.8` OV7251 source
(SHA-256 `3588a52e0a3a4dfe23dd3425db95388d93af17c6f8eebe5b004ee8d1eea5aee`)
using `scripts/build-ov7251-stream-trace.sh` and the existing stream/PLL
diagnostics plus `patches/ov7251-mipi-state-readback.patch`.

The loaded diagnostic module SHA-256 was:

```text
e9f7d4728939a2aca5cda3d63e6c135cdffe58f8af224cbf379e4350a5b79531
```

It was loaded and removed in memory without rebooting or overwriting the
distribution module. The RGB bridge processes remained alive and continued to
own `/dev/video60` and `/dev/video61` throughout the test.

## Controlled route and result

The IR-only media route was explicitly enabled with the repository's dynamic
link flags:

```text
ov7251 2-0060 -> Intel IPU4 CSI-2 1 -> Intel IPU4 CSI2 BE SOC -> /dev/video42
```

The route negotiated `Y10_1X10` at 640x480 and the capture node used fourcc
`Y10 `, bytesperline 1280, and sizeimage 615680. The bounded 120-frame capture
ran from 19:06:55 to 19:07:18 local time. The output remained 0 bytes;
`VIDIOC_STREAMON` returned `Connection timed out` after the driver's 31 sensor
attempts, followed by a stream-stop timeout.

On every stream-on retry, all read-only sensor transactions succeeded and the
same values were returned:

```text
0x4805 = 0x10
0x4806 = 0x0f
0x4837 = 0x19
0x4838 = 0x02
0x483a = 0x08
0x4865 = 0x0c
```

The existing trace also recorded `0x0100` write/readback success with
`0x01`, the 19.2 MHz external clock, successful power/global initialization,
and successful PLL/MIPI register reads. The IPU4P source-6 receiver still
reported `0x4000`, with no data-lane HS activity and no payload bytes.

## Interpretation

Demonstrated:

- The complete IR media route can be enabled without disturbing the RGB
  bridge.
- The OV7251 accepts the Linux stream sequence and exposes stable readable
  MIPI control/status values during each retry.
- The no-frame failure remains reproducible after the route is correctly
  configured; it was not caused by the temporary diagnostic module or a
  missing media link.

Not demonstrated:

- That `0x4865=0x0c` proves the sensor is electrically driving MIPI high-speed
  data. The datasheet defines the field, but this experiment does not provide
  an independent lane probe or a known-good comparison value.
- That any register write, PLL change, lane timing change, reset-polarity
  change, or emitter change would fix streaming.

The next evidence required is an attributable Windows/firmware OV7251 stream
sequence, such as the `OV7251_MSHW0173_ICL.cpf` configuration, or a synchronized
source-backed comparison that identifies a concrete sensor-side discrepancy.
Further speculative IPU4P PHY mutations are not justified by this result.

## Rollback proof

The temporary module was unbound and removed, the normal `ov7251` module was
reloaded, and the sensor again reported revision 7 at address `0x60`. The
distribution module remained at its original SHA-256:

```text
00cfa05cbdf46a8d6c55b077d7729fa419a3a072d88bb343b5985d4e3ad4deac
```

The temporary IR links were disabled. No reboot was used.
