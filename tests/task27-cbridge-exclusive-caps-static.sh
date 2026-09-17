#!/bin/sh
# Static contract checks for exclusive-caps loopback format discovery.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CROOT="$ROOT/cbridge"
SOURCE="$CROOT/media-backend.c"
MODULE_OPTIONS="$ROOT/modprobe.d/98-v4l2loopback.conf"
DOC="$ROOT/docs/surface-cameras.md"

test -f "$SOURCE"
grep -Fq 'v4l2_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;' "$SOURCE"
grep -Fq 'capture_errno = errno;' "$SOURCE"
grep -Fq 'v4l2_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;' "$SOURCE"
grep -Fq 'VIDEO_OUTPUT fallback' "$SOURCE"
grep -Fq 'exclusive_caps=1,1,1' "$MODULE_OPTIONS"
grep -Fq 'VIDEO_OUTPUT` format as a fallback' "$DOC"

# Capture remains the preferred query whenever the producer has already
# opened the endpoint; OUTPUT is only the second ioctl attempt.
awk '
  /v4l2_format\.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;/ { capture = NR }
  /v4l2_format\.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;/ { output = NR }
  END { exit !(capture > 0 && output > capture) }
' "$SOURCE"

printf '%s\n' 'task27-cbridge-exclusive-caps-static: PASS'
