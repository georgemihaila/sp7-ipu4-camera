#!/bin/sh
# Static contract checks for exclusive-caps loopback startup and format discovery.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CROOT="$ROOT/cbridge"
SOURCE="$CROOT/media-backend.c"
MODULE_OPTIONS="$ROOT/modprobe.d/98-v4l2loopback.conf"
DOC="$ROOT/docs/surface-cameras.md"
UNIT="$ROOT/systemd/user/sp7-camera-bridge.service"

test -f "$SOURCE"
grep -Fq 'v4l2_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;' "$SOURCE"
grep -Fq 'capture_errno = errno;' "$SOURCE"
grep -Fq 'v4l2_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;' "$SOURCE"
grep -Fq 'VIDEO_OUTPUT fallback' "$SOURCE"
grep -Eq 'exclusive_caps=1,1,1[[:space:]]*$' "$MODULE_OPTIONS"
if grep -Fq 'exclusive_caps=1,0,0' "$MODULE_OPTIONS"; then
	echo 'task27: named Surface endpoints must remain exclusive by default' >&2
	exit 1
fi
grep -Fq 'front/rear endpoints use `exclusive_caps=1`' "$DOC"
grep -Fq 'falls back to `VIDEO_OUTPUT` while an' "$DOC"
grep -Fq 'ExecStartPost` readiness barrier' "$DOC"

# The exclusive producer must be ready before WirePlumber's V4L2 scan.
grep -Fq 'Wants=wireplumber.service' "$UNIT"
grep -Fq 'After=pipewire.service' "$UNIT"
grep -Fq 'Before=wireplumber.service' "$UNIT"
grep -Fq 'ExecStartPost=/bin/sh -c' "$UNIT"
grep -Fq 'seq 1 30' "$UNIT"
grep -Fq '/dev/video60 --all' "$UNIT"
grep -Fq '/dev/video61 --all' "$UNIT"
grep -Fq 'grep -A6 "Device Caps"' "$UNIT"
grep -Fq 'Video Capture' "$UNIT"
grep -Fq 'exit 1' "$UNIT"

# Capture remains the preferred query whenever the producer has already
# opened the endpoint; OUTPUT is only the second ioctl attempt.
awk '
  /v4l2_format\.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;/ { capture = NR }
  /v4l2_format\.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;/ { output = NR }
  END { exit !(capture > 0 && output > capture) }
' "$SOURCE"

printf '%s\n' 'task27-cbridge-exclusive-caps-static: PASS'
