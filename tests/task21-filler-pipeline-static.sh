#!/bin/sh
# Hardware-independent checks for the cached filler payload path.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CROOT="$ROOT/cbridge"

# Camera capture retains the conversion and scaling stages, while fillers emit
# fresh timestamped headers over cached black payloads.
grep -Fq 'source, source_caps_filter, convert, scale' "$CROOT/media-backend.c"
grep -Fq 'gst_buffer_copy_region' "$CROOT/media-backend.c"
grep -Fq 'make_black_yuyv_buffer' "$CROOT/media-backend.c"
grep -Fq 'make_black_mjpeg_buffer' "$CROOT/media-backend.c"
grep -Fq 'source, caps_filter, jpegparse' "$CROOT/media-backend.c"
grep -Fq 'source, caps_filter, sink' "$CROOT/media-backend.c"

make -C "$CROOT" clean all
printf '%s\n' 'task21-filler-pipeline-static: PASS'
