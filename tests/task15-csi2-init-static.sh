#!/bin/sh
# Regression check for CSI-2 metadata-format lookups during subdev init.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CSI="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu-isys-csi2.c"

check_guarded_lookup() {
	function_name=$1
	body=$(sed -n "/^static .*${function_name}(/,/^}/p" "$CSI")
	guard=$(printf '%s\n' "$body" | grep -nF 'if (!sd->entity.graph_obj.mdev)' | head -n 1 | cut -d: -f1)
	lookup=$(printf '%s\n' "$body" | grep -nF 'media_pad_remote_pad_first(' | head -n 1 | cut -d: -f1)
	[ -n "$guard" ] && [ -n "$lookup" ] && [ "$guard" -lt "$lookup" ]
}

check_guarded_lookup get_metadata_fmt
check_guarded_lookup csi2_set_ffmt

# The init-time metadata set-format precedes subdev registration, so the guard
# must keep that initialization path away from the not-yet-created links list.
init_meta=$(grep -nF '__ipu_isys_subdev_set_ffmt(&csi2->asd.sd, NULL, &fmt_meta);' "$CSI" | cut -d: -f1)
register=$(grep -nF 'v4l2_device_register_subdev(&isys->v4l2_dev, &csi2->asd.sd)' "$CSI" | cut -d: -f1)
[ -n "$init_meta" ] && [ -n "$register" ] && [ "$init_meta" -lt "$register" ]

echo 'task15-csi2-init-static: PASS'
