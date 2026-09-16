#!/bin/bash
# One-shot contrast autofocus for the Surface Pro 7 rear OV8865 + DW9719.
# This is a userspace focus sweep; it does not provide continuous/GUI AF.
set -Eeuo pipefail

HERE=$(dirname "$(readlink -f "$0")")
CAPTURE_SCRIPT="$HERE/test-capture.sh"
MIN_POS=${MIN_POS:-0}
MAX_POS=${MAX_POS:-1023}
STEP=${STEP:-128}
LEVELS=${LEVELS:-3}
CAPTURE_TIMEOUT=${CAPTURE_TIMEOUT:-30}
MEDIA_DEVICE=${MEDIA_DEVICE:-}

usage() {
	cat <<'EOF'
Usage: sudo ./autofocus-rear.sh

Runs a one-shot contrast-based focus sweep on the rear OV8865/DW9719 camera,
keeps the sharpest tested lens position, and removes temporary raw captures.
The target should be still and well lit during the sweep.

Environment overrides: MIN_POS (0), MAX_POS (1023), STEP (128), LEVELS (3),
CAPTURE_TIMEOUT (30 seconds per capture), MEDIA_DEVICE (auto-detected).
Requires v4l2-ctl, media-ctl, timeout, Python 3 and NumPy.
EOF
}

