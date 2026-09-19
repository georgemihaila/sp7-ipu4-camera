# OV7251 IR standalone camera

Status: source and userspace backend implemented; continuous hardware
qualification is currently **failed** and GNOME Snapshot qualification remains
deferred. This feature is deliberately standalone. The existing front/rear
bridge controller does not enumerate, call, switch to, or restart the IR
camera.

## Source reproducibility

The bundled module was originally built from a temporary three-file source
delta that was absent from the repository. The exact patch operations were
recovered from the local session record and integrated into:

- `linux-6.19.8/drivers/media/pci/intel/ipu4/ipu4p-isys-csi2.c`
- `linux-6.19.8/drivers/media/pci/intel/ipu-isys.c`
- `linux-6.19.8/drivers/media/pci/intel/ipu-isys-queue.c`

The delta is source-6-only: BB8 CPHY/DPHY/AFE initialization uses the recorded
Table-B values, and the receiver, firmware, and direct-tap packet diagnostics
are scoped to source 6. Fatal CSI handling is unchanged. The exact target build
was run with:

```sh
./scripts/build-modules.sh
```

Target: `6.19.8-3.surface.fc43.x86_64`. The resulting module and the bundled
module both hash to:

```text
546d80d5c692e0b394b56f4771fbf244bb9427419f6126405fe766fc9b1eaf7b
```

This is a reproducible source build, not a binary-only fix. The module remains
kernel-specific and is not installed or loaded by this change.

## Capture backend

`cbridge/sp7-camera-ir` is an opt-in producer. It:

1. discovers `/dev/media*` and requires the enabled
   `ov7251 2-0060 -> Intel IPU4 CSI-2 1 -> ... capture 0` route;
2. discovers the video node from the media entity major/minor rather than a
   hard-coded video minor;
3. negotiates one-plane, 640x480 packed `Y10` with the direct-tap stride;
4. requests eight MMAP buffers and runs a persistent dequeue/requeue loop;
5. validates `bytesused`, `data_offset`, row headers, error flags, sequence
   progression, monotonic timestamps, and the packed RAW10 layout;
6. decodes RAW10 to grayscale YUYV with chroma `0x80`; and
7. writes the frames to `/dev/video62`.

Malformed or truncated buffers never reach the decoder. Stream errors receive a
bounded reopen/retry sequence; shutdown performs `STREAMOFF`, closes the output,
unmaps buffers, and closes graph/video descriptors. The synthetic decoder test
also covers pixel packing, neutral chroma, truncation rejection, and header
rejection:

```sh
make -C cbridge clean all test-controller test-ir
sh tests/task35-ir-backend-static.sh
```

## Installation and use

The loopback profile reserves `/dev/video62` as `Surface Camera (IR)` with
`exclusive_caps=1`, preserving `/dev/video55`, `/dev/video60`, and
`/dev/video61`. The normal bridge service readiness barrier still checks only
the front/rear endpoints. It never starts `sp7-camera-ir`.

After an authorized installation and after source-6 qualification has passed,
start the producer manually:

```sh
/usr/local/libexec/sp7-camera-ir
```

Once the producer is ready, verify the endpoint's `Device Caps` contains
`Video Capture`, then reopen GNOME Snapshot so it rescans devices. No temporary
module-swapping wrapper is a service dependency.

## Qualification record

The source-backed persistent qualifier was exercised on 2026-09-19 against
`6.19.8-3.surface.fc43.x86_64`, using the bundled module SHA-256
`546d80d5c692e0b394b56f4771fbf244bb9427419f6126405fe766fc9b1eaf7b`.
The run was configured for 10 seconds and two short cycles while debugging the
harness; the persistent failure prevented the cycle phase from running, so it
was not a production gate attempt. Its artifacts are preserved at:

```text
/var/tmp/ov7251-ir-qualification-smoke-20260919-4/
```

The persistent run reached the decoder and produced 15 frames at 0.543 frames
per second over 27.638 seconds. Fourteen consecutive decoded frames changed;
the first valid frame reported `bytesused=399360`, `data_offset=4`,
`stride=832`, and luma range `3..255`. It then rejected one buffer with an
invalid `V4L2_BUF_FLAG_ERROR`/timestamp metadata combination, recovered twice,
observed 539 sequence gaps, and hit a `VIDIOC_STREAMON` connection timeout.
The kernel log contained 45 receiver-error status messages, 139 fatal-path
messages, 32 CSI-2 startup errors, 32 `no frames from ov7251` retries, 119
`error=8` firmware events, and repeated `0x400`, `0x480`, and `0x4000` states.

This is useful evidence of changing decoded payload, but it is not stable
continuous capture. The required gate is therefore still not passed:

- 10 minutes of changing, correctly decoded frames: **NOT TESTED/PASS NOT
  CLAIMED**;
- 20 successful stop/start cycles: **NOT TESTED/PASS NOT CLAIMED**;
- Snapshot lists IR separately, shows changing grayscale video, and saves a
  photo: **NOT TESTED**;
- repeated RGB-to-IR switching, app reopen, service restart, reboot, and
  suspend/resume: **NOT TESTED**;
- front/rear preservation: static controller and build tests pass; live RGB
  regression is still required after an authorized installation.

The earlier documented tap run showed receiver SOF/EOF and packet headers and
produced one decodable buffer, but that is not clean continuous raster
qualification. Do not suppress the fatal path or infer success from
enumeration. No receiver timing/PHY mutation was made in response to this run:
the existing source-6 evidence still lacks a known-good Windows receiver trace
or physical CSI lane measurement that would justify such a change.

