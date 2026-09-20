# OV7251 receiver lifecycle instrumentation

Date: 2026-09-20
Status: diagnostic candidate prepared; not loaded and not hardware-qualified

## Scope

This candidate adds observation only to the IPU4P ISYS receiver path. It does
not change retry counts, error returns, subdevice references, firmware
commands, teardown ordering, or pointer validity checks. The trace is disabled
by default and is enabled only with the module parameter
`lifecycle_trace=1` when a separately authorized receiver-only experiment is
performed.

The instrumentation covers four hypotheses from the preserved failure
evidence:

| Hypothesis | Evidence emitted | Discriminating observation |
|---|---|---|
| Sensor-subdevice lifetime or stale operations pointer | Registration/unregistration events plus `sd`, entity, `ops`, owner, and device pointer snapshots; before/after sensor `s_stream()` snapshots | A changed/missing operations pointer or subdevice identity before a retry supports a lifetime/teardown interaction. This is observation, not a lifetime fix. |
| Retry/teardown interaction | Site-labelled `s_stream(0)` and `s_stream(1)` begin/end events, return values, stream identity, and lock state | A failure or missing after-event at a specific retry site locates the interruption relative to the retry call. |
| Firmware stop acknowledgement | Stop/flush/close command before-send, after-send, and after-wait records, including handle, command result, elapsed time, completion state, and firmware error; response-handler records for `STREAM_STOP_ACK`, `STREAM_FLUSH_ACK`, and `STREAM_CLOSE_ACK` | A command timeout with no matching acknowledgement distinguishes a firmware-stop/close path from a sensor callback failure. A late acknowledgement can be correlated by stream handle. |
| Lock ownership / blocked close | `mutex_is_locked()` and lockdep-held state for the video, ISYS stream, ISYS, and media graph mutexes; `video_release()` entry and post-VB2 markers | `video_release:before_vb2` showing the video mutex locked but not held by the releasing task, followed by no post-VB2 marker, supports the documented self-deadlock candidate. `-1` means lockdep is unavailable in the built configuration. |

The callback dispatch remains the original `v4l2_subdev_call(sd, video,
s_stream, enable)` expression. The trace samples `READ_ONCE(sd->ops)` before
and after it but deliberately does not dereference the sampled operations
table or add a speculative guard; a fault at the original dispatch remains a
visible failure rather than being suppressed.

## Files

- `linux-6.19.8/drivers/media/pci/intel/ipu-isys.c`: external-subdevice
  registration/unregistration timeline and firmware stop/flush/close ACK
  records.
- `linux-6.19.8/drivers/media/pci/intel/ipu-isys-video.c`: opt-in parameter,
  sensor-call wrapper, lock-state records, firmware stop/close command and
  wait records, and blocked-close boundary markers.
- `linux-6.19.8/drivers/media/pci/intel/ipu-isys-queue.c`: retry-off and
  retry-on calls routed through the diagnostic wrapper.
- `linux-6.19.8/drivers/media/pci/intel/ipu-isys.h`: internal declarations
  shared by the receiver objects.
- `tests/task41-ipu4p-receiver-lifecycle-instrumentation-static.sh`: offline
  boundary checks; it performs no camera, media, module, or service action.

## Exact-kernel build provenance

The candidate was built from the repository source overlay against the
prepared target build tree. The candidate artifact was not loaded; no
`modprobe`, `insmod`, service restart, hardware operation, PAM change, or GDM
change was performed.

| Item | Value |
|---|---|
| Target kernel release | `6.19.8-3.surface.fc43.x86_64` |
| Build/source tree | `/usr/src/kernels/6.19.8-3.surface.fc43.x86_64` |
| Kernel config SHA-256 | `fa055bf09060a92eb128b3e54c1a07bd1537db196d820e9bddd1cd39dc386889` |
| Compiler used | `gcc (GCC) 16.2.1 20260819 (Red Hat 16.2.1-2)` |
| Kernel compiler recorded by build tree | `gcc (GCC) 15.2.1 20260123 (Red Hat 15.2.1-7)` |
| Candidate | `linux-6.19.8/drivers/media/pci/intel/ipu4/intel-ipu4p-isys.ko` |
| Candidate SHA-256 | `f79aac68ca3277e1df0ea11f75ce0b0ef05d72fa1a260e500a759a0092a2a40f` |
| Candidate module name | `intel_ipu4p_isys` |
| Candidate vermagic | `6.19.8-3.surface.fc43.x86_64 SMP preempt mod_unload` |
| Instrumented source diff SHA-256 | `0fa425fc29cc68e94a3de8d694f55bc9d004c899524c7006ff0ce7cb4141ad6d` |

The build completed successfully. It reported the expected compiler-version
mismatch between the target kernel's recorded compiler and the host compiler;
that warning is retained here as provenance. The already-running system
module was not replaced or reloaded, and the newly built artifact was not
loaded.

## Offline checks

Passed:

```text
task41-ipu4p-receiver-lifecycle-instrumentation-static: PASS
git diff --check: PASS
KDIR=/usr/src/kernels/6.19.8-3.surface.fc43.x86_64 \
KREL=6.19.8-3.surface.fc43.x86_64 scripts/build-modules.sh: PASS
```

These results establish source/build integrity only. They do not establish a
valid receiver stream, clean firmware teardown, absence of an OOPS, or safe
module unload. A later live attempt must remain receiver-only, strobe-disabled,
bounded, and separately approved.
