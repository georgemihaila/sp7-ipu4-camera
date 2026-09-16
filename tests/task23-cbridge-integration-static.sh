#!/bin/sh
# Static checks for installing the C bridge as the camera service.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
INSTALL="$ROOT/install.sh"
SETUP="$ROOT/scripts/setup-camera-bridge.sh"
PACKAGE="$ROOT/scripts/package-release.sh"
UNIT="$ROOT/systemd/user/sp7-camera-bridge.service"

sh -n "$INSTALL" "$SETUP" "$PACKAGE"
grep -Fq 'BRIDGE_BUILD_PACKAGES=' "$INSTALL"
grep -Fq 'make -C "$ROOT/cbridge" all' "$INSTALL"
grep -Fq 'BRIDGE_BINARY=' "$SETUP"
grep -Fq 'install -D -m 0755 "$BRIDGE_BINARY"' "$SETUP"
grep -Fq 'C_BRIDGE_BINARY=' "$PACKAGE"
grep -Fq 'cbridge/sp7-camera-bridge' "$PACKAGE"
grep -Fq 'ExecStart=/usr/local/libexec/sp7-camera-bridge' "$UNIT"
grep -Fq 'worker_process' "$ROOT/cbridge/surface-camera-bridge.c"
grep -Fq 'waitpid' "$ROOT/cbridge/surface-camera-bridge.c"
grep -Fq 'SIGKILL' "$ROOT/cbridge/surface-camera-bridge.c"
grep -Fq 'read_systemd_cgroup' "$ROOT/cbridge/surface-camera-bridge.c"
grep -Fq 'systemd_cgroup' "$ROOT/cbridge/surface-camera-bridge.c"

make -C "$ROOT/cbridge" clean all
printf '%s\n' 'task23-cbridge-integration-static: PASS'
