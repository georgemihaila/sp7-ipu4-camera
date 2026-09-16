#!/bin/sh
# Deterministic, hardware-independent checks for Task 9's CSI-2 error paths.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CSI="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu-isys-csi2.c"
HDR="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu-isys-csi2.h"
VIDEO="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu-isys-video.c"
QUEUE="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu-isys-queue.c"
IPU4P="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu4/ipu4p-isys-csi2.c"

grep -q 'IPU_ISYS_CSI2_FATAL_ERRORS' "$HDR"
grep -q 'ipu_isys_csi2_reset_errors(csi2)' "$CSI"
grep -q 'rval = ipu_isys_csi2_set_stream(sd, timing, nlanes, enable)' "$CSI"
grep -q 'rval = ipu_isys_csi2_error(ip->csi2)' "$VIDEO"
grep -q 'dev_warn_ratelimited' "$QUEUE"
grep -q 'status & IPU_ISYS_CSI2_FATAL_ERRORS' "$IPU4P"
grep -q 'fatal_receiver_errors = 0' "$IPU4P"

echo 'task9-csi2-static: PASS'
