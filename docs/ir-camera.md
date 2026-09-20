# IR camera: OV7251 control and data-capture path

The IR camera is a separate OV7251 monochrome producer. It is not managed by
the front/rear RGB bridge. The implementation has reached the sensor, the
source-6 receiver, direct packet buffers, and a userspace RAW10 decoder, but
continuous desktop-camera qualification is still failed. Keep that distinction
when debugging this path.

## Hardware and ownership

| Layer | Value |
|---|---|
| Sensor | OV7251 monochrome |
| ACPI / platform identity | HID `INT347E`, I2C client `i2c-INT347E:00` |
| I2C bus/address | DesignWare bus `2`, 7-bit address `0x60` (`2-0060`) |
| External clock | `19.2 MHz` |
| IPU4P firmware source | source `6` |
| CSI-2 receiver | compact CSI index `1`, entity `Intel IPU4 CSI-2 1` |
| Data lanes | `1` |
| Link frequency property | `319.2 MHz` from the ACPI/IPU bridge endpoint |
| Media-bus format | `MEDIA_BUS_FMT_Y10_1X10` |
| Capture format | `V4L2_PIX_FMT_Y10`, one multi-planar plane |
| Nominal image | `640x480`, 30/60/90-fps sensor modes are advertised by the external driver |
| Public loopback | **Surface Camera (IR)** on `/dev/video62`, opt-in |

The normal bridge service never starts `sp7-camera-ir`, never switches an RGB
request to IR, and never changes the front/rear configuration. `/dev/video62`
is reserved by `v4l2loopback` but remains output-only until the standalone
producer opens it and writes frames.

## Media graph and receiver route

The direct route used by the producer is:

```text
ov7251 2-0060:0
    -> Intel IPU4 CSI-2 1:0
    -> Intel IPU4 CSI-2 1:1
    -> Intel IPU4 CSI-2 1 capture 0:0
    -> direct packet-buffer video node (minor is discovered dynamically)
```

The two links that must be enabled are:

```text
ov7251 2-0060:0 -> Intel IPU4 CSI-2 1:0
Intel IPU4 CSI-2 1:1 -> Intel IPU4 CSI-2 1 capture 0:0
```

`cbridge/ir-v4l2.c` does not assume `/dev/media0`, `/dev/video5`, or a fixed
sub-device minor. It enumerates every `/dev/media*`, finds entities by name,
checks both link directions and `MEDIA_LNK_FL_ENABLED`, then matches the
entity's major/minor to `/dev/v4l-subdev*` and `/dev/video*`.

For a diagnostic-only prepared route, the repository helper shows the same
names:

```sh
media-ctl -d "$MEDIA" -p
media-ctl -d "$MEDIA" \
  -V '"Intel IPU4 CSI-2 1":0 [fmt:Y10_1X10/640x480 field:none]'
media-ctl -d "$MEDIA" \
  -V '"Intel IPU4 CSI-2 1":1 [fmt:Y10_1X10/640x480 field:none]'
```

The source-6 receiver is started with one lane and RAW10 before the OV7251
driver's stream-on callback. The sensor's stream control is the standard
OV7251 write `0x0100 = 0x01` for streaming and `0x0100 = 0x00` for standby;
the receiver does not become a clean image path merely because that I2C write
reads back successfully.

## Negotiated V4L2 layout

The producer configures both CSI pads with the exact 640x480 `Y10_1X10` code,
then negotiates one multi-planar capture plane:

```c
struct v4l2_format format = { 0 };

format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
format.fmt.pix_mp.width = 640;
format.fmt.pix_mp.height = 480;
format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_Y10; /* fourcc: "Y10 " */
format.fmt.pix_mp.field = V4L2_FIELD_NONE;
format.fmt.pix_mp.num_planes = 1;
ioctl(video_fd, VIDIOC_S_FMT, &format);
ioctl(video_fd, VIDIOC_G_FMT, &format);
```

The backend accepts only a negotiated stride that satisfies all of these:

```text
RAW10 payload per row = 640 * 10 / 8 = 800 bytes
source-6 line header  = 4 bytes
minimum stride        = 832 bytes
maximum row padding   = 64 bytes after header + payload
sizeimage             >= stride * 480
```

The verified direct-tap run used `stride=832`, `bytesused=399360` and
`data_offset=4`. The plane may report a larger `sizeimage` (the observed node
reported `400384`); `bytesused` is the amount of this dequeued frame that the
decoder is allowed to read.

The direct node is a CSI packet/raster buffer with a per-line transport
header. It is not the clean processed RGB path used by the BE-SOC cameras.
Observed packet records include:

```text
vc=0 dtype=0x12b word_count=800
line 0 converted header: 0x012b0320 / 0x80000040
line 1 converted header: 0x212b0320 / 0x00000040
```

`0x2b` is the CSI-2 RAW10 data type and `0x0320` is the 800-byte word count.
The decoder's stable contract is narrower than the diagnostic printout: it
checks that the low byte of each 32-bit line header is `0x40`, then decodes
the 800 bytes following that header.

## RAW10 unpacking and YUYV publication

