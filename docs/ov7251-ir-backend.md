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
kernel-specific. The normal source installer now builds and installs the
patched OV7251 sensor module alongside the IPU4P modules; it does not load the
module, enable the illuminator, or start the IR producer automatically.

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

### 2026-09-19 corrected-accounting short run

After the metadata-preservation fix was committed, a 120-second persistent
capture plus all 20 stop/start cycles was run in baseline mode with the BB8
module and configuration unchanged. The complete artifacts are preserved at:

```text
/var/tmp/ov7251-ir-qualification-short-20260919-181411/
```

The run used commit `0d3825fa317dc9c94fa1999dfb11c1b79b989c93`, executable
SHA-256
`645e94e3bb7abfbc4e555b65293a73585369e1b7a636cc8bbb23eb37c454b2ec`, and the
unchanged BB8 module SHA-256
`546d80d5c692e0b394b56f4771fbf244bb9427419f6126405fe766fc9b1eaf7b`.

Persistent capture requested 120 seconds and ran for 121.211 seconds
(258.087 seconds total wall time). It decoded 745 frames, 744 changed, at
6.146 FPS. The corrected totals were:

- startup failures: 1;
- metadata errors: 23;
- timestamp, sequence, decode, requeue, poll, and DQBUF errors: 0;
- genuine DQBUF sequence gaps: 1;
- rejected buffers: 23;
- recoveries: 24;
- timeouts and cleanup failures: 0.

All 20 cycles were attempted. Fourteen passed and six failed. Cycles 3, 4, 8,
and 20 failed at startup with `VIDIOC_STREAMON: Connection timed out`; cycles
15 and 18 failed on invalid source-6 metadata after 1 and 4 decoded frames,
respectively. The cycle phase had zero sequence gaps, two rejected buffers,
four startup failures, six recoveries, and zero cleanup failures. In
particular, cycle 17 passed with five frames and zero gaps; the previous
baseline's cycle 17 had failed only because the old accounting reported 94
false gaps.

Progress accounting reconciled: all 13 persistent progress snapshots were
monotonic, and the final snapshot matched the `persistent_summary` for every
field, including frames, error categories, recoveries, gaps, rejected buffers,
and cleanup failures. The final progress line was not added again at close.
The committed `test-ir-metadata` regression also passed with QBUF deliberately
overwriting sequence, timestamp, flags, and plane metadata: preserved DQBUF
values remained reportable, consecutive frames had zero gaps, a genuine skip
counted correctly, timestamp regression remained detectable, wraparound was
accepted, and reopening reset the sequence domain.

Kernel evidence remains separate from userspace counters. This run emitted
428 explicit `receiver error status ... (fatal)` lines, 4,705 source-6
`error=8` lines, and 247 `no frames from ov7251` lines. It also contained 2,348
receiver error snapshots; 2,213 had nonzero `fatal_receiver_errors`, including
600 where the current `receiver_errors` field was zero. Those 600 are evidence
that the fatal field can be retained in later snapshots, not 600 newly emitted
fatal transitions. Existing kernel rate limiting remains in effect, so emitted
counts are lower bounds.

The decoded-frame evidence is committed under
[`docs/ir-evidence/20260919-short/`](ir-evidence/20260919-short/). The two
current window/sofa frames are recognizable and differ by only 1.0755 mean
absolute pixel levels, with no pixel differing by more than 32 levels; their
different hashes therefore do not establish motion or image integrity. The
historical tablet frame is explicitly labeled as a different timeline; its
large, recognizable content difference from the current room scene verifies
gross scene-correlated payload changes across captures, but is not same-run
motion evidence.

