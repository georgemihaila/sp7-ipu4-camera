# OV7251 IR CSI port-config experiment

Date: 2026-09-18
Host: Surface Pro 7
Kernel: `6.19.8-3.surface.fc43.x86_64`

## Purpose

Test the source-6/one-lane OV7251 receiver configuration as one bounded
variable after the INT347E `POWER_ENABLE` to `vdda` mapping fixed probe.

The experiment parameterized both IPU4P CSI GPREG port-config writes. The
default remains the qualified RGB value `0x3895`; the temporary test selected
`0x2e95` through:

```text
options intel_ipu4p_isys csi2_port_config=0x2e95
```

The test module was built from commit `aa47d31` and was attributed in the
running kernel by its loaded-module hash and the sysfs parameter value
`11925` (`0x2e95`).

## Preconditions and route

- The temporary INT3472 module containing the upstream OV7251 `vdda` mapping
  was loaded from `/lib/modules/6.19.8-3.surface.fc43.x86_64/extra/`.
- OV7251 probe succeeded and reported revision 7 at I2C address `0x60`.
- The sensor advertised `Y10_1X10`, `640x480`, and 30/60/90 fps modes.
- The bridge service was stopped for the exclusive IR test.
- The media route was sensor -> CSI-2 1 -> CSI2 BE SOC -> BE SOC capture 0,
  with the dynamic links enabled using media-controller flags `5`
  (`MEDIA_LNK_FL_DYNAMIC | MEDIA_LNK_FL_ENABLED`).
- The negotiated format at every route stage was `Y10_1X10`, `640x480`;
  `/dev/video42` reported fourcc `Y10 `, bytesperline `1280`, and
  sizeimage `615680`.

## Results

| Configuration | Result |
| --- | --- |
| Default `0x3895` | `VIDIOC_STREAMON` returned `-1 (Connection timed out)`; output size was 0 bytes |
| Test `0x2e95` in both banks | `VIDIOC_STREAMON` returned `-1 (Connection timed out)`; output size was 0 bytes |

Both attempts produced the same receiver behavior:

```text
csi2-1 receiver error status 0x4000
no frames from ov7251 2-0060 after start; bouncing sensor (retry 1..30)
no clean frames ... after 31 sensor attempts
stream stop time out (source=6 handle=0 vc=0 stream_id=0 send_type=5)
failed to stop pipeline after stream-start error: -5
```

No data-lane HS/SOF/EOF evidence or valid payload was observed. The
`vdda` regulator was disabled again after the failed attempt. The test did
not reach raw-frame validation, packing analysis, 120-frame capture, or
preview qualification.

## Asymmetric Windows-reconstructed follow-up

The historical static AddInput reconstruction identifies a different pair
that had not been tested on this IR path: legacy bank `0x38b4`, combo bank
`0x2e95`. A two-parameter module was built from the same source tree and
loaded with:

```text
options intel_ipu4p_isys csi2_legacy_port_config=0x38b4 csi2_combo_port_config=0x2e95
```

The loaded module hash was
`2128f6cc5b410027cf219f71f6ad464a9a251897cbd943ce9c6f1acdbb317520` and
the live sysfs values were `14516` (`0x38b4`) and `11925` (`0x2e95`). The
same dynamic BE SOC route and `Y10 ` 640x480 format were used. The result
was again:

- `VIDIOC_STREAMON returned -1 (Connection timed out)`;
- `/tmp/ir-asymmetric-1789734282.raw` remained 0 bytes;
- receiver status was `0x4000` with no clean frames after 30 retries;
- stream stop timed out and cleanup returned `-5`.

This tests the remaining known static port-config discrepancy and is also
negative for IR capture. It does not justify further port-config mutation.

## Rollback proof

The temporary module was replaced with the exact baseline binary whose
SHA-256 is:

```text
10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce
```

After reboot:

- the live module matched that hash;
- `/sys/module/intel_ipu4p_isys/parameters/csi2_port_config` was absent;
- `modprobe --show-depends intel_ipu4p_isys` selected the normal module path
  without a test parameter;
- `99-sp7-ipu4p-port-config.conf` was absent from `/etc/modprobe.d` and was
  moved out of `/etc/modprobe.d` before reboot (the volatile `/tmp` copy was
  cleared by the subsequent reboot);
- `sp7-camera-bridge.service` was active again.

## Interpretation

Both the symmetric `0x2e95` test and the asymmetric
`0x38b4`/`0x2e95` test are demonstrated negative results for this IR capture
path. Neither explains the missing CSI data, and neither is retained as a
live configuration. The remaining no-frame blocker is unresolved; further
PHY or receiver mutations require a new source-backed discrepancy rather
than these experiments' results.
