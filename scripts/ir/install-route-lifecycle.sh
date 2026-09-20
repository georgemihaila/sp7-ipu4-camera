#!/usr/bin/env bash
# Install the graph-discovered source-6 route lifecycle without enabling it.
set -Eeuo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
ROUTE_BINARY=${ROUTE_BINARY:-$ROOT/cbridge/sp7-camera-ir-route}
PREFIX=${PREFIX:-/usr/local}
UNIT_DIR=${UNIT_DIR:-/etc/systemd/system}

fail() {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

[[ $(id -u) -eq 0 ]] || fail 'run as root'
[[ -x "$ROUTE_BINARY" ]] || fail "route binary is missing or not executable: $ROUTE_BINARY"

TARGET="$PREFIX/libexec/sp7-camera-ir-route"
UNIT="$UNIT_DIR/sp7-camera-howdy-route.service"
if [[ -e "$TARGET" ]] && ! cmp -s "$ROUTE_BINARY" "$TARGET"; then
	fail "refusing to overwrite a different route binary: $TARGET"
fi
if [[ -e "$UNIT" ]] && ! cmp -s "$ROOT/systemd/system/sp7-camera-howdy-route.service" "$UNIT"; then
	fail "refusing to overwrite a different route unit: $UNIT"
fi

install -D -o root -g root -m 0755 "$ROUTE_BINARY" "$TARGET"
install -D -o root -g root -m 0644 \
	"$ROOT/systemd/system/sp7-camera-howdy-route.service" "$UNIT"
systemctl daemon-reload

printf '%s\n' 'Installed the source-6 route lifecycle; it remains disabled and stopped.'
printf '%s\n' 'Enable only after the receiver, illumination, and no-PAM Howdy gates pass:'
printf '%s\n' '  sudo systemctl enable --now sp7-camera-howdy-route.service'
