#!/bin/sh
# Static checks for installer dependency separation.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
INSTALL="$ROOT/install.sh"
SETUP="$ROOT/scripts/setup-camera-bridge.sh"
MODULE_INSTALL="$ROOT/scripts/install-modules.sh"
DOC="$ROOT/docs/surface-cameras.md"

sh -n "$INSTALL" "$SETUP" "$MODULE_INSTALL"
grep -q 'INSTALL_MODE=.*full' "$INSTALL"
grep -q -- '--driver-only' "$INSTALL" "$DOC"
grep -q 'DRIVER_PACKAGES=' "$INSTALL"
grep -q 'BUILD_PACKAGES=' "$INSTALL"
grep -q 'BRIDGE_PACKAGES=' "$INSTALL"
grep -q 'if \[ "$INSTALL_MODE" = full \]' "$INSTALL"
grep -q 'gst-inspect-1.0' "$INSTALL" "$SETUP"
grep -q 'msitools' "$INSTALL"
! sed -n '1,100p' "$INSTALL" | grep -q 'msitools'
! grep -Eq 'dnf|gcc|gstreamer|libcamera' "$MODULE_INSTALL"
grep -q 'prebuilt release archive' "$DOC"
printf '%s\n' 'task17-installer-static: PASS'
