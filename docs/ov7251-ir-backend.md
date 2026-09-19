# OV7251 IR standalone camera

Status: source and userspace backend implemented; hardware and GNOME Snapshot
qualification remains pending. This feature is deliberately standalone. The
existing front/rear bridge controller does not enumerate, call, switch to, or
restart the IR camera.

## Source reproducibility

The bundled module was originally built from a temporary three-file source
delta that was absent from the repository. The exact patch operations were
recovered from the local session record and integrated into:

- `linux-6.19.8/drivers/media/pci/intel/ipu4/ipu4p-isys-csi2.c`
- `linux-6.19.8/drivers/media/pci/intel/ipu-isys.c`
- `linux-6.19.8/drivers/media/pci/intel/ipu-isys-queue.c`

The delta is source-6-only: BB8 CPHY/DPHY/AFE initialization uses the recorded
Table-B values, and the receiver, firmware, and direct-tap packet diagnostics
are scoped to source 6. Fatal CSI handling is unchanged. The exact target build
was run with:

```sh
./scripts/build-modules.sh
```

Target: `6.19.8-3.surface.fc43.x86_64`. The resulting module and the bundled
module both hash to:

```text
546d80d5c692e0b394b56f4771fbf244bb9427419f6126405fe766fc9b1eaf7b
```

This is a reproducible source build, not a binary-only fix. The module remains
kernel-specific and is not installed or loaded by this change.

## Capture backend

`cbridge/sp7-camera-ir` is an opt-in producer. It:

1. discovers `/dev/media*` and requires the enabled
   `ov7251 2-0060 -> Intel IPU4 CSI-2 1 -> ... capture 0` route;
2. discovers the video node from the media entity major/minor rather than a
   hard-coded video minor;
3. negotiates one-plane, 640x480 packed `Y10` with the direct-tap stride;
4. requests eight MMAP buffers and runs a persistent dequeue/requeue loop;
5. validates `bytesused`, `data_offset`, row headers, error flags, sequence
   progression, monotonic timestamps, and the packed RAW10 layout;
6. decodes RAW10 to grayscale YUYV with chroma `0x80`; and
7. writes the frames to `/dev/video62`.

Malformed or truncated buffers never reach the decoder. Stream errors receive a
bounded reopen/retry sequence; shutdown performs `STREAMOFF`, closes the output,
unmaps buffers, and closes graph/video descriptors. The synthetic decoder test
also covers pixel packing, neutral chroma, truncation rejection, and header
rejection:

```sh
make -C cbridge clean all test-controller test-ir
sh tests/task35-ir-backend-static.sh
```

## Installation and use

The loopback profile reserves `/dev/video62` as `Surface Camera (IR)` with
`exclusive_caps=1`, preserving `/dev/video55`, `/dev/video60`, and
`/dev/video61`. The normal bridge service readiness barrier still checks only
the front/rear endpoints. It never starts `sp7-camera-ir`.

After an authorized installation and after source-6 qualification has passed,
start the producer manually:

```sh
/usr/local/libexec/sp7-camera-ir
```

Once the producer is ready, verify the endpoint's `Device Caps` contains
`Video Capture`, then reopen GNOME Snapshot so it rescans devices. No temporary
module-swapping wrapper is a service dependency.

## Qualification record

The existing direct-tap evidence proves packet and buffer activity, but also
records fatal receiver synchronization states `0x400` and `0x480`. Therefore the
required gate is still not passed:

- 10 minutes of changing, correctly decoded frames: **NOT TESTED/PASS NOT
  CLAIMED**;
- 20 successful stop/start cycles: **NOT TESTED/PASS NOT CLAIMED**;
- Snapshot lists IR separately, shows changing grayscale video, and saves a
  photo: **NOT TESTED**;
- repeated RGB-to-IR switching, app reopen, service restart, reboot, and
  suspend/resume: **NOT TESTED**;
- front/rear preservation: static controller and build tests pass; live RGB
  regression is still required after an authorized installation.

The documented tap run showed receiver SOF/EOF and packet headers and produced
one decodable buffer, but that is not clean continuous raster qualification.
Do not suppress the fatal path or infer success from enumeration.

## Rollback

To stop the standalone producer, send it `SIGTERM` or press `Ctrl-C`; it will
finish its bounded cleanup. To remove the endpoint and restore the previous
loopback profile, run the existing removal script with authorization:

```sh
sudo ./scripts/remove-camera-bridge.sh
```

The script is not run automatically. Module reload alone is not a BB8 hardware
rollback; the recorded experiment left BB8 residue after reload and bounded
power-cycle attempts. Any module replacement, physical power-cycle, reboot, or
suspend/resume qualification requires explicit authorization. IR illumination
and authentication are outside this feature.
