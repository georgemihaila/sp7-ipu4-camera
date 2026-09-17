# Optional native PipeWire qualification

This repository includes an optional WirePlumber profile and a bounded,
read-only validator for the native libcamera-to-PipeWire path. The profile is
not part of the default bridge installer and does not change the existing
`wireplumber/50-sp7-ipu4.conf` bridge behavior.

## Current qualification status

Native PipeWire preview is currently unqualified on this Surface Pro 7 and is
known black in the last bridge-free test. That test enumerated the physical
libcamera cameras but produced black frames. Direct `cam` capture remained
valid, so the result points to an
unresolved PipeWire/libcamera integration defect. The validator reports
`SKIP` when the tools, native monitor, or native nodes are unavailable, and
reports `FAIL` when a discovered native source negotiates but emits black or
static frames. A pass is only a bounded source-level result; it does not prove
portal permissions, Flatpak access, or preview/capture in Snapshot, Zoom,
Discord, or another desktop application.

This also explains the default-install symptom: the bridge policy disables the
physical libcamera monitor, so `wpctl` shows only the named V4L2 bridge devices
and GNOME Snapshot can report that no camera was found. Do not treat enabling
this profile as a complete application fix until the validator passes with
non-black, advancing frames.

## Activation

Run this as the logged-in desktop user, with the driver and libcamera stack
already loaded. Do not activate this profile while the bridge profile is still
active:

```sh
sudo install -D -m 0644 wireplumber/60-sp7-ipu4-native.conf \
  /etc/wireplumber/wireplumber.conf.d/60-sp7-ipu4-native.conf
sudo mv /etc/wireplumber/wireplumber.conf.d/50-sp7-ipu4.conf \
  /etc/wireplumber/wireplumber.conf.d/50-sp7-ipu4.conf.bridge-disabled
systemctl --user restart wireplumber.service
NATIVE_PIPEWIRE_VALIDATION_DIR="$PWD/reports/native-pipewire" \
  ./tests/native-pipewire-validation.sh --live
```

The validator discovers current `Video/Source` nodes using `pw-dump`, selects
their current `node.name`, requests a bounded RGB 640x480 30-fps GStreamer
preview, and checks that captured buffers contain nonzero and changing image
bytes. PipeWire object ids and `/dev/video*` minors are deliberately not
persisted or assumed.

## Rollback

Remove only the optional profile, restore the bridge fragment if it was moved,
and restart WirePlumber:

```sh
sudo rm -f /etc/wireplumber/wireplumber.conf.d/60-sp7-ipu4-native.conf
sudo mv /etc/wireplumber/wireplumber.conf.d/50-sp7-ipu4.conf.bridge-disabled \
  /etc/wireplumber/wireplumber.conf.d/50-sp7-ipu4.conf
systemctl --user restart wireplumber.service
```

If the bridge fragment was not present before activation, do not recreate it;
simply remove the optional file and restart WirePlumber. This profile and
validator never stop or restart services themselves, change links, load
modules, or touch the C bridge and `v4l2loopback`.
