#!/bin/sh
# Reproducible Task 11 validation entry point.
#   camera-suite.sh [--static|--live|--all] [--pm-safe]
# Static checks are the default. Live mode only inspects/uses currently loaded
# devices; it never loads/unloads modules, reboots, changes services, or writes
# kernel logs. Graph setup is transient and restored by the next media reset.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$ROOT/tests/capture-validation.sh"
MODE=static
PM_SAFE=0
TIMEOUT=${CAMERA_TEST_TIMEOUT:-20}
REPEATS=${CAMERA_TEST_REPEATS:-2}
say() { printf '%s\n' "$*"; }
pass() { say "PASS [$1] $2"; }
skip() { say "SKIP [$1] $2"; }
FAILED=0
fail() { say "FAIL [$1] $2 (diagnostics: $LOGDIR)"; FAILED=$((FAILED + 1)); }
run() {
	stage=$1; shift
	log="$LOGDIR/$stage.log"
	if timeout --signal=TERM --kill-after=2 "$TIMEOUT" "$@" >"$log" 2>&1; then
		pass "$stage" "$*"
		return 0
	fi
	fail "$stage" "$*; $(tail -n 3 "$log" 2>/dev/null | tr '\n' ' ')"
	return 1
}

while [ "$#" -gt 0 ]; do
	case $1 in
		--static) MODE=static ;;
		--live) MODE=live ;;
		--all) MODE=all ;;
		--pm-safe) PM_SAFE=1 ;;
		--help|-h) say "usage: $0 [--static|--live|--all] [--pm-safe]"; exit 0 ;;
		*) say "unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done

case $REPEATS in
	''|*[!0-9]*) say "invalid CAMERA_TEST_REPEATS: $REPEATS (expected a positive integer)" >&2; exit 2 ;;
