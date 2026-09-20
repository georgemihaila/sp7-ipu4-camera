# OV7251 diagnostic teardown diagnosis

Date: 2026-09-20  
Status: static diagnosis only; further IR experiments suspended

## Result

The normal camera stack was recovered by the explicitly authorized reboot.
The installed distribution modules and services are normal again, and the
established front and rear RGB bridge paths produced warmed, changing frames.
No IR diagnostic route, candidate module, camera ioctl, module removal, or
hardware write was attempted after reboot.

The strongest explanation for the saved teardown wait is a receiver OOPS
followed by a self-deadlock in V4L2 release:

1. source-6 `STREAMON` entered `verify_stream_start()` and was bouncing the
   OV7251 after no clean frames;
2. the callback dereferenced an invalid external-subdevice operations pointer
   and faulted in the `v4l2_subdev_call(..., s_stream, 0)` expansion;
3. the fault interrupted the stream-on thread while it owned the video queue
   mutex for the one-queue case;
4. `do_exit()` ran file cleanup, and `v4l2_release()` waited for that same
   mutex.

This accounts for the saved `wchan=v4l2_release`, the `D` state, and the
persistent receiver-module reference without requiring an illuminator fault.
It is a supported diagnosis, not proof of why the subdevice operations pointer
became invalid. The firmware completion waits remain relevant to ordinary
receiver rollback, but the one saved `v4l2_release` stack does not identify one
of them as its immediate owner.

## Evidence and provenance

The failure artifacts are preserved outside the repository:

`/var/tmp/ov7251-illuminator-repeat-m1z4Dt/recovery-20260920-eqxIEv/`

The blocked task was PID 325886. Its saved stack was:

```text
v4l2_release+0x5f/0xe0 [videodev]
__fput
task_work_run
do_exit
make_task_dead
rewind_stack_and_make_dead
```

The relevant kernel log sequence for that task was:

```text
no frames from ov7251 2-0060 after start; bouncing sensor (retry 27)
BUG: unable to handle page fault for address: ffffffffc2477538
RIP: verify_stream_start+0x102/0x2ca [intel_ipu4p_isys]
note: v4l2-ctl[325886] exited with irqs disabled
```

The same saved run also contains receiver rollback failures:

```text
stream stop time out (source=6 ... send_type=5)
failed to stop pipeline after stream-start error: -5
skipping ISYS device close after firmware lifecycle failure
```

Those messages prove a missing or late stream-stop response occurred during
failed-start rollback, but they do not prove that the later `v4l2_release`
wait was blocked on that completion rather than on a mutex left held by the
OOPS.

The receiver object used by the diagnostic run is recorded as:

| Item | Value |
|---|---|
| Kernel | `6.19.8-3.surface.fc43.x86_64` |
| Diagnostic source | `920ca5819d287402a9320f7d100c7c09c0140aad` |
| Pinned diagnostic object | `intel-ipu4p-isys-csi-header-tap-bb8.ko` |
| Object SHA-256 | `546d80d5c692e0b394b56f4771fbf244bb9427419f6126405fe766fc9b1eaf7b` |
| Object vermagic | `6.19.8-3.surface.fc43.x86_64 SMP preempt mod_unload` |
| Fault symbol convention | `symbol+offset/size`, e.g. `verify_stream_start+0x102/0x2ca` |
| Installed distribution ISYS SHA-256 | `10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce` |

The post-reboot installed OV7251 hash remains
`00cfa05cbdf46a8d6c55b077d7729fa419a3a072d88bb343b5985d4e3ad4deac`.

## Faulting callback

The saved instruction bytes at `verify_stream_start+0x102` are:

```text
48 8b 85 a0 00 00 00    mov rax,[rbp+0xa0]
48 8b 40 18             mov rax,[rax+0x18]
```

For the exact pinned object, the first load is consistent with loading
`esd->ops`; the second selects the video-operations group. The saved values
were:

