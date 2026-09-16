# C camera bridge and media backend prototype

This opt-in prototype owns one GStreamer pipeline in-process. It queries the
selected loopback endpoint with `VIDIOC_G_FMT`, sets `camera-name` as a native
GStreamer property, and reports bus errors, warnings, EOS, and state changes
without launching `gst-launch-1.0`.

The MJPEG path intentionally remains:

```text
I420 -> jpegenc quality=85 -> jpegparse -> filesink(/dev/videoN)
```

The YUYV path uses `v4l2sink` after conversion/scaling. The compressed
`filesink` workaround is retained because the current loopback rejects the
compressed `v4l2sink` renegotiation.

Build and deterministic controller tests:

```sh
make -C cbridge
make -C cbridge test-controller
```

The complete C controller is still opt-in and is not installed by the normal
setup script. It owns the two filler pipelines, selects a requested camera
after debounce, applies the close grace period, retries failed capture and
filler starts independently, bounds WirePlumber operations, and adapts the
`/proc` consumer scan interval between 200 ms while active and 1000 ms while
stable and idle. Its live entrypoint is:

```sh
./cbridge/sp7-camera-bridge
```

The original media-backend prototype remains available for repeated direct
pipeline checks:

```sh
make -C cbridge sanitize
./cbridge/sp7-camera-backend-prototype --camera front --device /dev/video60 \
  --seconds 10 --cycles 3
```

Neither C binary installs or replaces the Python service. Startup and shutdown
have 10-second and 5-second bounds respectively. An unavailable device,
unsupported format, producer error, EOS, or failed state transition is a
nonzero result.