Every four 10-bit luma samples occupy five bytes. If a row's packed payload is
`s[0]..s[4]`, the decoder reconstructs the samples as:

```c
uint16_t p0 = ((uint16_t)s[0] << 2) | ((s[4] >> 0) & 0x03);
uint16_t p1 = ((uint16_t)s[1] << 2) | ((s[4] >> 2) & 0x03);
uint16_t p2 = ((uint16_t)s[2] << 2) | ((s[4] >> 4) & 0x03);
uint16_t p3 = ((uint16_t)s[3] << 2) | ((s[4] >> 6) & 0x03);
```

The public loopback is grayscale YUYV, not RAW10. Each reconstructed sample
is reduced from 10 to 8 bits and gets neutral chroma:

```c
for (unsigned i = 0; i < 4; i++) {
    yuyv[out + i * 2 + 0] = (uint8_t)(pixel[i] >> 2); /* Y */
    yuyv[out + i * 2 + 1] = 0x80;                     /* neutral U/V */
}
```

The producer writes exactly `640 * 480 * 2 = 614400` bytes per output frame
to `/dev/video62`, after first setting its `VIDEO_OUTPUT` format to YUYV,
640x480, no field, `bytesperline=1280`, and `sizeimage=614400`.

## Buffer ownership and validation

`ir_capture_open()` requests eight MMAP capture buffers. On start it queues
every buffer and calls `VIDIOC_STREAMON` with
`V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE`. Each dequeue is checked before decode:

```text
buffer index          < mapped buffer count
plane count           == 1
V4L2_BUF_FLAG_ERROR   absent for a normal frame
timestamp flags       == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC
timestamp             non-empty and strictly increasing
sequence              strictly advancing, with gaps counted
data_offset           < bytesused <= mapped capacity
line headers          low byte == 0x40 on all 480 rows
RAW10                 fits inside the negotiated stride and bytesused
```

The buffer is requeued with `VIDIOC_QBUF` after a successful decode. A
rejected buffer is also requeued when its index, plane count, and mapping make
that safe. The optional error-buffer continuation accepts only a buffer whose
sole rejection reason is `V4L2_BUF_FLAG_ERROR` and whose requeue succeeded; it
stops after five consecutive discards or two seconds without a valid frame.

On a fatal capture error, the producer performs `VIDIOC_STREAMOFF`, closes the
capture, waits one second, and re-discovers/reopens the route. Startup and
runtime failures are bounded to five recovery attempts in `sp7-camera-ir`.

## Commands and protected frame protocol

After an authorized source-6 route is prepared, the standalone producer is:

```sh
/usr/local/libexec/sp7-camera-ir
# or, from a build checkout:
SP7_IR_OUTPUT=/dev/video62 ./cbridge/sp7-camera-ir
```

For one direct packet-buffer diagnostic capture and RAW10-to-PNG conversion:

```sh
scripts/ir/capture-ov7251-ir-frame.sh /var/tmp/ov7251-ir-capture
```

That helper requires the prepared direct route and a matching temporary
source-6 module. It is not an installer and does not itself write sensor,
receiver, PLL, or port registers.

The protected authentication helper exports luma without exposing YUYV. Its
binary frame header is little-endian and exactly 44 bytes:

```c
struct {
    char     magic[8];       /* "SP7IRF01" */
    uint32_t version;        /* 1 */
    uint32_t width;          /* 640 */
    uint32_t height;         /* 480 */
    uint32_t payload_bytes;  /* 307200 */
    uint32_t sequence;
    uint64_t timestamp_sec;
    uint64_t timestamp_usec;
};
/* followed by 640 * 480 one-byte luma samples */
```

`sp7_ir_protocol.py` accepts only the root-owned fixed helper path, requires a
fresh monotonic timestamp, rejects stale/regressed sequence values, and bounds
one capture session to at most 12 frames and three seconds. It also requires
the separate illuminator module parameter; it does not turn the illuminator on
itself.

## Qualification boundary

The direct path has demonstrated changing decoded RAW10 payloads and valid
packet headers. The persistent source-6 run still reports receiver/fatal
errors, stream-start timeouts, sequence gaps, and failed stop/start cycles.
Therefore this page does not claim a stable IR camera, a qualified GNOME
Snapshot source, or simultaneous RGB+IR capture. Probe success, a media node,
`STREAMON`, one changing buffer, or `/dev/video62` enumeration is not the
acceptance criterion; qualification requires sustained changing frames,
reopen/stop-start behavior, and moving preview plus a saved capture in the
named application.

Related source and evidence:

- [`docs/ov7251-ir-backend.md`](ov7251-ir-backend.md)
- [`cbridge/ir-v4l2.c`](../cbridge/ir-v4l2.c)
- [`cbridge/ir-camera-bridge.c`](../cbridge/ir-camera-bridge.c)
- [`scripts/ir/capture-ov7251-ir-frame.sh`](../scripts/ir/capture-ov7251-ir-frame.sh)
- [`docs/ov7251-source6-bb8-rollback-and-csi-header-tap.md`](ov7251-source6-bb8-rollback-and-csi-header-tap.md)
