#!/bin/bash
# Load the separately staged, instrumented modules after a clean reboot.
# The IPU4 stack is intentionally one-load-per-boot; do not use this while it
# is already present.
set -euo pipefail

[[ $EUID -eq 0 ]] || { echo "Run with sudo after reboot." >&2; exit 1; }
if [[ -e /sys/module/intel_ipu4p ]]; then
	echo "IPU4 core is already loaded; resuming staged load."
fi

# This stage includes the validated Windows source-7 receiver timing path,
# GPREG tracing/candidate overrides, and automatic ISYS power-cycle recovery
# after a firmware release timeout.  The environment variables remain
# overridable so older candidates can still be reproduced explicitly.
STAGE=${INSTRUMENTED_MODULE_STAGE:-/home/george/repos/sp7-camera/work/source7-mipi-timing-20260915}
GPREG_PARAMS=(
	"csi_gpreg_hpll_freq=${CSI_GPREG_HPLL_FREQ:-2}"
	"csi_gpreg_isclk_ratio=${CSI_GPREG_ISCLK_RATIO:-12}"
)
WINDOWS_SOURCE7_MIPI_TIMING=${WINDOWS_SOURCE7_MIPI_TIMING:-Y}
for module in ipu-bridge intel-ipu4p-isys-csslib intel-ipu4p-psys-csslib \
	intel-ipu4p intel-ipu4p-psys intel-ipu4p-isys ov5693; do
	[[ -f "$STAGE/$module.ko" ]] || {
		echo "Missing $STAGE/$module.ko" >&2
		exit 1
	}
done

# Pull in exported vb2 symbols before the ISYS module. The first version of
# this script used only insmod, which leaves these distro modules unloaded.
modprobe videobuf2-common 2>/dev/null || modprobe videobuf2_common
modprobe videobuf2-v4l2 2>/dev/null || modprobe videobuf2_v4l2
modprobe videobuf2-dma-contig 2>/dev/null || modprobe videobuf2_dma_contig

[[ -e /sys/module/ipu_bridge ]] || insmod "$STAGE/ipu-bridge.ko"
[[ -e /sys/module/intel_ipu4p_isys_csslib ]] || \
	insmod "$STAGE/intel-ipu4p-isys-csslib.ko"
[[ -e /sys/module/intel_ipu4p_psys_csslib ]] || \
	insmod "$STAGE/intel-ipu4p-psys-csslib.ko"
[[ -e /sys/module/intel_ipu4p ]] || \
	insmod "$STAGE/intel-ipu4p.ko" fw_version_check=0
[[ -e /sys/module/intel_ipu4p_psys ]] || \
	insmod "$STAGE/intel-ipu4p-psys.ko"

MMU1=/sys/bus/intel-ipu4-bus/devices/intel-ipu4-mmu1/power/control
for i in $(seq 1 50); do
	[[ -e "$MMU1" ]] && break
	sleep 0.1
done
[[ -e "$MMU1" ]] || { echo "MMU1 power control missing" >&2; exit 1; }
printf 'on\n' > "$MMU1"
[[ -e /sys/module/intel_ipu4p_isys ]] || \
	insmod "$STAGE/intel-ipu4p-isys.ko" csi2_csettle=-1 csi2_dsettle=-1 \
		"${GPREG_PARAMS[@]}" \
		"windows_source7_mipi_timing=$WINDOWS_SOURCE7_MIPI_TIMING"

if [[ -e /sys/module/intel_ipu4p_isys/parameters/windows_source7_mipi_timing ]]; then
	printf '%s\n' "$WINDOWS_SOURCE7_MIPI_TIMING" > \
		/sys/module/intel_ipu4p_isys/parameters/windows_source7_mipi_timing
fi

udevadm settle --timeout=30 || true
sleep 3
echo "Instrumented IPU4 stack loaded. Run ./trace-capture.sh next."
