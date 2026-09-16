# C bridge qualification — 2026-09-17

This report records the current `optimize` branch after the C controller was
made the installed service executable. The Python bridge remains removed. No
reboot, kernel-module unload, or GitHub push was performed during this pass.

## Verified locally

| Check | Result | Evidence |
| --- | --- | --- |
| Strict C build and deterministic controller tests | PASS | `make -C cbridge clean all test-controller` |
| Static regression suite | PASS | `./tests/camera-suite.sh --static`, including Task 24 |
| Installed executable provenance | PASS | `cbridge/sp7-camera-bridge` and `/usr/local/libexec/sp7-camera-bridge` SHA-256 matched |
| YUYV front/rear real-scene payload | PASS after startup warm-up | 120 frames per endpoint; 1280x720; non-black luma detected |
| MJPEG front/rear real-scene payload | PASS after startup warm-up | 120 decoded frames per endpoint; `ffprobe` reported MJPEG 1280x720 |
| Repeated open/close lifecycle | PASS | 20 alternating front/rear V4L2 opens; service remained active |
| Repeated front/rear switching | PASS | 20 alternating switches with overlap/close-grace timing; no bridge producer failures or orphan bridge processes |
| Package assembly and checksums | PASS | exact running-kernel release archive verified with `sha256sum -c`; no `.py` or Python artifact included |
| Narrow kernel maintenance build | PASS, not installed | `scripts/build-modules.sh` produced matching `6.19.8-3.surface.fc43.x86_64` vermagic |

The first frames after a camera handoff can still be black filler while the
physical camera starts. The acceptance check therefore evaluates the settled
payload, not the initial debounce/filler interval. The C controller preserves
the requested 1280x720/30 endpoint contract and the MJPEG quality-85 path.

## Runtime state

At the end of the live lifecycle checks:

```text
sp7-camera-bridge.service: active/running
MainPID: 27722 (C bridge)
/dev/video60: named front loopback
/dev/video61: named rear loopback
```

WirePlumber remained active and the configured policy preserved the audio
devices and named V4L2 loopbacks. The running kernel module still predates the
Task 9 logging-level change: the rebuilt `.ko` was verified but deliberately
not installed or activated without a user-approved reboot/module reload.

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
negotiation issue, not evidence that the C bridge reverted to Python.
