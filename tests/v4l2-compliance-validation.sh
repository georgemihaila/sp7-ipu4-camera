#!/bin/sh
# Read-only Phase 5 V4L2 compliance validation.
#
# Discover video nodes and their owning sysfs driver/module. Only physical,
# module-backed capture nodes are tested; virtual loopback/application-bridge
# nodes are excluded by their discovered identity.
set -u

MODE=live
TIMEOUT=${CAMERA_TEST_TIMEOUT:-20}
OUT=${V4L2_COMPLIANCE_DIR:-${OUTPUT_DIR:-}}

say() { printf '%s\n' "$*"; }
record() {
	say "$*"
	printf '%s\n' "$*" >>"$REPORT"
}
skip() { record "SKIP [$1] $2"; }
pass() { record "PASS [$1] $2"; }
fail() { record "FAIL [$1] $2"; FAILED=1; }

while [ "$#" -gt 0 ]; do
	case $1 in
		--live) MODE=live ;;
		--static) MODE=static ;;
		--help|-h)
			say "usage: $0 [--live|--static]"
			exit 0
			;;
		*) say "unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done

if [ "$MODE" = static ]; then
	say "PASS [v4l2-compliance-static] script syntax and contract are checked by the static suite"
	exit 0
fi

case $TIMEOUT in
	''|*[!0-9]*) say "invalid CAMERA_TEST_TIMEOUT: $TIMEOUT" >&2; exit 2 ;;
esac
[ "$TIMEOUT" -gt 0 ] 2>/dev/null || {
	say "invalid CAMERA_TEST_TIMEOUT: $TIMEOUT (expected a positive integer)" >&2
	exit 2
}

KEEP=1
if [ -z "$OUT" ]; then
	OUT=$(mktemp -d "${TMPDIR:-/tmp}/sp7-v4l2-compliance.XXXXXX") || exit 1
	KEEP=0
else
	if ! mkdir -p "$OUT"; then
		say "FAIL [v4l2-compliance] cannot create report directory: $OUT" >&2
		exit 1
	fi
fi

REPORT=$OUT/compliance.txt
: >"$REPORT" || exit 1
FAILED=0
TESTED=0

