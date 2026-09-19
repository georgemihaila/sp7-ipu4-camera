#!/usr/bin/env bash
set -Eeuo pipefail

# Run the source-backed source-6 module and the persistent userspace qualifier
# for the hardware gates. This is an explicit diagnostic action, never a
# service-start path and never an installation path.

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_DIR=$(cd -- "$SCRIPT_DIR/../.." && pwd)
MODULE=$SCRIPT_DIR/intel-ipu4p-isys-csi-header-tap-bb8.ko
LINK_HELPER=$SCRIPT_DIR/media-link
QUALIFIER=$REPO_DIR/cbridge/ir-hardware-qualification
MEDIA=${OV7251_MEDIA:-/dev/media0}
BRIDGE_SERVICE=${OV7251_BRIDGE_SERVICE:-sp7-camera-bridge.service}
DURATION=${OV7251_QUALIFY_DURATION:-600}
CYCLES=${OV7251_QUALIFY_CYCLES:-20}
CYCLE_FRAMES=${OV7251_QUALIFY_CYCLE_FRAMES:-5}
BASELINE=0

if [[ ${OV7251_QUALIFY_BASELINE:-0} == 1 ]]; then
	BASELINE=1
fi

OUT_DIR=''
while (($# > 0)); do
	case $1 in
	--baseline)
		BASELINE=1
		shift
		;;
	--)
		shift
		break
		;;
	-*)
		printf 'error: unknown option: %s\n' "$1" >&2
		exit 2
		;;
	*)
		if [[ -n $OUT_DIR ]]; then
			printf 'error: too many positional arguments\n' >&2
			exit 2
		fi
		OUT_DIR=$1
		shift
		;;
	esac
