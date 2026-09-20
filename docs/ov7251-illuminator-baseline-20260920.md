# OV7251 illuminator default-off diagnostic baseline

Date: 2026-09-20
Kernel: `6.19.8-3.surface.fc43.x86_64`
Starting commit: `e65507b370e06e0cffdbd3757b956edc1f986a0a`
Experiment: default-off only; no independent detector was required.

## Result

The documented source-6 diagnostic route was reproduced with the prepared
OV7251 module held at `experimental_strobe_output=0` and
`strobe_diagnostics=1`. Two bounded captures, including one stop/reopen,
returned fresh valid decoded images and valid strobe-register diagnostics.

This establishes a usable bounded diagnostic capture path. It does not
establish stable continuous capture: the receiver continued to report the
documented fatal synchronization status (`fatal_receiver_errors=0x480`),
although the CSI header tap completed buffers and the decoder accepted them.
No illuminator was enabled and no optical conclusion is made.

## Provenance and preflight

| Artifact/state | Recorded value |
| --- | --- |
| Candidate OV7251 | `/var/tmp/ov7251-illuminator-experiment-pmfix.ko`, SHA-256 `2daacb2fea6176955ef15ac887fad5d20c2b7bf57904bd6b45030376f05f6f75` |
| Candidate origin | commit `9bf2748`, built for the running kernel; `vermagic=6.19.8-3.surface.fc43.x86_64 SMP preempt mod_unload` |
| Original OV7251 | `/lib/modules/6.19.8-3.surface.fc43.x86_64/kernel/drivers/media/i2c/ov7251.ko.xz`, SHA-256 `00cfa05cbdf46a8d6c55b077d7729fa419a3a072d88bb343b5985d4e3ad4deac`; Fedora-signed |
| Bundled diagnostic ISYS | `scripts/ir/intel-ipu4p-isys-csi-header-tap-bb8.ko`, SHA-256 `546d80d5c692e0b394b56f4771fbf244bb9427419f6126405fe766fc9b1eaf7b` |
| Distribution ISYS | `/lib/modules/6.19.8-3.surface.fc43.x86_64/updates/extra/intel-ipu4p-isys.ko`, SHA-256 `10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce` |
| Original receiver parameter | `debug_capture_links=N` |
| Original services | `sp7-camera-bridge.service` enabled/active; PipeWire active; WirePlumber enabled/active |
| Original users | the bridge owned `/dev/video60` and `/dev/video61`; it was stopped gracefully before replacement |

The full preflight graph, module state, users, service state, restoration
plan, and raw capture logs are outside the repository at:

`/var/tmp/ov7251-illuminator-baseline-20260920-ZoMmqf/`

## Temporary route

The ordinary distribution modules were removed and the candidate was inserted
temporarily. The candidate reported:

```text
experimental_strobe_output=N
strobe_diagnostics=Y
```

The bundled receiver was inserted with `debug_capture_links=1`. Discovery,
not a fixed node assumption, found:

```text
ov7251 2-0060        /dev/v4l-subdev13 (during diagnostic run)
Intel IPU4 CSI-2 1   /dev/v4l-subdev1
capture 0            /dev/video5
```

The two documented links were then enabled:

```text
ov7251 2-0060:0 -> Intel IPU4 CSI-2 1:0
Intel IPU4 CSI-2 1:1 -> Intel IPU4 CSI-2 1 capture 0:0
```

The receiver emitted the recorded BB8 initialization on each stream start:

```text
before=(0x1001b,0x0,0x44104015)
requested=cphy=(field=13,mask=0xffffff81) dphy=(field=32,mask=0xffffff81) afe=0x44104015
after=(0x1001b,0x41,0x44104015)
```

No new BB8, PHY, PLL, timing, fatal-mask, or error-suppression setting was
introduced.

## Fixed controls and register diagnostics

Before each capture, the candidate sensor read back:

```text
exposure=504
analogue_gain=16
```

The bounded diagnostic read set was identical on both starts and stops. The
representative `before-experiment`, `before-stop`, and `after-disable` values
were:

