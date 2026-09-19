#!/bin/sh
# Assemble the verified modules and existing install/rollback helpers into a
# kernel-specific archive suitable for attaching to a GitHub Release.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KREL=${KREL:?set KREL to the exact target kernel release}
VERSION=${PACKAGE_VERSION:-snapshot}
SOURCE_COMMIT=${SOURCE_COMMIT:-unknown}
OUTPUT_DIR=${OUTPUT_DIR:-$ROOT/dist}
MODULE_MANIFEST=${MODULE_MANIFEST:-$ROOT/modules/ipu4p-camera.modules}
C_BRIDGE_BINARY=${C_BRIDGE_BINARY:-$ROOT/cbridge/sp7-camera-bridge}
C_IR_BINARY=${C_IR_BINARY:-$ROOT/cbridge/sp7-camera-ir}
C_AUTH_BINARY=${C_AUTH_BINARY:-$ROOT/cbridge/sp7-camera-auth-capture}

case $VERSION in
	''|*[!A-Za-z0-9._+-]*)
		printf 'error: invalid package version: %s\n' "$VERSION" >&2
		exit 2
		;;
esac

case $KREL in
	''|*[!A-Za-z0-9._+-]*)
		printf 'error: invalid kernel release: %s\n' "$KREL" >&2
		exit 2
		;;
esac

if ! command -v modinfo >/dev/null 2>&1; then
	printf '%s\n' 'error: modinfo is required to verify module vermagic' >&2
	exit 2
fi
[ -f "$MODULE_MANIFEST" ] || {
	printf 'error: module manifest is missing: %s\n' "$MODULE_MANIFEST" >&2
	exit 2
}
[ -x "$C_BRIDGE_BINARY" ] || {
	printf 'error: C camera bridge is missing or not executable: %s\n' "$C_BRIDGE_BINARY" >&2
	exit 2
}
[ -x "$C_IR_BINARY" ] || {
	printf 'error: standalone IR producer is missing or not executable: %s\n' "$C_IR_BINARY" >&2
	exit 2
}
[ -x "$C_AUTH_BINARY" ] || {
	printf 'error: authentication capture helper is missing or not executable: %s\n' "$C_AUTH_BINARY" >&2
	exit 2
}