```text
RAX before the fault: ffffffffc2477520
CR2:                 ffffffffc2477538
```

Thus the failing memory access was `ops + 0x18`, not a completion wait. The
call is the compiler expansion of:

```c
v4l2_subdev_call(esd, video, s_stream, 0);
```

The caller chain captured in the OOPS is:

```text
v4l2_ioctl
  __video_do_ioctl
    vb2_core_streamon
      vb2_start_streaming
        start_streaming
          ipu_isys_stream_start
            verify_stream_start
```

The source-backed retry loop at
`linux-6.19.8/drivers/media/pci/intel/ipu-isys-queue.c` calls sensor
`s_stream(0)`, records CSI errors, then calls `s_stream(1)` and refills the
firmware capture queue. The fault occurred before the normal error-unwind code
could run. The evidence does not distinguish a stale subdevice lifetime,
memory corruption, or another teardown interaction as the reason for the
invalid pointer.

## Why `v4l2_release` is the immediate wait candidate

The exact 6.19.8 VB2/V4L2 sources show that queue ioctls are serialized by
`vdev->queue->lock`. The ISYS queue initializes that lock to `av->mutex`:

```c
aq->vbq.lock = &ipu_isys_queue_to_video(aq)->mutex;
```

In the source-6 one-queue path, `start_streaming()` temporarily unlocks
`av->mutex`, then locks `pipe_av->mutex`; when `pipe_av == av`, this reacquires
the same video mutex before calling `ipu_isys_stream_start()`. The page fault
therefore interrupts the stream-on thread while that mutex is held. The
normal return path that would unlock it is not reached.

The release path is:

```text
do_exit
  __fput
    video_release
      vb2_fop_release
        _vb2_fop_release
          mutex_lock(av->mutex)       <- immediate self-deadlock candidate
          vb2_queue_release
            __vb2_queue_cancel
              stop_streaming
```

This is why the saved top frame is `v4l2_release` rather than
`wait_for_completion_timeout`. It is not a claim that every blocked close has
the same owner; it is the best-supported explanation for PID 325886.

## Completion and lock ownership map

| Object | Owner / wait site | Completion or unlock owner | Status for PID 325886 |
|---|---|---|---|
| `av->mutex` | V4L2 ioctl serialization and `_vb2_fop_release()` | Stream-on normal return or the corresponding release path | Strong immediate wait candidate; the OOPS bypassed the unlock path in the one-queue case |
| `pipe_av->mutex` | Pipeline queue coordination in `start_streaming()` and `stop_streaming()` | Same stream-start/stop control flow | Held by the stream-start path when `pipe_av == av`; same underlying mutex in the one-queue case |
| `isys->stream_mutex` | `start_streaming()`, `stop_streaming()`, and `ipu_isys_stream_start()` rollback | Owning stream operation | Possible secondary lock, not identified by the saved release stack |
| `stream_stop_completion` | `stop_streaming_firmware()` after `STREAM_FLUSH` or `STREAM_STOP` | ISYS firmware response handler on `STREAM_FLUSH_ACK` or `STREAM_STOP_ACK` | Proven completion owner; a timeout occurred in failed-start rollback, but not proven as the PID's release wait |
| `stream_close_completion` | `close_streaming_firmware()` after `STREAM_CLOSE` | ISYS firmware response handler on `STREAM_CLOSE_ACK` | Separate candidate for normal close; failure rollback disables close after stop failure |
| `csi2->eof_completion` | `ipu_isys_csi2_wait_last_eof()` | CSI EOF event handler | Separate EOF wait; no evidence that PID 325886 was here |
| `aq->lock` | Incoming/active buffer list operations | Short IRQ-safe critical sections | Not a sleepable `v4l2_release` owner |
| `q->mmap_lock` | VB2 buffer free during queue release | VB2 queue release path | Not implicated by the saved top frame |