esac
while [ "${REPEATS#0}" != "$REPEATS" ]; do REPEATS=${REPEATS#0}; done
if [ -z "$REPEATS" ] || ! [ "$REPEATS" -gt 0 ] 2>/dev/null; then
	say "invalid CAMERA_TEST_REPEATS: ${CAMERA_TEST_REPEATS:-2} (expected a positive integer)" >&2
	exit 2
fi

LOGDIR=$(mktemp -d "${TMPDIR:-/tmp}/sp7-camera-suite.XXXXXX")
cleanup() {
	status=$?
	trap - EXIT HUP INT TERM
	if [ "$FAILED" -gt 0 ]; then
		say "diagnostics retained: $LOGDIR"
	else
		rm -rf "$LOGDIR"
	fi
	exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

if [ "$MODE" = static ] || [ "$MODE" = all ]; then
	for test in "$ROOT"/tests/task8-camera-static.sh \
		"$ROOT"/tests/task9-csi2-static.sh \
		"$ROOT"/tests/task10-production-static.sh \
		"$ROOT"/tests/task11-static.sh \
		"$ROOT"/tests/task12-libcamera-static.sh \
		"$ROOT"/tests/task13-hardening-static.sh \
		"$ROOT"/tests/task14-v4l2-streams-static.sh \
			"$ROOT"/tests/task15-csi2-init-static.sh \
			"$ROOT"/tests/task16-camera-bridge-format-static.sh \
			"$ROOT"/tests/task17-installer-static.sh \
			"$ROOT"/tests/task18-cbridge-static.sh \
			"$ROOT"/tests/task19-camera-controller-static.sh \
			"$ROOT"/tests/task20-adaptive-polling-static.sh \
			"$ROOT"/tests/task21-filler-pipeline-static.sh \
			"$ROOT"/tests/task22-wireplumber-static.sh \
			"$ROOT"/tests/task23-cbridge-integration-static.sh \
			"$ROOT"/tests/task24-kernel-maintenance-static.sh \
			"$ROOT"/tests/task25-cbridge-live-static.sh \
			"$ROOT"/tests/task26-cbridge-parent-death-static.sh; do
		if ! run "static-$(basename "$test" .sh)" sh "$test"; then :; fi
	done
fi

if [ "$MODE" = live ] || [ "$MODE" = all ]; then
	if ! command -v media-ctl >/dev/null 2>&1 || ! command -v v4l2-ctl >/dev/null 2>&1 ||
		! command -v timeout >/dev/null 2>&1; then
		skip autoload "media-ctl, v4l2-ctl, and timeout are required"
	else
		MEDIA=""
		for candidate in /dev/media*; do
			[ -e "$candidate" ] || continue
			if media-ctl -d "$candidate" -p >"$LOGDIR/graph.$(basename "$candidate")" 2>&1; then
				MEDIA=$candidate; break
			fi
		done
		if [ -z "$MEDIA" ]; then
			skip graph "no readable media-controller device was discovered"
		else
			pass graph "discovered $MEDIA"
			GRAPH="$LOGDIR/graph"
			media-ctl -d "$MEDIA" -p >"$GRAPH" 2>&1
			if grep -q 'ov5693' "$GRAPH"; then pass discovery "ov5693 discovered"; else skip discovery "front ov5693 is absent"; fi
			if grep -q 'ov8865' "$GRAPH"; then pass discovery "ov8865 discovered"; else skip discovery "rear ov8865 is absent"; fi

			# Read-only prerequisites: report the installed module/firmware state.
			if command -v modinfo >/dev/null 2>&1 && modinfo intel_ipu4p >"$LOGDIR/modinfo" 2>&1; then
				pass autoload "intel_ipu4p module metadata and PCI alias available"
			else skip autoload "intel_ipu4p module metadata unavailable"; fi
			if grep -q 'ipu4p_cpd' "$LOGDIR/modinfo" 2>/dev/null &&
				find /lib/firmware /usr/lib/firmware -type f -name 'ipu4p_cpd*' -print -quit 2>/dev/null | grep -q .; then
				pass firmware "ipu4p_cpd firmware is discoverable"
			else skip firmware "ipu4p_cpd firmware is not visible to this environment"; fi

			for sensor in ov5693 ov8865; do
				if ! grep -q "$sensor" "$GRAPH"; then continue; fi
				# Extract the sensor's dynamically assigned sub-device node.
				subdev=$(awk -v s="$sensor" '$0 ~ "- entity .*: " s {hit=1} hit && /device node name/ {print $NF; exit}' "$GRAPH")
				[ -n "$subdev" ] || { fail discovery "$sensor has no sub-device node"; continue; }
				if run "control-$sensor" v4l2-ctl -d "$subdev" --list-ctrls-menus &&
					run "format-$sensor" v4l2-ctl -d "$subdev" --list-subdev-mbus-codes pad=0,stream=0 &&
					run "interval-$sensor" v4l2-ctl -d "$subdev" --get-subdev-fps pad=0,stream=0; then :; fi
				code=$(awk '/MEDIA_BUS_FMT_/ {print $1; exit}' "$LOGDIR/format-$sensor.log" 2>/dev/null)
				if [ -n "$code" ]; then
					if [ "$sensor" = ov5693 ]; then size=2592x1944; else size=3264x2448; fi
					width=${size%x*}; height=${size#*x}
					run "size-$sensor" v4l2-ctl -d "$subdev" --list-subdev-framesizes "pad=0,stream=0,code=$code"
					run "intervals-$sensor" v4l2-ctl -d "$subdev" --list-subdev-frameintervals "pad=0,stream=0,width=$width,height=$height,code=$code"
				else
					skip "size-$sensor" "no enumerated media-bus code"
				fi
				if grep -q 'V4L2_CID_LINK_FREQ\|link_freq' "$LOGDIR/control-$sensor.log"; then pass "link-$sensor" "link-frequency control is exposed"; else skip "link-$sensor" "driver does not expose a readable link-frequency control"; fi
				if grep -q 'CSI-2' "$GRAPH"; then pass "lane-$sensor" "CSI-2 graph endpoint discovered (lane count is validated by static contract where sensor API is unavailable)"; fi
			done

			if [ "${EUID:-$(id -u)}" -ne 0 ]; then skip stream "root privileges may be required for graph setup/capture"; else
				RECOVERY_CAPTURE_OK=0
				for sensor in ov5693 ov8865; do
					grep -q "$sensor" "$GRAPH" || continue
					cam=$([ "$sensor" = ov5693 ] && echo front || echo rear)
					SENSOR_CAPTURE_OK=1
					for n in $(seq 1 "$REPEATS"); do
						rm -f "$LOGDIR/$cam.raw"
						if run "stream-$cam-$n" env OUTPUT_DIR="$LOGDIR" CAPTURE_TIMEOUT="$TIMEOUT" "$ROOT/test-capture.sh" "$cam"; then
							raw="$LOGDIR/$cam.raw"
							if capture_is_valid "$cam" "$raw"; then
								pass "frame-$cam-$n" "three-frame capture has sufficient size and nonzero image data"
							else
								fail "frame-$cam-$n" "$cam capture is short, empty, or all zero"
								SENSOR_CAPTURE_OK=0
							fi
						else SENSOR_CAPTURE_OK=0; fi
					done
					[ "$SENSOR_CAPTURE_OK" -eq 1 ] || continue
					rm -f "$LOGDIR/$cam.raw"
					if run "recovery-capture-$cam" env OUTPUT_DIR="$LOGDIR" CAPTURE_TIMEOUT="$TIMEOUT" "$ROOT/test-capture.sh" "$cam"; then
						if capture_is_valid "$cam" "$LOGDIR/$cam.raw"; then
							pass "recovery-$cam" "re-capture completed after the bounded stream cycles"
							RECOVERY_CAPTURE_OK=1
						else
							fail "recovery-$cam" "$cam recovery capture is short, empty, or all zero"
						fi
					fi
				done
				if [ "$RECOVERY_CAPTURE_OK" -eq 1 ]; then
					pass recovery "at least one verified capture completed after the bounded stream cycles"
				else
					skip recovery "no verified capture completed after the bounded stream cycles"
				fi
			fi
			if [ "$PM_SAFE" -eq 1 ]; then
				found=0; for p in /sys/bus/intel-ipu4-bus/devices/*/power/control; do [ -r "$p" ] || continue; found=1; say "PASS [runtime-pm] $(dirname "$p")=$(cat "$p")"; done
				[ "$found" -eq 1 ] || skip runtime-pm "no readable IPU runtime-PM controls";
			else skip runtime-pm "not requested; suspend/power writes are never implicit"; fi
		fi
	fi
fi

[ "$FAILED" -eq 0 ] || exit 1
say "RESULT: PASS (SKIP is non-failure when hardware/tools/permissions are unavailable)"
