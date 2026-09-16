#!/bin/sh
# Static checks for the version-aware libcamera integration helper.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
HELPER="$ROOT/libcamera/rebuild-libcamera.sh"
DOC="$ROOT/libcamera/README.md"

test -x "$HELPER"
test -f "$DOC"
sh -n "$HELPER"

# The obsolete 0.5.2/Patch16 injection must not return.
! grep -Eq '0\.5\.2|Patch16|Patch17|2001-pipeline-simple-Intel-IPU4-support\.patch' "$HELPER" "$DOC"

grep -q 'intel-ipu6' "$HELPER" "$DOC"
grep -q 'V4L2_PIX_FMT_SBGGR10' "$HELPER" "$DOC"
grep -q 'V4L2_PIX_FMT_SBGGR10P' "$HELPER" "$DOC"
grep -q -- '--check SOURCE_TREE' "$HELPER"
grep -q 'layout outside the known form fails' "$DOC"
grep -q 'three-frame processed captures succeeded' "$DOC"
grep -q 'near-black at the sensor' "$DOC"
grep -q 'GNOME Snapshot enters' "$DOC"

echo 'task12-libcamera-static: PASS'
