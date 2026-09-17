# C camera bridge and media backend prototype

The installed C controller owns the named-camera service and supervises one
short-lived worker process per active pipeline. Each worker uses the C
GStreamer API, queries the selected loopback endpoint with `VIDIOC_G_FMT`
(`VIDEO_CAPTURE` first, then `VIDEO_OUTPUT` while an exclusive-caps endpoint is
still in its unopened producer state), sets `camera-name` as a native GStreamer
property, and reports bus errors,
warnings, EOS, and state changes. Process isolation bounds libcamera teardown
failures and ensures the service reaps every media worker.

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

The complete C controller is installed by the normal setup script as the
`sp7-camera-bridge.service` user service. It owns the two filler pipelines,
selects a requested camera after debounce, applies the close grace period,
retries failed capture and filler starts independently, bounds WirePlumber
operations, and adapts the `/proc` consumer scan interval between 200 ms while
active and 1000 ms while stable and idle. Idle `videotestsrc` fillers also link
directly to their negotiated caps; camera capture keeps the conversion and
scaling stages. Its live entrypoint is:

```sh
./cbridge/sp7-camera-bridge
```

The original media-backend C implementation remains available for repeated
direct pipeline checks:

```sh
make -C cbridge sanitize
./cbridge/sp7-camera-backend-prototype --camera front --device /dev/video60 \
  --seconds 10 --cycles 3
```

The prototype binary does not install or replace the service; the controller
binary is the service executable installed by setup. Worker startup and
shutdown have 10-second and 5-second bounds respectively. An unavailable
device, unsupported format, producer error, EOS, or failed state transition is
a nonzero result.
