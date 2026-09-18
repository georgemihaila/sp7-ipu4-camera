# OV7251 source-6 receiver-timing experiment

Date: 2026-09-18
Kernel: `6.19.8-3.surface.fc43.x86_64`
Test boot: `efafc942-a353-4789-83ac-ada682044fcf`

## Question

The original Windows receiver constructor uses source-specific MIPI timing
fields.  Its source-7 configuration writes `1155` for the clock/first-data
timing and `1269` for the data timing.  The original binary does not expose a
source-6 register trace, so applying those values to source 6 was a bounded
inference test, not a recovered source-6 configuration.

This test combined that receiver change with the previously tested OV7251
link-frequency index correction, which selected the endpoint's 319.2 MHz PLL.
No source-7 or RGB path was changed.

## Controlled result

The candidate IPU4P module was loaded through a temporary modprobe override.
Its SHA-256 was:

```text
30bad75ea3aed9fc9972abd7c9e41708cf013923d74d254a57c04d0542ffc0a2
```

The candidate OV7251 module was:

```text
435979b83accfd190f4d2ef103d4a44d641d8ef7b8fd332049d595e9eca03296
```

The controlled route was:

```text
ov7251 2-0060 -> Intel IPU4 CSI-2 1 -> Intel IPU4 CSI2 BE SOC -> /dev/video42
```

The negotiated format was `Y10_1X10` on the media route and `Y10 ` at
640×480, with bytesperline 1280 and sizeimage 615680.  The bounded
`v4l2-ctl --stream-mmap=120` test recorded:

```text
capture_bytes=0
VIDIOC_STREAMON returned -1 (Connection timed out)
```

The same boot recorded:

```text
source-6 inferred MIPI timing applied: clock/first-data=1155 ticks data=1269 ticks
pll-readback ... 30b1=0x04 30b3=0x85 30b4=0x01
no frames from ov7251 2-0060 after start                         60 times
csi2-1 receiver error status 0x4000                              32 times
```

The receiver snapshot showed one lane, `RX_CONFIG=0x3`, no data-lane HS
activity, and `fatal_receiver_errors=0`.  The `0x4000` value is the known
clock-lane ULP/escape event, not evidence of a frame or payload.

These values demonstrate that the inferred timing was applied and that the
sensor PLL correction was active.  They do not demonstrate CSI streaming,
payload delivery, or a usable IR image.

## Rollback and verification

The temporary overrides were moved to the retained evidence directory, the
candidate OV7251 module was moved aside, the baseline IPU4P module was
restored, and `depmod -a` was run before reboot.

Post-rollback verification showed:

- IPU4P module SHA-256:
  `10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce`;
- distribution OV7251 module SHA-256:
  `00cfa05cbdf46a8d6c55b077d7729fa419a3a072d88bb343b5985d4e3ad4deac`;
- no source-6 timing override remained under `/etc/modprobe.d`;
- OV7251 revision 7 still probed at I2C address `0x60` and `/dev/video42`
  remained present;
- `sp7-camera-bridge.service` was active.

## Conclusion

The inferred source-6 timing values are not a fix.  Together with the
tested port configurations and the OV7251 link-frequency correction, this is
the third bounded CSI-side experiment without a frame.  Per the execution
plan, further speculative PHY or timing mutations stop here.  The next
required evidence is an exact source-6 Windows/firmware receiver register
trace, or synchronized instrumentation of the Linux receiver and OV7251
clock/reset/power/`0x0100`/data-lane state during one start.
