#!/bin/sh
# Shell-only live qualification for the installed C camera bridge.
#
# The default run follows the original acceptance contract: front and rear
# endpoints in YUYV and MJPEG, three 60-second streams per matrix case, both
# endpoints probed together, twenty open/close cycles, twenty front/rear
# switches, and a service restart. Captures and logs live below /tmp only.
#
# This script can control the user service and loopback formats only when
# explicitly allowed. Do not run it while another camera application is using
# /dev/video60 or /dev/video61. Use --quick for a short smoke run.

set -u

FRONT=/dev/video60
REAR=/dev/video61
UNIT=sp7-camera-bridge.service
WIDTH=1280
HEIGHT=720
YUYV_BYTES=$((WIDTH * HEIGHT * 2))

REPEATS=${CBQ_REPEATS:-3}
DURATION=${CBQ_DURATION:-60}
SAMPLE_FRAMES=${CBQ_SAMPLE_FRAMES:-120}
WARMUP_FRAMES=${CBQ_WARMUP_FRAMES:-120}
PROBE_FRAMES=${CBQ_PROBE_FRAMES:-45}
OPEN_CLOSE_CYCLES=${CBQ_OPEN_CLOSE_CYCLES:-20}
SWITCH_CYCLES=${CBQ_SWITCH_CYCLES:-20}
START_TIMEOUT=${CBQ_START_TIMEOUT:-20}
STREAM_TIMEOUT=${CBQ_STREAM_TIMEOUT:-90}
STREAM_ATTEMPTS=${CBQ_STREAM_ATTEMPTS:-5}
KEEP_ARTIFACTS=${CBQ_KEEP_ARTIFACTS:-0}
CASE_FILTER=${CBQ_CASE:-all}
SKIP_LIFECYCLE=0
ALLOW_SERVICE_CONTROL=0
CURRENT_MATRIX_FORMAT=
FORMAT_CONTROL_ATTEMPTED=0

LOGDIR=
INITIAL_ACTIVE=0
INITIAL_FRONT_FORMAT=
INITIAL_REAR_FORMAT=
FAILED=0
SKIPPED=0

say() { printf '%s\n' "$*"; }
pass() { say "PASS [$1] $2"; }
skip() { SKIPPED=$((SKIPPED + 1)); say "SKIP [$1] $2"; }
fail() { FAILED=$((FAILED + 1)); say "FAIL [$1] $2 (diagnostics: $LOGDIR)"; }

usage() {
	cat <<EOF
usage: $0 [--quick] [--case CASE] [--allow-service-control]
       [--skip-lifecycle] [--keep-artifacts]

CASE is one of front-yuyv, rear-yuyv, front-mjpeg, rear-mjpeg, or all.
	Environment overrides: CBQ_REPEATS, CBQ_DURATION, CBQ_SAMPLE_FRAMES,
	CBQ_WARMUP_FRAMES,
CBQ_PROBE_FRAMES, CBQ_OPEN_CLOSE_CYCLES, CBQ_SWITCH_CYCLES,
CBQ_START_TIMEOUT, CBQ_STREAM_TIMEOUT, CBQ_STREAM_ATTEMPTS,
CBQ_KEEP_ARTIFACTS.
EOF
}

while [ "$#" -gt 0 ]; do
	case $1 in
	--quick)
			REPEATS=1
			DURATION=20
			SAMPLE_FRAMES=120
			PROBE_FRAMES=20
			OPEN_CLOSE_CYCLES=2
			SWITCH_CYCLES=2
			STREAM_TIMEOUT=40
			STREAM_ATTEMPTS=3
			;;
		--case)
			[ "$#" -ge 2 ] || { say "--case requires a value" >&2; exit 2; }
			CASE_FILTER=$2
			shift
			;;
		--skip-lifecycle) SKIP_LIFECYCLE=1 ;;
		--allow-service-control) ALLOW_SERVICE_CONTROL=1 ;;
		--keep-artifacts) KEEP_ARTIFACTS=1 ;;
		--help|-h) usage; exit 0 ;;
		*) say "unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
	shift
done

valid_positive_integer() {
	case $1 in
		''|*[!0-9]*) return 1 ;;
	esac
	[ "$1" -gt 0 ] 2>/dev/null
}

