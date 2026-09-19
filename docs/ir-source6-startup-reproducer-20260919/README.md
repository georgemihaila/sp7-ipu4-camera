# OV7251 source-6 startup reproducer

This package records one bounded comparison of a successful and a failed
OV7251 startup on IPU4 source 6. It is diagnostic evidence, not a stability
qualification and not physical CSI proof.

The complete unfiltered run artifacts are preserved outside the repository at:

```text
/var/tmp/ov7251-ir-source6-startup-20260919-200714/
```

The files and their SHA-256 values are listed in
[`artifact-sha256.txt`](artifact-sha256.txt). The paired monotonic timelines
and buffer accounting are in [`timeline.md`](timeline.md), and the questions
for IPU4/OV7251 maintainers are in
[`maintainer-questions.md`](maintainer-questions.md).

## Minimal reproducer

The recorded run used the source and harness commits below. They are named
explicitly because the current `feature/ir-camera` branch does not itself
contain the source-6 diagnostic commits; the commit objects remain available
locally and the diagnostic work is on the integration line.

```text
920ca58  ipu4p: trace source6 stream start buffer lifecycle
8ff6e30  ir: add bounded source6 startup comparison
98695e8  ir: record distribution module provenance
```

Build the qualifier and the source-backed module from that source chain, then
run the guarded reproducer. It refuses to touch hardware unless explicitly
enabled and refuses a different module or qualifier binary:

```sh
RUN_HARDWARE=1 \
OV7251_QUALIFY_MODULE=/absolute/path/to/intel-ipu4p-isys.ko \
OV7251_OUTPUT_DIR=/var/tmp/ov7251-ir-source6-reproducer-$(date +%Y%m%d-%H%M%S) \
./docs/ir-source6-startup-reproducer-20260919/reproduce-startup-compare.sh
```

The reproducer keeps the BB8 configuration and recovery policy unchanged,
requests the normal 600-second/20-cycle metadata, enables startup comparison,
limits the run to ten starts, and stops after at least one successful and one
failed start. Error-buffer discard is explicitly disabled. The wrapper's
existing cleanup path restores the direct links and the previously loaded
distribution module.

The exact build command for the recorded `.ko` was not preserved. The binary
hash, vermagic, Build ID, source commit, and selected path below are the
authoritative provenance; a future build must match the recorded hash before
being treated as the same candidate.

## Exact provenance

| Item | Recorded value |
| --- | --- |
| Kernel diagnostic source | commit `920ca5819d287402a9320f7d100c7c09c0140aad` |
| Diagnostic source files | `linux-6.19.8/drivers/media/pci/intel/ipu-isys-queue.c`, `ipu-isys-video.c`, `ipu-isys-video.h` |
| Diagnostic symbols | `verify_stream_start()`, `stream_capture_refeed()`, source-6 `IRTRACE` summaries |
| Selected candidate | `/home/george/repos/sp7-ipu4-camera/linux-6.19.8/drivers/media/pci/intel/ipu4/intel-ipu4p-isys.ko` |
| Candidate SHA-256 | `916c5e8d494663e0ccb3ccb071c8e84198bb4a24c345d03a90367ded77d5afc3` |
| Candidate vermagic | `6.19.8-3.surface.fc43.x86_64 SMP preempt mod_unload` |
| Candidate Build ID | `c85b39f45e452a02252c512483b5767d68ef1e9a` |
| Bundled original module | `scripts/ir/intel-ipu4p-isys-csi-header-tap-bb8.ko` |
| Bundled original SHA-256 | `546d80d5c692e0b394b56f4771fbf244bb9427419f6126405fe766fc9b1eaf7b` |
| Qualifier source | commit `8ff6e307d0897c86960223fb4e96d86d208d5005` |
| Module-selection/provenance wrapper | commit `98695e8810d743be1e2d6ca78335202fe18caf3f` |
| Qualifier SHA-256 | `30a299e22ca6a8f87121295e1d7c41039d2fae1c5190e7522ec7ac963399f8e1` |
| Distribution module restored | `/lib/modules/6.19.8-3.surface.fc43.x86_64/updates/extra/intel-ipu4p-isys.ko` |
| Distribution module SHA-256 | `10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce` |

The run log records the selected candidate path and hash, so the diagnostic
`IRTRACE source6` messages cannot be attributed to the bundled module.

## Configuration and outcome

The unchanged run used `/dev/media0`, OV7251 `2-0060` through
`Intel IPU4 CSI-2 1`, source 6, one receiver lane, VC 0, capture pin 0, and
the existing BB8 setup. CSI-tap records show port 4, data type `0x12b`, and
word count 800. The receiver snapshots include `rx enable=0x1`, `lanes=1`,
`config=0x3`, `ctermen=0`, `csettle=627`, `d0termen=0`, `d0settle=647`, and
`d1termen=0`, with `hs` varying between `0x0`, `0x100`, and `0x101`.

