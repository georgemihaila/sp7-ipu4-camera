#!/bin/sh
# Hardware-independent checks for narrow WirePlumber ownership handling.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RULE="$ROOT/wireplumber/50-sp7-ipu4.conf"
CROOT="$ROOT/cbridge"
BRIDGE_DOC="$ROOT/docs/surface-cameras.md"
NATIVE_DOC="$ROOT/docs/native-pipewire.md"

grep -Fq 'node.name = "~v4l2_input.*intel-ipu60.*"' "$RULE"
grep -Fq 'node.disabled = true' "$RULE"
[ "$(grep -Fc 'api.v4l2.path = ' "$RULE")" -eq 0 ]
! grep -Fq 'node.pause-on-idle = true' "$RULE"
grep -Fq 'device.api = "libcamera"' "$RULE"
grep -Fq 'GNOME Snapshot' "$BRIDGE_DOC"
grep -Fq 'native PipeWire/libcamera camera' "$BRIDGE_DOC"
grep -Fq 'This also explains the default-install symptom' "$NATIVE_DOC"
grep -Fq 'physical libcamera monitor' "$NATIVE_DOC"
grep -Fq 'known black in the last bridge-free test' "$NATIVE_DOC"
! grep -Eiq 'audio|microphone|alsa' "$RULE"
grep -Fq 'WIREPLUMBER_ALREADY_INACTIVE' "$CROOT/controller.h"
grep -Fq 'WirePlumber remains active' "$CROOT/surface-camera-bridge.c"
! grep -Fq 'run_systemctl' "$CROOT/surface-camera-bridge.c"

make -C "$CROOT" clean all
printf '%s\n' 'task22-wireplumber-static: PASS'
