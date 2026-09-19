#!/usr/bin/env bash
# Install the idempotent pre-login readiness files; do not enable or start them.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
[ "$(id -u)" -eq 0 ] || {
	printf '%s\n' 'error: run as root' >&2
	exit 2
}

install -o root -g root -m 0755 "$ROOT/scripts/ir/howdy-preflight.sh" \
	/usr/local/libexec/howdy-preflight.sh
install -D -o root -g root -m 0644 \
	"$ROOT/systemd/system/sp7-camera-howdy-preflight.service" \
	/etc/systemd/system/sp7-camera-howdy-preflight.service
systemctl daemon-reload
printf '%s\n' 'Installed pre-login preflight files; service remains disabled and stopped.'
printf '%s\n' 'Enable only after the isolated PAM and live camera gates pass:'
printf '%s\n' '  sudo systemctl enable --now sp7-camera-howdy-preflight.service'
