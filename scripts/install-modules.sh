#!/bin/sh
# Install already-built modules and the externally supplied CPD firmware.
# No module loading, service changes, or initramfs rebuild is performed here.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KREL=${KREL:-$(uname -r)}
MODDIR=${MODDIR:-/lib/modules/$KREL/updates/extra}
FIRMWARE=${FIRMWARE:-}

[ -n "$FIRMWARE" ] || {
	printf '%s\n' 'usage: FIRMWARE=/path/to/ipu4p_cpd.bin sudo ./scripts/install-modules.sh' >&2
	exit 2
}
[ -f "$FIRMWARE" ] || { printf 'error: firmware not found: %s\n' "$FIRMWARE" >&2; exit 2; }

found=0
while IFS= read -r module; do
	[ -n "$module" ] || continue
	found=1
	install -D -m 0644 "$module" "$MODDIR/$(basename "$module")"
done <<EOF
$(find "$ROOT/linux-6.19.8/drivers/media/pci/intel" -type f -name '*.ko' -print)
EOF
[ "$found" -eq 1 ] || { printf '%s\n' 'error: no built .ko files found; run scripts/build-modules.sh first' >&2; exit 2; }

FIRMWARE_TARGET=/lib/firmware/ipu4p_cpd.bin
if [ "$FIRMWARE" -ef "$FIRMWARE_TARGET" ]; then
	printf 'firmware already installed at %s\n' "$FIRMWARE_TARGET"
else
	install -D -m 0644 "$FIRMWARE" "$FIRMWARE_TARGET"
fi
depmod -a "$KREL"
printf 'installed modules in %s and firmware in %s\n' "$MODDIR" "$FIRMWARE_TARGET"
