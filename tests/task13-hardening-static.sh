#!/bin/sh
# Hardware-independent checks for bounded IPU4P install and capture validation.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
INSTALL="$ROOT/scripts/install-modules.sh"
UNINSTALL="$ROOT/scripts/uninstall-modules.sh"
CAMERA="$ROOT/tests/camera-suite.sh"

for file in "$INSTALL" "$UNINSTALL" "$CAMERA"; do sh -n "$file"; done

modules='ipu-bridge.ko
intel-ipu4p.ko
intel-ipu4p-isys.ko
intel-ipu4p-psys.ko
intel-ipu4p-isys-csslib.ko
intel-ipu4p-psys-csslib.ko'
while IFS= read -r module; do
	[ -n "$module" ] || continue
	grep -Fq "$module" "$INSTALL"
	grep -Fq "$module" "$UNINSTALL"
done <<EOF
$modules
EOF

# Installation uses fixed source paths for the allowlist; it must not discover
# and copy every module found below the Intel driver tree.
! grep -Eq 'find .*\.ko|find .* -name' "$INSTALL"
grep -Fq 'source_for "$name"' "$INSTALL"
grep -Fq '.ipu4p-camera-modules' "$INSTALL"
grep -Fq 'sha256sum' "$INSTALL"
grep -Fq 'refusing to overwrite untracked module' "$INSTALL"
grep -Fq 'refusing to overwrite existing firmware' "$INSTALL"

# Rollback is manifest-limited and preserves any module whose bytes changed.
grep -Fq 'done < "$MANIFEST"' "$UNINSTALL"
grep -Fq 'left modified or non-regular module in place' "$UNINSTALL"
grep -Fq 'rm -f "$target"' "$UNINSTALL"
! grep -Eq 'rm -f .*\*\.ko|rm -rf .*intel' "$UNINSTALL"

# The live suite rejects invalid repeat counts before probing hardware, retains
# failure logs, verifies all three frames, and gates recovery on a verified
# recovery capture.
grep -Fq 'invalid CAMERA_TEST_REPEATS' "$CAMERA"
grep -Fq 'diagnostics retained: $LOGDIR' "$CAMERA"
grep -Fq 'minimum=$((width * height * 2 * 3))' "$CAMERA"
grep -Fq "LC_ALL=C tr -d '\\000'" "$CAMERA"
grep -Fq 'if [ "$RECOVERY_CAPTURE_OK" -eq 1 ]; then' "$CAMERA"
grep -Fq 'if capture_is_valid "$cam" "$LOGDIR/$cam.raw"; then' "$CAMERA"
! grep -Fq 'dd if="$raw" bs=4096 count=1' "$CAMERA"

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT HUP INT TERM
for repeats in 0 -1 1.5 abc; do
	if CAMERA_TEST_REPEATS=$repeats sh "$CAMERA" --live >"$tmp" 2>&1; then
		echo "task13-hardening-static: invalid repeat count unexpectedly succeeded: $repeats" >&2
		exit 1
	else
		status=$?
	fi
	[ "$status" -eq 2 ]
	grep -q 'invalid CAMERA_TEST_REPEATS' "$tmp"
done

echo 'task13-hardening-static: PASS'
