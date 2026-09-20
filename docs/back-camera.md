# Back camera: OV8865 control and capture path

This page describes the back-camera path from the OV8865 sensor to a user-space
frame. It is intentionally more precise than the README summary: the OV8865
sensor driver is supplied by the kernel/linux-surface stack and is not present
in this repository.

## Hardware and media contract

The Surface Pro 7 back camera is the OV8865 on the rear CSI-2 input:

| Layer | Value |
|---|---|
| Sensor | OV8865; external driver |
| IPU4P firmware source | source `3` |
| CSI-2 receiver | port/index `0` |
| Data lanes | `4` |
| Link frequency | `360000000` Hz |
| Sensor bus code | `MEDIA_BUS_FMT_SBGGR10_1X10` |
| Active frame | `3264x2448` |
| Processed IPU V4L2 format | `V4L2_PIX_FMT_SBGGR10` / `BG10`, 16 bits per sample with 10 meaningful bits |
| Named loopback | **Surface Camera (back)**, normally `/dev/video61` |

The source/lane/link-frequency tuple comes from the rear reliability record and
the live media graph. Do not copy these values to a different Surface model or
assume that a `/dev/videoN` minor identifies the rear sensor after reboot.

The physical route is:

```text
OV8865 I2C sensor
    -> Intel IPU4 CSI-2 0, sink pad 0       (4-lane CSI-2, RAW10)
    -> Intel IPU4 CSI2 BE SOC, sink pad 0   (one input selected)
    -> Intel IPU4 CSI2 BE SOC, source pad 8
    -> Intel IPU4 BE SOC capture 0          (/dev/video*, BG10)
    -> libcamera Simple + SoftISP           (processed RGB/YUV)
    -> sp7-camera-bridge                    (optional V4L2 compatibility path)
    -> /dev/video61                         (YUYV or MJPEG, 1280x720)
```

The BE-SOC sink is a hardware input mux. The in-tree
`csi2_be_soc_link_setup()` callback rejects a second enabled link to the same
sink with `-EBUSY`; front and back capture are therefore serialized.

## Kernel graph setup and raw capture

`test-capture.sh rear` discovers the media device, sensor entity, sub-device
node, and CSI port from the graph. Its effective setup is:

```sh
MEDIA_DEVICE=/dev/mediaX                 # discover; do not assume X
SENSOR='ov8865 3-0010'                   # discover the entity name
PORT=0

media-ctl -d "$MEDIA_DEVICE" -r
media-ctl -d "$MEDIA_DEVICE" \
  -V "\"$SENSOR\":0 [fmt:SBGGR10_1X10/3264x2448]"
media-ctl -d "$MEDIA_DEVICE" \
  -V '"Intel IPU4 CSI-2 0":0 [fmt:SBGGR10_1X10/3264x2448]'
media-ctl -d "$MEDIA_DEVICE" \
  -V '"Intel IPU4 CSI-2 0":1 [fmt:SBGGR10_1X10/3264x2448]'

# [5] = MEDIA_LNK_FL_DYNAMIC (0x4) | MEDIA_LNK_FL_ENABLED (0x1).
media-ctl -d "$MEDIA_DEVICE" \
  -l "\"$SENSOR\":0 -> \"Intel IPU4 CSI-2 0\":0 [1]"
media-ctl -d "$MEDIA_DEVICE" \
  -l '"Intel IPU4 CSI-2 0":1 -> "Intel IPU4 CSI2 BE SOC":0 [5]'
media-ctl -d "$MEDIA_DEVICE" \
  -l '"Intel IPU4 CSI2 BE SOC":8 -> "Intel IPU4 BE SOC capture 0":0 [5]'
media-ctl -d "$MEDIA_DEVICE" \
  -V '"Intel IPU4 CSI2 BE SOC":0 [fmt:SBGGR10_1X10/3264x2448]'
media-ctl -d "$MEDIA_DEVICE" \
  -V '"Intel IPU4 CSI2 BE SOC":8 [fmt:SBGGR10_1X10/3264x2448]'

RAW_NODE=$(media-ctl -d "$MEDIA_DEVICE" -e 'Intel IPU4 BE SOC capture 0')
v4l2-ctl -d "$RAW_NODE" \
  --set-fmt-video=width=3264,height=2448,pixelformat=BG10 \
  --stream-mmap=4 --stream-count=3 --stream-to=back.raw
```

