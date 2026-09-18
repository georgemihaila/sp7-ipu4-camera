# Native application qualification matrix

This is the Phase 4 acceptance record for the no-bridge camera path. It is a
manual, hardware-gated test: a listed node, a successful device open, a raw
capture, or a working C bridge/`v4l2loopback` endpoint is not application
support.

## Result vocabulary

Use exactly one result for each application row:

| Result | Meaning |
|---|---|
| `PASS` | The named application showed a moving preview and captured usable image content, and every required lifecycle, camera-switching, permission, and log field is recorded. |
| `FAIL` | The application was exercised, but one or more required checks failed. Record the first failing check and the evidence. |
| `NOT TESTED` | The application was not exercised in this qualification run. |
| `ENUMERATED-ONLY` | The application or stack listed/identified a camera, but preview movement and capture were not proved in that application. |

`PASS` is the only support result. Do not infer it from another row: direct
V4L2 success does not qualify Snapshot, a browser, Zoom, or Discord, and a
PipeWire or portal listing does not qualify a client.

## Reproducibility and provenance

Run from the repository root on the prepared Surface Pro 7. Save the output
and timestamps with the application row. Do not substitute a guessed
`/dev/videoN` or `/dev/mediaN` minor; resolve every node at test time.

Record the kernel, session, and package/runtime versions first:

```sh
date --iso-8601=seconds
uname -a
rpm -q pipewire wireplumber xdg-desktop-portal xdg-desktop-portal-gnome libcamera 2>/dev/null || true
command -v cam >/dev/null 2>&1 && cam --version 2>&1 || true
command -v chromium >/dev/null 2>&1 && chromium --version 2>&1 || true
command -v zoom >/dev/null 2>&1 && zoom --version 2>&1 || true
flatpak list --app --columns=application,origin,version 2>/dev/null || true
flatpak permission-show org.gnome.Snapshot 2>/dev/null || true
flatpak permission-show com.google.Chrome 2>/dev/null || true
```

Discover the current physical graph and video nodes dynamically. The output
must be attached to the run; minor numbers are evidence, not identity:

```sh
for media in /dev/media*; do
    [ -e "$media" ] || continue
    media-ctl -d "$media" -p 2>&1
done

for entry in /sys/class/video4linux/video*; do
    [ -e "$entry" ] || continue
    node=/dev/$(basename "$entry")
    printf '\n=== %s (%s) ===\n' "$node" "$(cat "$entry/name" 2>/dev/null || true)"
    udevadm info --query=property --name="$node" 2>/dev/null || true
    v4l2-ctl --all --device="$node" 2>&1 || true
    v4l2-ctl --list-formats-ext --device="$node" 2>&1 || true
done

cam -l 2>&1 || true
pw-dump 2>&1 || true
```

Use the media entity/fwnode or libcamera camera ID and `Location` property to
record the selected physical camera as `front/OV5693` or `rear/OV8865` only
when the graph proves that mapping. Do not identify a camera by a minor number
alone.

For each application trial, capture bounded logs before and after the test:

```sh
journalctl -k -b --since "YYYY-MM-DD HH:MM:SS" --until "YYYY-MM-DD HH:MM:SS" \
    -o short-precise > application-kernel.log
journalctl --user -b --since "YYYY-MM-DD HH:MM:SS" --until "YYYY-MM-DD HH:MM:SS" \
    -u pipewire -u wireplumber -u xdg-desktop-portal -u xdg-desktop-portal-gnome \
    -o short-precise > application-pipewire-portal.log
```

Record whether the client was native/unsandboxed or Flatpak/sandboxed, the
portal permission result (`granted`, `denied`, or `not used`), and any
application-specific camera permission or format selection.

## Matrix

The following is the current qualification record. It intentionally starts
with no application support claim; replace `NOT TESTED` only with evidence
from the named application and the required fields below.

| Application/client | Native access path | Package/runtime and sandbox version | Selected physical camera identity | Preview movement and capture | Measured FPS | Lens-cover/content check | Front/rear switch | Close/reopen | Relaunch | Portal permission | Kernel/PipeWire log IDs | Result |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| GNOME Snapshot | PipeWire/libcamera or portal | Record package, GStreamer/libcamera, sandbox | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` |
| Chromium/WebRTC | PipeWire/portal/WebRTC | Record browser, runtime, sandbox | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` |
| Zoom Linux client | Native client access path actually selected | Record package/runtime and sandbox | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` |
| Discord Linux client | Native client access path actually selected | Record package/runtime and sandbox | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` |
| Direct V4L2 (`v4l2src` or FFmpeg) | Driver-owned processed V4L2 node | Record tool/package and sandbox | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `not used` | `NOT TESTED` | `NOT TESTED` |
| Native `cam` | libcamera | Record libcamera/cam version and sandbox | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `NOT TESTED` | `not used` | `NOT TESTED` | `NOT TESTED` |

## Required procedure for every row

1. Identify the package/runtime version and sandbox state. For Flatpak,
   record the application ID, origin, runtime, and portal permission result.
2. Select one physical camera using the stable graph/libcamera identity and
   record the selected front/rear sensor. Record the actual format and
   requested frame rate.
3. Start preview and confirm visible movement. Capture an image or short
   recording from that same application. A device-open message is not enough.
4. Measure observed frame rate over at least 10 seconds and record the method
   and value. Note drops, freezes, black frames, or stale frames.
5. Cover the selected lens, then uncover it and show changing scene content.
   Repeat for the other physical camera when switching is supported. A black
   or static preview fails this check even if the node enumerates.
6. Release the camera, reopen it, close the application, relaunch it, and
   repeat preview/capture. Record each outcome separately.
7. Switch front to rear and rear to front only after a clean release. Record
   unsupported switching as `FAIL` for clients whose row claims both cameras.
8. Save the bounded kernel and PipeWire/WirePlumber/portal logs and correlate
   errors with the trial timestamps.

For a direct V4L2 row, use a driver-owned node discovered above and record the
exact command, format, buffer type, payload/stride, and advancing frame
sequence. For the native `cam` row, record the camera ID, location, stream
configuration, capture count, and release/reopen result. These rows still
need moving, non-black content; enumeration or a raw Bayer dump alone is not
enough.

## Support statement rule

At release time, copy only rows with result `PASS` into the support statement,
including their exact package/runtime, kernel, firmware, sensor-driver,
libcamera, PipeWire, and portal versions. Keep `FAIL`, `NOT TESTED`, and
`ENUMERATED-ONLY` visible as qualification limits. The C bridge and
`v4l2loopback` are fallback/test tooling and cannot change any row to `PASS`.
