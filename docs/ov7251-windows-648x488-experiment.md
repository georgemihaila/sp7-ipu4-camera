# OV7251 Windows 648x488 mode experiment

Date: 2026-09-18
Kernel: `6.19.8-3.surface.fc43.x86_64`

## Question

The original Surface Pro 7 Windows OV7251 driver contains attributable
648x488 mode tables. This experiment applied the Windows 30-fps sensor-side
register sequence to Linux, while retaining the existing diagnostic and
receiver-trace instrumentation, to test whether the Linux 640x480 geometry
or associated sensor timing was the missing payload condition.

This was a temporary module experiment. The patch is retained as a
reproducible test artifact, but is not selected by the normal build or
installed over the distribution module.

## Candidate provenance

Windows input:

```text
/home/george/repos/sp7-camera/work/microsoft-sp7/extracted/SurfaceUpdate/ov7251/ov7251.sys
SHA-256: 201d52f2a1d9441ca20b0a7801ec450276e9161a9aa5617fcc165941c74f0d39
```

The Linux candidate was built from the exact target source with the
source-backed experiment in
`patches/ov7251-windows-648x488-experiment.patch`.

```text
candidate: /var/tmp/ov7251-windows-648x488.ko
SHA-256: 86c73b07e058818aa61649dc5643bfbc79ae230fac5111d9e20153d307be913b
vermagic: 6.19.8-3.surface.fc43.x86_64 SMP preempt mod_unload
```

The candidate changed the 30-fps mode to 648x488, applied the Windows PLL
and timing values, and changed the corresponding mode metadata. It did not
change the IPU4 receiver configuration.

## Controlled test

The candidate was hot-loaded after unbinding only `i2c-INT347E:00`. The
working RGB bridge was left running. The route was explicitly configured as:

```text
ov7251 2-0060 -> Intel IPU4 CSI-2 1 -> Intel IPU4 CSI2 BE SOC -> /dev/video42
```

The media route used `Y10_1X10/648x488`; `/dev/video42` negotiated:

```text
width=648 height=488 pixelformat=Y10
bytesperline=1344 sizeimage=657216
```

The bounded capture command was:

```bash
timeout --signal=INT --kill-after=3s 30s \
  v4l2-ctl -d /dev/video42 --stream-mmap=120 \
  --stream-to=/var/tmp/ov7251-windows-648x488-20260918/capture.raw \
  --verbose
```

Evidence was saved outside the repository in
`/var/tmp/ov7251-windows-648x488-20260918/`.

## Demonstrated result

The sensor accepted the candidate register writes and the diagnostic
readback showed the Windows values during the actual capture attempt:

```text
mode=648x488 link_freq_idx=0
30b0=0x0a 30b1=0x01 30b3=0x7d 30b4=0x03 30b5=0x05
4801=0x0f 4806=0x0f 4837=0x19
4805=0x10 4838=0x02 483a=0x08 4865=0x0c
```

The capture produced no payload:

```text
capture bytes: 0
VIDIOC_STREAMON returned -1 (Connection timed out)
sensor attempts: 31
no clean frames: 31-attempt terminal report
receiver status: 0x4000, with no data-lane payload
stream stop: timed out after the failed start
```

Therefore this source-backed 648x488/30-fps sensor sequence did not make the
IR camera stream. It eliminates that sequence as a sufficient fix on this
Linux/IPU4P configuration, but does not identify whether the remaining
fault is in the sensor's physical MIPI output, source-6 receiver setup, or a
platform sequencing interaction.

## Rollback and preservation

The temporary candidate was removed without rebooting:

```bash
echo -n 'i2c-INT347E:00' | sudo tee /sys/bus/i2c/drivers/ov7251/unbind
sudo modprobe -r ov7251
sudo modprobe ov7251
```

Post-rollback evidence:

```text
live module: /lib/modules/6.19.8-3.surface.fc43.x86_64/kernel/drivers/media/i2c/ov7251.ko.xz
distribution SHA-256: 00cfa05cbdf46a8d6c55b077d7729fa419a3a072d88bb343b5985d4e3ad4deac
vermagic: 6.19.8-3.surface.fc43.x86_64 SMP preempt mod_unload
```

The IR route was disabled after the test. The existing front and rear
loopbacks remained present and continued to report `Video Capture`:

```text
/dev/video60: Surface Camera (front)
/dev/video61: Surface Camera (back)
```

No reboot or push was performed.

## Evidence boundary

Demonstrated: the Windows 648x488 sensor sequence is reproducible on Linux,
the candidate writes/readbacks are attributable, and it still produces zero
payload bytes on the IPU4P route.

Hypothesis: the unresolved blocker remains below usable CSI payload; this
test does not distinguish sensor-side MIPI electrical state from source-6
receiver configuration or platform ordering.

Untested: a known-good Windows source-6 receiver trace, physical lane
measurement, IR illumination control, decoded image data, preview-client
support, cold-start repetition, and suspend/resume behavior.
