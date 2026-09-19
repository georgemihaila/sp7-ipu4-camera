#!/usr/bin/env bash
# Install an isolated PAM service for wiring tests; never edits authselect files.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
TARGET=/etc/pam.d/sp7-camera-auth-test
BACKUP_DIR=/var/lib/sp7-camera-auth/rollback
[ "$(id -u)" -eq 0 ] || { printf '%s\n' 'error: run as root' >&2; exit 2; }
[ -e "$TARGET" ] && { printf 'error: refusing to overwrite %s\n' "$TARGET" >&2; exit 2; }
install -d -o root -g root -m 0700 "$BACKUP_DIR"
install -o root -g root -m 0644 "$ROOT/scripts/ir/pam-howdy-isolated" "$TARGET"
install -o root -g root -m 0600 /dev/null "$BACKUP_DIR/sp7-camera-auth-test.created"
restorecon "$TARGET" 2>/dev/null || true
printf 'installed isolated PAM service: %s\n' "$TARGET"
printf 'rollback: sudo rm -f %s\n' "$TARGET"
