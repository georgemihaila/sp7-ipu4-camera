# SP7 IPU4 camera port — raw capture VALIDATED (2026-07-15)

## Final validation (boot of 19:02, module built 18:27)

| Test | Result |
|------|--------|
| TPG (1920x1080 SBGGR8, 3 frames) | 6428148 bytes ✓, 0 bounces |
| Rear ov8865 (3264x2448 RAW10, 3 frames) | 47941632 bytes ✓, 0 bounces |
| Front ov5693 (2592x1944 RAW10, 3 frames) x6 | **6/6**, bounces per try: 1, 0, 6, 2, 0, 0 |
| Front real-image check (exposure=1900, analogue_gain=127) | ✓ full 10-bit range (0–1023), std 93, row-mean spread 323 — real scene content |

## Root cause of front-camera flakiness (and the fix)

The ov5693's D-PHY clock-lane settle/DLL lock at the sensor's LP→HS
transition is a ~25–40% dice roll per stream start on this link
(419.2 MHz x2 lanes); a missed initial SOT sync is non-recoverable for
the whole stream. On a failed lock the fw delivers one STR2MMIO-errored
frame per queued buffer (~430 ms to eat all 4), then starves — user
space cannot requeue because it is still blocked in STREAMON.

Fix (all in the out-of-tree isys module):
1. `atomic_t frames_done` counting only **error-free** PIN_DATA_READY
   responses — the only unambiguous stream-health signal.
2. `verify_stream_start()` at the END of `ipu_isys_stream_start()`
   (after every buffer is handed to the fw): poll 600 ms windows for a
   clean frame; if none, bounce the sensor's s_stream to re-roll the
   D-PHY lock (up to 15 times).
3. **Buffer parking**: while verification runs, STR2MMIO-errored
   buffers are moved back to the incoming queue instead of being
   completed to user space, and re-fed to the fw after each bounce — so
   the fw never starves and the first successful bounce is observable
   within ~150 ms.

Startup cost: 0 bounces when the first lock succeeds (~50% of starts),
~0.7 s per bounce otherwise; healthy streams (rear/TPG) are never
disturbed.

## Where everything lives

- Driver tree: `linux-6.19.8/drivers/media/pci/intel` (relative to repository root)
  (ruslanbay/ipu4-next 56-patch set + local fixes)
- Installed modules: `/lib/modules/6.19.8-3.surface.fc43.x86_64/updates/`
- Load: normal PCI modalias/modprobe path after module installation and
  `depmod`; `load-ipu4.sh` is only a manual diagnostic fallback (one load per
  boot — reload is broken)
- Capture: `sudo ./test-capture.sh front|rear`
- Full suite: `sudo ./validate-retry.sh`
- Caution: a logged-in graphical session's wireplumber grabs the video
  nodes and wrecks captures; validate-retry.sh masks it for the run.

## Local driver fixes over ruslanbay/ipu4-next (upstream candidates)

1. `av->pix_fmt` store in `vidioc_s_fmt_vid_cap` (EPIPE on STREAMON)
2. Bounded media-graph walks (infinite kernel spin on TPG)
3. Restored `is_external()`/`ip->external` in link_validate (oops + no
   sensor streaming)
4. `media_pipeline_stop_for_vc()` in prepare_streaming error path (oops)
5. Buttress-auth mmu1 pm_runtime_put leak (isys island could never
   power-cycle; latched EIO until reboot)
6. PHY bbconfig `{10, 13, 32, 0x15}` — front sensor's D-PHY AFE was
   never configured (front totally silent without it)
7. Stream-start verification with sensor bounce + buffer parking (this
   fix; items 1–3 of the section above)
8. Remove bring-up-only CSI-2/PHY runtime overrides; validated SP7 values are
   now selected from built-in platform data and generic timing calculations.

## Next steps

## Reproducible validation entry point

Task 11's maintained test entry point is `tests/camera-suite.sh`. It runs the
hardware-independent Task 8–11 checks by default and reports `PASS`, `FAIL`,
or `SKIP` with bounded command timeouts. Use `--live` to inspect currently
loaded hardware; it discovers media nodes and sensor sub-devices dynamically,
checks controls/formats/frame intervals, and—when run with sufficient device
permissions—performs bounded front/rear stream start/stop and nonzero-frame
checks. `--pm-safe` only reads runtime-PM state. The suite does not reboot,
unload/reload modules, consume dmesg, modify services, or change PM state by
default. The older `validate-retry.sh` and `run-all-tests.sh` remain historical
diagnostic scripts and are not the maintained CI/live entry point.

- libcamera integration: repo carries patches for libcamera v0.7.0;
  Fedora 43 ships 0.5.2 — needs ABI-compatible build or parallel
  install for pipewire-plugin-libcamera.
- Upstream the fixes to ruslanbay/ipu4-next.
- Known leftovers: IR camera ov7251 fails i2c probe (ignored); rare
  mid-stream death after a good start is not covered by the start
  guard; module reload requires reboot.
