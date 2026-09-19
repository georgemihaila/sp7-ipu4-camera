#!/usr/bin/env bash
set -Eeuo pipefail

# Load the verified, kernel-specific source-6 debug tap, initialize BB8, enable
# the two direct-capture links, run the decoder, then restore the distribution
# module and the bridge service. This is intentionally not run by default.

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
MODULE=$SCRIPT_DIR/intel-ipu4p-isys-csi-header-tap-bb8.ko
LINK_HELPER=$SCRIPT_DIR/media-link
CAPTURE=$SCRIPT_DIR/capture-ov7251-ir-frame.sh
MEDIA=${OV7251_MEDIA:-/dev/media0}
DEVICE=${OV7251_DEVICE:-/dev/video5}
BRIDGE_SERVICE=${OV7251_BRIDGE_SERVICE:-sp7-camera-bridge.service}
OUT_DIR=${1:-${OV7251_OUTPUT_DIR:-/var/tmp/ov7251-ir-capture-$(date +%Y%m%d-%H%M%S)}}

usage() {
	cat <<'EOF'
Usage: scripts/ir/setup-and-capture.sh [OUTPUT_DIR]

This performs the temporary source-6 diagnostic setup, captures one decoded
OV7251 frame, and restores the software module/bridge state on exit. It:

  - stops the active user bridge service, if it was active;
  - replaces the distribution ISYS module with the bundled BB8 debug module;
  - enables the OV7251 -> CSI-2 1 -> direct-capture links;
  - runs capture-ov7251-ir-frame.sh;
  - verifies the expected BB8 readback emitted when receiver streaming starts;
  - disables those links, reloads the distribution module, and restarts the
    bridge if it was active.

The bundled module is only for kernel 6.19.8-3.surface.fc43.x86_64. This
script does not perform a hardware power-cycle or claim BB8 register rollback.

Environment overrides: OV7251_MEDIA, OV7251_DEVICE, OV7251_BRIDGE_SERVICE,
OV7251_OUTPUT_DIR.
EOF
}

if [[ ${1:-} == '-h' || ${1:-} == '--help' ]]; then
	usage
	exit 0