The run returned `result=BASELINE` with `stability_pass=NO`, and the full
600-second qualification was not repeated because the shorter gate failed.
The corrected failure categories select receiver/transport and stream-start
correlation as the next investigation: the dominant new failures are invalid
source-6 metadata and `STREAMON` timeouts, while gap accounting is no longer
the primary explanation. PHY settings, fatal handling, and desktop
integration remain unchanged and deferred. After cleanup the distribution
module was restored with SHA-256
`10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce`, and the
bridge remained inactive.

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
`STREAMOFF` cleanup failures. The qualifier now reports startup, metadata,
timestamp, sequence, decode, requeue, poll, and DQBUF errors separately. Its
progress snapshot includes the currently open stream; closing that stream adds
its counters exactly once. The wrapper also records total wall-clock duration
and restoration failures. A baseline result is reported as
`result=BASELINE` with `stability_pass=NO`; it is an observation, not a
stability pass.

The userspace metadata regression target is hardware-independent:

```bash
make -C cbridge test-ir-metadata
```

It mutates sequence, timestamp, flags, and plane metadata in the mocked
`QBUF` call. The test covers consecutive frames, genuine sequence skips,
timestamp regression, 32-bit sequence wraparound, and stream restart. The
capture path copies the DQBUF metadata before requeue and uses those copies for
validation, continuity, decoding, and reporting.

The existing bundled source-6 module, media links, module options, and kernel
logging configuration are unchanged. The wrapper preserves `setup.log`,
`qualify.log`, and `kernel.log` without filtering fatal messages. Existing
kernel rate limiting remains in effect, so emitted log counts are lower bounds;
the absence of a repeated line does not prove that the underlying event did
not recur.

## 2026-09-19 instrumented 120-second diagnostic run

The instrumented qualifier was run with the unchanged BB8 module and
configuration, using `--baseline` so the requested 120-second capture and all
20 cycles were attempted after failures. Baseline mode retained the existing
requeue, close, reopen, and fatal-handling decisions; it only prevented the
qualification harness from stopping early. The complete artifacts are
preserved at:

```text
/var/tmp/ov7251-ir-qualification-instrumented-20260919-190000/
```

The directory contains `setup.log`, `qualify.log`, the unfiltered 8.7 MiB
`kernel.log`, `run.txt`, `graph-before.txt`, `graph-after.txt`,
`hashes-pre.txt`, `hashes-post.txt`, and the post-run `graph-verify.txt`.

Run provenance:

- commit: `fdea44beeb0b974bc8cfc24f56d6e4678d842896`;
- kernel: `6.19.8-3.surface.fc43.x86_64`;
- qualifier SHA-256: `05501670599bf964703293106b3f39b02d204091b8dc2e6e67935b8fbcbd88f3`;
- unchanged BB8 module SHA-256: `546d80d5c692e0b394b56f4771fbf244bb9427419f6126405fe766fc9b1eaf7b`;
- restored distribution module SHA-256: `10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce`.

The persistent phase requested 120 seconds and ran for 120.284 seconds. It
decoded 2,174 frames, 2,173 changed, at 18.074 FPS in the final summary. Its
totals were 5 metadata rejections, 5 recoveries, and zero startup, timeout,
timestamp, sequence-error, decode, requeue, poll, DQBUF, sequence-gap, or
cleanup errors. The wrapper's total wall time, including setup, cycles, and
cleanup, was 348.266 seconds.

All 20 cycles were attempted: 9 passed and 11 failed. The cycle totals were:

- startup failures: 8 (`VIDIOC_STREAMON: Connection timed out`);
- metadata rejections: 3;
- timestamp, sequence-error, decode, and requeue failures: 0;
- sequence gaps: 426, all reported in cycle 20;
- rejected buffers: 3;
- cleanup failures: 0.

Cycles 1, 4, 6, 7, 9, 11, 12, and 16 failed at startup. Cycles 14 and 15
failed after one frame on metadata rejection. Cycle 20 decoded three frames,
reported 426 forward sequence gaps, and then hit a metadata rejection. The
remaining nine cycles passed their five-frame criterion.

### Rejection classification

