#!/bin/sh
# Hardware-independent checks for the bounded direct-capture auth demo.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SOURCE="$ROOT/cbridge/auth-capture-helper.c"
SCRIPT="$ROOT/scripts/ir/howdy-direct-demo.py"
OFFLINE="$ROOT/scripts/ir/validate-auth-demo-offline.py"
PROTOCOL_TEST="$ROOT/tests/auth-capture-protocol-test.py"
REQUIREMENTS="$ROOT/scripts/ir/requirements-py314.txt"
PREPARE="$ROOT/scripts/ir/prepare-auth-env.sh"
SETUP="$ROOT/scripts/setup-camera-bridge.sh"
REMOVE="$ROOT/scripts/remove-camera-bridge.sh"
PACKAGE="$ROOT/scripts/package-release.sh"

grep -Fq 'AUTH_CAPTURE_BUDGET_NS UINT64_C(3000000000)' "$SOURCE"
grep -Fq 'AUTH_LOCK_PATH "/run/lock/sp7-camera-auth-capture.lock"' "$SOURCE"
grep -Fq 'flock(fd, LOCK_EX | LOCK_NB)' "$SOURCE"
grep -Fq 'fork()' "$SOURCE"
grep -Fq 'must not inherit the caller' "$SOURCE"
grep -Fq 'waitpid(worker' "$SOURCE"
grep -Fq 'kill(worker, SIGTERM)' "$SOURCE"
grep -Fq 'ir_capture_set_discard_error_buffers(capture, true)' "$SOURCE"
grep -Fq 'frame_timestamp_is_fresh' "$SOURCE"
grep -Fq 'AUTH_EXIT_TIMEOUT 124' "$SOURCE"
grep -Fq '/usr/local/libexec/sp7-camera-auth-capture' "$ROOT/scripts/ir/sp7_ir_protocol.py"
grep -Fq '/sys/module/ov7251/parameters/experimental_strobe_output' "$ROOT/scripts/ir/sp7_ir_protocol.py"
grep -Fq 'require_illuminator()' "$ROOT/scripts/ir/sp7_ir_protocol.py"
grep -Fq 'SP7IRF01' "$ROOT/scripts/ir/sp7_ir_protocol.py"
grep -Fq 'stale or regressed frame' "$ROOT/scripts/ir/sp7_ir_protocol.py"
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
grep -Fq 'selectors.DefaultSelector' "$ROOT/scripts/ir/sp7_ir_protocol.py"
grep -Fq 'close(STDOUT_FILENO)' "$SOURCE"
grep -Fq 'd3ab99382f88f043d15f15c1450ab69433892a1c' "$ROOT/scripts/ir/howdy-revision.txt"
grep -Fq 'recording_plugin = sp7_ir' "$ROOT/scripts/ir/howdy-sp7-ir-config.ini"
grep -Fq 'install_pam_config=false' "$ROOT/scripts/ir/build-howdy-runtime.sh"
grep -Fq -- '-Dpython_path=' "$ROOT/scripts/ir/build-howdy-runtime.sh"
grep -Fq 'return False' "$ROOT/scripts/ir/sp7_ir_reader.py"
grep -Fq 'MAX_FRAMES = 12' "$ROOT/scripts/ir/sp7_ir_protocol.py"
grep -Fq '/usr/local/lib/howdy/howdy' "$ROOT/scripts/ir/howdy-sp7-ir-diagnostic.py"
grep -Fq 'howdy-sp7-ir-runtime-20260919.md' "$PACKAGE"
grep -Fq 'sp7-camera-howdy-preflight.service' "$PACKAGE"
grep -Fq 'experimental_strobe_output' "$ROOT/scripts/ir/howdy-preflight.sh"
grep -Fq 'readlink -f "$PYTHON"' "$ROOT/scripts/ir/howdy-preflight.sh"
! grep -Fq '/dev/video62' "$OFFLINE"
! grep -Fq 'sp7-camera-auth-capture' "$OFFLINE"
! grep -Fq '/dev/video62' "$SOURCE"
! grep -Fq 'SP7_IR_OUTPUT' "$SOURCE"
! grep -Fq 'getenv' "$SOURCE"
! grep -Fq 'modprobe' "$SOURCE"
! grep -Fq 'insmod' "$SOURCE"
! grep -Fq 'systemctl' "$SOURCE"

bash -n "$SETUP" "$REMOVE" "$PACKAGE" "$PREPARE" \
	"$ROOT/scripts/ir/build-howdy-runtime.sh" \
	"$ROOT/scripts/ir/howdy-preflight.sh" \
	"$ROOT/scripts/ir/prepare-howdy-prelogin.sh" \
	"$ROOT/scripts/ir/install-isolated-pam-test.sh" \
	"$ROOT/scripts/ir/prepare-gdm-pam.sh" \
	"$ROOT/scripts/ir/selinux-howdy-diagnostics.sh"
PY_CACHE=$(mktemp -d /var/tmp/sp7-auth-pycompile.XXXXXX)
trap 'rm -rf "$PY_CACHE"' EXIT
PYTHONPYCACHEPREFIX="$PY_CACHE" python3 -m py_compile "$SCRIPT" "$OFFLINE" \
	"$ROOT/scripts/ir/sp7_ir_protocol.py" "$ROOT/scripts/ir/sp7_ir_reader.py" \
	"$ROOT/scripts/ir/howdy-sp7-ir-diagnostic.py" "$PROTOCOL_TEST"
python3 "$PROTOCOL_TEST"
make -C "$ROOT/cbridge" clean all test-ir test-ir-metadata test-auth-supervisor
"$ROOT/cbridge/sp7-camera-auth-capture" --help >/dev/null 2>&1
printf '%s\n' 'task36-auth-capture-static: PASS'