cleanup() {
	status=$?
	trap - EXIT HUP INT TERM
	if [ "$KEEP" -eq 0 ] && [ "$FAILED" -eq 0 ]; then
		rm -rf "$OUT"
	fi
	exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

missing=
for tool in v4l2-compliance v4l2-ctl timeout readlink awk cat grep mkdir mktemp; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		missing=${missing:+$missing, }$tool
	fi
done
if [ -n "$missing" ]; then
	skip tools "required read-only V4L2 tools are unavailable: $missing"
	exit 0
fi

record "SP7 V4L2 compliance validation (read-only)"
record "timestamp: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
record "kernel: $(uname -srvm 2>/dev/null || true)"
record "report directory: $OUT"
v4l2-compliance --version >"$OUT/v4l2-compliance-version.txt" 2>&1 || :

node_count=0
for sysnode in /sys/class/video4linux/video*; do
	[ -e "$sysnode" ] || continue
	base=${sysnode##*/}
	node=/dev/$base
	node_count=$((node_count + 1))
	name=$(cat "$sysnode/name" 2>/dev/null || true)
	sys_device=$(readlink -f "$sysnode/device" 2>/dev/null || true)
	driver_path=$(readlink -f "$sysnode/device/driver" 2>/dev/null || true)
	module_path=$(readlink -f "$sysnode/device/driver/module" 2>/dev/null || true)
	driver_name=${driver_path##*/}
	module_name=${module_path##*/}
	node_dir=$OUT/$base
	mkdir -p "$node_dir" || { fail "$base" "cannot create diagnostics directory"; continue; }

	record "NODE $node name=${name:-unknown} sysfs_device=${sys_device:-unknown} driver=${driver_name:-unknown} module=${module_name:-unknown}"

	# The physical/virtual and loopback/application-bridge boundary is based on
	# discovered sysfs and driver identity, never a fixed video minor.
	if [ -z "$driver_name" ] || [ -z "$module_name" ]; then
		skip "$base" "node has no discovered kernel driver/module identity"
		continue
	fi
	case "$sys_device:$driver_name:$module_name:$name" in
		/sys/devices/virtual/*:*|*v4l2loopback*|*cbridge*|*loopback*)
			skip "$base" "excluded virtual loopback/application-bridge identity"
			continue
			;;
	esac

	# Record capabilities and formats for every module-backed node considered.
	if v4l2-ctl --device="$node" --all >"$node_dir/capabilities.txt" 2>&1; then
		record "CAPABILITIES $node output=$node_dir/capabilities.txt status=0"
	else
		status=$?
		record "CAPABILITIES $node output=$node_dir/capabilities.txt status=$status"
	fi
	if v4l2-ctl --device="$node" --list-formats-ext >"$node_dir/formats.txt" 2>&1; then
		record "FORMATS $node output=$node_dir/formats.txt status=0"
	else
		status=$?
		record "FORMATS $node output=$node_dir/formats.txt status=$status"
	fi

	if ! grep -Eiq '(^|[[:space:]])Video Capture([[:space:]]+Multiplanar)?([[:space:]]|$)' "$node_dir/capabilities.txt"; then
		skip "$base" "discovered driver-owned node is not a V4L2 video-capture queue"
		continue
	fi

	# modinfo and firmware lookup are read-only provenance evidence.
	record "PROVENANCE $node driver=$driver_name module=$module_name sysfs_module=$module_path"
	if modinfo "$module_name" >"$node_dir/modinfo.txt" 2>&1; then
		record "MODULE $node output=$node_dir/modinfo.txt status=0"
	else
		record "SKIP [$base] modinfo could not read module metadata for $module_name"
	fi
	if modinfo -F firmware "$module_name" >"$node_dir/firmware-requested.txt" 2>&1; then
		while IFS= read -r firmware; do
			[ -n "$firmware" ] || continue
			found=
			for firmware_root in /lib/firmware /usr/lib/firmware; do
				candidate=$firmware_root/$firmware
				if [ -f "$candidate" ]; then
					found=$candidate
					break
				fi
			done
			if [ -n "$found" ]; then
				record "FIRMWARE $node requested=$firmware found=$found"
			else
				record "FIRMWARE $node requested=$firmware found=NO"
			fi
		done <"$node_dir/firmware-requested.txt"
	else
		record "SKIP [$base] module firmware metadata is unavailable"
	fi

	TESTED=$((TESTED + 1))
	if timeout --signal=TERM --kill-after=2 "$TIMEOUT" \
		v4l2-compliance --device="$node" >"$node_dir/compliance.txt" 2>&1; then
		pass "$base" "v4l2-compliance passed for $node; diagnostics=$node_dir"
	else
		status=$?
		fail "$base" "v4l2-compliance failed for actually tested $node (status=$status; diagnostics=$node_dir)"
	fi
done

if [ "$node_count" -eq 0 ]; then
	skip nodes "no /sys/class/video4linux/video* nodes were discovered"
elif [ "$TESTED" -eq 0 ] && [ "$FAILED" -eq 0 ]; then
	skip nodes "no physical driver-owned video-capture queue remained after sysfs filtering"
fi

if [ "$FAILED" -ne 0 ]; then
	record "RESULT: FAIL (one or more actually tested nodes failed v4l2-compliance)"
	exit 1
fi
if [ "$TESTED" -eq 0 ]; then
	record "RESULT: SKIP (no node was tested)"
	exit 0
fi
record "RESULT: PASS ($TESTED driver-owned capture node(s) passed v4l2-compliance; skipped nodes are not compliance evidence)"
exit 0
