# Fedora libcamera integration

The installed/current Fedora stack is `libcamera-0.7.1-1.fc44` with
`pipewire-plugin-libcamera-1.6.8-1.fc44`. The IPU4P media device reports
`driver intel-ipu6` and `model ipu4p`, so Fedora's existing Simple-pipeline
entry for `intel-ipu6` is the intended compatibility path and enables SoftISP.

No local libcamera patch is carried now. The former patch added the wrong
`intel-ipu4-isys` identity and relaxed a format check that is already correct
for this stack. The processed IPU4 BE/SOC nodes use unpacked `BG10`
(`V4L2_PIX_FMT_SBGGR10`, 16 bits per sample). Packed `pBAA`
(`V4L2_PIX_FMT_SBGGR10P`) belongs to the direct-MIPI capture path and must not
be substituted into the processed Simple-pipeline path.

## Reproducible checks and build

Validate an unpacked Fedora source tree, optionally with its spec:

```sh
./libcamera/rebuild-libcamera.sh --check \
    /path/to/libcamera-v0.7.1 /path/to/libcamera.spec
```

The check is version-aware and verifies the source files, `intel-ipu6` +
SoftISP entry, unpacked/packed Bayer mappings, and strict negotiated
fourcc/size guard. A source or spec layout outside the known form fails with
an actionable error; it never silently produces an unpatched package.

Build the current Fedora SRPM into a retained, fresh output directory:

```sh
./libcamera/rebuild-libcamera.sh
```

This downloads the current `libcamera` SRPM, runs `dnf builddep`, validates the
SRPM's source/spec, and runs `rpmbuild`. It does not install packages. Use
`RPMBUILD_DIR=/path/to/output ./libcamera/rebuild-libcamera.sh` to select the
parent output directory, or add `--install` to install only the rebuilt
libcamera RPM set after a successful build.

## Runtime validation boundary

`cam --list` enumerates both OV5693/OV8865 cameras through the current Simple +
SoftISP pipeline. After the IPU4P stream-lifecycle and dynamic-link fixes,
three-frame processed captures succeeded for both sensors. The rear capture is
near-black at the sensor's low initial exposure/gain; increasing exposure and
analogue gain produces scene detail, while the Simple IPA's automatic exposure
ramps up slowly with the uncalibrated fallback. This is an image-quality/AE
issue, not a zero-byte capture failure. No OV8865 tuning file is included.

These `cam` results do not prove that GNOME Snapshot enters `Playing`; the GUI
application and its PipeWire/portal startup path still require separate
validation.
