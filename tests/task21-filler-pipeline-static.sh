#!/bin/sh
# Hardware-independent checks for the filler-only conversion optimization.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CROOT="$ROOT/cbridge"

# Camera capture retains the conversion and scaling stages, while fillers link
# videotestsrc directly to their negotiated output caps.
grep -Fq 'source, source_caps_filter, convert, scale' "$CROOT/media-backend.c"
grep -Fq 'source, caps_filter, jpegenc' "$CROOT/media-backend.c"
grep -Fq 'source, caps_filter, sink' "$CROOT/media-backend.c"

make -C "$CROOT" clean all
printf '%s\n' 'task21-filler-pipeline-static: PASS'
