# Installing a GitHub release bundle

The GitHub Actions workflow builds the IPU4P modules against the Fedora 43
linux-surface kernel-devel package pinned in
`.github/workflows/driver-release.yml`. It runs the hardware-independent test
suite on pull requests and pushes to `main`, and builds the downloadable
archive against the exact kernel release recorded in the asset name.

When a GitHub Release is published, the workflow builds the tagged source and
attaches one `.tar.gz` asset containing the six verified modules, the install
and uninstall scripts, installation instructions, and checksums. The bundle
does not include `ipu4p_cpd.bin`; users must obtain this Microsoft-signed
firmware from an authorized source.

Download the asset that names the kernel release currently running on the
Surface, extract it, and follow its `INSTALL.txt`. The installer verifies the
module vermagic and expects the firmware path through `FIRMWARE=...`. Reboot
after installation to load the modules. Secure Boot may reject the modules if
the system requires signatures absent from the bundle. A kernel update needs a
new bundle built for that exact release.

To update the target kernel supported by releases, change `KREL` in the
workflow and confirm that the matching `kernel-surface-devel` package is
available from the linux-surface Fedora repository. The build fails if the
kernel build tree or any module's vermagic does not match the configured
release.
