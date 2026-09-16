# C bridge qualification — 2026-09-17

This report records the current `optimize` branch after the C controller was
made the installed service executable. The Python bridge remains removed. No
reboot, kernel-module unload, or GitHub push was performed during this pass.
Because repeated in-process libcamera teardown accumulated `soft_ipa_proxy`
workers and could hit libcamera's `queuedRequests_.empty()` assertion, the
installed controller now uses the plan's fallback: a C-owned worker process
per media pipeline, with bounded readiness, process-group termination, and
reaping.

## Verified locally

| Check | Result | Evidence |
| --- | --- | --- |
| Strict C build and deterministic controller tests | PASS | `make -C cbridge clean all test-controller` |
| Sanitizer build and deterministic controller tests | PASS | `make -C cbridge sanitize` |
| Static regression suite | PASS | `./tests/camera-suite.sh --static`, Tasks 8–25 |
| Installed executable provenance | PASS | `cbridge/sp7-camera-bridge` and `/usr/local/libexec/sp7-camera-bridge` SHA-256 matched |
| YUYV front/rear real-scene payload | PASS after startup warm-up | 120 frames per endpoint; 1280x720; front max YAVG 51.07/YMAX 104, rear 48.48/67 |
| MJPEG front/rear real-scene payload | PASS after startup warm-up | 120 decoded frames per endpoint; `ffprobe` reported MJPEG 1280x720; front 22.8 MB, rear 6.2 MB |
| Three 60-second streams per matrix case | PASS | front 28.66–28.67 fps; rear 30.07–30.09 fps; YUYV and MJPEG |
| Repeated open/close lifecycle | PASS | 20 alternating front/rear V4L2 opens; service remained active |
| Repeated front/rear switching | PASS | 20 alternating switches with overlap/close-grace timing; service remained active |
| Worker resource bounds | PASS | final lifecycle check: fd delta 0, parent `VmRSS` delta 4 KiB; worker count remained bounded |
| Service restart and recovery | PASS | explicit restart recovered `sp7-camera-bridge.service` |
| Package assembly and checksums | PASS | `localqual2` archive for `6.19.8-3.surface.fc43.x86_64`; `sha256sum -c` passed and no `.py`/`.pyc` artifact was present |
| Narrow kernel maintenance build | PASS, not installed | `scripts/build-modules.sh` produced matching `6.19.8-3.surface.fc43.x86_64` vermagic |

The first frames after a camera handoff can still be black filler while the
physical camera starts. The acceptance check therefore warms the source before
evaluating the settled payload. The C controller preserves the requested
1280x720/30 endpoint contract and the MJPEG quality-85 path. The physical
source mode is selected per camera before scaling: front uses 2560x1600 and
rear uses 1280x720, which avoids the rear frame-rate collapse without forcing
the front sensor into its unstable 1280x720 mode.

## Runtime state

At the end of the live lifecycle checks:

```text
sp7-camera-bridge.service: active/running
MainPID: current systemd user-service C supervisor
/proc/$MainPID/exe: /usr/local/libexec/sp7-camera-bridge
/dev/video60: named front loopback
/dev/video61: named rear loopback
```

WirePlumber remained active and the configured policy preserved the audio
devices and named V4L2 loopbacks. The running kernel module still predates the
Task 9 logging-level change: the rebuilt `.ko` was verified but deliberately
not installed or activated without a user-approved reboot/module reload.
The service parent has only its bounded worker children; media workers and
their libcamera helper descendants are terminated as a group and reaped after
each handoff.

## Remaining external qualification gap

GNOME Snapshot detects the camera through the portal, but its current session
still fails viewfinder negotiation with:

```text
handle_format_change: assertion 'gst_caps_is_fixed (pwsrc->caps)' failed
streaming stopped, reason not-negotiated (-4)
```

No still image was created, so Snapshot still capture is **not qualified**.
The available CUA surface exposed no native Snapshot window or shutter action;
the bridge itself continued running. This is an application/PipeWire
negotiation issue, not evidence that the C bridge reverted to Python. A direct
fixed-caps PipeWire probe did succeed, so no bridge-side ownership change was
justified for this external gap.

Audio playback and microphone concurrency were not exercised during this
qualification pass. Synthetic controller tests cover producer failure,
unavailable-device, format-query, WirePlumber, retry, and shutdown branches;
live service restart and recovery passed, but no destructive hardware-fault
injection was performed.
