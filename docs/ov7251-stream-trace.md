# OV7251 stream-boundary diagnostic

Date: 2026-09-18. Target kernel: `6.19.8-3.surface.fc43.x86_64`.

This is a Stage 2 diagnostic only. It does not change the sensor sequence or
claim that the IR camera captures images.

## Reproducible build

The source is the `linux-surface` `surface/v6.19.8` OV7251 driver at commit
`57d61aff0b53b089227f5a794363fec829114fc5`, SHA-256
`3588a52e0a3a4dfe23dd3425db95388d93af17c6f8eebe5b004ee8d1eea5aee9`.
The diagnostic patch is
`patches/ov7251-stream-diagnostics.patch`; the build helper is
`scripts/build-ov7251-stream-trace.sh`.

```sh
curl -fsSL \
  https://raw.githubusercontent.com/linux-surface/kernel/surface/v6.19.8/drivers/media/i2c/ov7251.c \
  -o /var/tmp/ov7251-surface-v6.19.8.c
scripts/build-ov7251-stream-trace.sh \
  /var/tmp/ov7251-surface-v6.19.8.c \
  6.19.8-3.surface.fc43.x86_64 \
  /var/tmp/ov7251-stream-trace.ko
```

The loaded diagnostic module was SHA-256
`feb0292ca742868d32d3299152be206727be4d951cb175d4465c514e593cb975` with
vermagic `6.19.8-3.surface.fc43.x86_64 SMP preempt mod_unload`. The rebuild
produced the same hash. The compiler warned that GCC 16.2.1 differed from the
GCC 15.2.1 used for the distribution kernel; the module built and loaded with
matching vermagic.

The patch logs the xclk, regulator/power boundary, logical `enable` GPIO,
PLL/mode completion, and writes/readbacks OV7251 register `0x0100` around
`s_stream()`. It preserves all original return values and cleanup paths.

## Controlled run

The vdda-mapped INT3472 experiment remained loaded. The bridge was stopped,
the diagnostic OV7251 module was selected through a temporary modprobe
override, and the system was rebooted into boot ID
`c91f58c8-a651-4e1c-9122-2b0c68c7774f`.

The route was:

```text
ov7251 2-0060 -> Intel IPU4 CSI-2 1 -> Intel IPU4 CSI2 BE SOC -> /dev/video42
```

It used `Y10_1X10` at `640x480`, video fourcc `Y10 `, bytesperline `1280`,
and sizeimage `615680`. The capture command requested 120 mmap frames:

```sh
v4l2-ctl -d /dev/video42 --stream-mmap=120 \
  --stream-to=/var/tmp/ov7251-stream-trace-20260918/capture-correct.raw \
  --verbose
```

The raw output remained 0 bytes and `VIDIOC_STREAMON` returned
`-1 (Connection timed out)`. The IPU retried 30 times. Across those retries:

| Boundary | Observed |
| --- | ---: |
| OV7251 `s_stream(1)` calls | 31 |
| `0x0100` write result `0` | 31 |
| `0x0100` readback `0x01` | 31 |
| standby readback `0x00` | 31 |
| no-frame retries | 30 |
| stream-stop timeouts | 1 |

The power trace reported `xclk=19200000`, regulator enable success, logical
reset/enable GPIO value `1`, and global initialization complete. The receiver
still reported `csi2-1 receiver error status 0x4000`; no valid frame or data
payload was observed.

Evidence is retained outside Git under
`/var/tmp/ov7251-stream-trace-20260918/`, including the capture log, graph,
negotiated video format, and test module copy.

## Rollback

The distribution compressed module was never overwritten. Its SHA-256 remains
`00cfa05cbdf46a8d6c55b077d7729fa419a3a072d88bb343b5985d4e3ad4deac`.
Rollback moved the temporary module and modprobe file out of their live paths,
ran `depmod -a`, and rebooted. The rollback boot ID was
`2aa6950d-54c0-4b0a-bc85-ad829d15618c`.

The post-rollback checks demonstrated:

- the distribution `ov7251.ko.xz` was selected by normal modprobe resolution;
- no temporary OV7251 override or uncompressed override module remained;
- OV7251 still probed revision 7 at `0x60` under the INT3472 vdda experiment;
- `sp7-camera-bridge.service` was active.

## Interpretation

Demonstrated: power-on reaches global initialization, the sensor accepts and
reports the streaming register value, and the IPU path still receives no
frames. The failed raw capture is a real negative result, not a probe-only
success.

Not demonstrated: that the sensor physically drives MIPI, that reset polarity
is correct electrically, that the built-in IR emitter works, or that any
client preview is possible. The remaining physical stream-state cause is a
hypothesis; proving it needs board-level or a source-backed sensor-sequence
experiment. No such functional change is retained by this commit.