for value in "$REPEATS" "$DURATION" "$SAMPLE_FRAMES" "$WARMUP_FRAMES" "$PROBE_FRAMES" \
	"$OPEN_CLOSE_CYCLES" "$SWITCH_CYCLES" "$START_TIMEOUT" "$STREAM_TIMEOUT" \
	"$STREAM_ATTEMPTS"; do
	if ! valid_positive_integer "$value"; then
		say "all numeric settings must be positive integers" >&2
		exit 2
	fi
done

case $CASE_FILTER in
	all|front-yuyv|rear-yuyv|front-mjpeg|rear-mjpeg) ;;
	*) say "invalid --case: $CASE_FILTER" >&2; exit 2 ;;
esac

if ! LOGDIR=$(mktemp -d "${TMPDIR:-/tmp}/sp7-cbridge-qualification.XXXXXX"); then
	say "could not create a temporary diagnostics directory" >&2
	exit 2
fi

cleanup() {
	status=$?
	trap - EXIT HUP INT TERM

	# A timeout normally closes its V4L2 descriptor before returning. Stop the
	# service before restoring formats so no producer owns the loopback nodes.
	if [ "$ALLOW_SERVICE_CONTROL" -eq 1 ] && [ "$FORMAT_CONTROL_ATTEMPTED" -eq 1 ] &&
		[ -n "$INITIAL_FRONT_FORMAT" ] && [ -n "$INITIAL_REAR_FORMAT" ]; then
		if systemctl --user is-active --quiet "$UNIT" 2>/dev/null && owners_are_bridge_only; then
			systemctl --user stop "$UNIT" >"$LOGDIR/cleanup-stop.log" 2>&1 || true
		else
			say "cleanup left service and formats untouched because another process owns a camera endpoint"
			INITIAL_FRONT_FORMAT=
			INITIAL_REAR_FORMAT=
		fi
		if [ -n "$INITIAL_FRONT_FORMAT" ] && [ -n "$INITIAL_REAR_FORMAT" ]; then
			set_format "$FRONT" "$INITIAL_FRONT_FORMAT" >"$LOGDIR/cleanup-front.log" 2>&1 || true
			set_format "$REAR" "$INITIAL_REAR_FORMAT" >"$LOGDIR/cleanup-rear.log" 2>&1 || true
			if [ "$INITIAL_ACTIVE" -eq 1 ]; then
				systemctl --user start "$UNIT" >"$LOGDIR/cleanup-start.log" 2>&1 || true
			fi
		fi
	fi

	if [ "$KEEP_ARTIFACTS" -eq 1 ] || [ "$FAILED" -gt 0 ] || [ "$status" -ne 0 ]; then
		say "diagnostics retained: $LOGDIR"
	else
		# This is the exact private temporary directory created above; no
		# repository or user data is targeted.
		rmdir "$LOGDIR" 2>/dev/null || true
	fi
	exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

require_tool() {
	command -v "$1" >/dev/null 2>&1 || {
		skip prerequisites "$1 is unavailable"
		return 1
	}
	return 0
}

for tool in systemctl v4l2-ctl ffmpeg ffprobe timeout awk sed date stat fuser find wc grep sleep rmdir; do
	require_tool "$tool" || exit 0
done

if [ ! -c "$FRONT" ] || [ ! -c "$REAR" ]; then
	skip prerequisites "both $FRONT and $REAR must be present character devices"
	exit 0
fi
if ! systemctl --user cat "$UNIT" >"$LOGDIR/unit.log" 2>&1; then
	skip prerequisites "user unit $UNIT is not installed"
	exit 0
fi

service_pid() {
	pid=$(systemctl --user show -p MainPID --value "$UNIT" 2>/dev/null || true)
	case $pid in
		''|*[!0-9]*|0) return 1 ;;
	esac
	printf '%s\n' "$pid"
}

service_cgroup() {
	systemctl --user show -p ControlGroup --value "$UNIT" 2>/dev/null || true
}

owner_is_service_member() {
	owner=$1
	bridge_pid=$(service_pid 2>/dev/null || true)
	[ -n "$bridge_pid" ] || return 1
	[ "$owner" -eq "$bridge_pid" ] && return 0
	cgroup=$(service_cgroup)
	[ -n "$cgroup" ] || return 1
	grep -Fq ":$cgroup" "/proc/$owner/cgroup" 2>/dev/null
}

