# OV7251 illuminator: first bounded enabled comparison

Date: 2026-09-20
Result: register-control path exercised; enabled capture regressed before a valid frame; optical observation inconclusive.

## Scope and provenance

This was one temporary, userspace-enabled comparison using the documented source-6 diagnostic route from commit `41caf08`. No module was installed permanently, no reboot, force-unload, power-cycle, authentication change, or push was used.

The running kernel was `6.19.8-3.surface.fc43.x86_64`. The temporary artifacts were:

| Artifact | SHA-256 |
|---|---|
| Candidate OV7251 module | `2daacb2fea6176955ef15ac887fad5d20c2b7bf57904bd6b45030376f05f6f75` |
| Pinned source-6 diagnostic ISYS module | `546d80d5c692e0b394b56f4771fbf244bb9427419f6126405fe766fc9b1eaf7b` |
| Original OV7251 module | `00cfa05cbdf46a8d6c55b077d7729fa419a3a072d88bb343b5985d4e3ad4deac` |
| Original ISYS module | `10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce` |

The candidate was loaded with `experimental_strobe_output=0/1` and `strobe_diagnostics=1`; the receiver used `debug_capture_links=1`. The fixed sensor controls were 640x480, exposure 504, and analogue gain 16. The receiver configuration, BB8 setup, capture command, decoder, and image processing were unchanged.

## Fresh off observation

The off capture produced a fresh decoded 640x480 frame. Raw payload was 399356 bytes with 399360 bytes used by the buffer; the decoded pixel statistics were min 14, max 1023, mean 358.6245, and standard deviation 223.2159. The frame SHA-256 was:

`48d6108bdbe0fdf093bea8424f51abb6282270f43b5f1f9da0d6a19247c10dee`

Controls read back as exposure 504 and analogue gain 16. The relevant off state was `0x3005=0x00` and `0x3b96=0x40`.

## One enabled attempt

There was one userspace capture attempt with the same 15-second deadline. The candidate's initialization/stream-start path performed the prepared two-field update:

| Register | Before | Enabled | Readback | After cleanup |
|---|---:|---:|---:|---:|
| `0x3005[3]` | `0x00` | `0x08` | `0x08` | `0x00` |
| `0x3b96[7]` | `0x40` | `0xc0` | `0xc0` | `0x40` |

The other diagnostic registers, including `0x3027`, `0x3009`, and `0x3b80..0x3b95`, retained their pre-enable values. Cleanup readbacks confirmed both experimental bits clear and the unrelated bits restored. The driver reported `first-error=0` for the cleanup operation; the independent optical shutdown state was not verified.

The capture did not produce a valid enabled buffer: the raw output was zero bytes, no decoded image was written, and the bounded command ended with status 137. The kernel also recorded the known receiver `fatal_receiver_errors=0x480` path and a stream-stop error `-5`. These are reported as capture/receiver limitations; they are not evidence that the two register writes failed. Existing receiver/internal retry callbacks appeared in the kernel log, but no additional userspace capture attempts or enabled cycles were started.

Because the first enabled attempt regressed before a valid frame, the requested three-cycle continuation was not performed.

## Optical observation during the first attempt

No usable independent observation was obtained. The user reported that the USB camera was not visible in the camera application and did not report a positive naked-eye signal. This is not evidence that the emitter stayed off: wavelength sensitivity, exposure, pulse sampling, and the app's camera publication are uncharacterized. No detector-backed emission or shutdown claim can be made.

## Restoration and camera safety

The candidate and diagnostic receiver were removed with ordinary module removal. The original signed OV7251 and distribution ISYS modules were restored; their hashes matched the preflight values. `debug_capture_links` returned to `N`, the normalized media graph and formats matched the preflight snapshot, and the bridge, PipeWire, and WirePlumber states were restored.

Post-restore front and rear RGB captures were valid:

| Path | SHA-256 |
|---|---|
| Front RGB | `6bad287522bfe4707e912eb58b400d08e9a1239067e345752cd1fb80796f71e9` |
| Rear RGB | `eb3ce3ffa3da8119538bba1a70a4e1da0af6b025ba0f931662757a5edf0243` |

The known BB8 before/after logging was preserved as diagnostic evidence;
hardware-register rollback cannot be claimed from this run. The first attempt
was not retried under its original conditions. An independent optical-power
claim still requires a usable external observer; the fixed-scene OV7251 image
comparison below is explicitly a secondary, non-independent observation.

Bulky logs, captures, register dumps, and restoration snapshots are outside the repository at:

`/var/tmp/ov7251-illuminator-comparison-20260920-3Vfqg8/`

## Follow-up fixed-scene image comparison

After the first enabled attempt, a separate single off/on comparison was run
at the user's request with an object held in front of the tablet. This used the
same temporary source-6 route, candidate module, decoder, 640x480 format,
exposure 504, analogue gain 16, and 15-second deadline. The receiver and
BB8 configuration were unchanged. There was one userspace off capture and one
userspace enabled capture; no output setting was increased.

Both captures produced valid 399356-byte decoded payloads using 399360-byte
buffers. The candidate's enabled path read back the prepared changes:

```text
0x3b96: 0x40 -> 0xc0, readback 0xc0
0x3005: 0x00 -> 0x08, readback 0x08
```

On cleanup, the driver read back `0x3005=0x00` and `0x3b96=0x40`, with
`first-error=0`. The fixed controls remained exposure 504 and analogue gain
16 in both captures.

| Frame | PNG SHA-256 | Pixel min/max | Mean | Standard deviation |
|---|---|---:|---:|---:|
| Off | `092d777ae59b1e729b0be4f75532a5e0dafb56500eaa811802267310e84974c1` | 14 / 1023 | 292.5960 | 260.1833 |
| On | `2388c6f5d2cf6baaa4b83ffe42dc218f0b620dee4de52aa9f3fa921bc0dcfac6` | 13 / 1023 | 299.6405 | 257.9156 |

The user visually confirmed an unequivocal difference between the displayed
off and on images. This is positive evidence that the enabled control changes
the OV7251 scene response and is consistent with useful illumination. It is
not an independent optical-power measurement: the same OV7251 is both the
sample source and the observing sensor, so wavelength response, spatial
uniformity, absolute output, and calibrated shutdown remain unresolved.

The fresh comparison artifacts are outside the repository at:

`/var/tmp/ov7251-image-comparison-20260920-1sVMjm/object-scene/`
