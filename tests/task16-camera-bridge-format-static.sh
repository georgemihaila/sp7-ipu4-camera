#!/bin/sh
# Behavioral checks for C bridge loopback format negotiation.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CROOT=$ROOT/cbridge

# The controller test exercises both negotiated formats through the C backend
# boundary: rear uses MJPEG and front uses YUYV.
make -C "$CROOT" test-controller
printf '%s\n' 'task16-camera-bridge-format-static: PASS'
