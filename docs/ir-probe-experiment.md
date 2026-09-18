# OV7251 INT3472 `vdda` probe experiment

This is the Stage 2 experiment for the upstream INT347E
POWER_ENABLE-to-`vdda` fix. It is deliberately separate from the production
kernel overlay and does not install, overwrite, or add an initramfs module.

## What is built

The experiment backports upstream media.git commit
`59cab094b8b50d7d4483bb04143294e1fb66aacc` to the older
`surface/v6.19.8` INT3472 source. The target source uses the older `.hid`
mapping table, so the patch adds only this entry:

```c
{ .hid = "INT347E", .type_from = INT3472_GPIO_TYPE_POWER_ENABLE,
  .type_to = INT3472_GPIO_TYPE_POWER_ENABLE, .con_id = "vdda",
  .enable_time_us = GPIO_REGULATOR_ENABLE_TIME },
```

The existing INT347E RESET mapping to the OV7251 sensor connection named
`enable` is unchanged. No OV7251 sensor source, ACPI table, CSI timing, GPIO
number, or RGB bridge file is modified.

Build from the isolated worktree with the exact running kernel:

```text
KDIR=/usr/src/kernels/6.19.8-3.surface.fc43.x86_64 \
KREL=6.19.8-3.surface.fc43.x86_64 \
OUT=/tmp/sp7-int3472-vdda-build \
./scripts/build-int3472-vdda-experiment.sh
```

The script downloads four source files from the pinned linux-surface commit,
checks their SHA-256 values, applies only
`patches/int3472-ov7251-vdda.patch`, builds
`intel_skl_int3472_discrete.ko`, and checks its vermagic. It never invokes
`depmod`, copies into `/lib/modules`, loads or unloads a module, rebinds a
device, stops a service, or reboots.

## Build result demonstrated on 2026-09-18

The build passed against `/usr/src/kernels/6.19.8-3.surface.fc43.x86_64`.
The retained output was:

```text
module=/tmp/sp7-int3472-vdda-build.o8Rz05/intel_skl_int3472_discrete.ko
sha256=e16d4c6630cfa3da8c43b26a3b3122f87552998bc0e6dd86ada6a923cb9796f4
vermagic=6.19.8-3.surface.fc43.x86_64 SMP preempt mod_unload
```

The module is an unsigned external build. This host reports Secure Boot
disabled and `CONFIG_MODULE_SIG_FORCE` unset, but module loading still
requires root. Two pre-existing compiler format warnings in the pinned
source were emitted; neither is in the vdda patch.

The distribution module was not overwritten. For the controlled hardware run,
the built module was installed at
`/lib/modules/6.19.8-3.surface.fc43.x86_64/extra/` and selected through the
temporary `/etc/modprobe.d/99-sp7-int3472-vdda-experiment.conf` install rule.
Its loaded-file SHA-256 remained
`e16d4c6630cfa3da8c43b26a3b3122f87552998bc0e6dd86ada6a923cb9796f4` and
`intel_skl_int3472_discrete` was marked `OE` in `/proc/modules`.

The patched provider resolved an `INT3472:02-vdda` regulator consumer instead
of the previous `avdd` name. On the same boot, OV7251 reported revision 7 at
I2C address `0x60`, registered as `ov7251 2-0060`, and advertised
`Y10_1X10` at 640x480 with 30/60/90 fps intervals. The prior `-110` write of
register `0x0103` and the `vdda` dummy-regulator message did not recur.

## Controlled load/retry procedure

This procedure is intentionally manual because the INT3472 provider is shared
by the two working RGB sensors. Do not run it while a preview or capture is
open. Do not proceed if any preflight check fails. The bridge is a user unit;
use the same desktop user that owns it.

1. Save the exact output directory path and record the current service state:

   ```text
   systemctl --user is-active sp7-camera-bridge.service
   systemctl --user is-enabled sp7-camera-bridge.service
   fuser -v /dev/video60 /dev/video61 /dev/media0
   ```

   The expected starting state is `active`, with no application other than
   the bridge using the camera devices. Preserve the starting enabled/active
   values for rollback.

