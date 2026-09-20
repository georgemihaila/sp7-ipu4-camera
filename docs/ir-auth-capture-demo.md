# Bounded direct-capture face-recognition demo

This milestone adds a standalone, non-PAM authentication experiment. It is
not a login method and it does not change PAM, GNOME login, the lock screen,
or the existing RGB bridge.

## Trust boundary

The capture path is:

```text
OV7251 -> enabled source-6 media route -> direct V4L2 capture
       -> ir-v4l2.c validation/RAW10 decode -> auth helper frame protocol
       -> standalone dlib/Howdy-style enrollment or matching demo
```

`sp7-camera-auth-capture` never opens `/dev/video62`, never reads a cached
image, and never writes a bridge filler. It discovers the enabled OV7251
source-6 route through the media graph and reuses `ir-v4l2.c`. It forwards
only frames that have passed the existing metadata, timestamp, sequence,
buffer-capacity, RAW10 header, and decode checks. `V4L2_BUF_FLAG_ERROR` buffers
are requeued and discarded when they are the only rejection reason; mixed or
malformed errors fail closed.

The installed helper has a fixed capture path and no environment/configuration
override. It requires effective UID 0, is installed root-owned mode `0755`,
and serializes sessions with the root-owned mode `0600` lock
`/run/lock/sp7-camera-auth-capture.lock`. The standalone Python demo verifies
that installed helper before invoking it.

## Illuminator prerequisite

Howdy does not write OV7251 registers from userspace. The patched OV7251
driver enables and verifies its sensor STROBE/frame-PWM output during
`STREAMON` when the load-time option `experimental_strobe_output=1` is
active. Before any live enrollment or matching attempt, the protected
recorder requires:

```text
/sys/module/ov7251/parameters/experimental_strobe_output = Y
```

If the parameter is absent or disabled, the attempt fails closed and the
caller must use password fallback. The capture helper does not load, unload,
or swap the sensor module, change the media graph, or write sensor registers.
The patched module and its option must therefore be prepared before the
attempt, and the driver's stream-stop/failure cleanup must complete before
the camera is released.

## Deadline and ownership

The parent supervisor starts one capture worker and owns the lock for the
whole session. Its fixed three-second budget covers worker startup, direct
capture, and output forwarding. The worker itself polls in short intervals,
but `VIDIOC_STREAMON` remains isolated in the worker because the driver can
block longer than the authentication budget.

On deadline, the supervisor sends `SIGTERM`, closes the frame pipe, and
returns status `124` so a caller can offer its normal password fallback. The
worker retains the inherited lock while it finishes `STREAMOFF`, buffer
unmapping, and descriptor cleanup. A second invocation sees status `2` while
that cleanup worker still owns the camera; it never overlaps or interrupts
the first worker. No module replacement, module unload, media-link change,
or bridge restart is performed by this path.

If RGB already owns the shared camera backend, route discovery or direct-node
open/stream setup fails without changing RGB state. The result is an explicit
no-decision/fallback condition; the helper does not stop RGB to obtain a
frame.

## Frame protocol

The helper writes a binary stream to standard output. Each frame is a fixed
little-endian 44-byte header followed by 640x480 8-bit luma bytes:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 8 | `SP7IRF01` magic |
| 8 | 4 | protocol version (`1`) |
| 12 | 4 | width (`640`) |
| 16 | 4 | height (`480`) |
| 20 | 4 | luma payload bytes |
| 24 | 4 | V4L2 sequence |
| 28 | 8 | monotonic timestamp seconds |
| 36 | 8 | monotonic timestamp microseconds |

The Python demo additionally requires strictly increasing sequence/timestamp
tuples. The C helper rejects decoded frames older than two seconds at the
forwarding boundary. There is no last-frame fallback.

## Standalone enrollment/matching

After the normal full setup has installed the helper, first confirm that
Howdy's dlib data and Python dependencies are present. The demo uses the same
detector, five-point landmark predictor, and face descriptor flow as Howdy,
but keeps its model in an explicitly supplied demo JSON file:

```sh
sudo ./scripts/setup-camera-bridge.sh
sudo python3 scripts/ir/howdy-direct-demo.py enroll \
  --model /var/lib/sp7-camera-auth-demo/george.json \
  --data-dir /usr/etc/howdy/dlib-data \
  --label george

sudo python3 scripts/ir/howdy-direct-demo.py match \
  --model /var/lib/sp7-camera-auth-demo/george.json \
  --data-dir /usr/etc/howdy/dlib-data
```

Enrollment captures five fresh frames and requires at least three frames with
exactly one detected face. Matching requires two matching frames by default.
No face, multiple faces, stale frames, camera failure, ownership contention,
or a timeout produces a match; it produces an unavailable/no-match result.

This is deliberately a separate demo rather than a Howdy installation. The
current Howdy recorder selects OpenCV, FFmpeg, or `pyv4l2` from its config and
expects `read()`/`read_frame()` semantics, so a reviewed recorder adapter is
still needed before Howdy itself can consume this private protocol. See the
[Howdy recorder implementation](https://github.com/boltgolt/howdy/blob/master/howdy/src/recorders/video_capture.py).

## Qualification still required

The demo is recognition-only and has no liveness or spoof resistance. Before
any PAM work, qualify adequate-light enrollment and matching across face
detail, exposure, orientation, glasses, distance, no-face and another-person
cases. Separately test printed photos, phone displays, and replayed video;
any acceptance is a security failure for authentication, not a success.
Controlled IR illumination, darkness, boot, resume, and actual login/lock
screen behavior remain later, approval-gated milestones.
