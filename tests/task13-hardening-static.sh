#!/bin/sh
# Hardware-independent checks for bounded IPU4P install and capture validation.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
INSTALL="$ROOT/scripts/install-modules.sh"
UNINSTALL="$ROOT/scripts/uninstall-modules.sh"
CAMERA="$ROOT/tests/camera-suite.sh"
CAPTURE_VALIDATION="$ROOT/tests/capture-validation.sh"

for file in "$INSTALL" "$UNINSTALL" "$CAMERA" "$CAPTURE_VALIDATION"; do sh -n "$file"; done

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
grep -Fq '[ ! -L "$target" ] && [ -f "$target" ]' "$UNINSTALL"
grep -Fq 'rm -f "$target"' "$UNINSTALL"
! grep -Eq 'rm -f .*\*\.ko|rm -rf .*intel' "$UNINSTALL"

# The live suite rejects invalid repeat counts before probing hardware, retains
# failure logs, verifies all three frames, and gates recovery on a verified
# recovery capture.
grep -Fq 'invalid CAMERA_TEST_REPEATS' "$CAMERA"
grep -Fq 'diagnostics retained: $LOGDIR' "$CAMERA"
grep -Fq '. "$ROOT/tests/capture-validation.sh"' "$CAMERA"
grep -Fq 'minimum=$((width * height * 2 * 3))' "$CAPTURE_VALIDATION"
grep -Fq 'frame_bytes=$((bytes / 3))' "$CAPTURE_VALIDATION"
grep -Fq 'while [ "$frame" -lt 3 ]; do' "$CAPTURE_VALIDATION"
grep -Fq 'if [ "$RECOVERY_CAPTURE_OK" -eq 1 ]; then' "$CAMERA"
grep -Fq 'if capture_is_valid "$cam" "$LOGDIR/$cam.raw"; then' "$CAMERA"
! grep -Fq 'dd if="$raw" bs=4096 count=1' "$CAMERA"

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
tmp="$tmpdir/output"

# Exercise install/uninstall against isolated temp trees. modinfo reports the
# selected test release; fake module payloads avoid depending on build outputs.
mockbin="$tmpdir/bin"
mkdir -p "$mockbin"
cat > "$mockbin/modinfo" <<'EOF'
#!/bin/sh
if [ "$1" = -k ] && [ "$3" = -n ] && [ "$4" = ipu_bridge ]; then
	[ -n "${NATIVE_BRIDGE_PATH:-}" ] || exit 1
	printf '%s\n' "$NATIVE_BRIDGE_PATH"
	exit 0
fi
[ "$1" = -F ] && [ "$2" = vermagic ] || exit 2
printf '%s SMP test\n' "$KREL"
EOF
cat > "$mockbin/depmod" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod +x "$mockbin/modinfo" "$mockbin/depmod"
fwsrc="$tmpdir/firmware-source"
printf 'firmware bytes\n' > "$fwsrc"
source_root="$tmpdir/source"
intel_src="$source_root/linux-6.19.8/drivers/media/pci/intel"
mkdir -p "$intel_src/ipu4/ipu4p-css/lib2600psys"
while IFS= read -r module; do
	[ -n "$module" ] || continue
	case $module in
		ipu-bridge.ko) module_source="$intel_src/$module" ;;
		intel-ipu4p-psys-csslib.ko) module_source="$intel_src/ipu4/ipu4p-css/lib2600psys/$module" ;;
		*) module_source="$intel_src/ipu4/$module" ;;
	esac
	printf 'fake module payload for %s\n' "$module" > "$module_source"
done <<EOF
$modules
EOF
moddir="$tmpdir/install-modules"
firmware_target="$tmpdir/firmware/ipu4p_cpd.bin"
PATH="$mockbin:$PATH" MODULE_SOURCE_ROOT="$source_root" KREL=task13-test MODDIR="$moddir" \
	FIRMWARE="$fwsrc" FIRMWARE_TARGET="$firmware_target" sh "$INSTALL"
[ "$(find "$moddir" -maxdepth 1 -type f -name '*.ko' | wc -l | tr -d '[:space:]')" -eq 6 ]
[ "$(wc -l < "$moddir/.ipu4p-camera-modules" | tr -d '[:space:]')" -eq 6 ]
PATH="$mockbin:$PATH" MODULE_SOURCE_ROOT="$source_root" KREL=task13-test MODDIR="$moddir" \
	FIRMWARE="$fwsrc" FIRMWARE_TARGET="$firmware_target" sh "$INSTALL"
PATH="$mockbin:$PATH" KREL=task13-test MODDIR="$moddir" sh "$UNINSTALL"
[ -z "$(find "$moddir" -maxdepth 1 -type f -name '*.ko' -print -quit)" ]
[ -f "$firmware_target" ]
[ ! -e "$moddir/.ipu4p-camera-modules" ]

