#!/bin/bash
# Manual diagnostic/fallback loader. Production systems should install the
# modules, run depmod, and let the IPU4 PCI modalias invoke modprobe normally.
# This script deliberately has no boot delay, MMU power pin, or experimental
# module parameters.
set -e

modprobe intel_ipu4p
MEDIA_DEVICE=""
for candidate in /dev/media*; do
    [ -e "$candidate" ] || continue
    MEDIA_DEVICE=$candidate
    break
done
if [ -n "$MEDIA_DEVICE" ]; then
    echo "IPU4 stack present, $MEDIA_DEVICE:"
    dmesg | grep -E 'CSE|Connected.*cameras' | tail -4
else
    echo "ERROR: no media-controller device after manual diagnostic load; check dmesg" >&2
    exit 1
fi