format_name() {
	device=$1
	value=$(v4l2-ctl -d "$device" --get-fmt-video 2>/dev/null |
		awk -F"'" '/Pixel Format/ {print $2; exit}')
	case $value in
		YUYV) printf '%s\n' YUYV ;;
		MJPG|JPEG) printf '%s\n' MJPG ;;
		*) return 1 ;;
	esac
}

set_format() {
	device=$1
	format=$2
	v4l2-ctl -d "$device" \
		--set-fmt-video="width=$WIDTH,height=$HEIGHT,pixelformat=$format"
}

wait_service() {
	deadline=$(( $(date +%s) + START_TIMEOUT ))
	while [ "$(date +%s)" -le "$deadline" ]; do
		if systemctl --user is-active --quiet "$UNIT" && service_pid >/dev/null; then
			return 0
		fi
		sleep 1
	done
	return 1
}

owners_are_bridge_only() {
	bridge_pid=$(service_pid 2>/dev/null || true)
	[ -n "$bridge_pid" ] || return 1
	for device in "$FRONT" "$REAR"; do
		for owner in $(fuser "$device" 2>/dev/null || true); do
			case $owner in
				''|*[!0-9]*) continue ;;
			esac
			owner_is_service_member "$owner" || return 1
		done
	done
	return 0
}

stop_for_format_change() {
	if [ "$ALLOW_SERVICE_CONTROL" -ne 1 ]; then
		fail format "format control requires --allow-service-control"
		return 1
	fi
	FORMAT_CONTROL_ATTEMPTED=1
	if ! owners_are_bridge_only; then
		fail format "another process owns a camera endpoint; refusing to stop the service"
		return 1
	fi
	if ! systemctl --user stop "$UNIT" >"$LOGDIR/stop.log" 2>&1; then
		fail service "could not stop $UNIT for a format change"
		return 1
	fi
	if systemctl --user is-active --quiet "$UNIT"; then
		fail service "$UNIT remained active after stop"
		return 1
	fi
	return 0
}

start_after_format_change() {
	if ! systemctl --user start "$UNIT" >"$LOGDIR/start.log" 2>&1 || ! wait_service; then
		fail service "$UNIT did not recover after format setup"
		return 1
	fi
	return 0
}

set_matrix_format() {
	format=$1
	if ! stop_for_format_change; then return 1; fi
	if ! set_format "$FRONT" "$format" >"$LOGDIR/set-front-$format.log" 2>&1 ||
		! set_format "$REAR" "$format" >"$LOGDIR/set-rear-$format.log" 2>&1; then
		fail format "could not set both loopback endpoints to $format"
		return 1
	fi
	if ! start_after_format_change; then return 1; fi
	if ! format_name "$FRONT" | grep -Fxq "$format" ||
		! format_name "$REAR" | grep -Fxq "$format"; then
		fail format "format readback did not remain $format on both endpoints"
		return 1
	fi
	pass format "both endpoints configured as $format"
	return 0
}

capture_metrics() {
	format=$1
	input=$2
	metrics=$3
	if [ "$format" = YUYV ]; then
		ffmpeg -hide_banner -loglevel error -f rawvideo -pix_fmt yuyv422 \
			-s "${WIDTH}x${HEIGHT}" -i "$input" \
			-vf 'signalstats,metadata=print:file=-' -frames:v "$SAMPLE_FRAMES" -f null - \
			>"$metrics" 2>&1
	else
		ffmpeg -hide_banner -loglevel error -f mjpeg -i "$input" \
			-vf 'signalstats,metadata=print:file=-' -frames:v "$SAMPLE_FRAMES" -f null - \
			>"$metrics" 2>&1
	fi
}

