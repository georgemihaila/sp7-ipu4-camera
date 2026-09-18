# Bridge-free camera testing — 2026-09-17

## Scope and limitation

This was an investigation on one Surface Pro 7, in
`/home/george/repos/sp7-ipu4-camera`, on Fedora GNOME Wayland with kernel
`6.19.8-3.surface.fc43.x86_64`. The checkout was on branch `optimize`, ahead
of `origin/optimize` by 31 commits, with no pre-existing working-tree changes.
There is no local `AGENTS.md` in this checkout.

The Codex desktop session exposed no native application-control surface: its
application inventory was empty and only an empty in-app browser was
available. Therefore Zoom and Discord GUI camera lists, previews, switching,
reopen, relaunch, and restoration-in-application were not observed. This
report does not claim either desktop client worked or failed.

## Baseline

Installed package versions:

| Component | Version / packaging |
|---|---|
| Zoom | Flatpak `us.zoom.Zoom`, 7.1.5.4332, Flathub, runtime `org.freedesktop.Platform/x86_64/25.08` |
| Discord | Flatpak `com.discordapp.Discord`, 1.0.158, Flathub, runtime `org.freedesktop.Platform/x86_64/25.08` |
| libcamera | RPM `0.7.1-1.fc44` |
| libcamera-gstreamer | RPM `0.7.1-1.fc44` |
| pipewire / pipewire-plugin-libcamera | RPM `1.6.8-1.fc44` |
| WirePlumber | RPM `0.5.14-1.fc44` |
| xdg-desktop-portal | RPM `1.22.1-1.fc44` |
| xdg-desktop-portal-gnome | RPM `50.0-1.fc44` |
| v4l2loopback / akmod-v4l2loopback | RPM `0.15.4-1.fc44` |

Neither Zoom nor Discord is installed as an RPM. Both Flatpaks reported
`devices=all`; Discord also has `xdg-run/pipewire-0`, while Zoom has its
normal `xdg-documents/Zoom:create` filesystem. `flatpak override --show`
reported no user or system overrides for either application.

The effective project configuration was
`/etc/wireplumber/wireplumber.conf.d/50-sp7-ipu4.conf`; no user WirePlumber
override files existed. Before testing, `sp7-camera-bridge.service` and
WirePlumber were active. The dynamic device inventory was:

* Physical IPU4P raw nodes: `/dev/video0` through `/dev/video54`, media graph
  `/dev/media0`, driver `intel-ipu6`, model `ipu4p`.
* Physical sensor subdevices: `ov8865 2-0010` and `ov5693 1-0036`.
* `/dev/video55`: `OBS Virtual Camera`.
* `/dev/video60`: `Surface Camera (front)`.
* `/dev/video61`: `Surface Camera (back)`.

The bridge initially owned `/dev/video60` and `/dev/video61`; no process owned
the OBS endpoint. PipeWire initially exposed only the three V4L2 loopbacks.

## Temporary changes and commands

Backups were made with permissions and ownership preserved under a temporary
directory outside the repository. Backed-up files were the installed
WirePlumber policy, v4l2loopback options, modules-load file, user service, and
installed bridge binary. No file was missing at backup time.

The reversible runtime change was:

```sh
systemctl --user stop sp7-camera-bridge.service
systemctl --user mask --runtime sp7-camera-bridge.service
```

The bridge workers exited. `v4l2loopback` was deliberately left loaded so the
unrelated OBS virtual camera was not disrupted. The three loopbacks remained
explicitly excluded from all native-camera results.

The installed WirePlumber file was temporarily replaced with a policy that
retained only the raw-IPU4 V4L2 hide rule. The `monitor.libcamera` disable rule
and the `wireplumber.profiles.main.monitor.libcamera = disabled` setting were
omitted. WirePlumber was restarted, and the original file was restored before
the bridge was unmasked and started.

No exposure, autofocus, sensor timing, kernel driver, Flatpak permission, or
application setting was changed. No reboot, uninstall, bridge-removal script,
push, meeting, call, transmission, or message was performed.

## Native path result

With the bridge stopped and masked, WirePlumber exposed two processed
libcamera devices:

* `ov5693`, location `front`, PipeWire description `Built-in Front Camera`.
* `ov8865`, location `back`, PipeWire description `Built-in Back Camera`.

The raw IPU4 nodes stayed hidden. Source object IDs were transient; during the
test they were 42 and 69. A GStreamer PipeWire client negotiated both sources
as `640x480 RGBx`, but the captured JPEG frames were all black: the first and
last frames for both sources had identical SHA-256
`eab731f30c0839f63819fa0c2e7be3e9acd492e29c62d8a0d47b93b566af9347`, with
ImageMagick `mean=0`, `min=0`, and `max=0`.

The PipeWire probe used this form, with the current transient source ID:

```sh
gst-launch-1.0 -q pipewiresrc target-object=42 num-buffers=60 \
  ! videoconvert ! jpegenc quality=85 \
  ! multifilesink location=/tmp/front-%03d.jpg
```

