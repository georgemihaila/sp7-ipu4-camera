#!/bin/bash
# Load the IPU4 camera driver stack in the required order.
# Modules are blacklisted in /etc/modprobe.d/ipu4.conf, so run this
# once after each boot (as root).
set -e

modprobe ipu_bridge
modprobe intel_ipu4p_isys_csslib
modprobe intel_ipu4p_psys_csslib
# The Surface Pro 7 ships the signed 2019 CPD while the open driver carries
# the older 2018 CSS release identifier.  The kernel driver's strict check
# rejects that known-good firmware before the camera graph is even created;
# the reference setup intentionally disables only this metadata check.
modprobe intel_ipu4p fw_version_check=0
modprobe intel_ipu4p_psys

# Pin mmu1 (psys island) ON before the isys module creates video nodes.
# Otherwise the udev v4l_id probe storm (~50 node opens, each
# re-authenticating fw) bounces the psys power island off/on rapidly;
# one buttress power handshake timing out (-ETIMEDOUT, "Change power
# status timeout") latches mmu1 into runtime PM 'error' state, which is
# unrecoverable without a reboot. Island cycles must temporarily set
# this back to auto (see cycle_island in the test scripts).
MMU1=/sys/bus/intel-ipu4-bus/devices/intel-ipu4-mmu1/power/control
for i in $(seq 1 50); do
    [ -e "$MMU1" ] && break
    sleep 0.1
done
echo on > "$MMU1" || echo "WARN: could not pin mmu1" >&2

modprobe intel_ipu4p_isys

sleep 2
if [ -e /dev/media0 ]; then
    echo "IPU4 stack loaded, /dev/media0 present:"
    dmesg | grep -E 'CSE|Connected.*cameras' | tail -4
else
    echo "ERROR: /dev/media0 missing, check dmesg" >&2
    exit 1
fi

# Unpin mmu1 once the udev probe storm is over (30s is plenty). With
# mmu1 permanently pinned, mmu0 can never suspend, so a wedged stream's
# reset_needed latch could only be cleared by a manual island cycle —
# app-driven use (libcamera/PipeWire) needs the island to self-heal via
# runtime PM instead. The handshake-timeout race the pin guards against
# only occurs during the load-time node-probe storm.
(
    sleep 30
    echo auto > "$MMU1" 2>/dev/null || true
) &
disown
