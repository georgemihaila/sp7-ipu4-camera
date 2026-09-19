#!/usr/bin/env bash
# Build/install a protected, pinned Howdy runtime without enabling PAM.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
REV=$(tr -d '[:space:]' < "$ROOT/scripts/ir/howdy-revision.txt")
ARCHIVE_SHA256=e0b58928c6d1362ea8c630f056261bccb446fe84b507f7b7cceb7c5a55706061
PREFIX=${HOWDY_PREFIX:-/usr/local}
RUNTIME_ROOT=${HOWDY_RUNTIME_ROOT:-$PREFIX/libexec/sp7-camera-howdy}
PYTHON_ROOT=$RUNTIME_ROOT/python
PYTHON=$PYTHON_ROOT/bin/python
PY_SOURCES=$PREFIX/lib/howdy
CONFIG_DIR=${HOWDY_CONFIG_DIR:-/etc/howdy}
MODEL_DIR=${HOWDY_MODEL_DIR:-$PREFIX/share/howdy/dlib-data}
USER_MODELS_DIR=${HOWDY_USER_MODELS_DIR:-/var/lib/howdy/models}
PAM_DIR=${HOWDY_PAM_DIR:-/usr/lib64/security}
LOG_DIR=${HOWDY_LOG_DIR:-/var/log/howdy}
MODEL_SOURCE_DIR=${HOWDY_MODEL_SOURCE_DIR:-}
AUTH_HELPER=${AUTH_HELPER_SOURCE:-$ROOT/cbridge/sp7-camera-auth-capture}
WORK=$(mktemp -d /var/tmp/sp7-howdy-build.XXXXXX)

cleanup() {
	status=$?
	trap - EXIT
	rm -rf -- "$WORK"
	exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

fail() {
	printf 'error: %s\n' "$*" >&2
	exit 2
}

[ "$(id -u)" -eq 0 ] || fail 'run as root; this installs the protected runtime and pam_howdy.so'
case $RUNTIME_ROOT in
	/*) ;;
	*) fail 'HOWDY_RUNTIME_ROOT must be absolute' ;;
esac
case $RUNTIME_ROOT in
	"$ROOT"/*) fail 'refusing to place the PAM runtime inside the repository' ;;
esac
[ -n "$MODEL_SOURCE_DIR" ] || fail 'set HOWDY_MODEL_SOURCE_DIR to the verified dlib model directory'
[ -d "$MODEL_SOURCE_DIR" ] || fail "model directory is missing: $MODEL_SOURCE_DIR"
[ -x "$AUTH_HELPER" ] || fail "build the protected capture helper first: $AUTH_HELPER"
command -v curl >/dev/null 2>&1 || fail 'curl is required'
command -v meson >/dev/null 2>&1 || fail 'meson is required'
command -v ninja >/dev/null 2>&1 || fail 'ninja is required'
command -v patch >/dev/null 2>&1 || fail 'patch is required'
pkg-config --exists pam libevdev INIReader || fail 'install build dependencies: pam-devel libevdev-devel inih-devel'

if [ -e "$CONFIG_DIR/config.ini" ]; then
	fail "$CONFIG_DIR/config.ini already exists; back it up and review it before replacing it"
fi

install -d -o root -g root -m 0755 "$RUNTIME_ROOT" "$CONFIG_DIR" "$MODEL_DIR" \
	"$USER_MODELS_DIR" "$LOG_DIR" "$PAM_DIR" "$PY_SOURCES"
install -o root -g root -m 0755 "$AUTH_HELPER" "$PREFIX/libexec/sp7-camera-auth-capture"
install -o root -g root -m 0755 "$ROOT/scripts/ir/howdy-sp7-ir-diagnostic.py" \
	"$PREFIX/sbin/howdy-sp7-ir-diagnostic"

if [ ! -x "$PYTHON" ]; then
	python3 -m venv "$PYTHON_ROOT"
fi
chown -R root:root "$PYTHON_ROOT"
"$PYTHON" -m pip install --no-cache-dir -r "$ROOT/scripts/ir/requirements-py314.txt"
"$PYTHON" - <<'PY'
import cv2
import dlib
import numpy
print("protected howdy python: PASS")
print("numpy=" + numpy.__version__)
print("opencv=" + cv2.__version__)
print("dlib=" + dlib.__version__)
PY

ARCHIVE="$WORK/howdy-$REV.tar.gz"
curl -fsSL "https://github.com/boltgolt/howdy/archive/$REV.tar.gz" -o "$ARCHIVE"
printf '%s  %s\n' "$ARCHIVE_SHA256" "$ARCHIVE" | sha256sum -c -
tar -xzf "$ARCHIVE" -C "$WORK"
HOWDY_SOURCE=$(find "$WORK" -mindepth 1 -maxdepth 1 -type d -name "howdy-*" -print -quit)
[ -n "$HOWDY_SOURCE" ] || fail 'Howdy source archive did not contain a source directory'
patch -d "$HOWDY_SOURCE" -p1 < "$ROOT/scripts/ir/howdy-sp7_ir.patch"
install -o root -g root -m 0644 "$ROOT/scripts/ir/sp7_ir_protocol.py" \
	"$HOWDY_SOURCE/howdy/src/recorders/sp7_ir_protocol.py"
install -o root -g root -m 0644 "$ROOT/scripts/ir/sp7_ir_reader.py" \
	"$HOWDY_SOURCE/howdy/src/recorders/sp7_ir_reader.py"

meson setup "$WORK/build" "$HOWDY_SOURCE" \
	-Dprefix="$PREFIX" \
	-Dpython_path="$PYTHON" \
	-Dpy_sources_dir="$PY_SOURCES" \
	-Dconfig_dir="$CONFIG_DIR" \
	-Ddlib_data_dir="$MODEL_DIR" \
	-Duser_models_dir="$USER_MODELS_DIR" \
	-Dlog_path="$LOG_DIR" \
	-Dpam_dir="$PAM_DIR" \
	-Dinstall_pam_config=false \
	-Dwith_polkit=false \
	-Dinstall_in_site_packages=false
meson compile -C "$WORK/build"
meson install -C "$WORK/build"

while read -r hash model; do
	[ -n "$hash" ] || continue
	printf '%s  %s\n' "$hash" "$MODEL_SOURCE_DIR/$model" | sha256sum -c -
	install -o root -g root -m 0644 "$MODEL_SOURCE_DIR/$model" "$MODEL_DIR/$model"
done < "$ROOT/scripts/ir/howdy-models.sha256"
install -o root -g root -m 0644 "$ROOT/scripts/ir/howdy-sp7-ir-config.ini" "$CONFIG_DIR/config.ini"
install -o root -g root -m 0644 "$ROOT/scripts/ir/howdy-models.sha256" "$MODEL_DIR/SHA256SUMS"
install -o root -g root -m 0644 "$ROOT/scripts/ir/howdy-revision.txt" "$PREFIX/share/howdy/HOWDY_REVISION"
install -o root -g root -m 0644 "$ROOT/scripts/ir/howdy-sp7-ir-config.ini" "$CONFIG_DIR/config.ini.example"

find "$RUNTIME_ROOT" -type d -exec chmod go-w {} +
find "$RUNTIME_ROOT" -type f -exec chmod go-w {} +
chmod 0700 "$USER_MODELS_DIR" "$LOG_DIR"
chmod 0755 "$PREFIX/libexec/sp7-camera-auth-capture"
printf 'protected Howdy runtime built; PAM configuration was not installed\n'
printf 'pam_howdy.so: %s/pam_howdy.so\n' "$PAM_DIR"
