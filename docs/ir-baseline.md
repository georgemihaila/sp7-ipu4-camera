# OV7251 infrared bring-up baseline and recovery

Captured 2026-09-18 on the execution worktree branch
`codex/ov7251-ir-bringup`. Raw ACPI tables, complete kernel logs, module
hashes, media topology, and live-test diagnostics are retained outside the
repository under `/tmp/ov7251-ir-baseline-20260918/`.

## Host and installed stack

| Item | Observed value |
| --- | --- |
| Hardware | Microsoft Surface Pro 7 |
| BIOS version/date | `24.109.140` / `07/21/2025` |
| Running kernel | `6.19.8-3.surface.fc43.x86_64` |
| Kernel packages | `kernel-surface`, `-core`, `-modules`, `-devel` all `6.19.8-3.surface.fc43.x86_64` |
| Repository HEAD before execution | `4f028aecda640f7f4ad91a4c7f6b77b272c659a0` |
| libcamera / Snapshot | `0.7.1-1.fc44` / `50.0-1.fc44` |
| PipeWire / WirePlumber | `1.6.8-1.fc44` / `0.5.17-1.fc44` |
| v4l2loopback | `0.15.4-1.fc44` |
| Boot ID | `f544e92e-2065-4516-9a7a-336e19db7420` |

The current checkout had unrelated user changes (a modified `.gitignore` and
staged `cbridge` build artifacts); this execution uses a separate worktree
and does not include them.

## Loaded driver and camera state

The distribution OV7251 module is present but its probe failed during boot:

```text
/lib/modules/6.19.8-3.surface.fc43.x86_64/kernel/drivers/media/i2c/ov7251.ko.xz
SHA-256: 00cfa05cbdf46a8d6c55b077d7729fa419a3a072d88bb343b5985d4e3ad4deac
vermagic: 6.19.8-3.surface.fc43.x86_64 SMP preempt mod_unload
signer: Fedora kernel signing key
```

The loaded INT3472 discrete module is the distribution module:

```text
/lib/modules/6.19.8-3.surface.fc43.x86_64/kernel/drivers/platform/x86/intel/int3472/intel_skl_int3472_discrete.ko.xz
SHA-256: 17f421abeacabdd82e4c0b49e8cac8d8d57c2f08ff60b4a747585d8fb603cb68
vermagic: 6.19.8-3.surface.fc43.x86_64 SMP preempt mod_unload
signer: Fedora kernel signing key
```

The media device is `/dev/media0` (`driver=intel-ipu6`, `model=ipu4p`). It
contains the front OV5693 and rear OV8865 entities, but no OV7251 entity.
The generic `/dev/video*` nodes therefore do not demonstrate IR bring-up.
The live RGB endpoints are:

```text
/dev/video60  Surface Camera (front), v4l2loopback, Video Capture, YUYV 1280x720
/dev/video61  Surface Camera (back),  v4l2loopback, Video Capture, YUYV 1280x720
```

Boot logs in `kernel-monotonic.log` show the current failure sequence:

```text
ov7251 ... supply vdddo not found, using dummy regulator
ov7251 ... supply vddd not found, using dummy regulator
ov7251 ... supply vdda not found, using dummy regulator
i2c_designware.2: controller timed out
ov7251 ... ov7251_write_reg: write reg error -110: reg=103, val=1
ov7251 ... error during global init
ov7251 ... probe with driver ov7251 failed with error -110
```

`reg=103` is hexadecimal `0x0103`, the OV7251 software-reset write in the
matching driver source. This is probe failure before media registration or
CSI streaming.

## RGB baseline and named-client boundary

The user services were active and unchanged before the experiment:

```text
sp7-camera-bridge.service: active/running, enabled, MainPID=2922
wireplumber.service:     active/running
pipewire.service:        active/running
```

A fresh read-only bridge qualification captured 120 rear YUYV frames at
30.13 fps with nonzero luma (`max_yavg=52.33`, `max_ymax=82.00`). The same
run confirmed both loopback endpoints accepted short probes. Format-changing
MJPEG cases and lifecycle checks were skipped because they require explicit
service-control mode; they were not used as evidence of failure.

The installed GUI client is Snapshot `50.0-1.fc44`. The prior qualification
record in `docs/wireplumber-qualification-20260916.md` observed a live
`org.gnome.Snapshot` PipeWire stream consuming the back loopback, but did not
exercise still-image capture. A fresh human-visible Snapshot preview was not
repeated in this baseline because native app UI control is unavailable in this
execution surface; it remains `NOT TESTED` here and must not be inferred from
the V4L2 result.

## Recovery contract

No reboot, suspend, boot selection, PAM change, system installation, or
distribution module overwrite is authorized by this baseline. Before a
controlled module experiment:

1. Record the bridge service state and stop only camera consumers. Stop
   `systemctl --user stop sp7-camera-bridge.service` only for the bounded
   OV7251 test, after confirming no other process owns `/dev/video60` or
   `/dev/video61`.
2. Keep the original compressed modules and hashes above untouched. Install
   an experimental module only in a retained temporary override directory;
   never overwrite `/lib/modules`.
3. Restore the original module search path/override, run `depmod` only if a
   system override was actually created, rebind only `INT347E:00`, and verify
   that `intel_skl_int3472_discrete`, `ov7251`, `ipu_bridge`, and
   `v4l2loopback` match the recorded provenance.
4. Restore the bridge with
   `systemctl --user start sp7-camera-bridge.service`; verify both loopbacks,
   WirePlumber, and the front/rear RGB capture checks before ending the test.

If a module cannot be unloaded safely because of a live consumer, do not use
forced unload. Leave hardware state unchanged and report the test as pending.
Reboot is not a recovery step for this execution unless separately approved.

## Evidence commands

The concise evidence files are generated with:

```sh
journalctl -k -b -o short-monotonic
media-ctl -d /dev/media0 -p
v4l2-ctl -d /dev/video60 --all
v4l2-ctl -d /dev/video61 --all
modinfo ov7251
modinfo intel_skl_int3472_discrete
systemctl --user show sp7-camera-bridge.service wireplumber.service pipewire.service
```

The complete raw ACPI dump and disassembly are outside tracked source at
`/tmp/ov7251-ir-baseline-20260918/acpi-dump-root/`; no AML method was
executed.
