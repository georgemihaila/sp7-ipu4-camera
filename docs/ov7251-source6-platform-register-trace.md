# OV7251 source-6 platform-register trace

Date: 2026-09-18
Kernel: `6.19.8-3.surface.fc43.x86_64`
Test boot: `20806810-9c25-4ca2-9398-950fc356e656`

## Purpose

After the source-6 timing inference failed, a diagnostic-only IPU4P module
was built with read-only logging immediately before source-6 receiver timing
programming.  The logging covered both legacy and combo GPREG HPLL/ISCLK/
override/port fields and the BSCAN exclusion register.  It did not write or
alter any of those registers and was scoped to firmware source 6, so the RGB
path was not changed.

Candidate module SHA-256:

```text
cb0f4344080b389895978154ac2ac5d8dd5d2e8e19d41218407a37c07653adae
```

## Demonstrated Linux state

The synchronized source-6 start logged:

```text
source-6 before timing platform:
legacy hpll=0x0 isclk=0x0 override=0x0 port=0x3895
combo hpll=0x0 isclk=0x0 override=0x0 port=0x3895
bscan=0x8040200
```

The receiver then used one lane and the generic timing `ctermen=0`,
`csettle=627`, `d0termen=0`, `d0settle=647`.  Its status showed no data-lane
HS activity.  The bounded 20-second capture recorded:

```text
capture_bytes=0
no frames from ov7251 2-0060 after start: 30 retries
csi2-1 receiver error status 0x4000: 16 occurrences
stream stop time out (source=6 ...)
```

This is useful synchronized evidence of the Linux receiver state, but it is
not a Windows-equivalent trace and does not identify the correct source-6
values.  Zero HPLL/ISCLK fields may mean that those fields are default,
unused, or configured elsewhere; this experiment does not distinguish those
possibilities.

## Rollback

The diagnostic module was moved to the retained evidence directory and the
baseline IPU4P module was restored before reboot.  Post-rollback verification
showed:

- IPU4P SHA-256:
  `10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce`;
- distribution OV7251 SHA-256:
  `00cfa05cbdf46a8d6c55b077d7729fa419a3a072d88bb343b5985d4e3ad4deac`;
- no diagnostic modprobe override remained;
- OV7251 revision 7 still probed at `0x60`;
- `sp7-camera-bridge.service` was active.

The read-only diagnostic logging remains in the source tree for a future
attributable trace.  It is not a streaming fix and is not a reason to retain
an experimental receiver module on the host.

Evidence files are outside Git under
`/var/tmp/ov7251-source6-platform-trace-20260918/`.
