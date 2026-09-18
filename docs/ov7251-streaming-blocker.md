# OV7251 streaming blocker

Date: 2026-09-18  
Kernel: `6.19.8-3.surface.fc43.x86_64`

## Demonstrated boundary

The INT347E `POWER_ENABLE` → `vdda` mapping is required for this unit's
sensor to probe. With the temporary INT3472 backport loaded, OV7251 revision
7 is read successfully at I2C address `0x60`, the media entity registers, and
the sensor advertises `Y10_1X10` 640×480 at 30/60/90 fps. This proves probe
and chip access, not streaming.

The established first-start trace shows the IPU4P receiver opening source 6
with one lane and firmware acknowledging the stream before OV7251
`s_stream(1)` is called. The receiver then reports no data-lane HS activity.
In the IPU4P register definitions, receiver status `0x4000` is bit 14:
`CSI2_CSIRX_ESCAPE_MODE_ULTRALOW_POWER_EXIT_CLK`, a clock-lane escape/ULP
event. It is not a frame or payload indication.

## Three bounded negative streaming experiments

1. Default and Windows-reconstructed source-6 port configurations both
   timed out with zero bytes. The tested values were `0x3895`, `0x2e95`, and
   asymmetric `0x38b4`/`0x2e95`. No data-lane HS or clean frame appeared.
2. The unmodified OV7251 driver selected the 240 MHz PLL despite the IR
   endpoint's 319.2 MHz link frequency. A one-variable candidate changed the
   index assignment so the driver selected the 319.2 MHz PLL. Readback
   confirmed `30b1=0x04`, `30b3=0x85`, and `30b4=0x01` on all 31 starts, but
   the capture remained zero bytes with 30 no-frame retries, 16 `0x4000`
   receiver errors, and a stream-stop timeout.
3. The priority INT3472 supply-name fix changes probe from an I2C timeout to
   successful chip identification, but the controlled IR capture still has
   no payload after that probe improvement.
4. The original Windows binary exposes source-7 receiver timing values of
   `1155/1269`, but no source-6 trace.  Applying those values to source 6,
   together with the corrected 319.2 MHz OV7251 PLL, still produced zero
   bytes, 60 no-frame retries, and 32 `0x4000` receiver errors.  This
   source-6 timing inference was rolled back; see
   `docs/ov7251-source6-timing-experiment.md`.

5. The original Windows sensor tables expose a separate 648x488/30-fps
   sequence. Applying its PLL, crop, output and timing values to a temporary
   Linux module changed the live sensor readback, but the explicitly routed
   648x488 capture still produced zero bytes, 31 sensor attempts, and a
   `VIDIOC_STREAMON` timeout with source-6 status `0x4000`.  This sensor-side
   experiment was rolled back without rebooting; see
   `docs/ov7251-windows-648x488-experiment.md`.

6. A synchronized Linux trace then recorded the source-6 receiver and OV7251
   stream sequence on the same monotonic timeline. Source 6 opened with one
   lane and RAW10, the receiver sampled `hs=0`/`lp=0` and reported `0x4000`
   before the sensor `s_stream(1)` call, and the sensor subsequently reached
   `0x0100=0x01`, but no payload arrived after 31 attempts. See
   `docs/ov7251-synchronized-source6-trace.md`.

All experiments used the dynamically discovered route:

```text
ov7251 2-0060 -> Intel IPU4 CSI-2 1 -> Intel IPU4 CSI2 BE SOC -> /dev/video42
```

The negotiated format was `Y10_1X10` on the media route and `Y10 ` at
640×480 with bytesperline 1280 and sizeimage 615680.

## Conclusion

The root cause of missing frames is unresolved. The evidence eliminates the
original probe-supply failure, the tested source-6 port-config values, and
the sensor driver's link-frequency index as sufficient fixes. It does not
prove whether the remaining fault is an OV7251 physical MIPI-output state, a
source-6 IPU4P clock/PHY configuration, or an ordering/resource interaction.

Per the execution plan, no further speculative PHY or timing mutation is
retained after these evidence-backed failures.

The narrowly instrumented Linux trace now supplies the synchronized receiver
and sensor evidence, but not physical proof at the CSI pins. The next required
evidence is therefore either a known-good Windows/firmware source-6 trace or
physical measurement of the OV7251 clock/reset/power and CSI clock/data lanes
during the same Linux start. A usable IR image, illumination behavior, and
preview client remain unproven.

The synchronized Linux platform-register trace is recorded in
`docs/ov7251-source6-platform-register-trace.md`. It confirms the live
source-6 GPREG/BSCAN state but does not supply the missing known-good Windows
values or produce a frame.

The delegated static comparison of the original Windows camera binaries with
the current Linux source is recorded in
`docs/ov7251-windows-binary-source6-analysis.md`. It confirms the compact
index-to-source-6 mapping and the shared BSCAN value, but does not recover the
missing source-6 runtime PHY/clock values.

The subsequent read-only hot-loaded trace is recorded in
`docs/ov7251-source6-phy-readback.md`. It confirms Linux's live BB/AFE
readback during source-6 start, but still produces zero payload bytes and does
not recover the Windows source-6 building-block selection.

## Current safe state

- The signed/distribution OV7251 module is selected; the experimental
  link-frequency module and override were removed.
- The INT3472 `vdda` experiment remains installed through its explicit
  temporary override because it is the change that makes probe succeed.
- The baseline IPU4P module hash remains
  `10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce`.
- `sp7-camera-bridge.service` is active.
- No push was performed.
