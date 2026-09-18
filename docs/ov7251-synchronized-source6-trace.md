# Synchronized OV7251 and source-6 trace

Date: 2026-09-18
Kernel: `6.19.8-3.surface.fc43.x86_64`
Boot ID: `f7f91fc9-c146-4259-adf8-6560cd887d0e`

## Purpose

This is a diagnostic-only start trace at the boundary between the OV7251's
physical MIPI output and IPU4P source 6. It does not change receiver timing,
port configuration, pixel format, illumination, or preview behavior.

The IPU4P source tree's checked-in read-only diagnostics were built as:

```text
IPU4P ISYS candidate:
/home/george/repos/sp7-ipu4-camera/linux-6.19.8/drivers/media/pci/intel/ipu4/intel-ipu4p-isys.ko
SHA-256: 7e7c1bd355966a954aaf0411915cef3bf14782872dc97d345599022741ead837

OV7251 trace candidate:
/var/tmp/ov7251-synchronized-trace.ko
SHA-256: e9f7d4728939a2aca5cda3d63e6c135cdffe58f8af224cbf379e4350a5b79531
```

Both candidates matched the running kernel vermagic. They were hot-loaded
without rebooting and were removed after the bounded test. The RGB bridge was
not stopped.

The `/dev/video42` device resolves through
`/sys/bus/intel-ipu4-bus/drivers/intel-ipu6` and reports the runtime identity
`intel-ipu6 intel-ipu60`; the checked-in IPU4P module is the module hosting
this driver path. This name is not evidence of a second media device.

## Controlled route and command

The route was explicitly configured as:

```text
ov7251 2-0060 -> Intel IPU4 CSI-2 1 -> Intel IPU4 CSI2 BE SOC -> /dev/video42
```

The media bus format was `Y10_1X10/640x480`, and `/dev/video42` used `Y10 `.
The bounded command was:

```bash
timeout --signal=INT --kill-after=3s 30s \
  v4l2-ctl -d /dev/video42 --stream-mmap=120 \
  --stream-to=/var/tmp/ov7251-synchronized-trace-20260918-194428/capture.raw \
  --verbose
```

Evidence is retained outside Git under
`/var/tmp/ov7251-synchronized-trace-20260918-194428/`.

## Synchronized first start

The following events share the same kernel monotonic timeline. The first
source-6 start began at approximately `5154.950` seconds:

```text
5154.950  pipeline stream begin source=6
5154.952  receiver request source=6 external=ov7251 2-0060 nlanes=1
5154.953  source-6 platform: legacy/combo hpll=0 isclk=0 override=0 port=0x3895 bscan=0x8040200
5154.953  source-6 PHY: bb4=(0x1001b,0x41,0x4000000f)
                   bb6=(0x1001b,0x41,0x40000015)
                   bb10=(0x1001b,0x41,0x40000015)
                   bb12=(0x1001b,0x41,0x4000000f)
                   bb14=(0x1001b,0x41,0x40000015)
5154.954  receiver enabled rx=1 lanes=1 config=0x3 timing=0/627/0/647 hs=0 lp=0
5154.954  firmware open/start complete source=6 input=640x480 dt=0x2b
5154.955  receiver error status=0x4000; hs=0 lp=0
5154.955  call s_stream(1) for ov7251 2-0060
5154.955  OV7251 xclk=19200000, regulators enabled, reset/enable gpio=1
5154.971  OV7251 global init complete
5154.994  OV7251 0x0100 write ret=0; readback=0x01
5154.998  OV7251 PLL readback 30b3=0x4b 30b4=0x03
5155.000  OV7251 MIPI state readback 4805=0x10 4838=0x02 483a=0x08
5155.000  s_stream(1) succeeded; receiver snapshot hs=0 lp=0
5155.613  no frame; retry 1
5175.926  no clean frames after 31 sensor attempts
5177.966  source-6 stream stop timeout; receiver error status=0x4000
```

## Demonstrated result

- The Linux receiver starts source 6 with one lane and RAW10 before calling
  the sensor's `s_stream(1)`.
- The receiver's first error snapshot already contains `0x4000`, while its
  sampled data-lane HS and LP fields are both zero.
- The OV7251 reaches power-on, 19.2 MHz external clock, reset/enable, global
  initialization, successful stream-on I2C write/readback, and stable MIPI
  register readbacks.
- The receiver still samples no data-lane HS activity after sensor stream-on.
- The capture produced `0` bytes; the kernel made 31 sensor attempts, logged
  16 `0x4000` receiver errors, and timed out stopping the failed stream.

This is stronger synchronized evidence than separate sensor and receiver
logs, but it still does not prove whether the OV7251 physically drives a
transition. The receiver's `hs=0` result is an observation at the IPU input,
not a scope measurement at the sensor pins.

## Decision boundary

The next test should be physical measurement or a known-good Windows trace:

1. If the sensor clock/reset/power and CSI clock/data lanes show no MIPI
   transitions during the Linux `0x0100=0x01` interval, investigate platform
   sequencing, reset polarity, clock delivery, or resource ownership.
2. If CSI transitions are present but the IPU snapshot remains `hs=0`, compare
   the IPU4P source-6 PHY/receiver configuration and start ordering against
   the measurement or Windows trace.
3. Only if both sides show valid activity should packet data type, virtual
   channel, or receiver routing become the next branch.

No additional PLL, CSI timing, port, or pixel-format mutation is justified by
this trace.

## Rollback and RGB preservation

The temporary route was disabled and the normal modules were restored without
reboot:

```text
distribution OV7251 SHA-256: 00cfa05cbdf46a8d6c55b077d7729fa419a3a072d88bb343b5985d4e3ad4deac
distribution IPU4P ISYS SHA-256: 10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce
```

The bridge processes remained alive with the same PIDs (`11563`, `11566`,
`11579`), and `/dev/video60` and `/dev/video61` continued to report front and
back `Video Capture` devices.
