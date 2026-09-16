# WirePlumber ownership qualification

The installed policy hides the raw IPU4 V4L2 nodes and disables WirePlumber's
physical libcamera monitor. This lets the C bridge own the shared backend
while WirePlumber remains available for the application's V4L2 loopback
targets; it contains no audio, ALSA, or microphone changes.

On 2026-09-16, the current user session was restarted with:

```text
systemctl --user restart wireplumber.service -> status 0
wireplumber.service -> active/running, ExecMainStatus=0
sp7-camera-bridge.service -> active/running, ExecMainStatus=0
```

After the restart, `wpctl status` still showed the built-in audio sink and
source, and the Surface Camera front/back and OBS virtual V4L2 devices. The
physical `/dev/video42` and `/dev/media0` nodes had no users, while the C
bridge held the loopback endpoints. Front and rear 60-frame loopback captures
also completed successfully at 30 fps.

The live C bridge no longer stops WirePlumber when a loopback consumer appears:
the profile has already removed the competing physical monitor, and stopping
WirePlumber would remove the PipeWire target that the application is opening.
After a fresh `gtk-launch org.gnome.Snapshot`, `wpctl status -n` showed an
active `org.gnome.Snapshot` stream consuming `Surface Camera (back):capture_1`.
No fresh `pipewiresrc` target-not-found, camerabin state-change, or camera
stream errors were logged. Snapshot still-image capture was not exercised in
this qualification.
