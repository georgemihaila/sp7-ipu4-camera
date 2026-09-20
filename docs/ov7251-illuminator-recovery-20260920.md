# OV7251 illuminator experiment recovery preparation

Date: 2026-09-20
Starting evidence: commit `a52a135`
Status: recovered after the explicitly authorized normal reboot

## Current blocked state

No camera capture or device-format query is part of this recovery record. The
saved evidence is under:

`/var/tmp/ov7251-illuminator-repeat-m1z4Dt/recovery-20260920-eqxIEv/`

The original failed capture process remains PID 325886. Its saved kernel stack
is:

```text
v4l2_release
__fput
task_work_run
do_exit
make_task_dead
rewind_stack_and_make_dead
```

The failed stream-start log also records a page fault in the diagnostic
receiver at `intel_ipu4p_isys:verify_stream_start+0x102/0x2ca`, after the
existing source-6 no-frame retry path. This is a receiver stream-start failure
and teardown-wait lead, not an illuminator-control failure. The known
`fatal_receiver_errors=0x480` status is present separately.

A later cleanup `media-ctl` close and a subsequent read-only topology query
also entered `v4l2_release`; both are recorded. No further camera ioctls,
module-removal attempts, bridge restart, reset, force-unload, or power-cycle
will be attempted before recovery.

## Software and configuration checks

Running kernel: `6.19.8-3.surface.fc43.x86_64`

Installed distribution module hashes remain:

```text
00cfa05cbdf46a8d6c55b077d7729fa419a3a072d88bb343b5985d4e3ad4deac  ov7251.ko.xz
10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce  intel-ipu4p-isys.ko
```

The candidate and diagnostic modules are temporary files under `/var/tmp`,
not installed module paths. The configuration scan of `/etc/modprobe.d`,
`/etc/modules-load.d`, `/usr/lib/modprobe.d`, and `/usr/lib/modules-load.d`
found no `experimental_strobe_output`, `strobe_diagnostics`,
`debug_capture_links`, or temporary-module entry.

Current software state is intentionally not called restored:

| Item | State |
|---|---|
| Diagnostic ISYS | Loaded; `debug_capture_links=Y`; module reference count 32 at snapshot |
| OV7251 | Experimental sensor module absent; original sensor not reloaded |
| Illuminator | Experimental enable bits had been cleared before the hang; no enable state is active |
| Bridge | Inactive; intentionally not restarted on the diagnostic receiver |
| PipeWire / WirePlumber | Active / active |

## Recovery gate

The persistent kernel waits prevent ordinary restoration of the receiver and
interrupt the desktop session. A normal reboot is the bounded recovery action:

1. Save user work and close camera applications.
2. Obtain explicit approval in this task.
3. Perform one ordinary desktop reboot; do not force-unload, reset hardware, or
   power-cycle. If normal reboot itself stalls, stop without escalation.
4. After boot, verify the kernel, installed distribution module hashes, normal
   `ov7251` and distribution `intel_ipu4p_isys`, absence of the experimental
   module parameters, and the bridge/PipeWire/WirePlumber service states.
5. Verify both established RGB bridge paths with bounded captures. Do not load
   the diagnostic ISYS module, candidate OV7251, BB8 setup, or any illuminator
   option.

Only after those checks should the receiver teardown diagnosis continue. The
next static/runtime diagnosis should focus on the lock or completion owner
around `verify_stream_start`, stream-start retry cancellation, and
`v4l2_release`/`__fput` teardown after an illumination-disabled off capture.

## Post-reboot verification

The normal reboot completed without escalation. The running kernel is
`6.19.8-3.surface.fc43.x86_64`. The distribution OV7251 and ISYS modules are
loaded; their hashes remain the recorded values above. The diagnostic receiver
is not loaded, `debug_capture_links=N`, and both experimental OV7251 parameters
are absent. No candidate or BB8 diagnostic module was loaded after reboot.

The bridge is enabled/active; PipeWire is active; WirePlumber is
enabled/active. A short three-frame probe contained only the documented startup
filler, so it was not treated as camera success. The established RGB path was
then warmed with bounded 120-frame captures, serially and without touching the
IR route:

| Endpoint | Bytes | Non-flat frames | First non-flat frame | SHA-256 |
|---|---:|---:|---:|---|
| Front `/dev/video60` | 221184000 | 88 / 120 | 31 | `453955552d3ce1b9227cec2bf6f0d105d048ec7d2349d2112d4dcbedc81513a6` |
| Rear `/dev/video61` | 221184000 | 63 / 120 | 56 | `7bbd2a7735ef567a45871e5b78697a69e0af70870e878a0711325452cd579be3` |

The final frames had changing scene content: front Y mean 46.705 with range
16..69, and rear Y mean 49.132 with range 47..57. These captures verify the
normal RGB bridge after reboot. They do not qualify the IR route or the
illuminator.

Post-reboot artifacts are outside tracked source at:

`/var/tmp/ov7251-post-reboot-rgb-warm-p9DQlP/`
