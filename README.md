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
| `load-ipu4.sh` | load the module stack after boot (modules are blacklisted; one load per boot) |
| `test-capture.sh front\|rear` | media-ctl pipeline setup + 3-frame capture |
| `validate-retry.sh` | full validation suite: TPG + rear sanity, then front x6 |
| `autotest/RESULT.md` | final validation results + root-cause write-up |
| `autotest.sh` | boot-time autonomous test harness (systemd service, optional) |
| `front-diag.sh`, `phy-sweep.sh`, `settle-sweep.sh`, `run-all-tests.sh` | diagnostics used during bring-up |

## Building

Unpack a vanilla `linux-6.19.8` tree here (only
`drivers/media/pci/intel/` from this repo overlays it), have the
distro kernel's headers/build tree installed, then:

```sh
cd linux-6.19.8
make M=drivers/media/pci/intel \
     srcpath=$PWD/drivers/media/pci/intel \
     KBUILD_MODPOST_WARN=1 modules -j8
sudo cp drivers/media/pci/intel/ipu4/*.ko \
     /lib/modules/$(uname -r)/updates/
sudo depmod -a
```

Firmware: `ipu4p_cpd.bin` (ipu4-20191030, Microsoft-signed) goes in
`/usr/lib/firmware/`. It ships in the ipu4-next repo's assets and is
not redistributed here.

`/etc/modprobe.d/ipu4.conf` should blacklist the modules and set
`intel_ipu4p fw_version_check=0`; load manually with `load-ipu4.sh`.
Module reload does not work — one load per boot, reboot to iterate.

## Capturing

```sh
sudo ./load-ipu4.sh
sudo ./test-capture.sh front   # -> captures/front.raw, 3x 2592x1944 RAW10
sudo ./test-capture.sh rear    # -> captures/rear.raw,  3x 3264x2448 RAW10
```

The capture script writes to `captures/` by default. Override it with
`OUTPUT_DIR=/path/to/output`; `CAPTURE_TIMEOUT=20` is useful for bounded
diagnostic attempts. The loader passes `fw_version_check=0` explicitly because
the SP7 firmware is the signed 2019 CPD while the open CSS library identifies
itself as the older 2018 release.

## Initialization trace

The instrumented drivers read back the OV5693 clock, regulator, GPIO and
sensor-register state, along with IPU4P PHY and CSI receiver registers. After
loading the instrumented modules, run:

```sh
./trace-capture.sh
```

It saves the rear/front capture results and kernel trace under
`reports/init-trace-*`. The trace reports effective logical state; ACPI still
provides the physical GPIO and regulator wiring.

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
8. Diagnostics: runtime-writable module params `csi2_fw_src`,
   `csi2_csettle`/`csi2_dsettle`, `phy_bb_extra`/`phy_afe_extra`/
   `phy_jsl_bits`.
9. Fix `ipu_isys_buffer_list_queue()` double-booking buffers when called
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
    `enable-link.py` exists because the BE SOC links are DYNAMIC and
    media-ctl drops that flag on MEDIA_IOC_SETUP_LINK (kernel EINVAL).
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

The media device registers as `intel-ipu6`, which libcamera's simple
pipeline already supports with the software ISP. `libcamera/` holds a
one-patch rebuild of Fedora's libcamera 0.5.2 srpm (adds an
`intel-ipu4-isys` table entry and relaxes a fourcc check) plus
`rebuild-libcamera.sh`. `wireplumber/50-sp7-ipu4.conf` hides the ~55 raw
V4L2 nodes so apps only see the two libcamera camera nodes.

Two session-level pieces in `systemd/`:

- `camera-ipu4-load.timer`/`.service` (system): load the driver stack
  60 s after boot (CSE settle time).
- `camera-wireplumber-refresh.service` (user): restart WirePlumber once
  `/dev/media0` appears — its libcamera monitor does not create nodes
  for cameras that show up after it starts, and it starts at login,
  well before the driver loads.

## License

The driver code in `linux-6.19.8/drivers/media/pci/intel/` is
GPL-2.0 (Linux kernel / Intel, with changes from ruslanbay/ipu4-next
and this repo). Scripts in the repo root are GPL-2.0 as well.

## References

- https://github.com/ruslanbay/ipu4-next — the port this builds on
- https://github.com/linux-surface/linux-surface — SP7 kernel;
  camera discussion in issue #1353
