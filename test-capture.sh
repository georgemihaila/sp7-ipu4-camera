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
    SENSOR='ov5693 1-0036';  PORT=2; RES=2592x1944
else
    SENSOR='ov8865 2-0010';  PORT=0; RES=3264x2448
fi
FMT=SBGGR10_1X10
HERE=$(dirname "$(readlink -f "$0")")
OUTPUT_DIR=${OUTPUT_DIR:-"$HERE/captures"}
CAPTURE_TIMEOUT=${CAPTURE_TIMEOUT:-30}
mkdir -p "$OUTPUT_DIR"

media-ctl -d /dev/media0 -r
media-ctl -d /dev/media0 -V "\"$SENSOR\":0 [fmt:$FMT/$RES]"
media-ctl -d /dev/media0 -V "\"Intel IPU4 CSI-2 $PORT\":0 [fmt:$FMT/$RES]"
media-ctl -d /dev/media0 -V "\"Intel IPU4 CSI-2 $PORT\":1 [fmt:$FMT/$RES]"
media-ctl -d /dev/media0 -l "\"$SENSOR\":0 -> \"Intel IPU4 CSI-2 $PORT\":0 [1]"
# BE SOC links are DYNAMIC; media-ctl cannot enable those (drops the flag)
python3 "$HERE/enable-link.py" "Intel IPU4 CSI-2 $PORT:1" "Intel IPU4 CSI2 BE SOC:0"
python3 "$HERE/enable-link.py" "Intel IPU4 CSI2 BE SOC:8" "Intel IPU4 BE SOC capture 0:0"
media-ctl -d /dev/media0 -V "\"Intel IPU4 CSI2 BE SOC\":0 [fmt:$FMT/$RES]"
media-ctl -d /dev/media0 -V "\"Intel IPU4 CSI2 BE SOC\":8 [fmt:$FMT/$RES]"

DEV=$(media-ctl -d /dev/media0 -e "Intel IPU4 BE SOC capture 0")
W=${RES%x*}; H=${RES#*x}
echo "capturing from $DEV ($CAM camera, $RES, BE SOC path)..."
timeout "$CAPTURE_TIMEOUT" v4l2-ctl -d "$DEV" \
    --set-fmt-video=width=$W,height=$H,pixelformat=BG10 \
    --stream-mmap=4 --stream-count=3 \
    --stream-to="$OUTPUT_DIR/$CAM.raw"
ls -la "$OUTPUT_DIR/$CAM.raw"