2. Stop only the bridge and verify that it is inactive. Keep PipeWire and
   WirePlumber running:

   ```text
   systemctl --user stop sp7-camera-bridge.service
   systemctl --user is-active sp7-camera-bridge.service
   fuser -v /dev/video60 /dev/video61 /dev/media0
   ```

   If the bridge does not stop or a camera device remains in use, stop here.

3. As root, remove only the camera sensor/provider dependency set. Do not use
   `rmmod -f`, unload IPU modules, or touch the RGB loopback configuration:

   ```text
   sudo modprobe -r ov7251 ov5693 ov8865 intel_skl_int3472_discrete
   ```

   A refusal is a stop condition, not a reason to force removal. The
   experimental INT3472 module owns all three INT3472 devices, so this
   bounded test temporarily detaches the RGB sensor drivers as well; they are
   reloaded before the bridge is restored.

4. Still as root, load only the retained experiment and then the sensor
   drivers. Loading the INT3472 module should recreate its three providers;
   loading `ov7251` should attempt only the firmware-described `INT347E:00`
   client. Save a fresh monotonic kernel log around these commands:

   ```text
   sudo insmod /tmp/sp7-int3472-vdda-build.o8Rz05/intel_skl_int3472_discrete.ko
   sudo modprobe ov5693 ov8865 ov7251
   journalctl -k -b -o short-monotonic --no-pager -n 120
   ```

   Record all of the following before deciding the result:

   ```text
   readlink /sys/bus/i2c/devices/i2c-INT347E:00/driver
   ls -l /sys/class/regulator/*/name
   grep -R -H -E 'vdda|INT3472:02-avdd|num_users|state' /sys/class/regulator 2>/dev/null
   cat /sys/module/intel_skl_int3472_discrete/srcversion 2>/dev/null || true
   ```

   The pass boundary is not merely a loaded module or a changed regulator
   state. It requires the experimental provider to be attributable, a real
   `vdda` consumer for the IR sensor, successful reset/first transaction and
   chip-ID reads, and a bound OV7251/media entity. A probe log without chip
   identity is a failure or incomplete result.

5. Roll back immediately after the bounded retry, whether it passes or fails:

   ```text
   sudo modprobe -r ov7251 ov5693 ov8865 intel_skl_int3472_discrete
   sudo modprobe intel_skl_int3472_discrete
   sudo modprobe ov5693 ov8865 ov7251
   systemctl --user start sp7-camera-bridge.service
   systemctl --user is-active sp7-camera-bridge.service
   ```

   Confirm that the original distribution module is resident again, both RGB
   sensor clients are bound, `/dev/video60` and `/dev/video61` have their
   original `Video Capture` capabilities, and the existing RGB quick test
   passes. If any reload or RGB check fails, leave the bridge stopped and do
   not reboot; collect the failure log and request recovery direction.

The procedure above describes the bounded load/retry shape. The actual run
used the on-disk override and a permitted reboot because the IPU4P stack was
already resident. After the probe/capture attempts, the exact baseline ISYS
module was restored and verified live at SHA-256
`10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce`.
The temporary modprobe file was moved out of `/etc/modprobe.d`, and
`sp7-camera-bridge.service` was active again after reboot.

## Rollback and attribution

The distribution INT3472 module was not overwritten; the temporary override
is the only provider-selection change. The ISYS test module and its modprobe
configuration were restored after the run, and the RGB bridge was restarted.
The front/rear named-camera previews were not requalified during this IR-only
run; the bridge being active is the demonstrated restoration state. The RGB
baseline and bridge hashes are in `docs/ir-baseline.md`.

The experiment is one variable: only the INT3472 consumer-name mapping changes.
The `vdda` mapping is demonstrated to remove the failed probe boundary on
this unit and to allow chip identity and media registration. The following
remain unproven or unresolved:

* the physical rail's exact on-time state during the first transaction was not
  measured independently; it was disabled after the failed capture;
* reset polarity and the existing `enable` mapping have not had ten
  independent power-cycle trials;
* CSI reception, raw monochrome frames, illumination and preview remain
  unproven.
