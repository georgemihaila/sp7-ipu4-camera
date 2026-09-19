#!/bin/sh
# Hardware-independent checks for the bounded direct-capture auth demo.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SOURCE="$ROOT/cbridge/auth-capture-helper.c"
SCRIPT="$ROOT/scripts/ir/howdy-direct-demo.py"
OFFLINE="$ROOT/scripts/ir/validate-auth-demo-offline.py"
REQUIREMENTS="$ROOT/scripts/ir/requirements-py314.txt"
PREPARE="$ROOT/scripts/ir/prepare-auth-env.sh"
SETUP="$ROOT/scripts/setup-camera-bridge.sh"
REMOVE="$ROOT/scripts/remove-camera-bridge.sh"
PACKAGE="$ROOT/scripts/package-release.sh"

grep -Fq 'AUTH_CAPTURE_BUDGET_NS UINT64_C(3000000000)' "$SOURCE"
grep -Fq 'AUTH_LOCK_PATH "/run/lock/sp7-camera-auth-capture.lock"' "$SOURCE"
grep -Fq 'flock(fd, LOCK_EX | LOCK_NB)' "$SOURCE"
grep -Fq 'fork()' "$SOURCE"
grep -Fq 'waitpid(worker' "$SOURCE"
grep -Fq 'kill(worker, SIGTERM)' "$SOURCE"
grep -Fq 'ir_capture_set_discard_error_buffers(capture, true)' "$SOURCE"
grep -Fq 'frame_timestamp_is_fresh' "$SOURCE"
grep -Fq 'AUTH_EXIT_TIMEOUT 124' "$SOURCE"
grep -Fq '/usr/local/libexec/sp7-camera-auth-capture' "$SCRIPT"
grep -Fq 'SP7IRF01' "$SCRIPT"
grep -Fq 'stale or regressed frame' "$SCRIPT"
grep -Fq 'AUTH_BINARY=' "$SETUP"
grep -Fq 'sp7-camera-auth-capture' "$REMOVE"
grep -Fq 'C_AUTH_BINARY=' "$PACKAGE"
grep -Fq 'howdy-direct-demo.py' "$PACKAGE"
grep -Fq 'prepare-auth-env.sh' "$PACKAGE"
grep -Fq 'validate-auth-demo-offline.py' "$PACKAGE"
grep -Fq 'ir-auth-pipeline-validation-20260919.md' "$PACKAGE"
grep -Fq 'opencv-python-headless==4.13.0.92' "$REQUIREMENTS"
grep -Fq 'dlib==20.0.1' "$REQUIREMENTS"
grep -Fq 'capture_frames()' "$OFFLINE"
grep -Fq 'dependency/pipeline validation only' "$OFFLINE"
! grep -Fq '/dev/video62' "$OFFLINE"
! grep -Fq 'sp7-camera-auth-capture' "$OFFLINE"
! grep -Fq '/dev/video62' "$SOURCE"
! grep -Fq 'SP7_IR_OUTPUT' "$SOURCE"
! grep -Fq 'getenv' "$SOURCE"
! grep -Fq 'modprobe' "$SOURCE"
! grep -Fq 'insmod' "$SOURCE"
! grep -Fq 'systemctl' "$SOURCE"

bash -n "$SETUP" "$REMOVE" "$PACKAGE" "$PREPARE"
PY_CACHE=$(mktemp -d /var/tmp/sp7-auth-pycompile.XXXXXX)
trap 'rm -rf "$PY_CACHE"' EXIT
PYTHONPYCACHEPREFIX="$PY_CACHE" python3 -m py_compile "$SCRIPT" "$OFFLINE"
make -C "$ROOT/cbridge" clean all test-ir test-ir-metadata
"$ROOT/cbridge/sp7-camera-auth-capture" --help >/dev/null 2>&1
printf '%s\n' 'task36-auth-capture-static: PASS'
