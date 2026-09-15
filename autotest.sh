#!/bin/bash
# ARCHIVED EXPERIMENTAL TOOL. Not part of the install or production path.
# It was used for a local autonomous bring-up cycle and is intentionally not
# referenced by any retained systemd unit. Do not enable it as a service.
#
# Each boot: run the validation suite, then resume the Claude Code session
# headlessly so it can analyze results, fix, rebuild, and reboot for the
# next cycle. This script NEVER reboots on its own — only the resumed
# Claude session decides to reboot, so a failure here leaves the machine up.
#
# Kill switches:
#   sudo touch ./autotest/DONE          (skip all future cycles)
# Hard cap: MAX_BOOTS cycles, then the service disables itself.

CAM=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
AT=$CAM/autotest
MAX_BOOTS=8

mkdir -p "$AT"
[ -f "$AT/DONE" ] && exit 0

n=$(cat "$AT/count" 2>/dev/null || echo 0)
n=$((n + 1))
echo "$n" > "$AT/count"

if [ "$n" -gt "$MAX_BOOTS" ]; then
    echo "max boots ($MAX_BOOTS) reached" > "$AT/DONE"
    exit 0
fi

BOOTDIR=$AT/boot-$n
mkdir -p "$BOOTDIR"
exec > "$BOOTDIR/autotest.log" 2>&1
set -x
date

# Let CSE/ISP firmware settle after boot (manual-load flow assumed a
# leisurely human login before touching the ISP).
sleep 60

# Fresh logs dir for this cycle; keep the previous one with the boot record.
[ -d "$CAM/logs" ] && mv "$CAM/logs" "$BOOTDIR/prev-logs"
mkdir -p "$CAM/logs"

timeout 900 "$CAM/validate-retry.sh" > "$CAM/logs/validate.out" 2>&1
echo "validate-retry exit: $?"
cp -r "$CAM/logs" "$BOOTDIR/logs"

# Need network for the Claude API before handing over control.
nm-online -q -t 180
sleep 5

if command -v claude >/dev/null 2>&1; then
    claude --continue --dangerously-skip-permissions -p "ARCHIVED IPU4 bring-up cycle $n of $MAX_BOOTS. Results: $CAM/logs/validate.out; boot record: $BOOTDIR. Analyze and record findings in $AT/RESULT.md. Do not reboot, install services, or change runtime system configuration." > "$BOOTDIR/claude.out" 2>&1
else
    echo 'claude not found; leaving validation results for manual review' > "$BOOTDIR/claude.out"
fi
echo "claude exit: $?"
date
