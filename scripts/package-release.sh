#!/bin/sh
# Assemble the verified modules and existing install/rollback helpers into a
# kernel-specific archive suitable for attaching to a GitHub Release.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KREL=${KREL:?set KREL to the exact target kernel release}
VERSION=${PACKAGE_VERSION:-snapshot}
SOURCE_COMMIT=${SOURCE_COMMIT:-unknown}
OUTPUT_DIR=${OUTPUT_DIR:-$ROOT/dist}

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

MODULES='
linux-6.19.8/drivers/media/pci/intel/ipu-bridge.ko|ipu-bridge.ko
linux-6.19.8/drivers/media/pci/intel/ipu4/intel-ipu4p.ko|intel-ipu4p.ko
linux-6.19.8/drivers/media/pci/intel/ipu4/intel-ipu4p-isys.ko|intel-ipu4p-isys.ko
linux-6.19.8/drivers/media/pci/intel/ipu4/intel-ipu4p-psys.ko|intel-ipu4p-psys.ko
linux-6.19.8/drivers/media/pci/intel/ipu4/intel-ipu4p-isys-csslib.ko|intel-ipu4p-isys-csslib.ko
linux-6.19.8/drivers/media/pci/intel/ipu4/ipu4p-css/lib2600psys/intel-ipu4p-psys-csslib.ko|intel-ipu4p-psys-csslib.ko
linux-6.19.8/drivers/media/i2c/dw9719.ko|dw9719.ko'

while IFS='|' read -r relative_path module_name; do
	[ -n "$relative_path" ] || continue
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
done <<EOF
$MODULES
EOF

mkdir -p "$STAGE/scripts"
cp -p "$ROOT/scripts/install-modules.sh" "$STAGE/scripts/"
cp -p "$ROOT/scripts/uninstall-modules.sh" "$STAGE/scripts/"

cat > "$STAGE/INSTALL.txt" <<EOF
Surface Pro 7 IPU4P camera modules

Target kernel: $KREL
Package version: $VERSION
Source commit: $SOURCE_COMMIT

This archive contains prebuilt modules for this exact kernel release. It does
not contain firmware. Obtain the Microsoft-signed ipu4p_cpd.bin from an
authorized source before installing.

1. Extract this archive.
2. Install the modules and your firmware file:

   FIRMWARE=/absolute/path/to/ipu4p_cpd.bin sudo -E ./scripts/install-modules.sh

3. Reboot so the kernel loads the installed modules at boot.

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