# On a kernel with a native bridge, install the five IPU4P modules only and
# ensure the manifest/uninstaller never claims or removes that bridge.
native_moddir="$tmpdir/install-native-bridge"
native_path="$tmpdir/native/ipu-bridge.ko"
mkdir -p "$(dirname "$native_path")"
printf 'native bridge bytes\n' > "$native_path"
PATH="$mockbin:$PATH" NATIVE_BRIDGE_PATH="$native_path" MODULE_SOURCE_ROOT="$source_root" KREL=task13-test MODDIR="$native_moddir" \
	FIRMWARE="$fwsrc" FIRMWARE_TARGET="$tmpdir/native-fw" sh "$INSTALL"
[ ! -e "$native_moddir/ipu-bridge.ko" ]
[ "$(find "$native_moddir" -maxdepth 1 -type f -name '*.ko' | wc -l | tr -d '[:space:]')" -eq 5 ]
! grep -q '^ipu-bridge\.ko ' "$native_moddir/.ipu4p-camera-modules"
PATH="$mockbin:$PATH" KREL=task13-test MODDIR="$native_moddir" sh "$UNINSTALL"
[ -f "$native_path" ]
[ ! -e "$native_moddir/.ipu4p-camera-modules" ]

# An untracked collision must fail in preflight without placing peer modules.
conflict_dir="$tmpdir/install-conflict"
mkdir -p "$conflict_dir"
printf 'existing module\n' > "$conflict_dir/ipu-bridge.ko"
if PATH="$mockbin:$PATH" MODULE_SOURCE_ROOT="$source_root" KREL=task13-test MODDIR="$conflict_dir" \
	FIRMWARE="$fwsrc" FIRMWARE_TARGET="$tmpdir/conflict-fw" sh "$INSTALL" > "$tmp" 2>&1; then
	echo 'task13-hardening-static: installer replaced an untracked module' >&2
	exit 1
fi
grep -q 'refusing to overwrite untracked module' "$tmp"
[ "$(cat "$conflict_dir/ipu-bridge.ko")" = 'existing module' ]
[ -z "$(find "$conflict_dir" -maxdepth 1 -type f -name 'intel-ipu4p*.ko' -print -quit)" ]

# A prepared KDIR for another release must fail before invoking make.
fake_kdir="$tmpdir/fake-kdir"
mkdir -p "$fake_kdir/include/generated"
: > "$fake_kdir/Makefile"
: > "$fake_kdir/.config"
printf '#define UTS_RELEASE "task13-kdir-release"\n' > "$fake_kdir/include/generated/utsrelease.h"
cat > "$mockbin/make" <<EOF
#!/bin/sh
printf 'make was unexpectedly invoked\\n' > "$tmpdir/make-invoked"
exit 99
EOF
chmod +x "$mockbin/make"
if PATH="$mockbin:$PATH" KDIR="$fake_kdir" KREL=task13-target-release \
	sh "$ROOT/scripts/build-modules.sh" > "$tmp" 2>&1; then
	echo 'task13-hardening-static: build accepted a mismatched KDIR/KREL' >&2
	exit 1
fi
grep -q 'does not match target KREL task13-target-release' "$tmp"
[ ! -e "$tmpdir/make-invoked" ]

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

# Exercise the actual validator on sparse three-frame raw files. A nonzero byte
# in only the first frame must not make the complete stream pass.
. "$CAPTURE_VALIDATION"
frame_bytes=$((2592 * 1944 * 2))
capture="$tmpdir/capture.raw"
truncate -s "$((frame_bytes * 3))" "$capture"
printf '\001' | dd of="$capture" bs=1 seek=0 conv=notrunc 2>/dev/null
if capture_is_valid front "$capture"; then
	echo 'task13-hardening-static: one valid frame plus two zero frames unexpectedly passed' >&2
	exit 1
fi
for frame in 1 2; do
	printf '\001' | dd of="$capture" bs=1 seek="$((frame_bytes * frame))" conv=notrunc 2>/dev/null
done
capture_is_valid front "$capture"

# A symlink at a tracked module name is not an owned regular module and must
# survive uninstall even if it resolves to bytes with the recorded hash.
moddir="$tmpdir/modules"
mkdir -p "$moddir"
printf 'module bytes\n' > "$tmpdir/module-backing"
ln -s ../module-backing "$moddir/intel-ipu4p.ko"
hash=$(sha256sum "$tmpdir/module-backing" | awk '{print $1}')
printf 'intel-ipu4p.ko %s\n' "$hash" > "$moddir/.ipu4p-camera-modules"
MODDIR="$moddir" KREL=test sh "$UNINSTALL" > "$tmp" 2>&1
[ -L "$moddir/intel-ipu4p.ko" ]
grep -q 'intel-ipu4p.ko' "$moddir/.ipu4p-camera-modules"

echo 'task13-hardening-static: PASS'
