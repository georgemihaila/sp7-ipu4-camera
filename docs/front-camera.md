# Front camera: OV5693 control and capture path

The front camera is the in-tree OV5693 path. This page follows the data and
control flow through the sensor registers, IPU4P source-7 receiver quirk,
CSI2 BE/SOC capture node, libcamera, and the optional named V4L2 bridge.

## Hardware and media contract

| Layer | Value |
|---|---|
| Sensor | OV5693, driver `linux-6.19.8/drivers/media/i2c/ov5693.c` |
| ACPI identity used by the bridge | `\_SB_.PCI0.I2C2.CAMF` / HID `INT33BE` |
| IPU4P firmware source | source `7` |
| CSI-2 receiver | port/index `2` |
| Data lanes | `2` |
| External clock | `19.2 MHz` |
| CSI link frequency | `419.2 MHz` |
| Pixel rate control | `167.68 MHz` |
| Sensor bus code | `MEDIA_BUS_FMT_SBGGR10_1X10` |
| Native array | `2624x1956` |
| Active default crop | `2592x1944`, starting at `(16,6)` |
| Named loopback | **Surface Camera (front)**, normally `/dev/video60` |

The data path is:

```text
OV5693 I2C sensor
    -> Intel IPU4 CSI-2 2, sink pad 0       (2-lane CSI-2, RAW10)
    -> Intel IPU4 CSI2 BE SOC, sink pad 0   (one input selected)
    -> Intel IPU4 CSI2 BE SOC, source pad 8
    -> Intel IPU4 BE SOC capture 0          (/dev/video*, unpacked BG10)
    -> libcamera Simple + SoftISP           (processed frames)
    -> sp7-camera-bridge                    (optional compatibility path)
    -> /dev/video60                         (YUYV or MJPEG, 1280x720)
```

The IPU4P source-7 receiver is the only path with the Surface Pro 7 timing
quirk. It is deliberately scoped by receiver index, firmware source, and lane
count so that it cannot alter the rear or OV7251 source-6 paths.

## OV5693 register control

The driver uses the kernel CCI helpers over the sensor's I2C client. Register
addresses are 16-bit and the sensor values are big-endian on the wire. The
relevant stream and image controls are:

| Operation | Register operation | Encoding |
|---|---|---|
| Software reset | `0x0103 = 0x01` | one byte |
| Enter streaming | `0x0100 = 0x01` | one byte |
| Enter standby | `0x0100 = 0x00` | one byte |
| Exposure | `0x3500`, 24-bit | V4L2 value shifted left 4; mask `GENMASK(19,4)` |
| Analogue gain | `0x350a`, 16-bit | V4L2 gain shifted left 4; mask `GENMASK(10,4)` |
| Digital gain | `0x3400`, `0x3402`, `0x3404`, 16-bit each | same value written to red, green, and blue MWB gains |
| Frame length | `0x380e`, 16-bit | `VTS = active_height + VBLANK` |
| Horizontal flip | `0x3821` | updates both ISP and sensor horizontal-flip bits |
| Vertical flip | `0x3820` | updates both ISP and sensor vertical-flip bits |
| Test pattern | `0x5e00` | menu encoding from `ov5693_test_pattern_bits[]` |

For a 16-bit CCI write, the logical I2C payload is:

```text
register 0x350a, gain 0x0010:
    [ 0x35, 0x0a, 0x00, 0x10 ]

register 0x380e, VTS 0x07a0:
    [ 0x38, 0x0e, 0x07, 0xa0 ]
```

The I2C slave address is supplied by ACPI; the byte arrays above are the
register/value payload, not an address byte. The driver serializes controls
with its mutex and applies sensor controls only while runtime PM says the
device is powered. Changing active format, crop, or frame interval after
stream-on returns `-EBUSY`.

The active format is 10-bit BGGR. It is not converted to RGB by the sensor:
the BE-SOC capture table stores each 10-bit sample in a 16-bit `BG10` slot.
The three-frame raw size is therefore:

```text
2592 * 1944 * 2 = 10,077,696 bytes/frame
3 frames        = 30,233,088 bytes
```

## Source-7 receiver programming

The Surface Pro 7 quirk in `ipu4/ipu4-isys.c` matches:

```c
front_csi_index = 2;
front_source    = 7;
front_lanes     = 2;
front_phy_bb    = 10;
front_phy_afe   = 0x15;
```

Then `ipu4p-isys-csi2.c` writes the fixed receiver timing to the CSI2 block
relative to `csi2->base`:

```c
writel(0,    base + 0x30);
writel(1155, base + 0x34);       /* clock / first-data ticks */
for (unsigned int i = 0; i < 8; i++) {
    writel(0,    base + 0x38 + i * 8);
    writel(1269, base + 0x3c + i * 8); /* data-lane ticks */
}
```

