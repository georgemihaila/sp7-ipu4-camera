# OV7251 source-6 BB8 rollback and CSI header tap

Date: 2026-09-19  
Kernel: `6.19.8-3.surface.fc43.x86_64`  
Source: OV7251 on `Intel IPU4 CSI-2 1`, firmware source 6  
Receiver timing: unchanged; observed `0/627/0/647`

## BB8 rollback verification

A temporary read-only ISYS hook read BB8's CPHY override, DPHY override, and
AFE registers before receiver timing. It performed no BB8 writes. After the
earlier BB8 experiment, the readback was:

```text
bb8=(0x1001b,0x0,0x44104015)
```

Reloading the ISYS module did not change this residue. The bounded
`intel-ipu60/force_power_cycle` recovery path was then used and the same
read-only hook reported:

```text
bb8=(0x1001b,0x0,0x44104015)
```

Therefore hardware rollback is incomplete. The controlled restoration
procedure established by this test is:

1. Capture a read-only BB8 snapshot before and after any temporary module
   experiment.
2. If module reload leaves residue, use the bounded ISYS power-cycle recovery
   path once and read BB8 again.
3. If the readback is unchanged, do not claim register rollback. An exact
   register restoration or a physical power-cycle requires a separately
   justified action; this test did not reboot or guess another register value.

The distribution ISYS module was restored after the packet tap. Its installed
hash is:

```text
10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce
```

That verifies software-module restoration only, not the BB8 hardware state.

## Direct CSI packet tap with BB8 initialized

The temporary source-6-only tap loaded the recovered Table-B BB8 fields before
receiver timing and logged the readback:

```text
source-6 BB8 init: before=(0x1001b,0x0,0x44104015)
requested=cphy=(field=13,mask=0xffffff81)
dphy=(field=32,mask=0xffffff81) afe=0x44104015
after=(0x1001b,0x41,0x44104015)
```

The direct node was `/dev/video5`, configured as `640x480, Y10 ` with
`bytesperline=832` and `sizeimage=400384`. The bounded run returned timeout
status `124` as expected, but it completed direct packet buffers:

```text
capture.raw: 34340864 bytes
v4l2-ctl: bytesused=399360 on completed buffers
```

Before the first sensor retry at monotonic time `4486.381068`, the tap
recorded receiver and firmware frame events and packet headers. A
representative first buffer was:

```text
receiver SOF: vc=0 status=0x10000
fw SOF:       vc=0 pin=0 error=0
receiver EOF: vc=0 status=0x20400
fw EOF:       vc=0 pin=0 error=0
packet line 0: vc=0 dtype=0x12b word_count=800 sync=0 stype=0 sid=0 port=4
packet line 1: vc=0 dtype=0x12b word_count=800 sync=1 stype=0 sid=0 port=4
packet line 2: vc=0 dtype=0x12b word_count=800 sync=1 stype=0 sid=0 port=4
packet line 3: vc=0 dtype=0x12b word_count=800 sync=1 stype=0 sid=0 port=4
```

The corresponding converted header words were
`0x012b0320/0x80000040` for line 0 and
`0x212b0320/0x00000040` for line 1; the alternating second word continued
through the following lines. The tap logged 536 receiver SOF events, 536
receiver EOF events, and 3436 packet-header records during the bounded run.

The receiver still reported fatal synchronization states, including `0x400`
and `0x480`, and the existing fatal path remained active: the kernel printed
the fatal classification and the first retry followed. No fatal mask,
`-EIO` handling, receiver timing, sensor PLL, or port configuration was
changed by this diagnostic.

This is new evidence of packet-parser activity and firmware buffer completion
with BB8 initialized. It does not by itself prove clean image transport or
physical-lane correctness: the direct node is a packet buffer, and the
receiver simultaneously reported synchronization errors. The complete run
artifacts are in:

```text
/var/tmp/ov7251-csi-header-tap-bb8-20260919/
```
