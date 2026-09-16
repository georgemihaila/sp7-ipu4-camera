#!/bin/sh
# Reproducible baseline for the Python/GStreamer bridge.
# The default is deliberately long: 60-second windows, three repetitions.
# Set BASELINE_SECONDS and BASELINE_REPEATS for bounded local smoke tests.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SECONDS_PER_CASE=${BASELINE_SECONDS:-60}
REPEATS=${BASELINE_REPEATS:-3}
RUN_IDLE=${BASELINE_IDLE:-1}
RUN_STREAMS=${BASELINE_STREAMS:-1}
FORMATS=${BASELINE_FORMATS:-YUYV,MJPG}
OUTPUT_DIR=${BASELINE_OUTPUT_DIR:-"$(mktemp -d /tmp/sp7-camera-baseline.XXXXXX)"}
BRIDGE_UNIT=${BRIDGE_UNIT:-sp7-camera-bridge.service}

case $SECONDS_PER_CASE in ''|*[!0-9]*) echo 'BASELINE_SECONDS must be an integer' >&2; exit 2 ;; esac
case $REPEATS in ''|*[!0-9]*) echo 'BASELINE_REPEATS must be an integer' >&2; exit 2 ;; esac
[ "$SECONDS_PER_CASE" -gt 0 ] || { echo 'BASELINE_SECONDS must be positive' >&2; exit 2; }
[ "$REPEATS" -gt 0 ] || { echo 'BASELINE_REPEATS must be positive' >&2; exit 2; }
case $RUN_IDLE in 0|1) ;; *) echo 'BASELINE_IDLE must be 0 or 1' >&2; exit 2 ;; esac
case $RUN_STREAMS in 0|1) ;; *) echo 'BASELINE_STREAMS must be 0 or 1' >&2; exit 2 ;; esac

for command in systemctl v4l2-ctl python3 awk ps; do
	command -v "$command" >/dev/null 2>&1 || { echo "missing command: $command" >&2; exit 2; }
done

mkdir -p "$OUTPUT_DIR"
MAIN_PID=$(systemctl --user show "$BRIDGE_UNIT" -p MainPID --value)
CGROUP=$(systemctl --user show "$BRIDGE_UNIT" -p ControlGroup --value)
[ "$MAIN_PID" -gt 0 ] 2>/dev/null || { echo "unit is not running: $BRIDGE_UNIT" >&2; exit 2; }
[ -n "$CGROUP" ] || { echo "unit has no cgroup: $BRIDGE_UNIT" >&2; exit 2; }
CGDIR=/sys/fs/cgroup$CGROUP
[ -r "$CGDIR/cpu.stat" ] || { echo "cgroup metrics are unavailable: $CGDIR" >&2; exit 2; }

stat_value() {
	key=$1
	awk -v key="$key" '$1 == key { print $2; exit }' "$CGDIR/cpu.stat"
}

context_switches() {
	total=0
	while read -r pid; do
		[ -n "$pid" ] || continue
		value=$(awk '
			/^voluntary_ctxt_switches:/ { v = $2 }
			/^nonvoluntary_ctxt_switches:/ { n = $2 }
			END { print (v + 0) + (n + 0) }
		' "/proc/$pid/status" 2>/dev/null || printf '0')
		total=$((total + value))
	done < "$CGDIR/cgroup.procs"
	printf '%s\n' "$total"
}

memory_current() { cat "$CGDIR/memory.current"; }
processes_current() { cat "$CGDIR/pids.current"; }

snapshot() {
	label=$1
	printf '%s cpu_usec=%s memory_bytes=%s processes=%s context_switches=%s\n' \
		"$label" "$(stat_value usage_usec)" "$(memory_current)" \
		"$(processes_current)" "$(context_switches)"
}

print_process_tree() {
	while read -r pid; do
		[ -n "$pid" ] || continue
		ps -p "$pid" -o pid=,ppid=,stat=,etime=,%cpu=,%mem=,rss=,comm=,args= 2>/dev/null || true
	done < "$CGDIR/cgroup.procs"
}

prepare_format() {
	fmt=$1
	case $fmt in YUYV) pixel_format=YUYV ;; MJPG) pixel_format=MJPG ;; *) echo "unsupported format: $fmt" >&2; exit 2 ;; esac
	# v4l2loopback accepts a new capture format only while no producer owns the
	# endpoint. Restart the user service, then verify the producer preserved it.
	systemctl --user stop "$BRIDGE_UNIT"
	for device in /dev/video60 /dev/video61; do
		v4l2-ctl -d "$device" --set-fmt-video=width=1280,height=720,pixelformat="$pixel_format"
	done
	systemctl --user start "$BRIDGE_UNIT"
	sleep 3
	MAIN_PID=$(systemctl --user show "$BRIDGE_UNIT" -p MainPID --value)
	for device in /dev/video60 /dev/video61; do
		v4l2-ctl -d "$device" --get-fmt-video | grep -Fq "'$pixel_format'" || {
			echo "service did not preserve $fmt on $device" >&2
			return 1
		}
	done
}