The dynamic-link flag matters. `media-ctl` 1.32 passes link flags verbatim;
using `[1]` on a link registered as dynamic can cause the kernel to reject the
setup. The capture helper preserves the dynamic bit as `[5]`.

One unpacked `BG10` frame occupies:

```text
3264 pixels/row * 2448 rows * 2 bytes/sample = 15,980,544 bytes/frame
3 frames                                      = 47,941,632 bytes
```

`BG10` here is not the packed 10-bit `pBAA` representation. The BE-SOC format
table maps `MEDIA_BUS_FMT_SBGGR10_1X10` to `V4L2_PIX_FMT_SBGGR10` with
`bpp=16` and `depth=10`; the six remaining bits are storage padding, not extra
sensor precision.

During stream-on, the IPU path starts the firmware/capture side and enables the
receiver before calling the external sensor's `s_stream(1)`. On stop it
disables the route, returns buffers, and clears the receiver error state on a
clean final disable. A receiver error message or successful `STREAMON` alone
is not a valid image result: validate the output size and changing Bayer
payload.

## libcamera and the named V4L2 endpoint

The normal processed path is Fedora's existing `intel-ipu6` Simple pipeline
with SoftISP. This repository does not contain an OV8865 sensor driver, a
libcamera pipeline handler, or an OV8865 IPA. The processed path must therefore
be checked against the installed kernel/libcamera pair:

```sh
cam -l
LIBCAMERA_VALIDATION_DIR="$PWD/reports/native-libcamera" \
  ./tests/libcamera-native-validation.sh --live
```

The fallback bridge exposes the external camera through the stable label
**Surface Camera (back)**. The controller binds that label to the ACPI camera
identity:

```c
static const char *camera_ids[] = {
    "\\_SB_.PCI0.I2C2.CAMF", /* front */
    "\\_SB_.PCI0.I2C3.CAMR", /* back  */
};
```

For the back camera, the C worker requests a native GStreamer source with
`camera-name="\\_SB_.PCI0.I2C3.CAMR"`, enables libcamera AE, and publishes a
fixed 30-fps, 1280x720 contract. The implementation in
`cbridge/media-backend.c` uses this topology:

```text
libcamerasrc camera-name="\_SB_.PCI0.I2C3.CAMR" ae-enable=true
  ! video/x-raw,width=1280,height=720,framerate=30/1
  ! videoconvert ! videoscale
  ! video/x-raw,format=YUY2,width=1280,height=720,framerate=30/1
  ! v4l2sink device=/dev/video61 sync=false
```

When the consumer selected MJPEG/JPEG, the C implementation instead converts
to I420, runs `jpegenc quality=85`, parses the JPEG stream, and writes the
encoded buffers through the loopback's compressed path. The public endpoint is
queried with `VIDIOC_G_FMT` before the producer starts; an unopened
`exclusive_caps=1` loopback is queried as `VIDEO_OUTPUT` until a producer has
made `VIDEO_CAPTURE` available.

The user service keeps both loopbacks alive with a `videotestsrc` filler. It
uses inotify open/close events as a trigger and reconciles truth from
`/proc/<pid>/fd`; the controller applies a 450 ms open debounce and a 1.5 s
close grace period. If both RGB endpoints are opened, the rear request wins,
but the physical backend is still single-owner.

## What is and is not guaranteed

The repository contains the IPU4P receiver/BE-SOC side and the bridge. It does
not define the OV8865 register table, exposure/gain controls, exact sensor
I2C address, or external driver's CSI timing implementation. Those values must
be read from the loaded OV8865 driver and the live graph. A complete back-
camera qualification requires changing non-black frames from the intended
application; graph enumeration, `cam -l`, or an idle loopback is not enough.

Related source and evidence:

- [`test-capture.sh`](../test-capture.sh)
- [`docs/rear-ov8865-csi2-reliability-20260915.md`](../reports/rear-ov8865-csi2-reliability-20260915.md)
- [`docs/surface-cameras.md`](surface-cameras.md)
- [`linux-6.19.8/drivers/media/pci/intel/ipu-isys-csi2-be-soc.c`](../linux-6.19.8/drivers/media/pci/intel/ipu-isys-csi2-be-soc.c)
