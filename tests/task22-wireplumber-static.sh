#!/bin/sh
# Hardware-independent checks for narrow WirePlumber ownership handling.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RULE="$ROOT/wireplumber/50-sp7-ipu4.conf"
CROOT="$ROOT/cbridge"

grep -Fq 'device.name = "~v4l2_device.*intel-ipu60.*"' "$RULE"
grep -Fq 'device.disabled = true' "$RULE"
grep -Fq 'monitor.libcamera = disabled' "$RULE"
! grep -Eiq 'audio|microphone|alsa' "$RULE"
grep -Fq 'WIREPLUMBER_ALREADY_INACTIVE' "$CROOT/controller.h"
grep -Fq 'WirePlumber remains active' "$CROOT/surface-camera-bridge.c"
! grep -Fq 'run_systemctl' "$CROOT/surface-camera-bridge.c"

make -C "$CROOT" clean all
printf '%s\n' 'task22-wireplumber-static: PASS'
