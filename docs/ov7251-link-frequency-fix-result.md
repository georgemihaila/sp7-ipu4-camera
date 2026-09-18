# OV7251 link-frequency index experiment

Date: 2026-09-18  
Kernel: `6.19.8-3.surface.fc43.x86_64`  
Test boot: `39042884-15a6-4b3f-a512-7ce6b4d9b3c6`

## Question

The OV7251 driver selected `link_freq_idx=0` even though the generated IR
endpoint exposed one frequency, 319.2 MHz.  The sensor driver's endpoint-list
index and its supported-frequency-array index are different indexes.  This
experiment changed only the assignment from the former (`i`) to the latter
(`j`), producing `link_freq_idx=1` for the IR endpoint.

## Controlled result

The candidate module was built from the exact target source and loaded through
a temporary modprobe override.  Candidate module SHA-256:

```text
435979b83accfd190f4d2ef103d4a44d641d8ef7b8fd332049d595e9eca03296
```

The controlled route was `ov7251 2-0060` → `Intel IPU4 CSI-2 1` → `Intel
IPU4 CSI2 BE SOC` → `/dev/video42`, configured as `Y10_1X10` and `Y10 ` at
640×480.  The bounded `v4l2-ctl --stream-mmap=120` test produced:

```text
capture_bytes=0
VIDIOC_STREAMON returned -1 (Connection timed out)
```

Kernel evidence from the same boot:

```text
stream begin enable=1 ... link_freq_idx=1       31 times
pll-readback                                         31 times
  30b1=0x04 30b3=0x85 30b4=0x01                 31 times
mipi-readback                                       31 times
no frames from ov7251                               30 times
csi2-1 receiver error status 0x4000                16 times
stream stop time out                                 1 time
```

The values demonstrate that the candidate changed the sensor's selected PLL
from the earlier 240 MHz readback to the expected 319.2 MHz register values.
They do not demonstrate CSI streaming or a usable image: no payload bytes or
frames were delivered.

## Rollback

The temporary override and candidate module were removed, `depmod -a` was
run, and the host was rebooted.  Post-rollback verification showed:

- `modprobe -n -v ov7251` resolves to the distribution module;
- `/etc/modprobe.d/99-sp7-ov7251-link-frequency-fix.conf` is absent;
- the distribution `ov7251.ko.xz` hash is
  `00cfa05cbdf46a8d6c55b077d7729fa419a3a072d88bb343b5985d4e3ad4deac`;
- OV7251 revision 7 still probes at I2C address `0x60` and `/dev/video42`
  remains present;
- the patched IPU4 module hash remains
  `10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce`;
- `sp7-camera-bridge.service` is active.

The candidate patch is intentionally not retained in the build script or
tracked as a functional driver change.  The next investigation must explain
the CSI `0x4000`/no-frame result at the receiver or sensor timing layer before
another functional change is attempted.
