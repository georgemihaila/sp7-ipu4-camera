# OV7251 illuminator experiment: bounded live result

Date: 2026-09-20. Commit under test: `9bf2748`. No enabled illuminator
capture was performed because the required qualified source-6 route was not
available without loading the prohibited ISYS debug-link/BB8 configuration.

## Preflight

The worktree was clean at `9bf2748`, running kernel
`6.19.8-3.surface.fc43.x86_64`. The candidate was the temporary raw module

```text
/var/tmp/ov7251-illuminator-experiment-pmfix.ko
SHA-256: 2daacb2fea6176955ef15ac887fad5d20c2b7bf57904bd6b45030376f05f6f75
vermagic: 6.19.8-3.surface.fc43.x86_64 SMP preempt mod_unload
```

The original distribution module was recorded before replacement and remained
unchanged:

```text
/lib/modules/6.19.8-3.surface.fc43.x86_64/kernel/drivers/media/i2c/ov7251.ko.xz
SHA-256: 00cfa05cbdf46a8d6c55b077d7729fa419a3a072d88bb343b5985d4e3ad4deac
```

Before replacement, `ov7251` was bound to `i2c-INT347E:00` and the front/rear
bridge held `/dev/video60` and `/dev/video61`. The per-user
`sp7-camera-bridge.service` was active and enabled; PipeWire and WirePlumber
were active. The bridge was stopped temporarily, only the exact OV7251 client
was unbound, and no IPU/RGB module was unloaded.

The qualified capture command used for the baseline was the direct, non-wrapper
command with its established defaults:

```sh
OV7251_MEDIA=/dev/media0 OV7251_DEVICE=/dev/video5 \
OV7251_WIDTH=640 OV7251_HEIGHT=480 OV7251_STRIDE=832 \
OV7251_HEADER_BYTES=4 OV7251_DURATION=15 \
scripts/ir/capture-ov7251-ir-frame.sh /var/tmp/ov7251-illuminator-run-20260920-110207/baseline-capture
```

No `setup-and-capture.sh`, persistent qualifier, ISYS replacement, BB8 write,
CSI/PLL change, or receiver-register experiment was used.

## Default-off baseline

The candidate was loaded temporarily with:

```text
experimental_strobe_output=0
strobe_diagnostics=1
```

It probed successfully as OV7251 revision 7. The existing sensor controls read
back as exposure `504` and analogue gain `16`; no stream was reached, so no
fixed-control frame comparison can be claimed.

The baseline command failed closed before streaming:

```text
error: direct source-6 link is absent; load the debug-capture-links module
return code: 1
```

The running `intel_ipu4p_isys` module reported `debug_capture_links=0`. The
OV7251-to-CSI and CSI-to-direct-capture links were both disabled. Enabling the
missing direct route would require the historical debug-link ISYS module and
BB8 setup, which was explicitly outside this experiment. Therefore there are
no valid changing IR frames, prerequisite register readbacks, stop/reopen
results, or optical observations from this baseline.

The candidate emitted only the normal unpowered cleanup record:

```text
IRTRACE power-off complete gpio=0 cleanup=not-needed first-error=0 optical-shutdown=not-independently-verified
```

No independent IR detector was available or identified, and the enabled phase
was not attempted. Consequently there is no claim of register-control success,
illumination, low-light improvement, or optical shutdown.

## Restoration and RGB check

The candidate was unbound and removed without force. The distribution module
was reloaded and the bridge was restored to active/enabled state. PipeWire and
WirePlumber remained active. The original module path, binding, and SHA-256
matched the preflight record.

After restoration, the existing RGB BE-SOC capture path produced valid
three-frame captures for both cameras:

| Camera | Result | Artifact | Size | SHA-256 |
| --- | --- | --- | ---: | --- |
| front / OV5693 | PASS | `/var/tmp/ov7251-illuminator-run-20260920-110207/rgb/front.raw` | 30,233,088 | `4c5b72fd8b478cab7e8c2d77f5e882edbddd3d03f3e3189b170d454457ff2cb6` |
| rear / OV8865 | PASS | `/var/tmp/ov7251-illuminator-run-20260920-110207/rgb/rear.raw` | 47,941,632 | `3fa1af7690fed45a3852622c562333be6d621c14f0e65c5f663dfa85406f7775` |

The capture helper temporarily negotiated RGB formats. Those formats and links
were reset to the recorded preflight defaults before the final bridge restart.
The final topology matched the preflight links and formats; only the OV7251
media-entity enumeration ID changed from 975 to 999 after the normal
unbind/rebind sequence.

Complete non-tracked logs and captures are under:

```text
/var/tmp/ov7251-illuminator-run-20260920-110207/
```

This result is a qualified-route availability failure, not evidence for or
against the two-bit illuminator hypothesis. A future enabled test requires a
qualified capture route that does not swap ISYS/BB8, plus an independent
IR-sensitive detector.
