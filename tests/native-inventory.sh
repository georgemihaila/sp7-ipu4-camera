#!/bin/sh
# Phase 0 native camera inventory.
#
# This is intentionally read-only and best-effort. It discovers the current
# media, V4L2, module, firmware, libcamera, and PipeWire state without assuming
# a particular video/media/sub-device minor number. Missing hardware or tools
# are reported as SKIP and do not make the inventory fail.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
MODE=live
OUT=${NATIVE_INVENTORY_DIR:-${OUTPUT_DIR:-}}

say() { printf '%s\n' "$*"; }
record() {
	say "$*"
	printf '%s\n' "$*" >>"$REPORT"
}
skip() { record "SKIP [$1] $2"; }

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
	say "PASS [native-inventory-static] script syntax and contract are checked by the static suite"
	exit 0
fi

if [ -z "$OUT" ]; then
	OUT=$(mktemp -d "${TMPDIR:-/tmp}/sp7-native-inventory.XXXXXX") || exit 1
	KEEP=0
else
	KEEP=1
	if ! mkdir -p "$OUT"; then
		say "FAIL [native-inventory] cannot create report directory: $OUT" >&2
		exit 1
	fi
fi

REPORT=$OUT/inventory.txt
: >"$REPORT" || exit 1
record "SP7 native camera inventory"
record "timestamp: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
record "root: $ROOT"
record "kernel: $(uname -srvm 2>/dev/null || true)"
record "report directory: $OUT"

capture() {
	label=$1
	shift
	file=$OUT/$label.txt
	record "COMMAND [$label] $*"
	if "$@" >"$file" 2>&1; then
		record "STATUS [$label] 0; output: $file"
	else
		status=$?
		record "STATUS [$label] $status; output: $file"
	fi
}

record ""
record "[media graph and dynamically discovered nodes]"
media_count=0
if command -v media-ctl >/dev/null 2>&1; then
	for media in /dev/media*; do
		[ -e "$media" ] || continue
		media_count=$((media_count + 1))
		base=${media##*/}
		capture "graph-$base" media-ctl -d "$media" -p
	done
	if [ "$media_count" -eq 0 ]; then
		skip graph "no /dev/media* device was discovered"
	fi
else
	skip graph "media-ctl is unavailable"
fi

if command -v v4l2-ctl >/dev/null 2>&1; then
	capture v4l2-list-devices v4l2-ctl --list-devices
else
	skip v4l2 "v4l2-ctl is unavailable"
fi

node_count=0
for sysnode in /sys/class/video4linux/video* /sys/class/video4linux/v4l-subdev*; do
	[ -e "$sysnode" ] || continue
	base=${sysnode##*/}
	node=/dev/$base
	[ -e "$node" ] || continue
	node_count=$((node_count + 1))
	name=$(cat "$sysnode/name" 2>/dev/null || true)
	index=$(cat "$sysnode/index" 2>/dev/null || true)
	device=$(readlink -f "$sysnode/device" 2>/dev/null || true)
	record "NODE $node name=${name:-unknown} index=${index:-unknown} sysfs_device=${device:-unknown}"

	if command -v udevadm >/dev/null 2>&1; then
		capture "udev-$base" udevadm info --query=property --name="$node"
	fi
	if ! command -v v4l2-ctl >/dev/null 2>&1; then
		continue
	fi
	case $base in
		video*)
			capture "$base-all" v4l2-ctl --device="$node" --all
			capture "$base-formats" v4l2-ctl --device="$node" --list-formats-ext
			;;
		v4l-subdev*)
			capture "$base-controls" v4l2-ctl --device="$node" --list-ctrls-menus
			capture "$base-mbus-codes" v4l2-ctl --device="$node" --list-subdev-mbus-codes pad=0,stream=0
			;;
	esac
done
if [ "$node_count" -eq 0 ]; then
	skip nodes "no usable /sys/class/video4linux nodes were discovered"
fi

record ""
record "[kernel module and firmware provenance]"
capture uname uname -a
if [ -r /proc/cmdline ]; then
	capture kernel-cmdline sh -c 'cat /proc/cmdline'
fi

module_count=0
if command -v lsmod >/dev/null 2>&1; then
	capture loaded-modules lsmod
	if command -v modinfo >/dev/null 2>&1; then
		for module in $(lsmod | awk 'NR > 1 {print $1}' | grep -E '(^|_)(ipu|ov5693|ov8865)(_|$)|^ipu_bridge$' || true); do
			module_count=$((module_count + 1))
			capture "modinfo-$module" modinfo "$module"
			tmp=$OUT/firmware-$module.txt
			modinfo -F firmware "$module" >"$tmp" 2>&1 || :
			while IFS= read -r firmware; do
				[ -n "$firmware" ] || continue
				found=
				for firmware_root in /lib/firmware /usr/lib/firmware; do
					if [ -f "$firmware_root/$firmware" ]; then
						found=$firmware_root/$firmware
						break
					fi
				done
				if [ -n "$found" ]; then
					record "FIRMWARE module=$module requested=$firmware found=$found"
				else
					record "FIRMWARE module=$module requested=$firmware found=NO"
				fi
			done <"$tmp"
		done
	fi
else
	skip modules "lsmod is unavailable"
fi
[ "$module_count" -gt 0 ] || skip modules "no loaded IPU/sensor module matched the provenance filter"

firmware_count=0
for firmware_root in /lib/firmware /usr/lib/firmware; do
	[ -d "$firmware_root" ] || continue
	for firmware in "$firmware_root"/*ipu* "$firmware_root"/*css*; do
		[ -f "$firmware" ] || continue
		firmware_count=$((firmware_count + 1))
		record "FIRMWARE file=$firmware"
	done
done
[ "$firmware_count" -gt 0 ] || skip firmware "no IPU/CSS-named firmware file was found in standard firmware roots"

record ""
record "[native libcamera status]"
if command -v cam >/dev/null 2>&1; then
	capture libcamera-version cam --version
	capture libcamera-cameras cam -l
else
	skip libcamera "cam is unavailable"
fi
if command -v gst-inspect-1.0 >/dev/null 2>&1; then
	capture gstreamer-libcamerasrc gst-inspect-1.0 libcamerasrc
else
	skip libcamera-gstreamer "gst-inspect-1.0 is unavailable"
fi

record ""
record "[native PipeWire/WirePlumber status]"
if command -v pw-cli >/dev/null 2>&1; then
	capture pipewire-nodes pw-cli ls Node
else
	skip pipewire "pw-cli is unavailable"
fi
if command -v wpctl >/dev/null 2>&1; then
	capture wireplumber-status wpctl status
else
	skip wireplumber "wpctl is unavailable"
fi
if command -v systemctl >/dev/null 2>&1; then
	capture pipewire-user-service systemctl --user --no-pager --plain is-active pipewire
	capture wireplumber-user-service systemctl --user --no-pager --plain is-active wireplumber
fi

record ""
record "RESULT inventory complete; hardware-only stream/image quality and application preview checks remain separate"
if [ "$KEEP" -eq 0 ]; then
	record "temporary report retained at $OUT for this invocation"
fi
exit 0
