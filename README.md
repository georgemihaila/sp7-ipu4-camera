# Surface Pro 7 camera driver port (Intel IPU4, Linux 6.19)

Working **raw capture on both built-in cameras** of a Microsoft Surface
Pro 7 running Fedora 43 with the linux-surface kernel
(6.19.8-3.surface). The SP7's Ice Lake ISP (Intel IPU4/IPU4P, PCI
`8086:8a19`) has no upstream Linux driver; this repo carries an
out-of-tree port based on
[ruslanbay/ipu4-next](https://github.com/ruslanbay/ipu4-next)
(56 patches on vanilla 6.19.8) plus a series of local fixes that take it
from "loads but streams nothing" to reliable captures on both sensors.

**Status (2026-07-15):** front ov5693 (2592x1944) and rear ov8865
(3264x2448) both deliver real RAW10 frames, validated 6/6 across
repeated stream starts. libcamera/PipeWire integration not done yet —
capture is via `v4l2-ctl`/`media-ctl`. The IR camera (ov7251) fails its
i2c probe and is ignored.

## Layout

| Path | What |
|------|------|
| `linux-6.19.8/drivers/media/pci/intel/` | the driver (patched subtree of a vanilla 6.19.8 tree; only this subtree is tracked) |
| `load-ipu4.sh` | manual diagnostic/fallback load (not required for production boot) |
| `test-capture.sh front\|rear` | media-ctl pipeline setup + 3-frame capture |
| `validate-retry.sh` | full validation suite: TPG + rear sanity, then front x6 |
| `autotest/RESULT.md` | final validation results + root-cause write-up |
| `autotest.sh` | archived autonomous bring-up harness (not an install/runtime requirement) |
| `tests/camera-suite.sh` | documented Task 11 entry point (`--static` by default; `--live` opt-in) |
| `run-all-tests.sh` | historical root diagnostic battery (not the production test entry point) |
| `scripts/build-modules.sh` | external-KDIR build of the IPU module subtree |
| `scripts/install-modules.sh` / `scripts/uninstall-modules.sh` | explicit module install/rollback helpers; no service management |
| `docs/external-kernel-integration.md` | overlay layout, build variables, and required kernel-tree integration |
| `docs/upstream-readiness.md` | validation evidence, license/firmware audit, and remaining upstream work |

## Building, installing, and rollback

The `linux-6.19.8/` directory is an overlay-only kernel fragment, not a
complete kernel tree. Build the IPU module subtree against an explicit,
prepared external kernel build tree:

```sh
KDIR=/lib/modules/$(uname -r)/build ./scripts/build-modules.sh -j8
```

This builds the parent, ISYS, PSYS, and CSS-library modules. The OV5693 sensor
source needs the kernel-tree integration described in
`docs/external-kernel-integration.md`; it is not silently omitted from the
requirements. A distro's matching kernel headers/build tree, media/V4L2/I2C
configuration, compiler, and module-signing policy are prerequisites.

The required CPD is `ipu4p_cpd.bin` (ipu4-20191030, Microsoft-signed). It is
not redistributed here; obtain it from an authorized firmware source. Install
the built modules, firmware, and module dependency index with:

```sh
FIRMWARE=/path/to/ipu4p_cpd.bin sudo -E ./scripts/install-modules.sh
```

The helper installs every built IPU `.ko` into
`/lib/modules/$(uname -r)/updates/extra/`, installs firmware into
`/lib/firmware/ipu4p_cpd.bin`, and runs `depmod -a`. It does not load modules,
write systemd/udev files, or rebuild an initramfs. If the target uses an
initramfs, rebuild it using that distribution's normal administrative tool.
The standard PCI modalias and module soft-dependencies then provide discovery;
the exact alias, firmware name, GPL metadata, and softdeps are described in
`docs/upstream-readiness.md` and can be checked with `modinfo`.

To roll back the repository-installed modules without unloading a live driver:

```sh
sudo ./scripts/uninstall-modules.sh
```

It removes only the exact files in the `updates/extra` directory and runs
`depmod`; it deliberately leaves firmware in place. Remove
`/lib/firmware/ipu4p_cpd.bin` separately only if it is not shared, then rebuild
the initramfs if applicable. Reboot before attempting a new module load: this
tree does not support production module reload.

Do not blacklist the IPU4P or camera modules. The parent driver advertises the
Intel PCI `8086:8a19` alias; the child drivers intentionally do not advertise
that PCI alias because they bind to the private IPU bus. The parent's module
soft-dependencies pull in
`ipu-bridge`, the IPU4P CSS libraries, and the IPU4P ISYS/PSYS modules. Thus
the normal production path is udev's PCI modalias event followed by modprobe;
no loader service, timer, probe-storm delay, or WirePlumber/portal restart is
required. If the firmware is included in an initramfs on the target distro,
rebuild that initramfs after installing it.

The repository's `modprobe.d/ipu4p-sp7-camera.conf` is intentionally limited
to explanatory comments: the Surface Pro 7 compatibility behavior is built
into the driver and strict firmware/CSS validation remains the default.
For a manual fallback or diagnostic check, run `sudo ./load-ipu4.sh`.
Module reload does not work — one load per boot, reboot to iterate.

## Capturing

```sh
sudo ./load-ipu4.sh
sudo ./test-capture.sh front   # -> captures/front.raw, 3x 2592x1944 RAW10
sudo ./test-capture.sh rear    # -> captures/rear.raw,  3x 3264x2448 RAW10
```

The capture script writes to `captures/` by default. Override it with
`OUTPUT_DIR=/path/to/output`; `CAPTURE_TIMEOUT=20` is useful for bounded
diagnostic attempts. Firmware/CSS validation remains strict except for the
explicit built-in compatibility quirk matching the Surface Pro 7 DMI identity,
IPU4P PCI ID, signed `0x20191030` CPD, and `0x20181222` CSS release. That rule
emits a warning; generic systems and other mismatches still fail closed.

Frames at default exposure are near-black; raise
`exposure`/`analogue_gain` on the sensor subdev for visible content.
Beware: a logged-in desktop session's wireplumber grabs every
`/dev/video*` node the moment the modules load and wrecks captures
(`validate-retry.sh` masks it for the run).

## Fixes carried on top of ipu4-next (upstream candidates)

1. Store `av->pix_fmt` in `vidioc_s_fmt_vid_cap` — single-planar S_FMT
   never recorded the format the link validator reads → EPIPE on
   STREAMON.
2. Bound the media-graph walks in `is_support_vc`/
   `ipu_isys_query_sensor_info` — TPG pipelines ping-pong two entities
   forever → unkillable 100% CPU spin in the kernel.
3. Restore `is_external()`/`ip->external` assignment in the video-node
   `link_validate` (dropped in the 6.19 rewrite) — without it TPG
   pipelines oops in `__media_pipeline_stop` and sensors never stream.
4. Use `media_pipeline_stop_for_vc()` in the `prepare_streaming` error
   path — upstream `media_pipeline_stop()` walks uninitialized pads on
   pipes started by `media_pipeline_start_by_vc()` → NULL-deref oops.
5. Add the missing `pm_runtime_put` in `ipu_buttress_authenticate()`'s
   error/exit path — the leak (one per video-node open, ~50 at load via
   udev) pinned the psys IOMMU forever, so the isys power island could
   never cycle and one wedged stream latched EIO until reboot.
6. Configure D-PHY building block 10 in the buttress PHY setup
   (`{10, 13, 32, 0x15}` in `ipu4p_isys_bb_cfg`) — the front sensor's
   analog front end was simply never programmed; the front camera is
   totally silent without this.
7. Stream-start verification with sensor bounce + buffer parking (see
   below) — makes the front camera's marginal D-PHY link reliable.
8. Fix `ipu_isys_buffer_list_queue()` double-booking buffers when called
   with `INCOMING|SET_STATE` — a buffer completed to vb2 stayed linked on
   the driver's incoming list, so the app's requeue double-added the list
   node → kernel `BUG at lib/list_debug.c:32` on the next failed stream
   start (readily triggered by WirePlumber's retry loop).
10. Fix `BUTTRESS_POWER_TIMEOUT`: it feeds `readl_poll_timeout()`'s
    microseconds argument but was a bare `200` — 200 **µs** for the power
    island handshake (ipu6 uses 200 **ms** for the same register). Warm
    transitions pass; the first cold power-up after the island idles off
    times out and latches runtime PM `error` state until reboot.
11. ov5693: disable the 2x2-binned modes (SP7 quirk) — binned modes never
    achieve D-PHY lock on this link (0/15+ sensor restarts, vs ~20-70%
    per attempt at full resolution). The sensor always outputs the full
    crop; libcamera's software ISP scales to the requested size.
12. Unlink the debug capture nodes from the media graph (new isys param
    `debug_capture_links=1` restores them) so apps reach the only clean
    output route. Of the IPU4's three output paths, two scramble pixels:
    the per-CSI-2 direct capture nodes are str2mmio MIPI packet dumps
    whose DMA accumulates bytes instead of addressing lines (every D-PHY
    line glitch shears the rest of the frame — this is what libcamera
    picked, hence speckled/sheared SoftISP output), and the CSI2 BE
    (ISA/ISL) capture interleaves the fabric's dual-line processing into
    the buffer as rate-matched 64-byte bursts. Only the CSI2 BE SOC path
    (`RAW_SOC` pin) writes pixel-perfect line-addressed raster frames.
    The BE SOC links are ordinary mutable media-controller links, so
    standard media-ctl/libcamera graph setup can select this route without
    a private link-enabling helper. The helper remains available for
    historical diagnostics.
13. Recover a timed-out ISYS firmware release by forcing the existing IPU bus
    runtime-PM power-off/power-on sequence before the next video-node open;
    this clears the stale `reset_needed` state without requiring a reboot. A
    guarded root-only `force_power_cycle` sysfs trigger is also available on
    `intel-ipu60` for controlled bring-up tests.

## The front-camera reliability fix, in short

On this link (419.2 MHz x2 lanes) the ov5693's D-PHY clock-lane
settle/DLL lock at the LP->HS transition succeeds only ~25-40% of the
time per stream start, and a missed initial SOT sync is non-recoverable
for the whole stream. On a failed lock the firmware delivers one
STR2MMIO-errored frame per queued buffer, then starves (user space
can't requeue — it's still blocked in STREAMON).

The driver now counts only **error-free** `PIN_DATA_READY` responses
(`frames_done`), and after handing all buffers to the firmware it polls
for a clean frame; if none arrives in 600 ms it bounces the sensor's
`s_stream` to re-roll the lock (up to 30 times). While this runs,
corrupt-frame buffers are **parked back on the incoming queue and
re-fed to the firmware after each bounce**, so the firmware never
starves and the first successful lock is observable within ~150 ms.
Healthy streams are never disturbed; ~half of front starts need no
bounce at all, the rest typically 1-6.

## Known limitations

- One module load per boot (reload can wedge the CSE/firmware handshake); the
  new runtime-PM recovery is for stream-release failures, not module reload.
- The start-guard doesn't cover the rare case of a stream dying
  mid-capture after a good start.
- ov7251 (IR) i2c probe fails (`-110`) and is ignored.
- No per-sensor SoftISP tuning files (uncalibrated fallback: colors can
  look flat).

## libcamera / PipeWire integration

Fedora libcamera 0.7.1 already supports this IPU4P through its `intel-ipu6`
Simple-pipeline entry with SoftISP; no local pipeline patch is needed. The
version-aware helper under `libcamera/` validates and can rebuild the unmodified
Fedora source package. Hardware testing through `cam` enumerated both sensors
and captured processed frames from front and rear. The rear image is near-black
at its low initial exposure/gain, with automatic exposure ramping slowly in
the uncalibrated Simple IPA fallback. GNOME Snapshot's GUI/PipeWire/portal
startup path remains a separate validation item. `wireplumber/50-sp7-ipu4.conf`
is session policy for hiding raw V4L2 nodes. See `docs/task8-camera-contract.md`
for the exact kernel capability contract and external integration checklist.

The driver is expected to appear before normal camera enumeration through its
PCI modalias. No repository systemd loader or user-session refresh unit is
needed; applications should discover the camera through the normal libcamera,
PipeWire, and portal paths after installation.

Task 8's hardware-independent contract check is `tests/task8-camera-static.sh`.
It verifies the in-tree OV5693 format/timing/serialization claims and makes
the absent OV8865/libcamera/IPA components explicit; it does not replace
runtime V4L2 or libcamera validation on the target system.
`tests/task9-csi2-static.sh` and `tests/task10-production-static.sh` cover the
CSI-2 error paths and production-path knob/logging audit respectively.

## Validation suite

Run the reproducible, hardware-independent checks with:

```sh
./tests/camera-suite.sh                 # static checks: PASS/FAIL
./tests/camera-suite.sh --all            # static plus live checks
./tests/camera-suite.sh --live           # loaded hardware only
./tests/camera-suite.sh --live --pm-safe # additionally read runtime-PM state
```

Live mode discovers a readable media controller, sensor sub-device, CSI
endpoint, controls, formats, and frame interval using `media-ctl`/`v4l2-ctl`.
It performs bounded one-frame start/stop cycles for each discovered front/rear
sensor and verifies nonzero output. Missing hardware, tools, permissions, or
firmware are reported as `SKIP`; a failed stage is `FAIL` with a temporary log
directory path. The suite never reboots, unloads/reloads modules, writes
services, consumes dmesg, or changes runtime-PM state implicitly. Suspend is
not attempted; `--pm-safe` only reads power-control files.

## License

The driver code in `linux-6.19.8/drivers/media/pci/intel/` is
GPL-2.0 (Linux kernel / Intel, with changes from ruslanbay/ipu4-next
and this repo). Scripts in the repo root are GPL-2.0 as well.

## References

- https://github.com/ruslanbay/ipu4-next — the port this builds on
- https://github.com/linux-surface/linux-surface — SP7 kernel;
  camera discussion in issue #1353
