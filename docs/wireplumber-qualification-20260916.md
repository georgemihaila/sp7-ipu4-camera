# WirePlumber ownership qualification

The existing workaround remains in place because direct IPU4P capture still
needs exclusive ownership of the shared backend. The rule is limited to raw
IPU4 V4L2 nodes and idle libcamera input nodes; it contains no audio, ALSA, or
microphone changes.

On 2026-09-16, the current user session was restarted with:

```text
systemctl --user restart wireplumber.service -> status 0
wireplumber.service -> active/running, ExecMainStatus=0
sp7-camera-bridge.service -> active/running, ExecMainStatus=0
```

After the restart, `wpctl status` still showed the built-in audio sink and
source, and the Surface Camera front/back and OBS virtual V4L2 devices. A
30-frame front loopback capture also completed successfully (450810 bytes
from the JPEG-configured endpoint).

The C bridge's ownership callbacks distinguish an already-inactive service
from a service it stopped, and bound `systemctl --user` stop/start operations
to five seconds with child cleanup on timeout. The GUI Snapshot preview and
still-capture path has not yet been verified; no workaround is removed on the
basis of enumeration or direct V4L2 success alone.
