#!/bin/bash
# Configure the media pipeline and capture 3 raw frames via the CSI2 BE SOC
# path — the only IPU4 output route that produces clean raster frames
# (the per-CSI-2 direct capture nodes are MIPI packet dumps that shear on
# any PHY line glitch; the CSI2 BE/ISA path interleaves line pairs).
#   ./test-capture.sh front   (ov5693, 2592x1944, CSI-2 port 2)
#   ./test-capture.sh rear    (ov8865, 3264x2448, CSI-2 port 0)
set -e

CAM="${1:-front}"
if [ "$CAM" = front ]; then
    SENSOR_RE='ov5693'; RES=2592x1944
else
    SENSOR_RE='ov8865'; RES=3264x2448
fi
FMT=SBGGR10_1X10
HERE=$(dirname "$(readlink -f "$0")")
OUTPUT_DIR=${OUTPUT_DIR:-"$HERE/captures"}
CAPTURE_TIMEOUT=${CAPTURE_TIMEOUT:-30}
mkdir -p "$OUTPUT_DIR"

# Resolve the media device, sensor entity, and CSI endpoint from the live
# topology. MEDIA_DEVICE is useful to callers that already performed discovery.
MEDIA_DEVICE=${MEDIA_DEVICE:-}
if [ -z "$MEDIA_DEVICE" ]; then
    for candidate in /dev/media*; do
        [ -e "$candidate" ] || continue
        graph=$(media-ctl -d "$candidate" -p 2>/dev/null) || continue
        printf '%s\n' "$graph" | grep -q "$SENSOR_RE" || continue
        MEDIA_DEVICE=$candidate
        break
    done
fi
[ -n "$MEDIA_DEVICE" ] || { echo "no media device contains $SENSOR_RE" >&2; exit 77; }
GRAPH=$(media-ctl -d "$MEDIA_DEVICE" -p)
SENSOR=$(printf '%s\n' "$GRAPH" | awk -v re="$SENSOR_RE" \
    '$0 ~ "- entity .*: " re { hit=1; name=$0; sub(/.*: /, "", name); sub(/ \(.*/, "", name) } hit && name != "" && /device node name/ { print name; exit }')
PORT=$(printf '%s\n' "$GRAPH" | awk -v re="$SENSOR_RE" \
    '$0 ~ "- entity .*: " re { hit=1 } hit && /-> "Intel IPU4 CSI-2 [0-9]+":0/ { match($0, /CSI-2 [0-9]+/); p=substr($0, RSTART+6, RLENGTH-6); print p; exit }')
[ -n "$SENSOR" ] || { echo "sensor entity for $SENSOR_RE was not found" >&2; exit 77; }
[ -n "$PORT" ] || { echo "CSI endpoint for $SENSOR was not found" >&2; exit 77; }
echo "media device: $MEDIA_DEVICE; sensor: $SENSOR; CSI port: $PORT"

media-ctl -d "$MEDIA_DEVICE" -r
media-ctl -d "$MEDIA_DEVICE" -V "\"$SENSOR\":0 [fmt:$FMT/$RES]"
media-ctl -d "$MEDIA_DEVICE" -V "\"Intel IPU4 CSI-2 $PORT\":0 [fmt:$FMT/$RES]"
media-ctl -d "$MEDIA_DEVICE" -V "\"Intel IPU4 CSI-2 $PORT\":1 [fmt:$FMT/$RES]"
media-ctl -d "$MEDIA_DEVICE" -l "\"$SENSOR\":0 -> \"Intel IPU4 CSI-2 $PORT\":0 [1]"
media-ctl -d "$MEDIA_DEVICE" -l "\"Intel IPU4 CSI-2 $PORT\":1 -> \"Intel IPU4 CSI2 BE SOC\":0 [1]"
media-ctl -d "$MEDIA_DEVICE" -l "\"Intel IPU4 CSI2 BE SOC\":8 -> \"Intel IPU4 BE SOC capture 0\":0 [1]"
media-ctl -d "$MEDIA_DEVICE" -V "\"Intel IPU4 CSI2 BE SOC\":0 [fmt:$FMT/$RES]"
media-ctl -d "$MEDIA_DEVICE" -V "\"Intel IPU4 CSI2 BE SOC\":8 [fmt:$FMT/$RES]"

DEV=$(media-ctl -d "$MEDIA_DEVICE" -e "Intel IPU4 BE SOC capture 0")
W=${RES%x*}; H=${RES#*x}
echo "capturing from $DEV ($CAM camera, $RES, BE SOC path)..."
timeout "$CAPTURE_TIMEOUT" v4l2-ctl -d "$DEV" \
    --set-fmt-video=width=$W,height=$H,pixelformat=BG10 \
    --stream-mmap=4 --stream-count=3 \
    --stream-to="$OUTPUT_DIR/$CAM.raw"
ls -la "$OUTPUT_DIR/$CAM.raw"