measure_idle() {
	repetition=$1
	start_stat=$(snapshot start)
	sleep "$SECONDS_PER_CASE"
	end_stat=$(snapshot end)
	printf '%s idle %s start=%s end=%s\n' "$repetition" "$start_stat" "$end_stat"
	printf '%s idle_process_tree\n' "$repetition"
	print_process_tree
}

measure_stream() {
	cam=$1
	fmt=$2
	repetition=$3
	case $cam in front) device=/dev/video60 ;; rear) device=/dev/video61 ;; *) exit 2 ;; esac
	case $fmt in YUYV) pixel_format=YUYV ;; MJPG) pixel_format=MJPG ;; *) exit 2 ;; esac
	base="$OUTPUT_DIR/$cam-$fmt-$repetition"
	start_stat=$(snapshot start)
	frames=$((SECONDS_PER_CASE * 30))
	start_epoch=$(date +%s)
	set +e
	timeout --kill-after=2 "$((SECONDS_PER_CASE + 15))" v4l2-ctl -d "$device" \
		--stream-mmap=4 --stream-count="$frames" --stream-to="$base-data" 2>"$base-stream.log" &
	stream_pid=$!
	wait "$stream_pid" || true
	set -e
	end_epoch=$(date +%s)
	python3 "$ROOT/tests/bridge-frame-metrics.py" "$fmt" <"$base-data" >"$base-metrics" || true
	end_stat=$(snapshot end)
	stream_seconds=$((end_epoch - start_epoch))
	stream_frames=$(awk -F= '$1 == "frames" { print $2; exit }' "$base-metrics" 2>/dev/null || printf '0')
	stream_rate=0
	[ "$stream_seconds" -gt 0 ] && stream_rate=$(awk -v f="$stream_frames" -v s="$stream_seconds" 'BEGIN { printf "%.3f", f / s }')
	printf '%s %s elapsed_seconds=%s measured_rate=%s start=%s end=%s %s\n' \
		"$cam" "$fmt" "$stream_seconds" "$stream_rate" "$start_stat" "$end_stat" \
		"$(tr '\n' ' ' < "$base-metrics" 2>/dev/null || true)"
	printf '%s %s process_tree\n' "$cam" "$fmt"
	print_process_tree
}

printf 'baseline_unit=%s main_pid=%s cgroup=%s cgroup_dir=%s\n' "$BRIDGE_UNIT" "$MAIN_PID" "$CGROUP" "$CGDIR"
printf 'seconds_per_case=%s repeats=%s idle=%s streams=%s output_dir=%s\n' \
	"$SECONDS_PER_CASE" "$REPEATS" "$RUN_IDLE" "$RUN_STREAMS" "$OUTPUT_DIR"
printf '%s initial_process_tree\n' baseline
print_process_tree

if [ "$RUN_IDLE" -eq 1 ]; then
	i=1
	while [ "$i" -le "$REPEATS" ]; do
		measure_idle "$i"
		i=$((i + 1))
	done
fi

if [ "$RUN_STREAMS" -eq 1 ]; then
	for fmt in $(printf '%s' "$FORMATS" | tr ',' ' '); do
		prepare_format "$fmt"
		for cam in front rear; do
			i=1
			while [ "$i" -le "$REPEATS" ]; do
				measure_stream "$cam" "$fmt" "$i"
				i=$((i + 1))
			done
		done
	done
fi

printf 'baseline_complete output_dir=%s\n' "$OUTPUT_DIR"