nonblack_payload() {
	metrics=$1
	max_avg=$(awk -F= '/lavfi.signalstats.YAVG=/{if ($2 + 0 > max) max=$2 + 0} END {if (max == "") exit 1; printf "%.2f", max}' "$metrics") || return 1
	max_luma=$(awk -F= '/lavfi.signalstats.YMAX=/{if ($2 + 0 > max) max=$2 + 0} END {if (max == "") exit 1; printf "%.2f", max}' "$metrics") || return 1
	awk -v avg="$max_avg" -v luma="$max_luma" 'BEGIN { exit !(avg > 22 && luma > 24) }' || return 1
	printf 'max_yavg=%s max_ymax=%s\n' "$max_avg" "$max_luma"
}

sample_case() {
	cam=$1
	format=$2
	device=$([ "$cam" = front ] && printf '%s' "$FRONT" || printf '%s' "$REAR")
	capture="$LOGDIR/sample-$cam-$format.bin"
	metrics="$LOGDIR/sample-$cam-$format.metrics"
	if [ "$format" = YUYV ]; then
		minimum_bytes=$((YUYV_BYTES * SAMPLE_FRAMES))
	else
		minimum_bytes=$((SAMPLE_FRAMES * 1024))
	fi
	if ! stream_capture "$device" "$WARMUP_FRAMES" /dev/null \
		"$LOGDIR/warmup-$cam-$format.log"; then
		fail "warmup-$cam-$format" "could not read $WARMUP_FRAMES warmup frames"
		return 1
	fi
	if ! stream_capture "$device" "$SAMPLE_FRAMES" "$capture" \
		"$LOGDIR/sample-$cam-$format.log" "$minimum_bytes"; then
		fail "sample-$cam-$format" "could not read $SAMPLE_FRAMES frames"
		return 1
	fi
	bytes=$(stat -c '%s' "$capture" 2>/dev/null || printf 0)
	frames=$SAMPLE_FRAMES
	if [ "$format" = YUYV ]; then
		[ "$bytes" -ge $((YUYV_BYTES * SAMPLE_FRAMES)) ] || {
			fail "sample-$cam-$format" "YUYV payload is short: $bytes bytes"
			return 1
		}
	else
		probe=$(ffprobe -v error -f mjpeg -count_frames -select_streams v:0 \
			-show_entries stream=codec_name,width,height,nb_read_frames \
			-of default=nw=1 "$capture" 2>"$LOGDIR/probe-$cam-$format.log" || true)
		codec=$(printf '%s\n' "$probe" | sed -n 's/^codec_name=//p')
		width=$(printf '%s\n' "$probe" | sed -n 's/^width=//p')
		height=$(printf '%s\n' "$probe" | sed -n 's/^height=//p')
		frames=$(printf '%s\n' "$probe" | sed -n 's/^nb_read_frames=//p')
		case $frames in ''|*[!0-9]*) frames=0 ;; esac
		[ "$codec" = mjpeg ] && [ "$width" = "$WIDTH" ] && [ "$height" = "$HEIGHT" ] || {
			fail "sample-$cam-$format" "JPEG probe did not report mjpeg ${WIDTH}x${HEIGHT}"
			return 1
		}
		[ "$frames" -ge "$SAMPLE_FRAMES" ] || {
			fail "sample-$cam-$format" "JPEG decoder saw only $frames frames"
			return 1
		}
		[ "$bytes" -ge $((frames * 1024)) ] || {
			fail "sample-$cam-$format" "JPEG payload is implausibly short: $bytes bytes"
			return 1
		}
	fi
	if ! capture_metrics "$format" "$capture" "$metrics" || ! content=$(nonblack_payload "$metrics"); then
		fail "sample-$cam-$format" "decoded payload stayed black or lacked luma variation"
		return 1
	fi
	pass "sample-$cam-$format" "$frames frames, $bytes bytes, $content"
	return 0
}

stream_case() {
	cam=$1
	format=$2
	device=$([ "$cam" = front ] && printf '%s' "$FRONT" || printf '%s' "$REAR")
	frames=$((DURATION * 30))
	start_ns=$(date +%s%N)
	if ! stream_capture "$device" "$frames" /dev/null \
		"$LOGDIR/stream-$cam-$format.log"; then
		fail "stream-$cam-$format" "$DURATION-second stream did not deliver $frames frames"
		return 1
	fi
	end_ns=$(date +%s%N)
	elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
	[ "$elapsed_ms" -gt 0 ] || elapsed_ms=1
	fps=$(awk -v count="$frames" -v elapsed="$elapsed_ms" 'BEGIN {printf "%.2f", count * 1000 / elapsed}')
	if ! awk -v rate="$fps" 'BEGIN {exit !(rate >= 20)}'; then
		fail "stream-$cam-$format" "measured ${fps} fps over ${elapsed_ms} ms"
		return 1
	fi
	pass "stream-$cam-$format" "${frames} frames in ${elapsed_ms} ms (${fps} fps)"
	return 0
}

