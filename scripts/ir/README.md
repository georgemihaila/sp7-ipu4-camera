# OV7251 IR direct-capture bundle

This directory contains the complete, previously verified direct source-6
capture bundle:

- `setup-and-capture.sh` performs the temporary setup and cleanup.
- `capture-ov7251-ir-frame.sh` captures one 640x480 `Y10 ` buffer from the
  direct CSI packet tap and unpacks its 4-byte-per-line header plus packed
  RAW10 payload into `frame0.png`.
- `intel-ipu4p-isys-csi-header-tap-bb8.ko` is the exact temporary module that
  initializes source-6 BB8 with the recovered Table-B values before capture.
- `media-link` is the helper used to enable and disable the two media links.
- `module.info` records the module vermagic, hash, BB8 fields, masks, and
  expected readback.

## Run

From the repository root, with the camera bridge allowed to be stopped and
restarted temporarily:

```text
scripts/ir/setup-and-capture.sh /var/tmp/ov7251-ir-test
```

The wrapper requires non-interactive `sudo` access because it unloads the
distribution ISYS module, loads the bundled module, and changes media links.
It writes `setup.log`, `capture.raw`, `capture.log`, `kernel.log`,
`format.txt`, `stats.txt`, `run.txt`, and `frame0.png` into the output
directory. The wrapper does not reboot, perform a hardware power-cycle, or
change sensor PLL, port, or receiver timing.

The standalone decoder can also be run after the links and module have been
prepared manually:

```text
scripts/ir/capture-ov7251-ir-frame.sh /var/tmp/ov7251-ir-test
```

## Compatibility and rollback limits

The bundled module has vermagic
`6.19.8-3.surface.fc43.x86_64 SMP preempt mod_unload` and is not a generic
kernel module. The wrapper verifies the BB8 log readback
`after=(0x1001b,0x41,0x44104015)` after the receiver starts. The bundled
module emits this diagnostic during receiver setup, not at module insertion.
The decoder accepts a capture that is short only by trailing stride padding;
the observed `bytesused=399360`, `offset=4` direct-tap buffer can therefore
produce a complete image from a 399356-byte `--stream-to` file.

Cleanup reloads the distribution software module and restarts the bridge if
it was active. Earlier read-only verification showed that module reload and a
bounded ISYS power-cycle did not restore BB8 hardware registers; therefore
this bundle must not be described as proving hardware-register rollback.
The installed distribution module hash at the time of the verified run was:

```text
10ec710e00d411cc29b5f192200bd22b28b4e13a0cd131ab93ad9ecbc433e3ce
```

Artifact hashes for this bundle are recorded in `SHA256SUMS`.
