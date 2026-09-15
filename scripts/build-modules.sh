#!/bin/sh
# Build the IPU4P module subtree against an external, prepared kernel tree.
# This does not build the OV5693 sensor: see docs/external-kernel-integration.md.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KDIR=${KDIR:-/lib/modules/$(uname -r)/build}
SRC="$ROOT/linux-6.19.8/drivers/media/pci/intel"

if [ ! -f "$KDIR/Makefile" ] || [ ! -f "$KDIR/.config" ]; then
	printf '%s\n' "error: KDIR must be a prepared kernel build/source tree: $KDIR" >&2
	exit 2
fi

# srcpath is deliberately the overlay's intel directory. The fragment's
# ipu4/Makefile includes CSS makefiles relative to this directory.
exec make -C "$KDIR" M="$SRC" EXTERNAL_BUILD=1 srcpath="$SRC" \
	CONFIG_VIDEO_INTEL_IPU=m CONFIG_VIDEO_INTEL_IPU4P=y \
	CONFIG_VIDEO_INTEL_IPU_FW_LIB=y modules "$@"
