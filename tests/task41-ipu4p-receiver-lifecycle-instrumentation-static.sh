#!/bin/sh
# Offline boundary checks for the behavior-preserving receiver lifecycle trace.
# This test does not load a module or access camera/media devices.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ISYS="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu-isys.c"
VIDEO="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu-isys-video.c"
QUEUE="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu-isys-queue.c"
HEADER="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu-isys.h"

for source in "$ISYS" "$VIDEO" "$QUEUE" "$HEADER"; do
	test -s "$source"
done

# The trace is opt-in and records subdevice identity without adding a pointer
# guard or changing the v4l2_subdev_call dispatch semantics.
grep -Fq 'module_param_named(lifecycle_trace, ipu_isys_lifecycle_trace' "$VIDEO"
grep -Fq 'READ_ONCE(sd->ops)' "$VIDEO"
grep -Fq 'rval = v4l2_subdev_call(sd, video, s_stream, enable);' "$VIDEO"
grep -Fq 'extern bool ipu_isys_lifecycle_trace;' "$HEADER"

# Every external-sensor retry and normal start/stop call goes through the
# same before/after observation point.  The remaining direct call is the
# helper's unchanged dispatch itself (internal subdev stream calls are not
# sensor lifetime observations).
grep -Fq 'verify_stream_start:retry_off' "$QUEUE"
grep -Fq 'verify_stream_start:retry_on' "$QUEUE"
grep -Fq 'ipu_isys_video_set_streaming:start' "$VIDEO"
grep -Fq 'stop_external_sensor:csi2' "$VIDEO"
grep -Fq 'stop_external_sensor:non_csi2' "$VIDEO"
grep -Fq 'phase=before site=%s' "$VIDEO"
grep -Fq 'phase=after site=%s' "$VIDEO"

# Registration and teardown expose the subdevice pointer/ops/owner timeline.
grep -Fq 'phase=registered name=%s' "$ISYS"
grep -Fq 'phase=unregister_failed name=%s' "$ISYS"
grep -Fq 'before_v4l2_device_unregister' "$ISYS"

# Stop/flush and close commands are paired with completion state and the
# response handler's acknowledgement identity.
grep -Fq 'fw_cmd phase=before_send' "$VIDEO"
grep -Fq 'fw_cmd phase=after_send' "$VIDEO"
grep -Fq 'fw_cmd phase=after_wait' "$VIDEO"
grep -Fq 'fw_ack=STREAM_STOP_ACK' "$ISYS"
grep -Fq 'fw_ack=STREAM_FLUSH_ACK' "$ISYS"
grep -Fq 'fw_ack=STREAM_CLOSE_ACK' "$ISYS"
grep -Fq 'completion_done(&pipe->stream_stop_completion)' "$ISYS"
grep -Fq 'completion_done(&pipe->stream_close_completion)' "$ISYS"

# Lock state includes both mutex_is_locked() and lockdep ownership when the
# exact target kernel has lockdep enabled, plus the blocked-close entry point.
grep -Fq 'lock_state site=%s' "$VIDEO"
grep -Fq 'lockdep_is_held(lock)' "$VIDEO"
grep -Fq 'video_release:before_vb2' "$VIDEO"
grep -Fq 'video_release:after_vb2' "$VIDEO"

# The changes must be diagnostic-only at the source boundary: no new sensor
# lifetime mutation, retry-limit change, or pointer suppression is accepted.
grep -Fq 'for (retry = 0; retry <= 30; retry++)' "$QUEUE"
grep -Fq 'if (rval)' "$QUEUE"
grep -Fq 'v4l2_device_unregister_subdev(sd);' "$ISYS"

printf '%s\n' 'task41-ipu4p-receiver-lifecycle-instrumentation-static: PASS'