There were eight rejection records in total. Every record had the exact
reason mask `0x00000004`, meaning `V4L2_BUF_FLAG_ERROR` only:

| reason mask | count | successful requeues | raw evidence |
| --- | ---: | ---: | --- |
| `0x00000004` | 8 | 8 | `flags=0x00002041`, `timestamp_flags=0x00002000`, `plane_count=1`, `bytesused=399360`, `data_offset=4`, capacity `400384` |

Thus error-flag-only buffers with successful requeue: **8**. There were no
combined reason masks. Invalid index, invalid plane count, unavailable
capacity, invalid plane metadata, timestamp flags/timestamp order, sequence
regression, decode, and requeue-failure counts were all zero. The 426 sequence
gaps are continuity discontinuities, not `sequence_errors` or rejection masks.

### Monotonic rejection timeline

Userspace `monotonic_ns / 1e9` and the kernel log's `short-monotonic` seconds
share the same monotonic clock domain. `reopen` is shown for persistent
recoveries; cycle rows show the next cycle's `stream_open` instead.

| attempt | rejection time | stop | next reopen/open | nearby kernel correlation |
| ---: | ---: | ---: | ---: | --- |
| 1 | 22000.434435 | +0.000103 s | reopen +0.321591 s | source-6 `error=8` +0.000635 s; stream disable +0.316540 s |
| 2 | 22007.857574 | +0.000034 s | reopen +0.294629 s | `error=8` +0.000416 s; stream disable +0.289413 s |
| 3 | 22018.054213 | +0.000043 s | reopen +0.295008 s | `error=8` +0.000314 s; stream disable +0.288717 s |
| 4 | 22034.125656 | +0.000053 s | reopen +0.361747 s | `error=8` +0.000603 s; receiver status lines and stream disable +0.355940 s |
| 5 | 22095.518552 | +0.000055 s | reopen +0.361342 s | `error=8` +0.000745 s; receiver status lines and stream disable +0.356472 s |
| 20 | 22296.379445 | +0.000037 s | next open +0.305174 s | sensor retry -0.239408 s; `error=8` +0.000228 s; stream disable +0.301216 s |
| 21 | 22296.931970 | +0.000069 s | next open +0.265059 s | sensor retry -0.791933 s; `error=8` +0.000424 s; stream disable +0.260425 s |
| 26 | 22342.190307 | +0.000058 s | none | sensor retry -0.474531 s; `error=8` +0.000381 s; stream disable +0.445193 s |

Every rejection therefore had a source-6 `error=8` line within 0.00023 to
0.00075 seconds after the userspace rejection. The cycle rejections also had
nearby sensor-retry activity. This establishes temporal correlation, not
causation or proof that userspace reopen caused the kernel events.

The run emitted 4,470 receiver snapshots, of which 4,391 had nonzero
`fatal_receiver_errors`; 1,075 had a nonzero retained fatal field while the
current receiver field was zero. It also emitted 611 explicit fatal receiver
status lines, 8,890 `error=8` lines, and 305 sensor retries. These are emitted
message counts only; existing kernel rate limiting remains active. Fatal
handling was not changed.

The evidence supports proposing a separate, narrowly guarded discard-and-
continue policy for an exact `0x00000004` rejection when the DQBUF index and
plane metadata are valid and requeue succeeds. It does not yet prove that
capture remains healthy without reopening, because this run deliberately kept
the existing reopen decision after every rejection. Any other reason mask
must retain structural-error recovery, and kernel fatal receiver handling must
remain unchanged. A separate short A/B acceptance run should be used before
implementing that policy.

Cleanup completed with `cleanup_failures=0`. The distribution module was
restored with the recorded hash and `modinfo` provenance, the bridge remained
inactive, and a post-cleanup graph verification matched the pre-run graph.
This confirms software restoration only; it does not claim BB8 register
rollback. No PHY setting, fatal handling, or desktop integration was changed.

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