Four starts were attempted: attempts 1--3 succeeded and produced decoded
frames; attempt 4 failed with `VIDIOC_STREAMON: Connection timed out`. The
qualifier reported three startup successes, one startup failure, three valid
frames, zero userspace sequence gaps, zero rejected buffers, and zero cleanup
failures. This is a completed startup comparison, not a stability pass.

Firmware/receiver responses observed:

- `CSI-TAP fw PIN_DATA_READY source=6 ... error=0` for clean completions.
- `CSI-TAP fw PIN_DATA_READY source=6 ... error=8` for error-marked
  completions; the exact firmware meaning of `8` is unresolved.
- Packet records retained source-6 buffer identities, `vc=0`, `dtype=0x12b`,
  `word_count=800`, `port=4`, and the corresponding `error` value.
- Source-6 refeed records reported `submit`, `fail`, `fw`, incoming-queue, and
  active-queue counts. All observed firmware submission failures were zero.
- The failed attempt ended with driver result `-110` (`-ETIMEDOUT`) after 30
  retries and rollback of active buffers.

## Verified facts and hypotheses

Verified by the source and captured logs:

- The candidate module was selected by path and hash and emitted the new
  source-6 diagnostics.
- Attempts 1--3 had clean-frame evidence. Attempt 4 had no timely comparable
  completion; its first `error=8` arrived after the clean-frame timeout
  decision during teardown, so the run does not establish that all startup
  completions were error-marked.
- Attempts 1--2 paired every newly parked identity with a refed identity;
  attempt 3 required no new refeed; attempt 4 retained eight in-flight active
  buffers and rolled them back from `active=8` to `active=0`.
- Initial `hs=0x0` is not predictive: it occurred in successful attempts 1--2
  and in failed attempt 4.
- Attempt 4 produced no timely completion comparable to the successful starts.
  Its first `error=8` was logged at +22.7 seconds, after the clean-frame
  timeout decision and before the stop/rollback sequence completed. It must
  not be described as proof that startup produced only error-marked frames.
- No buffer was demonstrated lost, duplicated, or stranded. The result is
  classified as a transport failure, not a bookkeeping defect, but the late
  `error=8` is not sufficient to identify the startup transport cause.
- The current `fatal_receiver_errors` fields and snapshots were preserved;
  they do not independently prove a newly latched fatal state or a reset bug.

Evidence-backed hypotheses that remain unproven:

- A source-6 sensor LP-to-HS transition or receiver lock may be failing on the
  failed start. The failed attempt's first post-start snapshot showed `hs=0`
  and later `hs=0x101` with `receiver_errors=0x8643`; this is correlation, not
  physical lane evidence.
- Firmware `error=8` may be a recoverable per-buffer transport result, but no
  maintainer contract or firmware enum was available to establish that.
- The existing sensor-bounce retry policy may be the intended recovery for this
  condition, but its source-6 applicability and ordering assumptions are not
  proven for OV7251.

The earliest observable divergence is described precisely in
[`timeline.md`](timeline.md). No PHY value, fatal handling, module policy, or
desktop integration was changed.

## Next review boundary

One falsifiable hypothesis is sufficient for the next technical review:

> The failed start's first `error=8` is a late firmware completion emitted
> after the driver's clean-frame timeout while stream teardown is in progress,
> rather than the event that caused the initial startup failure.

Predicted observation: a failed start will show the clean-frame timeout before
the first `PIN_DATA_READY error=8`, with the error-8 completion occurring
before or during the stop/flush path; a timely error-8 completion before the
clean-frame timeout would falsify this hypothesis. The current run already
places the events as timeout summary `26377.445725`, first error-8 completion
`26379.412993`, stream-stop timeout `26379.498114`, failed stop
`26379.511233`, and rollback marker `26379.511523`. The flush function has no
dedicated timestamp, so its exact relation to the late completion is not yet
observable.

The narrowly scoped follow-up, if later authorized, is instrumentation only:
timestamp `flush_firmware_streamon_fail()` entry/return, stream-stop
entry/return, and source-6 SOF/EOF/PIN_DATA_READY events, then capture one
successful and one failed start with the unchanged configuration and recovery
policy. No PHY change, fatal-policy change, or endurance run is justified by
the present evidence.

## Restoration limits

Cleanup reported `cleanup_failures=0`; the distribution module was loaded
again, `modinfo` resolved to its distribution path, and the direct-capture
links were disabled. The post-run graph loses transient stream-format state as
expected after capture stops.

This verifies software restoration only. It does not establish BB8 register
rollback, sensor register rollback, power/reset/clock rollback, physical lane
behavior, reset of a latched firmware/receiver state, or a successful desktop
camera integration. The complete kernel log retains existing rate limiting.