stream_capture() {
	device=$1
	frames=$2
	output=$3
	log=$4
	minimum_bytes=${5:-}
	attempt=1
	last_result=1
	while [ "$attempt" -le "$STREAM_ATTEMPTS" ]; do
		if [ "$output" != /dev/null ]; then
			: >"$output"
		fi
		if timeout --signal=TERM --kill-after=3 "$STREAM_TIMEOUT" \
			v4l2-ctl -d "$device" --stream-mmap --stream-count="$frames" \
			--stream-to="$output" >"$log" 2>&1; then
			if grep -Eiq 'VIDIOC_STREAMON returned -1|streamon.*(error|failed)|input/output error' "$log"; then
				last_result=1
			elif [ -n "$minimum_bytes" ] &&
				[ "$(stat -c '%s' "$output" 2>/dev/null || printf 0)" -lt "$minimum_bytes" ]; then
				last_result=1
			else
				return 0
			fi
		else
			last_result=$?
		fi
		if [ "$attempt" -lt "$STREAM_ATTEMPTS" ]; then
			sleep 2
		fi
		attempt=$((attempt + 1))
	done
	return "$last_result"
}

probe_camera() {
	cam=$1
	device=$([ "$cam" = front ] && printf '%s' "$FRONT" || printf '%s' "$REAR")
	stream_capture "$device" "$PROBE_FRAMES" /dev/null "$LOGDIR/probe-$cam.log"
}

fd_count() {
	pid=$1
	find "/proc/$pid/fd" -mindepth 1 -maxdepth 1 -print 2>/dev/null | wc -l | tr -d '[:space:]'
}

rss_kib() {
	pid=$1
	awk '/^VmRSS:/ {print $2; exit}' "/proc/$pid/status" 2>/dev/null || printf 0
}

both_endpoint_probe() {
	probe_camera front & front_pid=$!
	probe_camera rear & rear_pid=$!
	front_result=0
	rear_result=0
	wait "$front_pid" || front_result=$?
	wait "$rear_pid" || rear_result=$?
	if [ "$front_result" -ne 0 ] || [ "$rear_result" -ne 0 ]; then
		fail both-probe "front=$front_result rear=$rear_result"
		return 1
	fi
	pass both-probe "both named loopback endpoints accepted short probes"
	return 0
}

lifecycle_checks() {
	pid_before=$(service_pid 2>/dev/null || true)
	[ -n "$pid_before" ] || { fail lifecycle "service PID unavailable"; return 1; }
	fd_before=$(fd_count "$pid_before")
	rss_before=$(rss_kib "$pid_before")

	iteration=1
	while [ "$iteration" -le "$OPEN_CLOSE_CYCLES" ]; do
		cam=$([ $((iteration % 2)) -eq 1 ] && printf front || printf rear)
		if ! probe_camera "$cam"; then
			fail "open-close-$iteration" "$cam probe failed"
			return 1
		fi
		iteration=$((iteration + 1))
	done
	pass open-close "$OPEN_CLOSE_CYCLES alternating probes completed"

	iteration=1
	while [ "$iteration" -le "$SWITCH_CYCLES" ]; do
		if ! probe_camera front || ! probe_camera rear; then
			fail "switch-$iteration" "front to rear switch failed"
			return 1
		fi
		iteration=$((iteration + 1))
	done
	pass switches "$SWITCH_CYCLES front-to-rear switches completed"

	pid_after=$(service_pid 2>/dev/null || true)
	if [ "$pid_after" != "$pid_before" ]; then
		fail lifecycle "bridge PID changed unexpectedly: $pid_before -> $pid_after"
		return 1
	fi
	fd_after=$(fd_count "$pid_after")
	rss_after=$(rss_kib "$pid_after")
	fd_delta=$((fd_after - fd_before))
	rss_delta=$((rss_after - rss_before))
	if [ "$fd_delta" -gt 4 ] || [ "$rss_delta" -gt 65536 ]; then
		fail lifecycle "resource growth: fd delta=$fd_delta, VmRSS delta=${rss_delta} KiB"
		return 1
	fi
	pass lifecycle "fd delta=$fd_delta, VmRSS delta=${rss_delta} KiB"
	return 0
}

