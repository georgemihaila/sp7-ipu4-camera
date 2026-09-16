# Task 8 camera capability contract

This repository contains a partial kernel source snapshot, not a complete
kernel or libcamera checkout. The capability statements below are deliberately
limited to code and captured topology present here.

## Implemented in this repository

The in-tree `ov5693.c` source advertises one sensor source pad and one CSI-2
format: `MEDIA_BUS_FMT_SBGGR10_1X10` (10-bit BGGR Bayer). Its active crop is
2592x1944 inside a 2624x1956 native array. It validates exactly two CSI-2
data lanes and requires the 419.2 MHz link frequency; the read-only
`V4L2_CID_LINK_FREQ` and `V4L2_CID_PIXEL_RATE` controls report those values.

The supported controls are exposure, analogue gain, digital gain (implemented
as equal red/green/blue MWB gains), horizontal/vertical flip, read-only
horizontal blanking, vertical blanking, and the four sensor test-pattern menu
entries. No focus, flash, or automatic white-balance control is advertised.
Frame interval enumeration and setting use the sensor's VTS timing and are
serialized with the same mutex as controls and streaming. Active format, crop,
and frame-interval changes return `-EBUSY` after stream-on; control writes are
serialized and applied during stream setup or through the sensor's timing/gain
registers while the device is powered.

The IPU topology exposes raw capture nodes and IPU metadata nodes. The sensor
does not generate an embedded metadata stream in this source, so those nodes
must not be interpreted as sensor 3A metadata without platform firmware
evidence.

## External / not implemented here

- OV8865 is referenced by the Surface Pro 7 topology and test scripts, but no
  OV8865 driver source is present in this repository. Its rear-camera format,
  lane count, link frequency, controls, and metadata contract remain external
  to this change and must be verified against the installed module/firmware.
- The Surface Pro 7 rear lens uses a DW9719 voice-coil actuator. This tree
  bundles its V4L2 lens driver with the I2C ID table needed for the ACPI-created
  device and exposes absolute lens-position control. This is actuator support;
  an automatic-focus algorithm still has to be provided by a userspace camera
  stack and is not included here.
- No libcamera pipeline-handler or IPA source is present. The `libcamera/`
  directory contains only a downstream patch and a rebuild helper. Therefore
  this repository does not claim libcamera discovery, processed-frame delivery,
  3A, calibration tuning, or metadata propagation is complete.

## Integration checklist for the external libcamera tree

1. Add a real IPU4 simple/pipeline handler entry for the actual media-driver
   name and verify both camera graph identities at runtime.
2. Map each sensor's enumerated mbus code, dimensions, two-lane/link-frequency
   configuration, orientation, and frame interval; reject any mode not
   returned by V4L2.
3. Add sensor-specific calibration/tuning data and an IPA only when the
   firmware/algorithm ABI and metadata source are known. Pass exposure, gain,
   frame interval, orientation, and timestamps through the documented control
   path; do not synthesize sensor metadata from raw IPU meta nodes.
4. Exercise `cam --list`, graph configuration, processed capture, metadata
   validation, and front/rear switching against the installed libcamera build.
