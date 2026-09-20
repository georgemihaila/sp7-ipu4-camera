#!/bin/sh
# Hardware-independent audit of the production camera contract.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DRIVERS="$ROOT/linux-6.19.8/drivers/media"
IPU="$DRIVERS/pci/intel"
SENSOR="$DRIVERS/i2c/ov5693.c"
CAPTURE="$ROOT/test-capture.sh"

has() {
	pattern=$1; shift
	for file in "$@"; do
		grep -q "$pattern" "$file" 2>/dev/null && return 0
	done
	return 1
}

test -s "$SENSOR"
test -s "$IPU/ipu-isys.c"
test -s "$IPU/ipu-isys-csi2.c"
test -s "$IPU/ipu-isys-video.c"
test -s "$IPU/ipu4/ipu4p-isys-csi2.c"
test -x "$CAPTURE"

# Normal discovery: PCI alias, firmware request, and module soft dependencies.
if ! grep -q '8a19' "$IPU/ipu.h"; then exit 1; fi
if ! grep -q 'ipu4p_cpd' "$IPU/ipu4/ipu-platform.h"; then exit 1; fi
if ! grep -q 'MODULE_SOFTDEP' "$IPU/ipu.c"; then exit 1; fi

# Tasks 1–7: bounded topology/firmware/PM and production error handling.
has 'readl_poll_timeout' "$IPU/ipu-buttress.c" "$IPU/ipu6/ipu6-buttress.c" || exit 1
has 'pm_runtime_put' "$IPU"/*.c "$IPU"/*/*.c || exit 1
has 'media_pipeline_stop_for_vc' "$IPU/ipu-isys-video.c" || exit 1
has 'is_support_vc\|ipu_isys_query_sensor_info' "$IPU/ipu-isys-video.c" || exit 1
grep -q 'MEDIA_BUS_FMT_SBGGR10_1X10' "$SENSOR"
grep -q 'V4L2_CID_LINK_FREQ\|V4L2_CID_PIXEL_RATE' "$SENSOR"

# Tasks 8–10: explicit camera contract, CSI-2 error recovery, no bring-up knobs.
grep -q 'num_data_lanes != 2' "$SENSOR"
grep -q 'enum_frame_interval' "$SENSOR"
grep -q 'IPU_ISYS_CSI2_FATAL_ERRORS' "$IPU/ipu-isys-csi2.h"
grep -q 'ipu_isys_csi2_reset_errors' "$IPU/ipu-isys-csi2.c"
grep -q 'dev_warn_ratelimited' "$IPU/ipu-isys-queue.c"

# The BE SOC mux and capture edges are dynamic by contract; the entity-local
# callback still rejects two enabled inputs targeting the same mux sink.
grep -q 'MEDIA_LNK_FL_DYNAMIC' "$IPU/ipu-isys.c"
grep -q 'MEDIA_LNK_FL_DYNAMIC' "$IPU/ipu-isys-csi2-be-soc.c"
grep -q 'csi2_be_soc_link_setup' "$IPU/ipu-isys-csi2-be-soc.c"

# media-ctl 1.32 must retain MEDIA_LNK_FL_DYNAMIC when enabling the dynamic
# CSI2-BE-SOC edges, and the canonical helper must select their linked node.
grep -q 'DYNAMIC_ENABLED=5' "$CAPTURE"
grep -Fq '[${DYNAMIC_ENABLED}]' "$CAPTURE"
grep -q 'Intel IPU4 BE SOC capture 0' "$CAPTURE"
for forbidden in csi2_fw_src csi2_csettle csi2_dsettle phy_bb_extra phy_afe_extra \
	phy_jsl_bits windows_bscan_late windows_source7_mipi_timing; do
	! has "$forbidden" "$IPU"/*.c "$IPU"/*/*.c "$SENSOR"
done

# Production entry points must not contain the removed bring-up service/knob path.
if [ -d "$ROOT/systemd" ] && [ -n "$(find "$ROOT/systemd" -type f -print 2>/dev/null)" ]; then
	if [ -n "$(find "$ROOT/systemd" -type f \
		! -path "$ROOT/systemd/user/sp7-camera-bridge.service" \
		! -path "$ROOT/systemd/system/sp7-camera-howdy-preflight.service" \
		! -path "$ROOT/systemd/system/sp7-camera-howdy-route.service" \
		-print 2>/dev/null)" ]; then
		echo 'task11-static: removed systemd bring-up files remain' >&2
		exit 1
	fi
fi
if grep -R -q '/dev/media0\|fuser -k\|systemctl.*restart' \
	"$ROOT/tests/task8-camera-static.sh" "$ROOT/tests/task9-csi2-static.sh" \
	"$ROOT/tests/task10-production-static.sh" "$ROOT/load-ipu4.sh" "$ROOT/modprobe.d"; then
	echo 'task11-static: hard-coded or service-mutating production helper found' >&2
	exit 1
fi

echo 'task11-static: PASS'
