# Optimization baseline — 2026-09-16

This is the baseline for the `optimize` branch. It records the published
checkout before the C bridge work began. Measurements were made on the live
Surface Pro 7 without rebooting or loading/unloading kernel modules. Capture
artifacts and raw measurement logs are in `/tmp/sp7-camera-baseline.*` and are
not part of the repository.

## Reproduction

The measurements below were collected before the Python bridge was removed;
the temporary baseline harness is no longer part of the checkout. It measured
the service cgroup's CPU time, current memory, process/thread
count, and aggregate voluntary/non-voluntary context switches. Streaming cases
hold a named V4L2 endpoint open for 60 seconds, save the output only in a
temporary directory, and inspect YUYV luma or decoded JPEG dimensions/contrast
to distinguish black filler from a non-filler frame. A format change is made
only while the bridge is stopped, because v4l2loopback rejects changing the
capture format while a producer owns the endpoint.

## Host and dependency inventory

- Source commit at measurement start: `4f200b6`
- Branch: `optimize`
- Kernel: `6.19.8-3.surface.fc43.x86_64`, x86_64
- Loaded bridge module: `/lib/modules/6.19.8-3.surface.fc43.x86_64/updates/extra/intel-ipu4p-isys.ko`
  and the matching installed IPU4P module set; `v4l2loopback` is 0.15.4.
- Runtime packages: `libcamera-gstreamer-0.7.1-1.fc44`,
  `gstreamer1-plugins-good-1.28.7-1.fc44`, GStreamer base 1.28.7,
  `v4l-utils-1.32.0-3.fc44`, `v4l2loopback-0.15.4-1.fc44`, and Python 3.14.7.
- Runtime elements present: `libcamerasrc`, `videotestsrc`, `videoconvert`,
  `videoscale`, `jpegenc`, `jpegparse`, `v4l2sink`, and `filesink`.
- Runtime bridge dependencies: Python 3, libcamera's GStreamer source,
  GStreamer good/base plugins, v4l2loopback, v4l2-ctl, and a user systemd/
  WirePlumber session.
- Build-only dependencies: GCC, make, kernel headers/prepared matching
  kernel tree, elfutils-libelf-devel, openssl-devel, perl, Python 3, bc,
  dwarves, flex, bison, kmod, and git. The current installer also installs
  these for a source module build.
- Installation-only dependencies: root, DNF, curl, dnf-plugins-core,
  `kernel-surface-devel` when the exact prepared tree is absent, `msitools`
  only for Microsoft firmware extraction, and the runtime packages above.
- Diagnostic-only tools used here: `ps`, cgroup v2 files, `journalctl`,
  `systemctl`, `file`, and Pillow in the developer measurement helper.

## Measured behavior

The existing service was active with two black 1280×720, 30 fps filler
producers. Three idle windows completed:

| case | cgroup CPU delta | current memory | cgroup pids | context-switch delta |
|---|---:|---:|---:|---:|
| idle 1 | 13.19 s / 60 s | 24.4 MB | 9 | 1,527 |
| idle 2 | 13.08 s / 60 s | 24.4 MB | 9 | 1,628 |
| idle 3 | 13.18 s / 60 s | 24.4 MB | 9 | 1,387 |

The process tree visible in each idle window was one Python controller and two
`gst-launch-1.0` fillers. `ps` showed each filler around 5.2–5.4% CPU on this
host; the controller's lifetime-average display was about 11.4–11.9% after
several minutes. The cgroup CPU delta is the comparable measurement.

Streaming observations:

- Front YUYV, three consecutive windows from the pre-harness clean stream
  series: 1,800 frames each at 28.0, 28.69, and 28.69 fps. First non-filler
  frames were 26, 1, and 1. Decoded/inspected YUYV luma ranged approximately
  52.2–52.9 with standard deviation 13.8–14.2 after startup.
- Front MJPG smoke case: 150 decoded 1280×720 JPEG frames at 28.97 fps;
  first non-filler frame 1; mean luma 55.9–56.1. This was a bounded smoke
  case, not the required three 60-second repetitions.
- Rear MJPG smoke case: 120 decoded 1280×720 JPEG frames at 30 fps. It
  completed, but this was also a bounded smoke case.
- Front/rear YUYV after controlled format setup were not reliable. A front
  60-second case delivered only 1,309 black filler frames and timed out; the
  following probes delivered no frames. The service then entered the existing
  WirePlumber restart/teardown failure described below.

The non-filler test is a content heuristic, not a calibration or image-quality
claim. GUI preview/capture in GNOME Snapshot was not verified in this baseline.

## Existing failures and compatibility contract

The initial contract remains: `/dev/video60` named front, `/dev/video61` named
rear, `/dev/video55` OBS, one physical camera at a time, rear priority when
both are first requested, 1280×720 at configured 30 fps, upright output,
automatic exposure, JPEG quality 85, current debounce/close-grace/retry/
route-settling timings, idle readable endpoints, and YUYV plus MJPG/JPEG
output.

Observed failures that must remain distinguishable from regressions:

1. A short front consumer can cause the Python `gst-launch` teardown to wait
   for EOS and hit the 4-second stop timeout.
2. After a failed or slow capture teardown, the Python controller can block in
   `systemctl --user start wireplumber.service`. systemd eventually aborts the
   bridge after its service stop timeout; the bridge's cgroup once reached a
   170 MB peak and was left failed until manually reset/restarted.
3. Repeated probes can leave a camera producer and prevent later probes from
   receiving frames. GNU `timeout` also needed an explicit kill-after in the
   harness to bound a blocked V4L2 reader.
4. The libcamera logs include expected existing TPG/no-format errors and
   uncalibrated IPA fallback warnings. They are not evidence of successful
   GUI camera support.

The current bridge is Python supervising separate GStreamer subprocesses. The
C plan must preserve the visible endpoint contract and JPEG sink workaround,
but it must make startup success, streaming success, teardown, retries, and
WirePlumber ownership independently observable and bounded.
