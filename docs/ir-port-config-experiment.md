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
| Test `0x2e95` | `VIDIOC_STREAMON` returned `-1 (Connection timed out)`; output size was 0 bytes |

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
  retained as `/tmp/99-sp7-ipu4p-port-config.conf.rolledback`;
- `sp7-camera-bridge.service` was active again.

## Interpretation

The `0x2e95` change is a demonstrated negative result for this IR capture
path. It does not explain the missing CSI data and is not retained as a live
configuration. The remaining no-frame blocker is unresolved; further PHY or
receiver mutations require a new source-backed discrepancy rather than this
experiment's result.
