# OV7251 first-start trace

Date: 2026-09-18
Kernel: `6.19.8-3.surface.fc43.x86_64`
Boot ID: `c22ae949-1742-4cdf-8897-ca9cd915d32e`

## Attribution

The temporary diagnostic IPU4P module was loaded from the normal override
path with SHA-256:

```text
8916827633a9374390cc452f0524df56e2a4f0b6ee61d5611fce564a07b82a56
```

Its two port-config parameters were at their defaults, `0x3895` and
`0x3895`; no port-config modprobe file was present. The INT3472 `vdda`
experiment module remained the active provider. The diagnostic module was
replaced afterward with the exact baseline IPU4P module
`10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce`, then
the system was rebooted and the bridge service restored.

## Capture setup

The bridge was stopped for exclusive access. The dynamically discovered route
was:

```text
ov7251 2-0060 -> Intel IPU4 CSI-2 1 -> Intel IPU4 CSI2 BE SOC -> /dev/video42
```

The route used `Y10_1X10` and `640x480`, with dynamic media links enabled
using flag `5`. The capture node negotiated fourcc `Y10 `, bytesperline
`1280`, and sizeimage `615680`.

Output `/var/tmp/ir-first-start-trace-1789737037.raw` remained 0 bytes;
`VIDIOC_STREAMON` returned `-1 (Connection timed out)`.

## Monotonic first-start timeline

```text
135.616017  pipeline stream begin, source=6, frames_done=0
135.616556  CSI-2 1 receiver request, source=6, external=ov7251, nlanes=1
135.617101  CSI-2 1 receiver enabled, timing=0/627/0/647
135.617529  firmware pre-start, source=6, stream_count=1, remote_streams=1
135.617695  firmware stream-open request, 640x480, MIPI DT 0x2b
135.617863  firmware stream-open complete
135.618073  firmware stream-start request
135.618281  firmware stream-start complete
135.618443  receiver error snapshot: receiver_errors=0x4000, hs=0, lp=0
135.618769  OV7251 s_stream(1) called
135.665374  OV7251 s_stream(1) returned success
```

The receiver state at enable and after the first error was:

```text
RX_ENABLE=0x1  RX_NOF_ENABLED_LANES=1  RX_CONFIG=0x3  RX_STATUS=0x0
HS=0x0  LP=0x0  ctermen=0  csettle=627
d0termen=0  d0settle=647  d1termen=0  d1settle=0
```

The `0x4000` event therefore occurs before the sensor's stream-on callback.
After the callback returns successfully, repeated retries still show no
data-lane HS activity. The run ended with no clean frames after 31 attempts,
a stream-stop timeout, and cleanup error `-5`.

## Interpretation

Demonstrated:

- IPU4P receiver setup succeeds for source 6 and one lane.
- Firmware stream open and start both acknowledge.
- OV7251 `s_stream(1)` returns success.
- No data-lane HS transition occurs after sensor stream-on.

The remaining cause is not proven. The leading boundary is now the OV7251
driver's physical stream state after `s_stream(1)`—sensor clock/reset/power
sequencing or an absent MIPI output—not firmware-open failure. A software
success return is not proof that the sensor is driving the lane. The next
experiment should instrument or obtain the exact OV7251 driver source and
observe its stream register, clock, reset, and power transitions. No further
CSI PHY or port-config mutation is justified by this trace.
