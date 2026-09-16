# C bridge integration acceptance matrix

The C controller is installed as the current user service executable on the
Surface Pro 7. The obsolete Python bridge and its bridge-specific test
harnesses have been removed; the historical optimization report retains the
pre-C measurements for comparison.

| Acceptance case | Result | Evidence |
| --- | --- | --- |
| C source builds with strict warnings | PASS | `make -C cbridge clean all` |
| Deterministic state-machine scenarios | PASS | rear priority, debounce, close grace, independent retries, format/WirePlumber/stop failures |
| Address/undefined sanitizers | PASS | `make -C cbridge sanitize` including the controller test |
| C service startup and idle fillers | PASS | one in-process PID, both `/dev/video60` and `/dev/video61` remain readable |
| C service crash recovery | PASS | SIGKILL of main PID; systemd restarted it with `NRestarts=1` |
| 60-second idle run x3 | PASS | 7.79/7.86/7.90 s cgroup CPU, ~10.3–10.8 MB, four cgroup processes |
| YUYV loopback reads | PASS for transport; FAIL for real scene | front/rear 300 frames at 30/27.3 fps, all black filler |
| MJPG loopback reads | PASS for transport; FAIL for real scene | front/rear 300 frames at 27.3/30 fps, all black filler |
| WirePlumber restart and audio preservation | PASS | service active, built-in audio sink/source and camera nodes present |
| Snapshot preview and still capture | NOT QUALIFIED | `Take Picture` was insensitive and created no photo because no camera stream was available |

The real-scene failures are attributable to the preserved historical
libcamera probe: the C service logs `/dev/video42: Device or resource busy`
and recovers with the configured backoff. Discord also held `/dev/video55`, so
the normal setup script correctly refused a destructive loopback reload; the C
binary was installed directly without disturbing that consumer.

The remaining acceptance gaps are narrow-kernel maintenance and a dedicated
Snapshot still-capture check. The active camera path is the C bridge and its
WirePlumber ownership policy described above.
