#!/bin/sh
# Install the explicit IPU4P dependency modules and externally supplied CPD firmware.
# No module loading, service changes, or initramfs rebuild is performed here.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KREL=${KREL:-$(uname -r)}
MODDIR=${MODDIR:-/lib/modules/$KREL/updates/extra}
FIRMWARE=${FIRMWARE:-}
FIRMWARE_TARGET=${FIRMWARE_TARGET:-/lib/firmware/ipu4p_cpd.bin}
MANIFEST="$MODDIR/.ipu4p-camera-modules"

MODULES='ipu-bridge.ko
intel-ipu4p.ko
intel-ipu4p-isys.ko
intel-ipu4p-psys.ko
intel-ipu4p-isys-csslib.ko
intel-ipu4p-psys-csslib.ko'

if [ -z "$FIRMWARE" ]; then
	printf '%s\n' 'usage: FIRMWARE=/path/to/ipu4p_cpd.bin sudo ./scripts/install-modules.sh' >&2
	exit 2
fi
[ -f "$FIRMWARE" ] || { printf 'error: firmware not found: %s\n' "$FIRMWARE" >&2; exit 2; }

source_for() {
	case $1 in
		ipu-bridge.ko) printf '%s\n' "$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu-bridge.ko" ;;
		intel-ipu4p.ko|intel-ipu4p-isys.ko|intel-ipu4p-psys.ko|intel-ipu4p-isys-csslib.ko)
			printf '%s/%s\n' "$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu4" "$1" ;;
		intel-ipu4p-psys-csslib.ko)
			printf '%s/%s\n' "$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu4/ipu4p-css/lib2600psys" "$1" ;;
		*) return 1 ;;
	esac
}

file_hash() { sha256sum "$1" | awk '{print $1}'; }
manifest_hash() {
	[ -f "$MANIFEST" ] || return 1
	awk -v module="$1" '$1 == module { print $2; found++ } END { if (found != 1) exit 1 }' "$MANIFEST"
}

if [ -e "$MANIFEST" ]; then
	[ -f "$MANIFEST" ] || { printf 'error: module manifest is not a regular file: %s\n' "$MANIFEST" >&2; exit 2; }
	while IFS=' ' read -r name hash extra; do
		[ -n "$name" ] || continue
		case $name in
			ipu-bridge.ko|intel-ipu4p.ko|intel-ipu4p-isys.ko|intel-ipu4p-psys.ko|intel-ipu4p-isys-csslib.ko|intel-ipu4p-psys-csslib.ko) ;;
			*) printf 'error: unexpected module in manifest: %s\n' "$name" >&2; exit 2 ;;
		esac
		[ -z "${extra:-}" ] && printf '%s\n' "$hash" | grep -Eq '^[0-9a-f]{64}$' || {
			printf 'error: malformed module manifest entry for %s\n' "$name" >&2; exit 2;
		}
		[ "$(awk -v n="$name" '$1 == n { c++ } END { print c + 0 }' "$MANIFEST")" -eq 1 ] || {
			printf 'error: duplicate module manifest entry: %s\n' "$name" >&2; exit 2;
		}
	done < "$MANIFEST"
fi

# Check the complete allowlist and all conflicts before changing the module or
# firmware directories. Existing module files are accepted only when the
# manifest proves that this installer placed the unchanged file there.
while IFS= read -r name; do
	[ -n "$name" ] || continue
	source=$(source_for "$name") || { printf 'error: module is not allowlisted: %s\n' "$name" >&2; exit 2; }
	[ -f "$source" ] || { printf 'error: required module not built: %s\n' "$source" >&2; exit 2; }
	target="$MODDIR/$name"
	if [ -e "$target" ] || [ -L "$target" ]; then
		owned_hash=$(manifest_hash "$name") || {
			printf 'error: refusing to overwrite untracked module: %s\n' "$target" >&2; exit 2;
		}
		current_hash=$(file_hash "$target") || { printf 'error: cannot hash existing module: %s\n' "$target" >&2; exit 2; }
		[ "$current_hash" = "$owned_hash" ] || {
			printf 'error: tracked module was modified; uninstall it before reinstalling: %s\n' "$target" >&2; exit 2;
		}
		source_hash=$(file_hash "$source")
		[ "$source_hash" = "$current_hash" ] || {
			printf 'error: module update would replace an installed version; uninstall it first: %s\n' "$target" >&2; exit 2;
		}
	fi
done <<EOF
$MODULES
EOF

if [ -e "$FIRMWARE_TARGET" ] || [ -L "$FIRMWARE_TARGET" ]; then
	[ -f "$FIRMWARE_TARGET" ] && cmp -s "$FIRMWARE" "$FIRMWARE_TARGET" || {
		printf 'error: refusing to overwrite existing firmware: %s\n' "$FIRMWARE_TARGET" >&2; exit 2;
	}
fi

mkdir -p "$MODDIR"
STAGE=$(mktemp -d "$MODDIR/.ipu4p-camera-stage.XXXXXX")
cleanup() {
	status=$?
	trap - EXIT
	[ -z "${STAGE:-}" ] || rm -rf "$STAGE"
	exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

# Publish the exact intended file set before placing any new module. If an
# interrupted install is rolled back, uninstall still has exact names and
# hashes and will remove only files that match these staged module bytes.
while IFS= read -r name; do
	[ -n "$name" ] || continue
	source=$(source_for "$name")
	install -m 0644 "$source" "$STAGE/$name"
	file_hash "$STAGE/$name" | awk -v module="$name" '{ print module " " $1 }' >> "$STAGE/manifest"
done <<EOF
$MODULES
EOF
mv -f "$STAGE/manifest" "$MANIFEST"

while IFS= read -r name; do
	[ -n "$name" ] || continue
	target="$MODDIR/$name"
	[ -e "$target" ] || mv "$STAGE/$name" "$target"
done <<EOF
$MODULES
EOF

if [ ! -e "$FIRMWARE_TARGET" ]; then
	install -D -m 0644 "$FIRMWARE" "$FIRMWARE_TARGET"
fi
rm -rf "$STAGE"
STAGE=
depmod -a "$KREL"
printf 'installed IPU4P modules in %s and firmware in %s\n' "$MODDIR" "$FIRMWARE_TARGET"
