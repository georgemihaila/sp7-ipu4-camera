#!/bin/sh
# Hardware-independent checks for the OV7251 failed-start diagnosis boundary.
#
# This test deliberately records the ownership and lifetime facts that make a
# future kernel change reviewable.  It does not claim to prove the saved OOPS
# cause, and it must not turn an unproven cancellation or subdevice-lifetime
# workaround into a runtime change.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ISYS="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu-isys.c"
VIDEO="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu-isys-video.c"
QUEUE="$ROOT/linux-6.19.8/drivers/media/pci/intel/ipu-isys-queue.c"

for source in "$ISYS" "$VIDEO" "$QUEUE"; do
	test -s "$source"
done

# The external sensor is registered on the ISYS v4l2_device.  This is the
# available static lifetime evidence; the pipeline stores a media-pad pointer
# and does not independently acquire a subdevice reference for verification.
grep -Fq 'v4l2_i2c_new_subdev_board(&isys->v4l2_dev' "$ISYS"
grep -Fq 'v4l2_device_unregister(&isys->v4l2_dev);' "$ISYS"
grep -Fq 'esd = media_entity_to_v4l2_subdev(ip->external->entity);' "$QUEUE"
# The receiver diagnostic wrapper preserves the original dispatch in
# ipu-isys-video.c while giving the retry sites stable evidence labels.
grep -Fq 'ipu_isys_lifecycle_s_stream(dev, ip, esd, 0' "$QUEUE"
grep -Fq 'ipu_isys_lifecycle_s_stream(dev, ip, esd, 1' "$QUEUE"
grep -Fq 'rval = v4l2_subdev_call(sd, video, s_stream, enable);' "$VIDEO"

# A single-queue STREAMON callback runs under the video queue mutex.  The
# driver transfers that ownership before waiting/retrying; release therefore
# cannot cancel the callback through stop_streaming while it is in progress.
grep -Fq 'av->vdev.lock = &av->mutex;' "$VIDEO"
grep -Fq 'aq->vbq.lock = &ipu_isys_queue_to_video(aq)->mutex;' "$QUEUE"
grep -Fq 'mutex_unlock(&av->mutex);' "$QUEUE"
grep -Fq 'mutex_lock(&pipe_av->mutex);' "$QUEUE"
grep -Fq 'vb2_fop_release(file);' "$VIDEO"

# Retry and parking are bounded and have an explicit active/inactive handoff.
grep -Fq 'atomic_set(&ip->verify_active, 1);' "$QUEUE"
grep -Fq 'atomic_set(&ip->verify_active, 0);' "$QUEUE"
grep -Fq 'for (retry = 0; retry <= 30; retry++)' "$QUEUE"
grep -Fq 'for (tick = 0; tick < 12; tick++)' "$QUEUE"
grep -Fq 'msleep(50);' "$QUEUE"
grep -Fq 'msleep(20);' "$QUEUE"

# Firmware stop/flush and close have distinct acknowledgements.  A timeout is
# surfaced as a failed cleanup, not converted into a successful close.
grep -Fq 'reinit_completion(&ip->stream_stop_completion);' "$VIDEO"
grep -Fq 'wait_for_completion_timeout(&ip->stream_stop_completion' "$VIDEO"
grep -Fq 'reinit_completion(&ip->stream_close_completion);' "$VIDEO"
grep -Fq 'wait_for_completion_timeout(&ip->stream_close_completion' "$VIDEO"
grep -Fq 'complete(&pipe->stream_stop_completion);' "$ISYS"
grep -Fq 'complete(&pipe->stream_close_completion);' "$ISYS"
grep -Fq 'return close_failed ? (rval ? rval : -EIO) : 0' "$VIDEO"

# Stream-start cleanup preserves the original failure and clears software
# streaming state even if firmware stop/close acknowledgement is missing.
grep -Fq 'int stop_rval;' "$QUEUE"
grep -Fq 'failed to stop pipeline after stream-start error' "$QUEUE"
grep -Fq 'ip->streaming = 0;' "$QUEUE"
grep -Fq 'return rval;' "$QUEUE"

printf '%s\n' 'task39-ipu4p-lifecycle-diagnosis-static: PASS'
