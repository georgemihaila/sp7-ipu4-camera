#!/bin/bash
# Capture rear and front streams while preserving the kernel initialization
# trace emitted by the instrumented OV5693/IPU4P drivers.
set -u

HERE=$(dirname "$(readlink -f "$0")")
STAMP=$(date +%Y%m%d-%H%M%S)
OUT=${TRACE_OUT:-"$HERE/reports/init-trace-$STAMP"}
TIMEOUT=${CAPTURE_TIMEOUT:-35}
mkdir -p "$OUT/captures"

START=$(date --iso-8601=seconds)
{
	echo "trace_start=$START"
	date --iso-8601=seconds
	un=$(uname -a); echo "$un"
	for m in intel_ipu4p_isys intel_ipu4p ov5693; do
		echo "--- modinfo $m ---"
		modinfo "$m" 2>&1 | grep -E '^(filename|vermagic|srcversion|parm:)' || true
	done
	echo "--- module parameters ---"
	for p in /sys/module/intel_ipu4p_isys/parameters/*; do
		[ -r "$p" ] || continue
		case "$p" in
			*csi2*|*phy*|*windows*|*front*) echo "$(basename "$p")=$(cat "$p" 2>/dev/null || echo '<unreadable>')";;
		esac
	done
	echo "--- sensor runtime state ---"
	for p in /sys/bus/i2c/devices/i2c-INT33BE:00/power/runtime_status \
		/sys/bus/i2c/devices/i2c-INT33BE:00/power/control; do
		[ -r "$p" ] && echo "$p=$(cat "$p")"
	done
} > "$OUT/baseline.txt"

for cam in rear front; do
	echo "capturing $cam; trace is in $OUT"
	CAM_START=$(date --iso-8601=seconds)
	OUTPUT_DIR="$OUT/captures" CAPTURE_TIMEOUT="$TIMEOUT" \
		"$HERE/test-capture.sh" "$cam" > "$OUT/$cam.stdout" 2>&1
	RC=$?
	CAM_END=$(date --iso-8601=seconds)
	echo "$cam rc=$RC start=$CAM_START end=$CAM_END" | tee "$OUT/$cam.result"
	journalctl -k -b --no-pager --since "$START" > "$OUT/kernel.log" 2>/dev/null || true
done

journalctl -k -b --no-pager --since "$START" > "$OUT/kernel.log" 2>/dev/null || true
{
	echo "--- capture files ---"
	find "$OUT/captures" -maxdepth 1 -type f -printf '%f %s bytes\n' | sort
	echo "--- trace lines ---"
	grep -E 'trace (power|sensor|phy|isys|csi)|DPHY|Frame sync|no frames' \
		"$OUT/kernel.log" || true
} > "$OUT/summary.txt"

echo "Trace bundle: $OUT"