done
if (($# > 0)); then
	if [[ -n $OUT_DIR ]]; then
		printf 'error: too many positional arguments\n' >&2
		exit 2
	fi
	OUT_DIR=$1
	shift
fi
OUT_DIR=${OUT_DIR:-${OV7251_OUTPUT_DIR:-/var/tmp/ov7251-ir-qualification-$(date +%Y%m%d-%H%M%S)}}

die()
{
	printf 'error: %s\n' "$*" >&2
	exit 1
}

[[ -r "$MODULE" ]] || die "bundled debug module is missing: $MODULE"
[[ -x "$LINK_HELPER" ]] || die "media-link helper is missing: $LINK_HELPER"
[[ -x "$QUALIFIER" ]] || die "qualifier is missing; run make -C cbridge qualify-ir"
[[ -c "$MEDIA" ]] || die "media node does not exist: $MEDIA"
command -v sudo >/dev/null || die "sudo is required"
command -v modprobe >/dev/null || die "modprobe is required"
command -v insmod >/dev/null || die "insmod is required"
command -v systemctl >/dev/null || die "systemctl is required"
command -v journalctl >/dev/null || die "journalctl is required"
sudo -n true 2>/dev/null || die "passwordless sudo is required"

mkdir -p "$OUT_DIR"
SETUP_LOG=$OUT_DIR/setup.log
QUALIFY_LOG=$OUT_DIR/qualify.log
KERNEL_LOG=$OUT_DIR/kernel.log
GRAPH_BEFORE=$OUT_DIR/graph-before.txt
GRAPH_AFTER=$OUT_DIR/graph-after.txt
RUN_INFO=$OUT_DIR/run.txt
setup_start=$(date --iso-8601=seconds)
wall_start=$(date +%s.%N)

bridge_was_active=0
base_was_loaded=0
base_removed=0
candidate_loaded=0
sensor_link_enabled=0
capture_link_enabled=0
cleanup_failures=0

systemctl --user is-active --quiet "$BRIDGE_SERVICE" && bridge_was_active=1 || :
sudo -n lsmod | awk '$1 == "intel_ipu4p_isys" { found = 1 } END { exit !found }' && \
	base_was_loaded=1 || :
media-ctl -d "$MEDIA" -p >"$GRAPH_BEFORE" 2>&1 || :
{
	printf 'start=%s\nmode=%s\nmedia=%s\nrequested_duration_seconds=%s\n' \
		"$setup_start" "$([[ $BASELINE -eq 1 ]] && printf baseline || printf normal)" \
		"$MEDIA" "$DURATION"
	printf 'requested_cycles=%s\ncycle_frames=%s\n' "$CYCLES" "$CYCLE_FRAMES"
	printf 'module_configuration=unchanged\nmodule_path=%s\n' "$MODULE"
	printf 'kernel_log=kernel.log\nkernel_log_filter=none\n'
	printf 'kernel_warning_rate_limiting=possible\n'
	printf 'kernel_log_note=counts represent emitted messages only; existing rate-limited kernel paths may suppress repeats\n'
	sha256sum "$MODULE"
	modinfo intel_ipu4p_isys 2>/dev/null | rg '^(filename|vermagic):' || :
} >"$RUN_INFO"

cleanup_failure()
{
	cleanup_failures=$((cleanup_failures + 1))
	printf 'cleanup_failure step=%s\n' "$1" >&2
}

unload_isys()
{
	local attempt
	local last_error=''
	for attempt in 1 2 3 4 5 6 7 8; do
		if last_error=$(sudo -n modprobe -r intel_ipu4p_isys 2>&1); then
			return 0
		fi
		sleep 1
	done
	printf '%s\n' "$last_error" >&2
	return 1
}

read_kernel_log()
{
	if ! journalctl -k -b -o short-monotonic --since "$setup_start" >"$KERNEL_LOG" 2>/dev/null; then
		sudo -n journalctl -k -b -o short-monotonic --since "$setup_start" >"$KERNEL_LOG" 2>/dev/null || :
	fi
}

cleanup()
{
	local original_rc=$?
	local cleanup_rc=0
	local software_restored=1
	local wall_end
	trap - EXIT
	set +e

	if [[ $capture_link_enabled -eq 1 ]]; then
		if ! sudo -n "$LINK_HELPER" "$MEDIA" \
			'Intel IPU4 CSI-2 1' 1 'Intel IPU4 CSI-2 1 capture 0' 0 off; then
			cleanup_rc=1
			cleanup_failure disable_capture_link
		fi
	fi
	if [[ $sensor_link_enabled -eq 1 ]]; then
		if ! sudo -n "$LINK_HELPER" "$MEDIA" \
			'ov7251 2-0060' 0 'Intel IPU4 CSI-2 1' 0 off; then
			cleanup_rc=1
			cleanup_failure disable_sensor_link
		fi
	fi
	if [[ $candidate_loaded -eq 1 ]]; then
		if ! unload_isys; then
			cleanup_rc=1
			cleanup_failure unload_candidate_module
			software_restored=0
			printf 'error: source-backed module remains loaded\n' >&2
		fi
	fi
	if [[ $base_was_loaded -eq 1 && $base_removed -eq 1 ]]; then
		if [[ $software_restored -eq 1 ]]; then
			if ! sudo -n modprobe intel_ipu4p_isys; then
				cleanup_rc=1
				cleanup_failure restore_distribution_module
				software_restored=0
			fi
		else
			cleanup_rc=1
			cleanup_failure restore_distribution_module
		fi
	fi
	if [[ $bridge_was_active -eq 1 && $software_restored -eq 1 ]]; then
		if ! systemctl --user start "$BRIDGE_SERVICE"; then
			cleanup_rc=1
			cleanup_failure restart_bridge
		fi
	elif [[ $bridge_was_active -eq 1 && $software_restored -eq 0 ]]; then
		printf 'error: bridge remains stopped because restoration failed\n' >&2
	fi
	media-ctl -d "$MEDIA" -p >"$GRAPH_AFTER" 2>&1 || :
	read_kernel_log
	wall_end=$(date +%s.%N)
	{
		printf 'actual_wall_duration_seconds=%.3f\n' \
			"$(awk -v start="$wall_start" -v end="$wall_end" 'BEGIN { print end - start }')"
		printf 'cleanup_failures=%s\ncleanup_result=%s\n' "$cleanup_failures" \
			"$([[ $cleanup_failures -eq 0 ]] && printf PASS || printf FAIL)"
		printf 'physical_csi_measurement=NOT_PERFORMED\n'
		printf 'phy_changes=NONE\ndesktop_integration=DEFERRED\n'
	} >>"$RUN_INFO"

	if [[ $original_rc -eq 0 && $cleanup_rc -ne 0 ]]; then
		printf 'error: qualification completed but restoration failed; inspect %s\n' \
			"$SETUP_LOG" >&2
		original_rc=$cleanup_rc
	fi
	exit "$original_rc"
}
trap cleanup EXIT

if [[ $bridge_was_active -eq 1 ]]; then
	systemctl --user stop "$BRIDGE_SERVICE" || die "could not stop $BRIDGE_SERVICE"
fi
if [[ $base_was_loaded -eq 1 ]]; then
	unload_isys || die "could not unload distribution intel_ipu4p_isys"
	base_removed=1
fi

sudo -n modprobe -a videobuf2-dma-contig videobuf2-v4l2 intel-ipu4p-isys-csslib || \
	die "could not load IPU4 capture dependencies"
sudo -n insmod "$MODULE" debug_capture_links=1 >"$SETUP_LOG" 2>&1 || \
	die "could not load source-backed module"
candidate_loaded=1

sudo -n "$LINK_HELPER" "$MEDIA" \
	'ov7251 2-0060' 0 'Intel IPU4 CSI-2 1' 0 on >>"$SETUP_LOG" 2>&1 || \
	die "could not enable OV7251-to-CSI link"
sensor_link_enabled=1
sudo -n "$LINK_HELPER" "$MEDIA" \
	'Intel IPU4 CSI-2 1' 1 'Intel IPU4 CSI-2 1 capture 0' 0 on >>"$SETUP_LOG" 2>&1 || \
	die "could not enable CSI-to-direct-capture link"
capture_link_enabled=1

media-ctl -d "$MEDIA" -p >>"$SETUP_LOG" 2>&1 || :
QUALIFIER_ARGS=(--duration "$DURATION" --cycles "$CYCLES" \
	--cycle-frames "$CYCLE_FRAMES")
if [[ $BASELINE -eq 1 ]]; then
	QUALIFIER_ARGS+=(--baseline)
fi
set +e
sudo -n "$QUALIFIER" "${QUALIFIER_ARGS[@]}" >"$QUALIFY_LOG" 2>&1
qualify_rc=$?
set -e
printf 'qualify_rc=%s\nend=%s\n' "$qualify_rc" "$(date --iso-8601=seconds)" >>"$RUN_INFO"
if [[ $qualify_rc -ne 0 ]]; then
	cat "$QUALIFY_LOG" >&2
	exit "$qualify_rc"
fi
printf 'qualification completed; restoring links and software module\n'
