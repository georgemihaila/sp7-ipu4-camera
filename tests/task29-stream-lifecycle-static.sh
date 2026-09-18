#!/bin/sh
# Hardware-independent checks for failed-start/stop vb2 ownership.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
QUEUE="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu-isys-queue.c"
VIDEO="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu-isys-video.c"

test -s "$QUEUE"
test -s "$VIDEO"

# A failed firmware start must remain an error even when its cleanup fails;
# software state is reset so a later STREAMON cannot reuse a stale pipeline.
start_cleanup=$(sed -n '/^out_requeue:/,/^}/p' "$QUEUE")
printf '%s\n' "$start_cleanup" | grep -Fq 'int stop_rval'
printf '%s\n' "$start_cleanup" | grep -Fq 'failed to stop pipeline after stream-start error'
printf '%s\n' "$start_cleanup" | grep -Fq 'ip->streaming = 0'
! printf '%s\n' "$start_cleanup" | grep -Fq 'rval = 0'

# Active buffers are detached under the queue lock and completed once outside
# the spinlock.  A start_streaming callback failure returns QUEUED per vb2;
# a failure after streaming has begun returns ERROR.  This prevents a retry
# from seeing a buffer still linked after vb2_buffer_done().
flush=$(sed -n '/^static void flush_firmware_streamon_fail/,/^}/p' "$QUEUE")
printf '%s\n' "$flush" | grep -Fq 'enum vb2_buffer_state state'
printf '%s\n' "$flush" | grep -Fq 'list_move_tail(&ib->head, &bl.head)'
printf '%s\n' "$flush" | grep -Fq 'bl.nbufs++'
printf '%s\n' "$flush" | grep -Fq 'IPU_ISYS_BUFFER_LIST_FL_SET_STATE'
! printf '%s\n' "$flush" | grep -Fq 'vb2_buffer_done'
grep -Fq 'error ? VB2_BUF_STATE_ERROR' "$QUEUE"
grep -Fq 'VB2_BUF_STATE_QUEUED);' "$QUEUE"

# Streamoff observes and logs teardown failures, while counter/list updates
# are guarded and idempotent.
stop=$(sed -n '/^static void stop_streaming/,/^}/p' "$QUEUE")
printf '%s\n' "$stop" | grep -Fq 'rval = ipu_isys_video_set_streaming(av, 0, NULL)'
printf '%s\n' "$stop" | grep -Fq 'failed to stop pipeline during streamoff'
printf '%s\n' "$stop" | grep -Fq 'if (ip->nr_streaming > 0)'
printf '%s\n' "$stop" | grep -Fq 'list_del_init(&aq->node)'

# The queue callback can only preserve a teardown failure if the video-layer
# firmware/sensor cleanup returns it instead of unconditionally returning 0.
grep -Fq 'static int close_streaming_firmware' "$VIDEO"
grep -Fq 'return close_failed ? (rval ? rval : -EIO) : 0' "$VIDEO"
grep -Fq 'static int stop_external_sensor' "$VIDEO"
grep -Fq 'return state ? 0 : cleanup_rval' "$VIDEO"

echo 'task29-stream-lifecycle-static: PASS'