### 2026-09-19 baseline execution

The documented baseline mode was run with the existing BB8 module and
configuration unchanged. Artifacts are preserved at:

```text
/var/tmp/ov7251-ir-qualification-baseline-20260919-173257/
```

The requested persistent duration was 600 seconds; the qualifier measured
600.571 seconds, with 690.055 seconds total wall-clock duration including
setup, cleanup, and the cycle phase. It decoded 11,348 frames, of which 11,347
changed, at 18.895 FPS. The persistent totals were:

- timeouts: 0;
- malformed/capture errors: 11;
- recoveries/reopens: 11;
- sequence gaps: 60,480,588 in the final cumulative summary (the last
  progress line before close reported 41,193);
- rejected buffers: 10; and
- cleanup failures: 0.

All 20 requested stop/start cycles were attempted. None passed: 0 passed and
20 failed, with 88 decoded frames total. The cycle totals were 0 timeouts, 3
malformed/capture errors (two `VIDIOC_STREAMON: Connection timed out` events
and one invalid source-6 metadata event), 3,093 sequence gaps, 1 rejected
buffer, and 0 cleanup failures. Each cycle's result is retained in
`qualify.log`.

The unfiltered `kernel.log` emitted 1,323 receiver-status lines marked
`(fatal)`, including recurring `0x400`, `0x480`, `0x8400`, and `0x8480` states;
12,694 error snapshots contained a nonzero `fatal_receiver_errors` field.
It also emitted 4,164 source-6 `error=8` lines and 159
`no frames from ov7251` recovery lines. These are emitted-message counts only:
the existing kernel rate limiting remains in effect, so they are lower bounds.
The direct tap did show source-6 packets and changing decoded payload, but the
receiver/fatal and sequence-gap errors make the result a failed qualification
gate.

The outcome is **baseline completed: YES** and **stability passed: NO**. This
is a baseline record, not a stability pass. The baseline wrapper returned
`qualify_rc=1` because its gates failed, while still completing the requested
duration and all 20 cycles. The run captured the bundled BB8 module hash
`546d80d5c692e0b394b56f4771fbf244bb9427419f6126405fe766fc9b1eaf7b`. After
cleanup, the distribution module was restored and loaded from
`/lib/modules/6.19.8-3.surface.fc43.x86_64/updates/extra/intel-ipu4p-isys.ko`
with hash
`10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce`; the
media graph enumerated normally and the bridge remained inactive. This confirms
software restoration only. It does not claim BB8 register rollback.

No physical CSI measurement was performed, no PHY change was made, and desktop
integration remains deferred. The older `hs=0` trace and the later BB8
`hs=0x101`/changing-payload trace remain historical evidence from different
timelines and do not establish physical lane behavior.

The diagnostic wrapper completed cleanup after the failed run: the temporary
links were disabled, the source-backed module was unloaded, the distribution
`intel_ipu4p_isys` module was restored, and the bridge was not active before or
after the run. `/dev/video62` was not installed, no service was changed, and no
reboot or suspend/resume was performed.

## Baseline mode

The qualifier has two modes. The default mode is fail-fast: a persistent
capture failure prevents the cycle phase, and a failed cycle stops the normal
cycle phase. An explicit baseline run keeps attempting the requested persistent
duration and every requested cycle after capture failures:

```bash
make -C cbridge qualify-ir
./scripts/ir/qualify-persistent.sh --baseline \
  /var/tmp/ov7251-ir-qualification-baseline-$(date +%Y%m%d-%H%M%S)
```

The baseline qualifier still returns failure when its gates fail. It records
`requested_duration_seconds` separately from `actual_duration_seconds`, logs
each cycle, preserves cumulative timeout/malformed/recovery/sequence-gap/
rejected-buffer counts across reopen attempts, and counts surfaced
`STREAMOFF` cleanup failures. The wrapper also records total wall-clock
duration and restoration failures. A baseline result is reported as
`result=BASELINE` with `stability_pass=NO`; it is an observation, not a
stability pass.

The existing bundled source-6 module, media links, module options, and kernel
logging configuration are unchanged. The wrapper preserves `setup.log`,
`qualify.log`, and `kernel.log` without filtering fatal messages. Existing
kernel rate limiting remains in effect, so emitted log counts are lower bounds;
the absence of a repeated line does not prove that the underlying event did
not recur.

## Evidence boundaries

The older synchronized source-6 trace is historical evidence for its own
configuration and reported `hs=0`/`lp=0`, `0x4000`, and zero usable payload.
The later BB8 run used the existing bundled configuration and reported
`hs=0x101` with changing packet payload, while also reporting receiver and
firmware errors. These are different software runs. Neither establishes
physical CSI lane behavior, and no electrical lane measurement was performed.

Sensor power/reset/clock, initialization, and `0x0100=0x01` readbacks from
earlier trace runs are historical unless collected again on the same timeline
as a baseline run. They must not be merged into the baseline's synchronized
evidence. PHY changes and desktop integration remain deferred.

## Rollback

To stop the standalone producer, send it `SIGTERM` or press `Ctrl-C`; it will
finish its bounded cleanup. To remove the endpoint and restore the previous
loopback profile, run the existing removal script with authorization:

```sh
sudo ./scripts/remove-camera-bridge.sh
```

The script is not run automatically. Module reload alone is not a BB8 hardware
rollback; the recorded experiment left BB8 residue after reload and bounded
power-cycle attempts. Any module replacement, physical power-cycle, reboot, or
suspend/resume qualification requires explicit authorization. IR illumination
and authentication are outside this feature.
