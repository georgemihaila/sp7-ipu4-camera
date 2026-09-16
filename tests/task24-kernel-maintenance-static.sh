#!/bin/sh
# Static checks for the narrow Task 9 runtime-PM logging maintenance change.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SOURCE="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu-isys.c"

test -f "$SOURCE"

# These are diagnostic breadcrumbs, not state transitions or failures. Keep
# them behind dynamic debug so routine camera power cycling is quiet by
# default while the exact trace remains available when debugging is enabled.
for message in \
	'trace isys runtime resume: begin\n' \
	'trace isys runtime resume: complete\n' \
	'trace isys runtime suspend: begin\n' \
	'trace isys runtime suspend: complete\n'; do
	[ "$(grep -Fc "dev_dbg(dev, \"$message\");" "$SOURCE")" -eq 1 ]
	[ "$(grep -Fc "dev_info(dev, \"$message\");" "$SOURCE")" -eq 0 ]
done

# Preserve the runtime-PM hooks and the existing failure path while checking
# that the maintenance change did not alter any control-flow operation.
grep -Fq '.runtime_suspend = isys_runtime_pm_suspend' "$SOURCE"
grep -Fq '.runtime_resume = isys_runtime_pm_resume' "$SOURCE"
grep -Fq 'ret = ipu4_buttress_start_tsc_sync(isp);' "$SOURCE"
grep -Fq 'if (ret)' "$SOURCE"
grep -Fq 'return ret;' "$SOURCE"

printf '%s\n' 'task24-kernel-maintenance-static: PASS'
