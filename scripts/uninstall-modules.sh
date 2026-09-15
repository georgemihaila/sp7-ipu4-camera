#!/bin/sh
# Remove only the exact module files installed by install-modules.sh.
# Firmware is retained by default because it may be shared by another driver.
set -eu

KREL=${KREL:-$(uname -r)}
MODDIR=${MODDIR:-/lib/modules/$KREL/updates/extra}
for name in intel-ipu4p.ko intel-ipu4p-isys.ko intel-ipu4p-psys.ko \
	intel-ipu4p-isys-csslib.ko intel-ipu4p-psys-csslib.ko ipu-bridge.ko; do
	rm -f "$MODDIR/$name"
done
depmod -a "$KREL"
printf 'removed driver modules from %s; firmware was left in place\n' "$MODDIR"