The firmware response handler completes `stream_stop_completion` for both
`STREAM_STOP_ACK` and `STREAM_FLUSH_ACK`, and completes
`stream_close_completion` for `STREAM_CLOSE_ACK`. The saved timeout proves the
stop/flush response was not observed within the driver timeout at least once.
It does not show whether a late response arrived later, nor whether the
subsequent media-ctl waits were on the same mutex or on firmware cleanup.

## Proven, likely, unresolved

| Classification | Finding |
|---|---|
| Proven | Normal reboot restored the distribution stack; the candidate and source-6 modules are not installed or active, experimental parameters are absent, and both RGB bridge paths were revalidated with warmed changing frames. |
| Proven | The final saved OOPS happened in `verify_stream_start+0x102/0x2ca` while dereferencing the external sensor's video operations group. |
| Proven | `v4l2_release` was the blocked task's wait channel and the receiver module remained referenced until reboot. |
| Proven | The receiver's failed-start rollback logged a stream-stop timeout and `-EIO`-equivalent failure; `video_release()` then skipped ISYS firmware close after the lifecycle failure. |
| Strong hypothesis | The OOPS left `av->mutex` held, and `v4l2_release()` self-deadlocked trying to reacquire it. This directly explains the saved wait channel and is consistent with the exact queue-lock and stream-start code. |
| Strong hypothesis | The receiver retry/rollback path exposed an invalid sensor-subdevice lifetime or pointer state. The evidence does not identify the invalidation event. |
| Unresolved | Whether any later blocked close waited on `stream_stop_completion`, `stream_close_completion`, `csi2->eof_completion`, or another mutex. |
| Unresolved | Whether the late firmware response, receiver error `0x480`, sensor power transition, or a lifetime race caused the original no-frame condition. |
| Unresolved | Whether a safe receiver-only fix is to guard subdevice lifetime, change retry cancellation, or repair firmware-stop recovery. No fix is proposed from this evidence alone. |

The known `0x480` receiver status remains a limitation/evidence item. It is
not, by itself, an illuminator regression or an explanation of the kernel
page fault.

## Focused next diagnosis (not to be run now)

Further IR work is suspended. If a later, separately authorized diagnosis is
needed, keep illumination disabled and use the normal distribution stack. The
smallest useful observation is a controlled receiver-only failed start with
tracing around the exact ownership boundary, not another illuminator run.

Capture only these specific facts:

1. At each `v4l2_subdev_call(esd, video, s_stream, 0/1)`, record the `esd`
   address, `esd->ops` address, selected `video` operations pointer, callback
   address, and whether the sensor module/device lifetime is still held.
2. Immediately before and after the `STREAM_FLUSH`/`STREAM_STOP` command, record
   the stream handle, command result, and whether the firmware response handler
   completed `stream_stop_completion`.
3. If a task blocks, record the same PID's `/proc/<pid>/stack`, `wchan`, task
   state, and a lock-owner snapshot. Do not issue additional media or video
   queries while a close is blocked.
4. Separately record `STREAM_CLOSE` and `csi2->eof_completion` only if the
   stack reaches those waits.

An observation that `s_stream(0)` sees a stable, live `esd->ops` followed by a
missing `STREAM_FLUSH_ACK` would support a firmware-stop/receiver problem. An
observation that the callback pointer changes or becomes invalid before the
call would support a subdevice lifetime problem. A release stack blocked on
`av->mutex` immediately after a deliberately captured OOPS would confirm the
self-deadlock mechanism. These are discriminating observations; a request for
an undefined “full Windows trace” is not required.

No implementation change is justified yet. In particular, do not resume the
illuminator comparison, increase any output setting, or modify CSI/PLL/BB8
configuration until the receiver lifetime/teardown owner is resolved.

## Recovery note

The pre-reboot recovery snapshot also records one later `media-ctl` close and
one topology query entering `v4l2_release`; those processes disappeared only
with the reboot. They are retained as historical evidence, not repeated. The
normal software restoration is verified; rollback of any transient BB8 hardware
register state is not independently demonstrated.
