#!/bin/bash
# Validate the SOF-retry (sensor bounce) fix after a clean boot:
#   sudo ./validate-retry.sh
# Expects: TPG + rear clean (no bounces), front ~6/6 (bounces allowed).
ROOT=$(dirname "$(readlink -f "$0")")
OUTPUT_DIR=${OUTPUT_DIR:-"$ROOT/captures"}
LOGDIR=${LOGDIR:-"$ROOT/logs"}
mkdir -p "$LOGDIR"

cycle_island() {
    echo auto > /sys/bus/intel-ipu4-bus/devices/intel-ipu4-mmu1/power/control
    echo on   > /sys/bus/intel-ipu4-bus/devices/intel-ipu4-mmu0/power/control
    sleep 1
    echo auto > /sys/bus/intel-ipu4-bus/devices/intel-ipu4-mmu0/power/control
    sleep 5
    echo on   > /sys/bus/intel-ipu4-bus/devices/intel-ipu4-mmu1/power/control
    sleep 1
}

# A logged-in graphical session starts wireplumber, which grabs every
# /dev/video* node the moment the IPU modules load. Concurrent opens wreck
# captures AND pin isys active so cycle_island can't clear a reset latch.
# Stop it for the whole run (runtime mask only; gone after reboot).
if pgrep -x wireplumber > /dev/null; then
    echo "WARNING: wireplumber running (user session logged in) - masking it for this run"
    target_user=${SUDO_USER:-$(logname 2>/dev/null || true)}
    if [[ -n $target_user && $target_user != root ]]; then
        runuser -l "$target_user" -c 'systemctl --user mask --runtime wireplumber; systemctl --user stop wireplumber' 2>/dev/null
    fi
    sleep 2
fi
fuser -k /dev/video* /dev/media0 2>/dev/null && sleep 2

echo "########## LOAD ##########"
"$ROOT/load-ipu4.sh" || exit 1
sleep 1

echo "########## TPG + REAR sanity ##########"
dmesg -C
M="media-ctl -d /dev/media0"
$M -r
v4l2-ctl -d $($M -e "Intel IPU4 TPG 0") -c test_pattern=2
$M -V '"Intel IPU4 TPG 0":0 [fmt:SBGGR8_1X8/1920x1080]'
$M -l '"Intel IPU4 TPG 0":0 -> "Intel IPU4 TPG 0 capture":0 [1]'
timeout 40 v4l2-ctl -d $($M -e "Intel IPU4 TPG 0 capture") \
    --set-fmt-video=width=1920,height=1080,pixelformat=BA81 \
    --stream-mmap=4 --stream-count=3 --stream-to=$LOGDIR/tpg.raw
echo "TPG: $(stat -c%s $LOGDIR/tpg.raw 2>/dev/null || echo 0) bytes (want 6428148)"
cycle_island
OUTPUT_DIR="$OUTPUT_DIR" "$ROOT/test-capture.sh" rear > "$LOGDIR/rear.out" 2>&1
echo "REAR: $(stat -c%s "$OUTPUT_DIR/rear.raw" 2>/dev/null || echo 0) bytes (want 47941632)"
echo "rear/tpg bounces (want 0): $(dmesg | grep -c 'bouncing sensor')"
dmesg > $LOGDIR/sanity.dmesg
cycle_island

echo "########## FRONT x6 (SOF-retry active) ##########"
ok=0
for i in 1 2 3 4 5 6; do
    dmesg -C
    rm -f "$OUTPUT_DIR/front.raw"
    OUTPUT_DIR="$OUTPUT_DIR" "$ROOT/test-capture.sh" front > "$LOGDIR/vfront-$i.out" 2>&1
    rc=$?
    size=$(stat -c%s "$OUTPUT_DIR/front.raw" 2>/dev/null || echo 0)
    bounces=$(dmesg | grep -c 'bouncing sensor')
    storms=$(dmesg | grep -c 'status 0x400')
    dmesg > "$LOGDIR/vfront-$i.dmesg"
    echo "front try $i: exit=$rc size=$size bounces=$bounces storms=$storms"
    [ "$size" -gt 0 ] && ok=$((ok+1))
    cycle_island
done
echo "FRONT SUCCESS RATE: $ok/6"
