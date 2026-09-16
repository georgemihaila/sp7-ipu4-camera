#!/bin/sh
# Hardware-independent checks for the explicit C controller state machine.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CROOT="$ROOT/cbridge"

test -f "$CROOT/controller.h"
test -f "$CROOT/controller.c"
test -f "$CROOT/controller-test.c"
test -f "$CROOT/surface-camera-bridge.c"
grep -Fq 'CONTROLLER_IDLE' "$CROOT/controller.h"
grep -Fq 'CONTROLLER_STARTING' "$CROOT/controller.h"
grep -Fq 'CONTROLLER_STREAMING' "$CROOT/controller.h"
grep -Fq 'CONTROLLER_STOPPING' "$CROOT/controller.h"
grep -Fq 'CONTROLLER_RETRY_WAIT' "$CROOT/controller.h"
grep -Fq 'capture_retry_after' "$CROOT/controller.c"
grep -Fq 'filler_retry_after' "$CROOT/controller.c"
grep -Fq 'OPEN_DEBOUNCE_MS' "$CROOT/controller.c"
grep -Fq 'CLOSE_GRACE_MS' "$CROOT/controller.c"
grep -Fq 'consumer_mask' "$CROOT/surface-camera-bridge.c"
grep -Fq 'readlink' "$CROOT/surface-camera-bridge.c"
grep -Fq 'VIDIOC_G_FMT' "$CROOT/media-backend.c"
grep -Fq 'return WIREPLUMBER_ALREADY_INACTIVE;' "$CROOT/surface-camera-bridge.c"

make -C "$CROOT" clean test-controller
printf '%s\n' 'task19-camera-controller-static: PASS'
