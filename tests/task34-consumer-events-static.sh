#!/bin/sh
# Hardware-independent checks for event-triggered consumer reconciliation.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CROOT="$ROOT/cbridge"
SOURCE="$CROOT/surface-camera-bridge.c"

grep -Fq '#include <sys/inotify.h>' "$SOURCE"
grep -Fq 'inotify_init1(IN_NONBLOCK | IN_CLOEXEC)' "$SOURCE"
grep -Fq 'IN_OPEN | IN_CLOSE_NOWRITE | IN_CLOSE_WRITE' "$SOURCE"
grep -Fq 'drain_consumer_events' "$SOURCE"
grep -Fq 'IN_Q_OVERFLOW' "$SOURCE"
grep -Fq 'falling back to polling' "$SOURCE"

make -C "$CROOT" clean all test-controller
printf '%s\n' 'task34-consumer-events-static: PASS'
