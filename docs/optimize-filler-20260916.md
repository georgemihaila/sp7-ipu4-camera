# Idle filler optimization evidence

The dormant `appsrc` producer remains an unqualified follow-up experiment;
the currently installed path retains continuous `videotestsrc` fillers.

The historical idle filler path negotiated `videotestsrc` directly to the requested
1280x720 caps. Camera capture still uses `videoconvert` and `videoscale`; the
optimization changes only the two idle pipelines.

The host-only comparison used a ten-second `gst-launch-1.0` run ending in
`fakesink`, so it did not change a camera device or the installed service:

| Path | Before user CPU | After user CPU | Before system CPU | After system CPU |
| --- | ---: | ---: | ---: | ---: |
| MJPEG filler | 0.47 s | 0.47 s | 0.00 s | 0.00 s |
| YUYV filler | 0.05 s | 0.05 s | 0.01 s | 0.00 s |

The result is CPU-neutral within this short host-only measurement while
removing two unnecessary filler elements per pipeline. No capture quality or
frame-rate setting changed. A live `/dev/video60` and `/dev/video61` capture
qualification remains required before treating this as a production default.