| Register | Readback |
| --- | ---: |
| `0x3005` | `0x00` |
| `0x3027` | `0x00` |
| `0x3009` | `0x00` |
| `0x3b80..0x3b87` | `00 a5 10 00 08 00 01 00` |
| `0x3b88..0x3b8f` | `00 00 00 05 00 00 00 1a` |
| `0x3b90..0x3b96` | `01 b4 00 10 05 f2 40` |

With the option disabled, the candidate performed no experimental RMW writes;
these are read-only diagnostics. The expected output-enable bit remained
`0x3005[3]=0`, and frame-PWM enable remained `0x3b96[7]=0`.

## Capture evidence

The existing `scripts/ir/capture-ov7251-ir-frame.sh` was used with the
discovered `/dev/video5` and `/dev/media0`, `640x480`, `Y10`, stride `832`,
and four-byte line headers. Both bounded invocations exited normally with a
completed buffer (`bytesused=399360`) and raw output of `399356` bytes; the
four-byte difference is the documented trailing padding behavior.

| Capture | PNG SHA-256 | Pixel min/max | Mean / stddev | Packet evidence |
| --- | --- | --- | --- | --- |
| first | `040c0ec8d422cce8bdfdfd42a06da9d40a0389e84a4cc4eee4117d7c5e0c9129` | `13/1023` | `360.5383 / 244.7619` | 17 SOF, 17 EOF, 68 packet records |
| stop/reopen | `ac3d280e17ff5a092d5b3222a5c0039c9dc50c5afe9ccf4c8d23002acd1da684` | `14/1023` | `363.2687 / 245.5931` | 17 SOF, 17 EOF, 68 packet records |

The decoded PNGs were both `640x480` 8-bit grayscale and differed by hash.
The receiver reported nine fatal-status lines in each bounded kernel log,
including `0x8480` snapshots and persistent fatal classification `0x480`.
These errors were preserved in the logs and not suppressed.

The candidate logged `cleanup=not-needed first-error=0` on normal power-off;
there was no cleanup or runtime-PM fault. The post-stop diagnostic reads
returned the same output-disabled values.

## Restoration

The two temporary links were disabled before ordinary removal. The candidate
receiver and sensor were removed without force; the original distribution
receiver and Fedora-signed OV7251 were loaded again. The graph and formats
were restored, including the preflight `Y10_1X10/4096x3072` IPU formats and
the disabled OV7251-to-CSI links. A normalized topology/format comparison
matched preflight. Media entity and subdevice numbers were renumbered by the
unbind/rebind cycle; that is enumeration, not a topology change.

Front and rear RGB smoke captures after restoration were valid:

```text
front: 30233088 bytes, SHA-256 05dfff0e8d30ffc02e8f88cb0ac5a3f965b0c8ebbfb17ff958747f3471213ab2
rear:  47941632 bytes, SHA-256 0d710366dc5ad26b907389c21418f7e0cba87deba3476c0a4ce8fe1d304b9d67
```

At completion, `debug_capture_links=N`, the candidate parameters were absent,
the original OV7251 was bound to `i2c-INT347E:00`, the original module hashes
matched preflight, and all recorded services were restored active/enabled as
before.

The BB8 tuple above is evidence of the temporary diagnostic initialization,
not rollback. No independent post-unload BB8 reader was available in the
documented workflow, and no reset write, power cycle, or reboot was performed.
Therefore software restoration is demonstrated; hardware-register rollback
is not claimed, consistent with the existing BB8 rollback document.

## Reuse boundary for a future off/on comparison

The exact prepared setup to reuse is the pinned candidate and bundled receiver
described above, with dynamically discovered nodes and the same fixed controls
and capture decoder. The only future variable should be
`experimental_strobe_output`; `strobe_diagnostics=1`, the route, receiver
module, exposure, gain, format, processing, and bounded start/stop procedure
must remain unchanged. The enabled phase remains pending independent IR
observation and is not part of this baseline.