The independent direct test used `cam` while WirePlumber was stopped, then
started WirePlumber again:

```sh
systemctl --user stop wireplumber.service
cam --camera 2 --capture --file=/tmp/front-#.ppm
cam --camera 1 --capture --file=/dev/null
systemctl --user start wireplumber.service
```

The lower libcamera path was checked independently with WirePlumber stopped:

* Front `cam` configured `2588x1944 ABGR8888` and delivered advancing frames
  at about 28.6 fps. Valid PPM frames had changing pixel statistics (mean
  12789 to 33160.9), so the front path was not a zero-byte stream.
* Rear `cam` configured `3260x2448 ABGR8888` and delivered advancing frames at
  about 15 fps with `bytesused: 31961088`.

The native test therefore reached both physical sensors and direct libcamera
frame delivery, but the tested PipeWire preview client produced black output.
This is a native PipeWire/libcamera integration failure for this test, not
evidence that Zoom or Discord reject the cameras. Relevant logs include:

* `journalctl --user -u wireplumber.service -b`
* `journalctl --user -u sp7-camera-bridge.service -b`

Expected but non-fatal libcamera messages also appeared for the unsupported
IPU4 TPG nodes (`No image format found`, `No valid pipeline`) and for missing
sensor YAML files, which fell back to the uncalibrated IPA configuration.

## Application and alternatives

The application trials below were not run because GUI access was unavailable.
Enumeration alone is intentionally not recorded as success.

| Application / mode | Physical cameras listed | Front live preview | Rear live preview | Switch / reopen | Proven capture path |
|---|---|---|---|---|---|
| Zoom desktop, default | Not observed | Not tested | Not tested | Not tested | None proven |
| Discord desktop, default | Not observed | Not tested | Not tested | Not tested | None proven |
| Supported PipeWire option, if available | No exact-version option identified | Not tested | Not tested | Not tested | None |
| libcamerify, if tested | Not available: command/package absent | Not tested | Not tested | Not tested | None |
| Browser alternative, if tested | Not tested: no authenticated browser session was available | Not tested | Not tested | Not tested | None |

The installed Zoom Flatpak is a proprietary bundled client. The Discord
Flatpak is an Electron bundle and its launcher only consumes the normal
`~/.config/discord-flags.conf`; no camera-specific PipeWire option was
documented in the installed files. Upstream Electron has a closed enhancement
request specifically asking for a PipeWire Camera flag, while its current
desktop-capturer documentation describes PipeWire screen/window capture rather
than a camera backend. The freedesktop Camera portal API can
grant a PipeWire camera remote, but an application must use that API; its
presence does not prove either client does so.

References:

* [Electron PipeWire Camera enhancement #45058](https://github.com/electron/electron/issues/45058)
* [Electron desktop-capturer Linux/PipeWire documentation](https://github.com/electron/electron/blob/main/docs/api/desktop-capturer.md)
* [freedesktop Camera portal API](https://github.com/flatpak/xdg-desktop-portal/blob/main/data/org.freedesktop.portal.Camera.xml)

## Restoration

The saved WirePlumber file was restored byte-for-byte, and the v4l2loopback
options, modules-load file, user service, and installed bridge binary matched
their pre-test SHA-256 values. The runtime mask was removed. The bridge is
enabled and active; WirePlumber is active; `/dev/video55`, `/dev/video60`, and
`/dev/video61` have their original labels and are again visible as the only
PipeWire video devices.

One WirePlumber process emitted a `status=6/ABRT` core-dump entry during the
first restoration restart, while temporary direct-camera IPA workers were
being torn down. Systemd started a fresh WirePlumber process immediately; the
service then remained active with `NRestarts=0` for the follow-up stability
check. This teardown event is retained as a restoration caveat. No permanent
configuration or application setting was left changed.

Because GUI control was unavailable, the final “named cameras work again in
Zoom and Discord” check remains pending manual observation.

## Manual GUI completion steps

With the restored bridge active, open each client separately and fully quit it
between trials:

1. Open Video settings and record every camera name.
2. Select `Surface Camera (front)`, verify movement in preview, and briefly
   cover the front lens.
3. Repeat for `Surface Camera (back)` and the rear lens.
4. Leave each preview running for about 30 seconds; record black, frozen,
   delayed, crash, or switch failures.
5. Switch front → rear → front, close and reopen Video settings, then fully
   quit and relaunch the client and repeat the preview check.
6. During each trial, use `wpctl status` and `fuser -v /dev/video60
   /dev/video61` to record whether the named bridge endpoint is the actual
   consumer path. Do not count OBS or `/dev/video0`–`/dev/video54` as native
   success.

## Recommendation

Retain the bridge for now. The tested native PipeWire path exposed both
physical sensors but produced black frames, while the bridge was restored
cleanly and remains the only path prepared for the named Zoom/Discord V4L2
endpoints. Reconsider making the bridge optional only after the manual Zoom
and Discord preview procedure succeeds and the PipeWire black-frame issue is
resolved.
