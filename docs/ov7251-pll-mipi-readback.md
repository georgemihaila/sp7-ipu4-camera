# OV7251 PLL/MIPI readback experiment

Date: 2026-09-18. Target kernel: `6.19.8-3.surface.fc43.x86_64`.

This was a read-only sensor-register diagnostic. It did not change the
OV7251 register sequence, link-frequency selection, IPU4 configuration, or
CSI timing.

## Attribution and setup

The module was built from the pinned `surface/v6.19.8` OV7251 source
(SHA-256 `3588a52e0a3a4dfe23dd3425db95388d93af17c6f8eebe5b004ee8d1eea5aee`)
with the committed diagnostics and readback patches. Loaded module SHA-256:

```text
4e9fc4cc1f1185c42436678dd449481a2d058ff323cf844b6d29f603fce2b7ef
```

The INT3472 `POWER_ENABLE` to `vdda` experiment remained loaded. The bridge
was stopped and the test boot was:
`11eed947-0511-4c48-98e6-67ae200d7dc2`.

The route and format were the previously validated setup:

```text
ov7251 2-0060 -> Intel IPU4 CSI-2 1 -> Intel IPU4 CSI2 BE SOC -> /dev/video42
Y10_1X10 640x480; video fourcc Y10 ; bytesperline 1280; sizeimage 615680
```

## Readback result

The endpoint contract supplies one link frequency, 319.2 MHz. The source
contains these supported PLL configurations:

| Register | Expected for 319.2 MHz | Read back |
| --- | ---: | ---: |
| `0x30b0` | `0x0a` | `0x0a` |
| `0x30b1` | `0x04` | `0x01` |
| `0x30b3` | `0x85` | `0x4b` |
| `0x30b4` | `0x01` | `0x03` |
| `0x30b5` | `0x05` | `0x05` |
| `0x3098` | `0x04` | `0x04` |
| `0x3099` | `0x32` | `0x32` |
| `0x309a` | `0x05` | `0x05` |
| `0x309b` | `0x04` | `0x04` |
| `0x309d` | `0x00` | `0x00` |

The MIPI mode registers matched the source table:

```text
0x4801=0x0f  0x4806=0x0f  0x4819=0xaa
0x4823=0x3e  0x4837=0x19
```

All 15-register reads returned success. The mismatch is specifically the
PLL1 240 MHz versus 319.2 MHz selection, not an I2C readback failure.

## Capture result

Across 31 `s_stream(1)` attempts, the readbacks were identical:

- `0x0100` write succeeded and read back `0x01` 31 times;
- PLL and MIPI readbacks succeeded 31 times;
- CSI reported error `0x4000` and no data payload;
- 30 no-frame retries ended with one stream-stop timeout;
- the requested 120-frame output remained 0 bytes;
- no functional change was made during this experiment.

This is strong evidence for a link-frequency-index defect. In
`ov7251_check_hwcfg()`, the source finds the matching supported-link index in
`j` but stores the endpoint-list index `i` in `link_freq_idx`. With one
319.2 MHz endpoint entry, that produces `link_freq_idx=0`, selecting the
240 MHz PLL table. The causal fix still requires a separate one-variable
test; this experiment did not alter that assignment.

## Rollback

The signed distribution module was never overwritten. Its compressed
SHA-256 remains
`00cfa05cbdf46a8d6c55b077d7729fa419a3a072d88bb343b5985d4e3ad4deac`.
The temporary module and modprobe override were moved out of live paths,
`depmod -a` was run, and the system rebooted into
`abc0f116-80c4-4b39-bb8c-bb4bb18cdd95`.

Post-rollback: the signed OV7251 module is loaded, the temporary override is
absent, OV7251 still probes revision 7, and `sp7-camera-bridge.service` is
active.

The raw capture, journal excerpt, graph, and negotiated format are retained
outside Git under `/var/tmp/ov7251-pll-mipi-20260918/`.