This is receiver MMIO, not an OV5693 I2C register sequence. The driver first
derives the sensor link frequency and lane count from the media graph, then
uses the quirk only for the exact source-7/two-lane Surface Pro 7 case. All
other CSI inputs retain the generic timing calculation.

## Graph setup and raw capture

`test-capture.sh front` discovers the current entity names and applies the
following equivalent graph:

```sh
MEDIA_DEVICE=/dev/mediaX                 # discover at run time
SENSOR='ov5693 2-0036'                   # discover the entity name

media-ctl -d "$MEDIA_DEVICE" -r
media-ctl -d "$MEDIA_DEVICE" \
  -V "\"$SENSOR\":0 [fmt:SBGGR10_1X10/2592x1944]"
media-ctl -d "$MEDIA_DEVICE" \
  -V '"Intel IPU4 CSI-2 2":0 [fmt:SBGGR10_1X10/2592x1944]'
media-ctl -d "$MEDIA_DEVICE" \
  -V '"Intel IPU4 CSI-2 2":1 [fmt:SBGGR10_1X10/2592x1944]'

# Preserve MEDIA_LNK_FL_DYNAMIC: enabled dynamic links use [5], not [1].
media-ctl -d "$MEDIA_DEVICE" \
  -l "\"$SENSOR\":0 -> \"Intel IPU4 CSI-2 2\":0 [1]"
media-ctl -d "$MEDIA_DEVICE" \
  -l '"Intel IPU4 CSI-2 2":1 -> "Intel IPU4 CSI2 BE SOC":0 [5]'
media-ctl -d "$MEDIA_DEVICE" \
  -l '"Intel IPU4 CSI2 BE SOC":8 -> "Intel IPU4 BE SOC capture 0":0 [5]'
media-ctl -d "$MEDIA_DEVICE" \
  -V '"Intel IPU4 CSI2 BE SOC":0 [fmt:SBGGR10_1X10/2592x1944]'
media-ctl -d "$MEDIA_DEVICE" \
  -V '"Intel IPU4 CSI2 BE SOC":8 [fmt:SBGGR10_1X10/2592x1944]'

RAW_NODE=$(media-ctl -d "$MEDIA_DEVICE" -e 'Intel IPU4 BE SOC capture 0')
v4l2-ctl -d "$RAW_NODE" \
  --set-fmt-video=width=2592,height=1944,pixelformat=BG10 \
  --stream-mmap=4 --stream-count=3 --stream-to=front.raw
```

The direct per-CSI packet node is not the normal clean-raster output. Use the
CSI2 BE/SOC route above, which interleaves the line pairs and produces the
unpacked `BG10` V4L2 format expected by the Simple pipeline.

## libcamera and the named V4L2 endpoint

The intended processed path is the distribution's `intel-ipu6` Simple pipeline
with SoftISP. Repository validation is deliberately separate from GUI support:

```sh
cam -l
LIBCAMERA_VALIDATION_DIR="$PWD/reports/native-libcamera" \
  ./tests/libcamera-native-validation.sh --live
```

The fallback C bridge maps the front endpoint to the stable ACPI camera ID and
starts it only after a consumer opens `/dev/video60`:

```text
libcamerasrc camera-name="\_SB_.PCI0.I2C2.CAMF" ae-enable=true
  ! video/x-raw,width=2560,height=1600,framerate=30/1
  ! videoconvert ! videoscale
  ! video/x-raw,format=YUY2,width=1280,height=720,framerate=30/1
  ! v4l2sink device=/dev/video60 sync=false
```

The 2560x1600 source request is intentional: it is the stable 30-fps front
mode in the bridge implementation. The public loopback is always 1280x720.
If the consumer selected MJPEG/JPEG, the C worker uses I420, `jpegenc`
quality 85, `jpegparse`, and a compressed loopback write instead of the YUYV
`v4l2sink` branch.

The controller owns only one physical RGB capture at a time. It detects
endpoint consumers from inotify plus `/proc/<pid>/fd`, applies open debounce
and close grace periods, stops the active worker before switching, and starts
the other sensor after querying that loopback's current format.

## Qualification boundary

The front path has repository-owned sensor and receiver code and has produced
valid changing raw and processed captures on the qualified Surface Pro 7
kernel. That does not by itself qualify GNOME Snapshot, a browser, Zoom, or
another application. For an application claim, record moving preview and a
usable saved capture in that exact application.

Related source and evidence:

- [`linux-6.19.8/drivers/media/i2c/ov5693.c`](../linux-6.19.8/drivers/media/i2c/ov5693.c)
- [`test-capture.sh`](../test-capture.sh)
- [`docs/task8-camera-contract.md`](task8-camera-contract.md)
- [`docs/surface-cameras.md`](surface-cameras.md)
- [`linux-6.19.8/drivers/media/pci/intel/ipu4/ipu4p-isys-csi2.c`](../linux-6.19.8/drivers/media/pci/intel/ipu4/ipu4p-isys-csi2.c)
