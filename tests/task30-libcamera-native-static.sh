#!/bin/sh
# Static contract checks for native libcamera processed-capture validation.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VALIDATOR="$ROOT/tests/libcamera-native-validation.sh"

test -x "$VALIDATOR"
sh -n "$VALIDATOR"
grep -Fq 'cam -l' "$VALIDATOR"
grep -Fq -- '--camera=' "$VALIDATOR"
grep -Fq -- '--capture="$count"' "$VALIDATOR"
grep -Fq -- '--file="$directory/frame-#.ppm"' "$VALIDATOR"
grep -Fq 'width=640,height=480' "$VALIDATOR"
grep -Fq 'validate_ppm' "$VALIDATOR"
grep -Fq 'nonzero' "$VALIDATOR"
grep -Fq 'cksum' "$VALIDATOR"
grep -Fq 'validate_reopen_capture' "$VALIDATOR"
grep -Fq 'location=' "$VALIDATOR"

# The native test must not silently become a bridge/service/graph mutation.
! grep -Eq 'v4l2loopback|cbridge|media-ctl|modprobe|systemctl|wireplumber|pipewire' "$VALIDATOR"
if grep -Eq '/dev/video[0-9]|/dev/media[0-9]|/dev/v4l-subdev[0-9]' "$VALIDATOR"; then
	echo "hard-coded media/video/sub-device minor found" >&2
	exit 1
fi

echo 'PASS [libcamera-native-static] dynamic cam discovery, processed payload/image checks, and reopen contract are present'
