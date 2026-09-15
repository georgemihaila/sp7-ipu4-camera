#!/bin/sh
# Deterministic, hardware-independent checks for Task 8's advertised contract.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SENSOR="$ROOT/linux-6.19.8/drivers/media/i2c/ov5693.c"
DOC="$ROOT/docs/task8-camera-contract.md"

test -f "$SENSOR"
test -f "$DOC"

# The only in-tree sensor implementation is the two-lane OV5693 RAW10 path.
grep -q 'MEDIA_BUS_FMT_SBGGR10_1X10' "$SENSOR"
grep -q 'OV5693_LINK_FREQ_419_2MHZ' "$SENSOR"
grep -q 'num_data_lanes != 2' "$SENSOR"
grep -q 'set_frame_interval = ov5693_set_frame_interval' "$SENSOR"
grep -q 'enum_frame_interval = ov5693_enum_frame_interval' "$SENSOR"

# ACTIVE format/timing changes must not race an enabled stream.
grep -q 'if (ov5693->streaming)' "$SENSOR"
grep -q 'return -EBUSY' "$SENSOR"
grep -q 'ov5693->streaming = true' "$SENSOR"
grep -q 'ov5693->streaming = false' "$SENSOR"

# Do not accidentally claim an in-tree OV8865 or IPA implementation.
! find "$ROOT/linux-6.19.8/drivers/media" -type f \
    \( -iname '*ov8865*' -o -iname '*ipa*' \) -print | grep -q .
grep -q 'OV8865 driver source is present' "$DOC"
grep -q 'No libcamera pipeline-handler or IPA source' "$DOC"

echo 'task8-camera-static: PASS'