fi
if [[ $# -gt 1 ]]; then
	usage >&2
	exit 2
fi

die() {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

[[ -r "$MODULE" ]] || die "bundled debug module is missing: $MODULE"
[[ -x "$LINK_HELPER" ]] || die "bundled media-link helper is missing: $LINK_HELPER"
[[ -x "$CAPTURE" ]] || die "capture script is missing or not executable: $CAPTURE"
[[ -c "$MEDIA" ]] || die "media node does not exist: $MEDIA"
[[ -c "$DEVICE" ]] || die "capture node does not exist: $DEVICE"
command -v sudo >/dev/null || die "sudo is required"
command -v modprobe >/dev/null || die "modprobe is required"
command -v insmod >/dev/null || die "insmod is required"
command -v systemctl >/dev/null || die "systemctl is required"
command -v journalctl >/dev/null || die "journalctl is required"
sudo -n true 2>/dev/null || die "passwordless sudo is required; rerun with sudo credentials available"

mkdir -p "$OUT_DIR"
SETUP_LOG=$OUT_DIR/setup.log
setup_start=$(date --iso-8601=seconds)

bridge_was_active=0
base_was_loaded=0
base_removed=0
candidate_loaded=0
candidate_removed=0
sensor_link_enabled=0
capture_link_enabled=0

if systemctl --user is-active --quiet "$BRIDGE_SERVICE"; then
	bridge_was_active=1
fi
if sudo -n lsmod | awk '$1 == "intel_ipu4p_isys" { found = 1 } END { exit !found }'; then
	base_was_loaded=1
fi

read_kernel_log() {
	if journalctl -k -b -o short-monotonic --since "$setup_start" >"$SETUP_LOG" 2>/dev/null; then
		return 0
	fi
	sudo -n journalctl -k -b -o short-monotonic --since "$setup_start" >"$SETUP_LOG" 2>/dev/null || :
}

unload_isys() {
	local last_error=''
	local attempt
	for attempt in 1 2 3 4 5 6 7 8; do
		if last_error=$(sudo -n modprobe -r intel_ipu4p_isys 2>&1); then
			return 0
		fi
		sleep 1
	done
	printf '%s\n' "$last_error" >&2
	return 1
}

cleanup() {
	local original_rc=$?
	local cleanup_rc=0
	local software_restored=1
	trap - EXIT
	set +e

	if [[ $capture_link_enabled -eq 1 ]]; then
		sudo -n "$LINK_HELPER" "$MEDIA" \
			'Intel IPU4 CSI-2 1' 1 'Intel IPU4 CSI-2 1 capture 0' 0 off || cleanup_rc=1
	fi
	if [[ $sensor_link_enabled -eq 1 ]]; then
		sudo -n "$LINK_HELPER" "$MEDIA" \
			'ov7251 2-0060' 0 'Intel IPU4 CSI-2 1' 0 off || cleanup_rc=1
	fi
	if [[ $candidate_loaded -eq 1 ]]; then
		if unload_isys; then
			candidate_removed=1
		else
			cleanup_rc=1
			software_restored=0
			printf 'error: debug module remains loaded; refusing to restart the bridge on it\n' >&2
		fi
	fi
	if [[ $base_was_loaded -eq 1 && $base_removed -eq 1 ]]; then
		if [[ $candidate_removed -eq 1 ]]; then
			if ! sudo -n modprobe intel_ipu4p_isys; then
				cleanup_rc=1
				software_restored=0
			fi
		else
			cleanup_rc=1
			software_restored=0
		fi
	fi
	if [[ $bridge_was_active -eq 1 && $software_restored -eq 1 ]]; then
		systemctl --user start "$BRIDGE_SERVICE" || cleanup_rc=1
	elif [[ $bridge_was_active -eq 1 && $software_restored -eq 0 ]]; then
		printf 'error: bridge remains stopped because software restoration is incomplete\n' >&2
	fi

	if [[ $original_rc -eq 0 && $cleanup_rc -ne 0 ]]; then
		printf 'error: capture completed but restoration failed; inspect %s\n' "$SETUP_LOG" >&2
		original_rc=$cleanup_rc
	fi
	exit "$original_rc"
}
trap cleanup EXIT

if [[ $bridge_was_active -eq 1 ]]; then
	systemctl --user stop "$BRIDGE_SERVICE" || die "could not stop $BRIDGE_SERVICE"
fi
if [[ $base_was_loaded -eq 1 ]]; then
	unload_isys || die "could not unload distribution intel_ipu4p_isys; inspect active camera users"
	base_removed=1
fi

sudo -n modprobe -a videobuf2-dma-contig videobuf2-v4l2 intel-ipu4p-isys-csslib || \
	die "could not load IPU4 capture dependencies"
sudo -n insmod "$MODULE" debug_capture_links=1 || \
	die "could not load the bundled source-6 debug module"
candidate_loaded=1

sudo -n "$LINK_HELPER" "$MEDIA" \
	'ov7251 2-0060' 0 'Intel IPU4 CSI-2 1' 0 on || \
	die "could not enable the OV7251-to-CSI link"
sensor_link_enabled=1
sudo -n "$LINK_HELPER" "$MEDIA" \
	'Intel IPU4 CSI-2 1' 1 'Intel IPU4 CSI-2 1 capture 0' 0 on || \
	die "could not enable the CSI-to-direct-capture link"
capture_link_enabled=1

set +e
OV7251_MEDIA=$MEDIA OV7251_DEVICE=$DEVICE \
	"$CAPTURE" "$OUT_DIR"
capture_rc=$?
set -e

read_kernel_log
grep -Fq 'source-6 BB8 init:' "$SETUP_LOG" || \
	die "BB8 initialization was not reported after receiver start; see $SETUP_LOG"
grep -Fq 'after=(0x1001b,0x41,0x44104015)' "$SETUP_LOG" || \
	die "BB8 readback does not match the verified candidate; see $SETUP_LOG"

if [[ $capture_rc -ne 0 ]]; then
	exit "$capture_rc"
fi

printf 'capture finished; restoring links, module, and bridge state\n'
