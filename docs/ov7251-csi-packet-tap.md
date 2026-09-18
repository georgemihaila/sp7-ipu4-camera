# OV7251 source-6 CSI packet-tap attempt

Date: 2026-09-18  
Kernel: `6.19.8-3.surface.fc43.x86_64`  
Branch: `feature/ir-camera`

## Purpose

The physical CSI boundary cannot be measured in the current environment. This
was a software-only attempt to use the checked-in IPU4P CSI-2 direct debug
capture node as close as possible to the source-6 receiver:

```text
ov7251 2-0060 -> Intel IPU4 CSI-2 1 -> Intel IPU4 CSI-2 1 capture 0 (/dev/video5)
```

The direct node captures MIPI packets rather than a decoded image. It is useful
for checking whether the receiver completes any packet buffer, but it does not
measure the sensor pins and cannot by itself distinguish a silent sensor from a
receiver/PHY configuration failure.

## Controlled setup

The test temporarily loaded the checked-in diagnostic IPU4P ISYS module with
`debug_capture_links=1` and an instrumented OV7251 module. The temporary module
hashes were:

```text
intel-ipu4p-isys.ko  7e7c1bd355966a954aaf0411915cef3bf14782872dc97d345599022741ead837
ov7251 trace module  e9f7d4728939a2aca5cda3d63e6c135cdffe58f8af224cbf379e4350a5b79531
```

The direct capture link was enabled only for the test. The node was then
configured to match the upstream sensor format:

```text
640x480, Y10, one multiplanar plane
bytesperline=832, sizeimage=400384
```

The capture command queued 120 MMAP buffers and streamed to a temporary file.
The complete evidence is outside the repository at:

```text
/var/tmp/ov7251-csi-packet-tap-format-test-20260918-195344/
```

## Demonstrated result

The corrected-format run returned success for both set/get-format operations.
The kernel trace then recorded:

- source 6 requested with one lane and RAW10;
- the receiver enabled and firmware open/start completed;
- OV7251 power-on completed, `0x0100` read back as `0x01`, and the sensor
  stream callback returned success;
- 26 consecutive `no frames from ov7251` retries;
- receiver snapshots with `hs=0` and `lp=0`; and
- repeated receiver status `0x4000`, already classified elsewhere as the
  clock-lane ULP/escape event.

The direct node produced no completed userspace payload during the run:

```text
packet-tap.raw: 0 bytes
capture.log: buffer allocation/query/queue only; no successful dequeue recorded
run.txt: setfmt_rc=0, getfmt_rc=0, rc=137, bytes=0
```

The process was terminated by the bounded timeout after the source-6 retry
loop was active. The first two attempts are excluded from the result: one used
the node's incompatible 1x1 default and failed link validation with `EPIPE`,
and one used unsupported `v4l2-ctl` option names before capture began.

## Interpretation

This demonstrates that the source-6 pipeline can be started with the direct
packet node formatted to the sensor mode, while no direct packet buffer was
completed and no clean frame arrived. It strengthens the existing Linux
receiver evidence, but it is not physical proof that the OV7251 emits no MIPI
transitions. A direct file grows only after firmware produces a data-ready
response and the video queue completes a buffer. A receiver/PHY or firmware
packet-completion failure, an errored/recycled buffer, or the bounded start
verification timeout can therefore produce the same zero-byte userspace
result. The relevant queue behavior is in
`linux-6.19.8/drivers/media/pci/intel/ipu-isys-queue.c` and the response
dispatch is in `linux-6.19.8/drivers/media/pci/intel/ipu-isys.c`.

Therefore this test does not justify another guessed PLL, port, timing, lane,
or pixel-format mutation. The remaining discriminating evidence is a known-good
Windows source-6 runtime trace or physical observation of the sensor clock,
reset/power-enable and CSI clock/data lanes during the Linux start.

## Rollback and RGB preservation

After the run, both direct-capture links were disabled, the temporary OV7251
and diagnostic IPU4P modules were unloaded, and the distribution modules were
reloaded without rebooting. The restored IPU4P module hash is:

```text
10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce
```

The restored OV7251 module hash is:

```text
00cfa05cbdf46a8d6c55b077d7729fa419a3a072d88bb343b5985d4e3ad4deac
```

The RGB bridge remained running throughout rollback; `/dev/video60` and
`/dev/video61` remained present. No reboot and no push were performed.
