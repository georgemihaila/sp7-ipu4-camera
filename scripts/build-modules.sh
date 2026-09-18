#!/bin/sh
# Build the IPU4P module subtree against an external, prepared kernel tree.
# This does not build the OV5693 sensor: see docs/external-kernel-integration.md.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KDIR=${KDIR:-/lib/modules/$(uname -r)/build}
KREL=${KREL:-$(uname -r)}
SRC="$ROOT/linux-6.19.8/drivers/media/pci/intel"
MANIFEST=${MODULE_MANIFEST:-$ROOT/modules/ipu4p-camera.modules}

[ -f "$MANIFEST" ] || {
	printf 'error: module manifest is missing: %s\n' "$MANIFEST" >&2
	exit 2
}

if [ ! -f "$KDIR/Makefile" ] || [ ! -f "$KDIR/.config" ]; then
	printf '%s\n' "error: KDIR must be a prepared kernel build/source tree: $KDIR" >&2
	exit 2
fi

# Use the release embedded in the prepared tree. `make kernelrelease` may add
# a local `+` suffix based on the source checkout's current git state, even
# though the generated module UTS_RELEASE still matches the target kernel.
KDIR_RELEASE=$("$ROOT/scripts/kernel-release.sh" "$KDIR")
if [ -z "$KDIR_RELEASE" ]; then
	printf '%s\n' "error: KDIR has no generated kernel release metadata: $KDIR" >&2
	exit 2
fi
[ "$KDIR_RELEASE" = "$KREL" ] || {
	printf 'error: KDIR release %s does not match target KREL %s\n' \
		"$KDIR_RELEASE" "$KREL" >&2
	exit 2
}

if ! command -v modinfo >/dev/null 2>&1; then
	printf '%s\n' 'error: modinfo is required to verify built module vermagic' >&2
	exit 2
fi

# srcpath is deliberately the overlay's intel directory. The fragment's
# ipu4/Makefile includes CSS makefiles relative to this directory.
make -C "$KDIR" M="$SRC" EXTERNAL_BUILD=1 srcpath="$SRC" \
	CONFIG_VIDEO_INTEL_IPU=m CONFIG_VIDEO_INTEL_IPU4P=y \
	CONFIG_VIDEO_INTEL_IPU6= CONFIG_VIDEO_IPU3_CIO2= CONFIG_INTEL_VSC= \
	CONFIG_VIDEO_INTEL_IPU_FW_LIB=y modules "$@"

while IFS='|' read -r relative_path name extra; do
	[ -n "$relative_path" ] || continue
	case $relative_path in \#*) continue ;; esac
	[ -n "$name" ] && [ -z "${extra:-}" ] || {
		printf 'error: malformed module manifest entry\n' >&2
		exit 2
	}
	module="$ROOT/$relative_path"
	[ -f "$module" ] || {
		printf 'error: expected module was not built: %s\n' "$module" >&2
		exit 2
	}
	vermagic=$(modinfo -F vermagic "$module" 2>/dev/null) || {
		printf 'error: unable to read vermagic from %s\n' "$module" >&2
		exit 2
	}
	module_release=${vermagic%% *}
	[ "$module_release" = "$KREL" ] || {
		printf 'error: %s vermagic release %s does not match target KREL %s\n' \
			"$name" "${module_release:-unknown}" "$KREL" >&2
		exit 2
	}
done < "$MANIFEST"
