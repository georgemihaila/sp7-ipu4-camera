#!/bin/sh
# Keep reverse-engineering controls and bring-up logging out of production.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CSI="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu-isys-csi2.c"
IPU4P="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu4/ipu4p-isys-csi2.c"
ISYS="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu4/ipu4-isys.c"
SENSOR="$ROOT/linux-6.19.8/drivers/media/i2c/ov5693.c"

for needle in csi2_fw_src csi2_csettle csi2_dsettle \
	phy_bb_extra phy_afe_extra phy_jsl_bits csi_gpreg_hpll_freq \
	csi_gpreg_isclk_ratio windows_bscan_late windows_source7_mipi_timing; do
	if grep -q "$needle" "$CSI" "$IPU4P" "$ISYS" "$SENSOR"; then
		echo "task10-production-static: forbidden production reference: $needle" >&2
		exit 1
	fi
done

grep -q 'ipu4p_csi2_apply_source7_mipi_timing' "$IPU4P"
grep -q 'ipu4p_isys_get_quirks' "$IPU4P" "$ISYS"
grep -q 'dev_dbg' "$CSI" "$IPU4P" "$ISYS" "$SENSOR"
if grep -n 'dev_info.*trace\|pr_info.*trace' "$CSI" "$IPU4P" "$ISYS" "$SENSOR"; then
	echo 'task10-production-static: bring-up trace remains at normal log level' >&2
	exit 1
fi

echo 'task10-production-static: PASS'
