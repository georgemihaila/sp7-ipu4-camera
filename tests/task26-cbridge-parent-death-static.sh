#!/bin/sh
# Static contract checks for worker parent-death protection.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CROOT="$ROOT/cbridge"
SOURCE="$CROOT/surface-camera-bridge.c"

test -f "$SOURCE"
grep -Fq '#include <sys/prctl.h>' "$SOURCE"
grep -Fq 'PR_SET_PDEATHSIG, SIGKILL' "$SOURCE"
grep -Fq 'getppid() != parent_pid' "$SOURCE"
grep -Fq 'worker could not set parent-death signal' "$SOURCE"
grep -Fq 'worker_process(&config, status_pipe[1], parent_pid)' "$SOURCE"

# The guard must run before any GStreamer/libcamera initialization in the
# worker; this is intentionally checked against the source order.
awk '
  /configure_worker_parent_death\(parent_pid\)/ { guard = NR }
  /gst_init\(NULL, NULL\)/ { init = NR }
  END { exit !(guard > 0 && init > guard) }
' "$SOURCE"

printf '%s\n' 'task26-cbridge-parent-death-static: PASS'