case $OUTPUT_DIR in
	/*) ;;
	*) OUTPUT_DIR="$ROOT/$OUTPUT_DIR" ;;
esac
mkdir -p "$OUTPUT_DIR"
ASSET="$OUTPUT_DIR/sp7-ipu4-camera-$VERSION-$KREL.tar.gz"
STAGE=$(mktemp -d)
cleanup() {
	status=$?
	trap - EXIT
	rm -rf "$STAGE"
	exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

while IFS='|' read -r relative_path module_name extra; do
	[ -n "$relative_path" ] || continue
	case $relative_path in \#*) continue ;; esac
	[ -n "$module_name" ] && [ -z "${extra:-}" ] || {
		printf 'error: malformed module manifest entry\n' >&2
		exit 2
	}
	source="$ROOT/$relative_path"
	[ -f "$source" ] || {
		printf 'error: required module is missing: %s\n' "$source" >&2
		exit 2
	}
	vermagic=$(modinfo -F vermagic "$source" 2>/dev/null) || {
		printf 'error: cannot read vermagic from %s\n' "$source" >&2
		exit 2
	}
	module_release=${vermagic%% *}
	[ "$module_release" = "$KREL" ] || {
		printf 'error: %s vermagic %s does not match target %s\n' \
			"$module_name" "${module_release:-unknown}" "$KREL" >&2
		exit 2
	}
	mkdir -p "$STAGE/$(dirname -- "$relative_path")"
	cp -p "$source" "$STAGE/$relative_path"
done < "$MODULE_MANIFEST"

mkdir -p "$STAGE/scripts"
cp -p "$ROOT/scripts/install-modules.sh" "$STAGE/scripts/"
cp -p "$ROOT/scripts/uninstall-modules.sh" "$STAGE/scripts/"
cp -p "$ROOT/scripts/setup-camera-bridge.sh" "$STAGE/scripts/"
cp -p "$ROOT/scripts/remove-camera-bridge.sh" "$STAGE/scripts/"
cp -p "$ROOT/scripts/kernel-release.sh" "$STAGE/scripts/"
mkdir -p "$STAGE/scripts/ir" "$STAGE/docs"
cp -p "$ROOT/scripts/ir/howdy-direct-demo.py" "$STAGE/scripts/ir/"
cp -p "$ROOT/docs/ir-auth-capture-demo.md" "$STAGE/docs/"
mkdir -p "$STAGE/cbridge"
cp -p "$C_BRIDGE_BINARY" "$STAGE/cbridge/sp7-camera-bridge"
cp -p "$C_IR_BINARY" "$STAGE/cbridge/sp7-camera-ir"
cp -p "$C_AUTH_BINARY" "$STAGE/cbridge/sp7-camera-auth-capture"
mkdir -p "$STAGE/modules"
cp -p "$MODULE_MANIFEST" "$STAGE/modules/"
mkdir -p "$STAGE/modprobe.d" "$STAGE/wireplumber" "$STAGE/systemd/user"
cp -p "$ROOT/modprobe.d/98-v4l2loopback.conf" "$STAGE/modprobe.d/"
cp -p "$ROOT/wireplumber/50-sp7-ipu4.conf" "$STAGE/wireplumber/"
cp -p "$ROOT/systemd/user/sp7-camera-bridge.service" "$STAGE/systemd/user/"

cat > "$STAGE/INSTALL.txt" <<EOF
Surface Pro 7 IPU4P camera modules

Target kernel: $KREL
Package version: $VERSION
Source commit: $SOURCE_COMMIT

This archive contains prebuilt modules for this exact kernel release. It does
not contain firmware. Obtain the Microsoft-signed ipu4p_cpd.bin from an
authorized source before installing.

The archive includes the shared module inventory and kernel-release helper used
by the build, installation, and packaging checks. It does not include a
compiler or kernel development packages.

The included C named-camera bridge also needs Fedora's libcamera-gstreamer and
gstreamer1-plugins-good packages, plus the RPM Fusion Free
akmod-v4l2loopback and v4l2loopback packages.

1. Extract this archive.
2. Install the modules and your firmware file:

   FIRMWARE=/absolute/path/to/ipu4p_cpd.bin sudo -E ./scripts/install-modules.sh

3. Install the named Surface Camera (front), Surface Camera (back), and
   Surface Camera (IR) loopbacks while logged into the desktop session:
   endpoints while logged into the desktop session:

   sudo ./scripts/setup-camera-bridge.sh

   The IR producer is not started by the bridge service. Start it manually
   only after the source-6 capture qualification gates in the documentation
   have passed:

   /usr/local/libexec/sp7-camera-ir

   The protected standalone authentication capture helper is installed at
   /usr/local/libexec/sp7-camera-auth-capture. It is not connected to PAM.
   The no-PAM enrollment/matching demo is scripts/ir/howdy-direct-demo.py.

4. Reboot so the kernel loads the installed modules at boot.

To remove the installed modules later, run:

   sudo ./scripts/uninstall-modules.sh

The modules may be rejected when Secure Boot requires signatures not present
in this bundle. Kernel updates require a new bundle built for the new release.
EOF

cat > "$STAGE/BUILD-INFO.txt" <<EOF
Project: Surface Pro 7 IPU4P camera driver
Target kernel release: $KREL
Package version: $VERSION
Source commit: $SOURCE_COMMIT
Firmware included: no
EOF

(
	cd "$STAGE"
	find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum
) > "$STAGE/SHA256SUMS"

tar -C "$STAGE" -czf "$ASSET" .
printf 'created %s\n' "$ASSET"
