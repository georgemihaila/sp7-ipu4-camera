#!/usr/bin/env bash
# Show/apply/rollback only the GNOME gdm-password PAM insertion.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
TARGET=/etc/pam.d/gdm-password
ROLLBACK_ROOT=/var/lib/sp7-camera-auth/rollback
HOWDY_LINE='auth        sufficient    pam_howdy.so'

usage() {
	cat <<EOF
Usage: $0 --show
       $0 --apply
       $0 --rollback BACKUP

--show is read-only. --apply creates an exact root-owned backup first and
modifies only $TARGET. It never edits password-auth or authselect files.
EOF
}

fail() { printf 'error: %s\n' "$*" >&2; exit 2; }
[ "$(id -u)" -eq 0 ] || fail 'run as root'
[ -f "$TARGET" ] || fail "$TARGET is missing"
[ "$(authselect current 2>/dev/null | sed -n 's/^Profile ID: //p')" = local ] || \
	fail 'authselect profile is not local'
[ "$(stat -c '%u:%a' "$TARGET")" = '0:644' ] || fail "$TARGET ownership or mode is unexpected"
[ -f /usr/lib64/security/pam_howdy.so ] || fail 'pam_howdy.so is not installed'

mode=${1:---show}
case $mode in
--show)
	grep -Fqx "$HOWDY_LINE" "$TARGET" && fail 'Howdy line is already present'
	awk -v insertion="$HOWDY_LINE" \
		'{ print; if ($0 == "auth     [success=done ignore=ignore default=bad] pam_selinux_permit.so") print insertion }' \
		"$TARGET"
	;;
--apply)
	grep -Fqx "$HOWDY_LINE" "$TARGET" && fail 'Howdy line is already present'
	grep -Fqx 'auth     [success=done ignore=ignore default=bad] pam_selinux_permit.so' "$TARGET" || \
		fail 'expected gdm-password insertion anchor is missing'
	install -d -o root -g root -m 0700 "$ROLLBACK_ROOT"
	backup="$ROLLBACK_ROOT/gdm-password.$(date +%Y%m%d-%H%M%S)"
	cp -a --preserve=all "$TARGET" "$backup"
	temporary=$(mktemp /etc/pam.d/.gdm-password.XXXXXX)
	trap 'rm -f -- "$temporary"' EXIT
	awk -v insertion="$HOWDY_LINE" \
		'{ print; if ($0 == "auth     [success=done ignore=ignore default=bad] pam_selinux_permit.so") print insertion }' \
		"$TARGET" >"$temporary"
	chown root:root "$temporary"
	chmod 0644 "$temporary"
	mv -f "$temporary" "$TARGET"
	restorecon "$TARGET"
	printf 'applied GNOME-only Howdy PAM insertion\nbackup=%s\n' "$backup"
	printf 'test login and lock-screen unlock separately; rollback with --rollback %s\n' "$backup"
	;;
--rollback)
	[ $# -eq 2 ] || fail '--rollback requires the exact backup path'
	backup=$2
	[ -f "$backup" ] || fail "backup is missing: $backup"
	cp -a --preserve=all "$backup" "$TARGET"
	restorecon "$TARGET"
	printf 'restored %s from %s\n' "$TARGET" "$backup"
	;;
*) usage; exit 2 ;;
esac
