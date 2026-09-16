#!/bin/sh
# Hardware-independent checks for the opt-in C media backend prototype.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CROOT="$ROOT/cbridge"

test -f "$CROOT/Makefile"
test -f "$CROOT/media-backend.c"
test -f "$CROOT/media-backend.h"
test -f "$CROOT/prototype.c"
grep -Fq 'VIDIOC_G_FMT' "$CROOT/media-backend.c"
grep -Fq 'gst_element_factory_make' "$CROOT/media-backend.c"
grep -Fq 'gst_bus_timed_pop_filtered' "$CROOT/media-backend.c"
grep -Fq 'camera-name' "$CROOT/media-backend.c"
grep -Fq 'source_caps_filter' "$CROOT/media-backend.c"
grep -Fq 'caps = video_caps("video/x-raw", NULL' "$CROOT/media-backend.c"
grep -Fq 'source, source_caps_filter, convert, scale' "$CROOT/media-backend.c"
grep -Fq 'jpegenc' "$CROOT/media-backend.c"
grep -Fq 'jpegparse' "$CROOT/media-backend.c"
grep -Fq 'filesink' "$CROOT/media-backend.c"
grep -Fq 'v4l2sink' "$CROOT/media-backend.c"
grep -Fq 'START_TIMEOUT_US' "$CROOT/media-backend.c"
grep -Fq 'STOP_TIMEOUT_US' "$CROOT/media-backend.c"
! grep -Fq 'gst-launch-1.0' "$CROOT"/*.c

make -C "$CROOT" clean all
set +e
"$CROOT/sp7-camera-backend-prototype" --camera front --device /dev/not-a-camera \
	--seconds 1 >"${TMPDIR:-/tmp}/sp7-cbridge-invalid.log" 2>&1
status=$?
set -e
[ "$status" -eq 1 ]
grep -q 'format query failed' "${TMPDIR:-/tmp}/sp7-cbridge-invalid.log"
printf '%s\n' 'task18-cbridge-static: PASS'
