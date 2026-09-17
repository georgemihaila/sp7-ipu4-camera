#!/bin/sh
# Static contract checks for the Phase 0 native inventory.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
INVENTORY=$ROOT/tests/native-inventory.sh

test -f "$INVENTORY"
sh -n "$INVENTORY"
grep -Fq '/sys/class/video4linux/' "$INVENTORY"
grep -Fq '/dev/media*' "$INVENTORY"
grep -Fq 'media-ctl' "$INVENTORY"
grep -Fq 'v4l2-ctl' "$INVENTORY"
grep -Fq 'cam -l' "$INVENTORY"
grep -Fq 'pw-cli ls Node' "$INVENTORY"
grep -Fq 'modinfo' "$INVENTORY"
grep -Fq 'firmware' "$INVENTORY"
if grep -Eq '/dev/video[0-9]|/dev/media[0-9]|/dev/v4l-subdev[0-9]' "$INVENTORY"; then
	echo "hard-coded media/video/sub-device minor found" >&2
	exit 1
fi

echo "PASS [native-inventory-static] dynamic node discovery and provenance hooks are present"