if [[ ${1:-} == -h || ${1:-} == --help ]]; then usage; exit 0; fi
if (($#)); then usage >&2; exit 2; fi
if (( EUID != 0 )); then
	echo "Run with sudo so the camera graph and DW9719 control are accessible." >&2
	exit 77
fi
for cmd in media-ctl v4l2-ctl timeout python3; do
	command -v "$cmd" >/dev/null || { echo "Missing dependency: $cmd" >&2; exit 77; }
done
[[ -x $CAPTURE_SCRIPT ]] || { echo "Missing executable: $CAPTURE_SCRIPT" >&2; exit 77; }
(( MIN_POS >= 0 && MAX_POS <= 1023 && MIN_POS < MAX_POS && STEP > 0 && LEVELS > 0 )) || {
	echo "Invalid scan range or refinement settings." >&2; exit 2;
}
python3 -c 'import numpy' >/dev/null 2>&1 || {
	echo "Python NumPy is required to score Bayer sharpness." >&2; exit 77;
}

# Locate the V4L2 subdevice belonging to the actuator from the live media graph.
if [[ -z $MEDIA_DEVICE ]]; then
	for candidate in /dev/media*; do
		[[ -e $candidate ]] || continue
		graph=$(media-ctl -d "$candidate" -p 2>/dev/null) || continue
		if grep -q 'ov8865' <<<"$graph" && grep -q 'dw9719' <<<"$graph"; then
			MEDIA_DEVICE=$candidate
			break
		fi
	done
fi
[[ -n $MEDIA_DEVICE ]] || { echo "No media device contains both OV8865 and DW9719." >&2; exit 77; }
GRAPH=$(media-ctl -d "$MEDIA_DEVICE" -p)
FOCUS_NODE=$(awk '
	/dw9719/ { actuator=1 }
	actuator && /device node name/ { print $NF; exit }
	actuator && /^- entity/ { actuator=0 }
' <<<"$GRAPH")
[[ -n $FOCUS_NODE && -e $FOCUS_NODE ]] || {
	echo "DW9719 V4L2 subdevice node was not found in $MEDIA_DEVICE." >&2; exit 77;
}
v4l2-ctl -d "$FOCUS_NODE" --get-ctrl=focus_absolute >/dev/null 2>&1 || {
	echo "$FOCUS_NODE does not expose focus_absolute." >&2; exit 77;
}

TMP=$(mktemp -d "${TMPDIR:-/tmp}/sp7-af.XXXXXX")
ORIGINAL=$(v4l2-ctl -d "$FOCUS_NODE" --get-ctrl=focus_absolute | sed 's/.*: *//')
BEST=$ORIGINAL
SUCCESS=0
WIREPLUMBER_STOPPED=0
WP_USER=${SUDO_USER:-}
WP_RUNTIME_DIR=
WP_DBUS_ADDRESS=
user_systemctl() {
	runuser -u "$WP_USER" -- env \
		XDG_RUNTIME_DIR="$WP_RUNTIME_DIR" \
		DBUS_SESSION_BUS_ADDRESS="$WP_DBUS_ADDRESS" \
		systemctl --user "$@"
}
cleanup() {
	local status=$?
	if (( SUCCESS )); then
		v4l2-ctl -d "$FOCUS_NODE" --set-ctrl="focus_absolute=$BEST" >/dev/null 2>&1 || {
			echo "Warning: could not restore best focus position $BEST." >&2
			status=1
		}
	else
		v4l2-ctl -d "$FOCUS_NODE" --set-ctrl="focus_absolute=$ORIGINAL" >/dev/null 2>&1 ||
			echo "Warning: could not restore starting focus position $ORIGINAL." >&2
	fi
	if (( WIREPLUMBER_STOPPED )); then
		if user_systemctl start wireplumber.service >/dev/null 2>&1; then
			echo "Restored the invoking user's WirePlumber service."
		else
			echo "Warning: could not restart the invoking user's WirePlumber service." >&2
			(( status != 0 )) || status=1
		fi
	fi
	rm -rf -- "$TMP"
	exit "$status"
}
trap cleanup EXIT

# WirePlumber may claim the camera nodes. Pause it only when it was already
# active in the invoking desktop user's systemd manager, then restore in EXIT.
if [[ -n $WP_USER ]] && command -v runuser >/dev/null && command -v systemctl >/dev/null; then
	if wp_uid=$(id -u "$WP_USER" 2>/dev/null); then
		WP_RUNTIME_DIR="/run/user/$wp_uid"
		WP_DBUS_ADDRESS="unix:path=$WP_RUNTIME_DIR/bus"
		if [[ -S $WP_RUNTIME_DIR/bus ]] && user_systemctl is-active --quiet wireplumber.service 2>/dev/null; then
			if user_systemctl stop wireplumber.service >/dev/null 2>&1; then
				WIREPLUMBER_STOPPED=1
				echo "Paused the invoking user's WirePlumber service for camera capture."
			else
				echo "WirePlumber is active but could not be paused; continuing without stopping it." >&2
			fi
		fi
	fi
fi

score_position() {
	local pos=$1 raw="$TMP/rear.raw" score
	v4l2-ctl -d "$FOCUS_NODE" --set-ctrl="focus_absolute=$pos" >/dev/null
	# Let the actuator settle before the three-frame capture.
	sleep 0.12
	OUTPUT_DIR="$TMP" CAPTURE_TIMEOUT="$CAPTURE_TIMEOUT" MEDIA_DEVICE="$MEDIA_DEVICE" \
		"$CAPTURE_SCRIPT" rear >/dev/null
	score=$(python3 - "$raw" <<'PY'
import sys
import numpy as np

path = sys.argv[1]
width, height = 3264, 2448
frame_bytes = width * height * 2
with open(path, "rb") as stream:
    stream.seek(-frame_bytes, 2)
    raw = stream.read(frame_bytes)
if len(raw) != frame_bytes:
    raise SystemExit("capture did not contain a complete final frame")
# BG10 is delivered in 16-bit little-endian words. Compare same-color Bayer
# samples at two-pixel intervals to measure high-frequency detail.
image = np.frombuffer(raw, dtype="<u2").reshape(height, width)[::2, ::2].astype(np.float32)
image = image[2:-2, 2:-2]
lap = image[2:, 1:-1] + image[:-2, 1:-1] + image[1:-1, 2:] + image[1:-1, :-2] - 4 * image[1:-1, 1:-1]
mean = float(np.mean(image))
if mean <= 0:
    raise SystemExit("captured frame is black; cannot score focus")
# Normalize for moderate exposure variation between sequential captures.
print(float(np.var(lap, dtype=np.float64)) / (mean * mean))
PY
)
	printf '%s\t%s\n' "$score" "$pos" >> "$TMP/scores"
	printf 'focus %4d  sharpness %s\n' "$pos" "$score"
}

echo "Rear contrast autofocus using $FOCUS_NODE (starting at $ORIGINAL). Keep the scene still."
positions=()
for ((pos=MIN_POS; pos<=MAX_POS; pos+=STEP)); do positions+=("$pos"); done
(( positions[${#positions[@]}-1] == MAX_POS )) || positions+=("$MAX_POS")
for pos in "${positions[@]}"; do score_position "$pos"; done
for ((level=1; level<LEVELS; level++)); do
	best_row=$(sort -gr "$TMP/scores" | head -n1)
	center=${best_row#*$'\t'}
	step=$(( STEP >> level )); (( step > 0 )) || break
	low=$(( center - 2 * step )); (( low < MIN_POS )) && low=$MIN_POS
	high=$(( center + 2 * step )); (( high > MAX_POS )) && high=$MAX_POS
	for ((pos=low; pos<=high; pos+=step)); do
		grep -q $'\t'"$pos"'$' "$TMP/scores" || score_position "$pos"
	done
done
best_row=$(sort -gr "$TMP/scores" | head -n1)
BEST=${best_row#*$'\t'}
SUCCESS=1
echo "Best tested position: $BEST (starting position was $ORIGINAL)."
