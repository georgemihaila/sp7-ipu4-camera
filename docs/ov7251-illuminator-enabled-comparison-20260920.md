# OV7251 enabled-illuminator comparison: detector prerequisite missing

Date: 2026-09-20
Status: blocked before hardware activation

The first enabled comparison was not started. Detector readiness was checked
read-only after baseline commit `41caf08`.

## Missing prerequisite

No independent IR-sensitive detector is available on the host. The video
inventory contains only IPU4P CSI/BE/ISA/TPG nodes and the existing front/rear
bridge loopbacks (`/dev/video60` and `/dev/video61`). The USB inventory contains
no IR photodiode, optical power sensor, thermal/IR camera, or other independent
detector endpoint.

Consequently, detector sensitivity, response time, timestamp correlation, and
the off-state return threshold cannot be recorded. A brighter auto-exposed
OV7251 image or an indicator LED would not satisfy this prerequisite.

## Actions deliberately not taken

- No OV7251 candidate module was loaded.
- No ISYS replacement or BB8 initialization was performed.
- No sensor register writes, stream, capture, or emitter activation occurred.
- No receiver, graph, service, authentication, or installed-module state was
  changed.

The documented baseline remains the completed result in commit `41caf08`; it
was not repeated. The next attempt requires an independently connected
IR-sensitive detector with documented or measured spectral sensitivity and a
timestampable output that can be observed before stream start, during the
bounded stream, and after stop until it returns to the off baseline.
