#!/bin/sh
# Hardware-independent checks for the opt-in OV7251 RAW10 producer.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CROOT="$ROOT/cbridge"
MODULE_OPTIONS="$ROOT/modprobe.d/98-v4l2loopback.conf"
SETUP="$ROOT/scripts/setup-camera-bridge.sh"
UNIT="$ROOT/systemd/user/sp7-camera-bridge.service"

grep -Fq 'MEDIA_IOC_ENUM_ENTITIES' "$CROOT/ir-v4l2.c"
grep -Fq 'MEDIA_IOC_ENUM_LINKS' "$CROOT/ir-v4l2.c"
grep -Fq 'name_is(entities[index].name, "Intel IPU4 CSI-2 1")' "$CROOT/ir-v4l2.c"
grep -Fq 'VIDIOC_SUBDEV_S_FMT' "$CROOT/ir-v4l2.c"
grep -Fq 'V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE' "$CROOT/ir-v4l2.c"
grep -Fq 'V4L2_BUF_FLAG_ERROR' "$CROOT/ir-v4l2.c"
grep -Fq 'data_offset' "$CROOT/ir-v4l2.c"
grep -Fq 'sequence_gaps' "$CROOT/ir-v4l2.c"
grep -Fq '0x80U' "$CROOT/ir-v4l2.c"
grep -Fq 'V4L2_BUF_TYPE_VIDEO_OUTPUT' "$CROOT/ir-camera-bridge.c"
grep -Fq 'video_nr=55,60,61,62' "$MODULE_OPTIONS"
grep -Fq 'Surface Camera (IR)' "$MODULE_OPTIONS"
grep -Fq 'IR_BINARY=' "$SETUP"
! grep -Fq 'sp7-camera-ir' "$UNIT"

make -C "$CROOT" clean all test-ir
printf '%s\n' 'task35-ir-backend-static: PASS'
