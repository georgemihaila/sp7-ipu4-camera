#!/bin/sh
# Hardware-independent checks for the IPU4P V4L2 stream-state port.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IPU="$ROOT/linux-6.19.8/drivers/media/pci/intel"
CSI="$IPU/ipu-isys-csi2.c"
BE="$IPU/ipu-isys-csi2-be-soc.c"
VIDEO="$IPU/ipu-isys-video.c"
SUBDEV="$IPU/ipu-isys-subdev.c"

# Stream-aware internal entities must provide pad stream operations and must
# not register their old global .s_stream callbacks.
grep -Fq '.enable_streams = csi2_enable_streams' "$CSI"
grep -Fq '.disable_streams = csi2_disable_streams' "$CSI"
grep -Fq '.enable_streams = csi2_be_soc_enable_streams' "$BE"
grep -Fq '.disable_streams = csi2_be_soc_disable_streams' "$BE"
! grep -Fq '.s_stream = set_stream' "$CSI" "$BE"

# The IPU-owned pipeline walker uses V4L2's stream helpers for stream-aware
# entities while retaining the external sensor's separately ordered s_stream.
grep -Fq 'v4l2_subdev_enable_streams(sd, pad, streams_mask)' "$VIDEO"
grep -Fq 'v4l2_subdev_disable_streams(sd, pad, streams_mask)' "$VIDEO"
grep -Fq 'pad = CSI2_PAD_SOURCE(ip->vc)' "$VIDEO"
grep -Fq 'start_stream_firmware(av, bl)' "$VIDEO"
grep -Fq 'v4l2_subdev_call(esd, video, s_stream, state)' "$VIDEO"
stream_op_line=$(grep -n 'ipu_isys_video_subdev_stream(av, sd, ip, state)' "$VIDEO" | head -n 1 | cut -d: -f1)
firmware_line=$(grep -n 'rval = start_stream_firmware(av, bl);' "$VIDEO" | cut -d: -f1)
sensor_line=$(grep -n 'rval = v4l2_subdev_call(esd, video, s_stream, state);' "$VIDEO" | head -n 1 | cut -d: -f1)
[ "$stream_op_line" -lt "$firmware_line" ]
[ "$firmware_line" -lt "$sensor_line" ]

# Routing changes preserve existing TRY formats and seed formats on newly
# activated tuples from the per-pad cache. Finalization locks active V4L2 state.
grep -Fq 'old_route->sink_pad == new_route->sink_pad' "$SUBDEV"
grep -Fq 'old_route->sink_stream == new_route->sink_stream' "$SUBDEV"
grep -Fq 'old_route->source_pad == new_route->source_pad' "$SUBDEV"
grep -Fq 'old_route->source_stream == new_route->source_stream' "$SUBDEV"
grep -Fq 'bool sink_was_active = false' "$SUBDEV"
grep -Fq 'bool source_was_active = false' "$SUBDEV"
grep -Fq 'if (!sink_was_active)' "$SUBDEV"
grep -Fq 'if (!source_was_active)' "$SUBDEV"
grep -Fq 'v4l2_subdev_lock_and_get_active_state(&asd->sd)' "$SUBDEV"
grep -Fq 'v4l2_subdev_unlock_state(state)' "$SUBDEV"

echo 'task14-v4l2-streams-static: PASS'
