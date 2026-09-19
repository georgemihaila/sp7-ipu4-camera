#!/bin/sh
# Hardware-independent checks for the opt-in OV7251 RAW10 producer.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CROOT="$ROOT/cbridge"

grep -Fq 'MEDIA_IOC_ENUM_ENTITIES' "$CROOT/ir-v4l2.c"
grep -Fq 'MEDIA_IOC_ENUM_LINKS' "$CROOT/ir-v4l2.c"
grep -Fq 'V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE' "$CROOT/ir-v4l2.c"
grep -Fq 'V4L2_BUF_FLAG_ERROR' "$CROOT/ir-v4l2.c"
grep -Fq 'data_offset' "$CROOT/ir-v4l2.c"
grep -Fq 'sequence_gaps' "$CROOT/ir-v4l2.c"
grep -Fq '0x80U' "$CROOT/ir-v4l2.c"
grep -Fq 'V4L2_BUF_TYPE_VIDEO_OUTPUT' "$CROOT/ir-camera-bridge.c"

make -C "$CROOT" clean all test-ir
printf '%s\n' 'task35-ir-backend-static: PASS'