run_case() {
	cam=$1
	format=$2
	case_name="$cam-$format"
	if [ "$CASE_FILTER" != all ] && [ "$CASE_FILTER" != "$case_name" ]; then
		return 0
	fi
	if [ "$format" = mjpeg ]; then
		matrix_format=MJPG
	else
		matrix_format=YUYV
	fi
	if [ "$CURRENT_MATRIX_FORMAT" != "$matrix_format" ]; then
		if [ "$ALLOW_SERVICE_CONTROL" -ne 1 ]; then
			if [ "$(format_name "$FRONT" 2>/dev/null || true)" = "$matrix_format" ] &&
				[ "$(format_name "$REAR" 2>/dev/null || true)" = "$matrix_format" ]; then
				CURRENT_MATRIX_FORMAT=$matrix_format
			else
				skip "$case_name" "$matrix_format requires --allow-service-control for format setup"
				return 0
			fi
		fi
		if ! set_matrix_format "$matrix_format"; then return 1; fi
		CURRENT_MATRIX_FORMAT=$matrix_format
	fi
	if ! sample_case "$cam" "$matrix_format"; then return 1; fi
	repeat=1
	while [ "$repeat" -le "$REPEATS" ]; do
		if ! stream_case "$cam" "$matrix_format"; then return 1; fi
		repeat=$((repeat + 1))
	done
	return 0
}

INITIAL_ACTIVE=0
if systemctl --user is-active --quiet "$UNIT"; then
	INITIAL_ACTIVE=1
else
	skip prerequisites "$UNIT is not active; live qualification requires the running bridge"
	exit 0
fi

INITIAL_FRONT_FORMAT=$(format_name "$FRONT" 2>"$LOGDIR/initial-front-format.log" || true)
INITIAL_REAR_FORMAT=$(format_name "$REAR" 2>"$LOGDIR/initial-rear-format.log" || true)
if [ -z "$INITIAL_FRONT_FORMAT" ] || [ -z "$INITIAL_REAR_FORMAT" ]; then
	skip prerequisites "initial loopback format is not YUYV, MJPG, or JPEG"
	exit 0
fi
if ! owners_are_bridge_only; then
	skip prerequisites "another process already owns a camera endpoint"
	exit 0
fi

pass prerequisites "C bridge active; initial formats front=$INITIAL_FRONT_FORMAT rear=$INITIAL_REAR_FORMAT"

if ! both_endpoint_probe; then :; fi
if ! run_case front yuyv; then :; fi
if ! run_case rear yuyv; then :; fi
if ! run_case front mjpeg; then :; fi
if ! run_case rear mjpeg; then :; fi

if [ "$SKIP_LIFECYCLE" -eq 0 ]; then
	if ! lifecycle_checks; then :; fi
	# The controller intentionally keeps the selected camera for its close
	# grace interval after the last reader disappears. Let it return to filler
	# ownership before exercising a service-level restart.
	sleep 3
	if [ "$ALLOW_SERVICE_CONTROL" -eq 1 ] &&
		systemctl --user restart "$UNIT" >"$LOGDIR/restart.log" 2>&1 && wait_service; then
		pass restart "$UNIT recovered after an explicit service restart"
	elif [ "$ALLOW_SERVICE_CONTROL" -ne 1 ]; then
		skip restart "service restart requires --allow-service-control"
	else
		fail restart "$UNIT did not recover after an explicit service restart"
	fi
else
	skip lifecycle "disabled by --skip-lifecycle"
fi

if [ "$FAILED" -gt 0 ]; then
	say "RESULT: FAIL ($FAILED failed checks, $SKIPPED skipped; diagnostics: $LOGDIR)"
	exit 1
fi
say "RESULT: PASS ($SKIPPED skipped; diagnostics: $LOGDIR)"
exit 0
