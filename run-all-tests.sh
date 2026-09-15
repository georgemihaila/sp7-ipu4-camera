#!/bin/bash
# One-shot IPU4 diagnostic battery. This is historical bring-up tooling, not
# the production test entry point. Run from any checkout after a clean boot:
#   sudo ./run-all-tests.sh
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
LOGDIR=${LOGDIR:-"$ROOT/logs-run-all-tests"}
mkdir -p "$LOGDIR"
M="media-ctl -d /dev/media0"

phase() { echo; echo "########## $1 ##########"; }
snap()  { dmesg -c > "$LOGDIR/$1.dmesg" 2>/dev/null; }

# Reset the isys power island via mmu0 (holds the buttress ctrl).
# Clears reset_needed after a failed stream so the next test is valid.
# The cycle is an explicit diagnostic power-cycle; production loading leaves
# runtime PM in its normal automatic mode.
cycle_island() {
    echo auto > /sys/bus/intel-ipu4-bus/devices/intel-ipu4-mmu1/power/control
    echo on   > /sys/bus/intel-ipu4-bus/devices/intel-ipu4-mmu0/power/control
    sleep 1
    echo auto > /sys/bus/intel-ipu4-bus/devices/intel-ipu4-mmu0/power/control
    sleep 5
    echo on   > /sys/bus/intel-ipu4-bus/devices/intel-ipu4-mmu1/power/control
    sleep 1
}

phase "LOAD"
"$ROOT/load-ipu4.sh" || exit 1
echo 'module intel_ipu4p_isys +p' > /sys/kernel/debug/dynamic_debug/control
echo 'module ov5693 +p'  > /sys/kernel/debug/dynamic_debug/control
echo 'module ov8865 +p'  > /sys/kernel/debug/dynamic_debug/control
snap 00-load

phase "TPG (no sensor: tests ISP+firmware only)"
$M -r
v4l2-ctl -d $($M -e "Intel IPU4 TPG 0") -c test_pattern=2 || true
$M -V '"Intel IPU4 TPG 0":0 [fmt:SBGGR8_1X8/1920x1080]'
$M -l '"Intel IPU4 TPG 0":0 -> "Intel IPU4 TPG 0 capture":0 [1]'
timeout 25 v4l2-ctl -d $($M -e "Intel IPU4 TPG 0 capture") \
    --set-fmt-video=width=1920,height=1080,pixelformat=BA81 \
    --stream-mmap=4 --stream-count=3 --stream-to=$LOGDIR/tpg.raw
echo "TPG exit: $?"; ls -la $LOGDIR/tpg.raw 2>/dev/null
snap 01-tpg
cycle_island

phase "REAR (ov8865, CSI-2 0)"
$M -r
$M -V '"ov8865 2-0010":0 [fmt:SBGGR10_1X10/3264x2448]'
$M -V '"Intel IPU4 CSI-2 0":0 [fmt:SBGGR10_1X10/3264x2448]'
$M -V '"Intel IPU4 CSI-2 0":1 [fmt:SBGGR10_1X10/3264x2448]'
$M -V '"Intel IPU4 CSI2 BE":0 [fmt:SBGGR10_1X10/3264x2448]'
$M -V '"Intel IPU4 CSI2 BE":1 [fmt:SBGGR10_1X10/3264x2448]'
$M -l '"ov8865 2-0010":0 -> "Intel IPU4 CSI-2 0":0 [1]'
$M -l '"Intel IPU4 CSI-2 0":1 -> "Intel IPU4 CSI2 BE":0 [1]'
$M -l '"Intel IPU4 CSI2 BE":1 -> "Intel IPU4 CSI2 BE capture":0 [1]'
timeout 25 v4l2-ctl -d $($M -e "Intel IPU4 CSI2 BE capture") \
    --set-fmt-video=width=3264,height=2448,pixelformat=BG10 \
    --stream-mmap=4 --stream-count=3 --stream-to=$LOGDIR/rear.raw
echo "REAR exit: $?"; ls -la $LOGDIR/rear.raw 2>/dev/null
snap 02-rear
cycle_island

phase "FRONT (ov5693, CSI-2 2)"
$M -r
$M -V '"ov5693 1-0036":0 [fmt:SBGGR10_1X10/2592x1944]'
$M -V '"Intel IPU4 CSI-2 2":0 [fmt:SBGGR10_1X10/2592x1944]'
$M -V '"Intel IPU4 CSI-2 2":1 [fmt:SBGGR10_1X10/2592x1944]'
$M -V '"Intel IPU4 CSI2 BE":0 [fmt:SBGGR10_1X10/2592x1944]'
$M -V '"Intel IPU4 CSI2 BE":1 [fmt:SBGGR10_1X10/2592x1944]'
$M -l '"ov5693 1-0036":0 -> "Intel IPU4 CSI-2 2":0 [1]'
$M -l '"Intel IPU4 CSI-2 2":1 -> "Intel IPU4 CSI2 BE":0 [1]'
$M -l '"Intel IPU4 CSI2 BE":1 -> "Intel IPU4 CSI2 BE capture":0 [1]'
timeout 25 v4l2-ctl -d $($M -e "Intel IPU4 CSI2 BE capture") \
    --set-fmt-video=width=2592,height=1944,pixelformat=BG10 \
    --stream-mmap=4 --stream-count=3 --stream-to=$LOGDIR/front.raw
echo "FRONT exit: $?"; ls -la $LOGDIR/front.raw 2>/dev/null
snap 03-front

phase "SUMMARY"
for f in tpg rear front; do
    s=$(stat -c%s $LOGDIR/$f.raw 2>/dev/null || echo 0)
    echo "$f.raw: $s bytes"
done
