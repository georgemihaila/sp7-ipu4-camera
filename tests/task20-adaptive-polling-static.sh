#!/bin/sh
# Hardware-independent checks for the first measured bridge optimization.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CROOT="$ROOT/cbridge"

grep -Fq '#define ACTIVE_CONSUMER_SCAN_MS 200U' \
	"$CROOT/surface-camera-bridge.c"
grep -Fq '#define IDLE_CONSUMER_SCAN_MS 1000U' \
	"$CROOT/surface-camera-bridge.c"
grep -Fq 'context->consumer_scan_valid' "$CROOT/surface-camera-bridge.c"
grep -Fq 'context->consumer_scans++' "$CROOT/surface-camera-bridge.c"
grep -Fq 'cached_consumer_mask == 0U' "$CROOT/surface-camera-bridge.c"

make -C "$CROOT" clean all
printf '%s\n' 'task20-adaptive-polling-static: PASS'
