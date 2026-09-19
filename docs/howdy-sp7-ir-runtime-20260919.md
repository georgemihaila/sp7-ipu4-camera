# Protected Howdy `sp7_ir` runtime

Date: 2026-09-19<br>
Status: **built and staged; authentication is not enabled**

This record covers the protected Howdy package and its staged PAM/pre-login
artifacts. It does not claim live OV7251 capture, face enrollment, face
matching, login, lock-screen, boot, resume, liveness, or spoof resistance.

## Exact inputs and build

The Howdy source is pinned to revision
`d3ab99382f88f043d15f15c1450ab69433892a1c`. The downloaded source archive was
verified against SHA-256
`e0b58928c6d1362ea8c630f056261bccb446fe84b507f7b7cceb7c5a55706061` before
patching. The adapter patch is checked against that exact revision and the
patched Python sources compile successfully.

The native build used the protected interpreter
`/usr/local/libexec/sp7-camera-howdy/python/bin/python`, with
`-Dinstall_pam_config=false`, `-Dwith_polkit=false`, and
`-Dinstall_in_site_packages=false`. The Python runtime contains the validated
pins `numpy==2.4.6`, `opencv-python-headless==4.13.0.92`, and
`dlib==20.0.1`; it does not use the repository `.venv-auth`. Howdy's build
options expose the custom Python and installation paths in its
[Meson options](https://github.com/boltgolt/howdy/blob/master/meson.options).

The adapter is a native `sp7_ir` recorder with `grab()`, `read()`,
`release()`, `isOpened()`, `get()`, `set()`, and `open()` methods. It launches
one protected helper per attempt, consumes only `SP7IRF01` frames, checks
sequence/timestamp freshness, converts luma to BGR, reports the fixed
12-frame limit as clean exhaustion, and never restarts or replays a stream.
Unsupported controls return `False`; they are not reported as applied. The
early Howdy `device_path` existence check is bypassed only when
`recording_plugin=sp7_ir`; the adapter still requires the fixed root-owned
helper path. The existing Howdy recorder structure is documented in its
[capture implementation](https://github.com/boltgolt/howdy/blob/master/howdy/src/recorders/video_capture.py).

The patched Howdy comparison path has a total five-second monotonic attempt
deadline, containing the helper's three-second capture budget. Capture errors
fall back as unavailable, and clean 12-frame exhaustion is a no-match. No
result arriving after cleanup can authorize an attempt.

## Installed protection boundary

The protected runtime currently contains:

```text
/usr/lib64/security/pam_howdy.so                         root:root 0755
/usr/local/libexec/sp7-camera-auth-capture               root:root 0755
/usr/local/libexec/sp7-camera-howdy/python               root:root 0755
/usr/local/lib/howdy                                    root-owned
/usr/local/share/howdy/dlib-data                        root:root model files 0644
/var/lib/howdy/models                                   root:root 0700
/var/log/howdy                                          root:root 0700
/etc/howdy/config.ini                                   root:root 0644
/usr/local/sbin/howdy-sp7-ir-diagnostic                 root:root 0755
```

The two installed model files are checked against
`scripts/ir/howdy-models.sha256`. Enrollment storage is `/var/lib/howdy/models`
and is not the user-writable repository. The helper retains its root-owned
capture lock while a timed-out worker completes cleanup; a second attempt is
blocked until ownership is actually released.

The headless diagnostic is available as:

```sh
sudo /usr/local/sbin/howdy-sp7-ir-diagnostic --frames 12
```

It reports startup, fresh-frame metadata, clean exhaustion, release, and
elapsed time without requiring GUI support. The command was checked with
`--help`; it was not run against hardware in this milestone.

## PAM and pre-login state

The build did not install automatic PAM configuration. `/etc/pam.d/gdm-password`
was not modified; its pre-build and post-build SHA-256 was
`cb6415a67e63dbc99c40c4b156ebdf7956f16e180401dd35ecc2d757351f5e25`.
The host remains on authselect profile `local`, and SELinux remains
**Enforcing**.

Prepared but inactive artifacts are:

* `scripts/ir/pam-howdy-isolated` and
  `scripts/ir/install-isolated-pam-test.sh` for a separate PAM service;
* `scripts/ir/prepare-gdm-pam.sh`, which defaults to read-only `--show` and
  creates an exact root-owned rollback before any GNOME-only change;
* `scripts/ir/howdy-preflight.sh` and
  `systemd/system/sp7-camera-howdy-preflight.service` for a system-level
  pre-login readiness check;
* `scripts/ir/selinux-howdy-diagnostics.sh` for read-only AVC evidence.

The pre-login files were installed, but the service is disabled and inactive.
The preflight checks protected files only; it does not open the camera, swap
modules, change media links, restart the RGB bridge, or block password
fallback. No isolated PAM service was installed yet because there is no live
enrollment to exercise and no login configuration should be activated before
the hardware gates pass.

## Qualification status and next authorized session

Offline dependency/pipeline validation passed separately and is recorded in
`docs/ir-auth-pipeline-validation-20260919.md`. Its transformed public-image
fixtures must not be promoted into an authentication model.

The following remain **not tested**: fresh OV7251 availability, repeated
startup timing, live enrollment, independent live matching, exposure,
orientation, glasses, normal login distance, no-face, wrong-person,
malformed/truncated/stale frames through real Howdy, busy capture, timeout,
cancellation, camera-start failure, password-fallback timing, isolated PAM,
GNOME login, lock-screen unlock, boot, and resume. Controlled IR illumination
and darkness are deliberately unclaimed.

When a hardware session is explicitly authorized, use one prepared IR session
for fresh enrollment and a separate session for matching. Record capture
availability, time to decision, match outcome, and password fallback as
separate measurements. Include no-face, wrong-person, photo/display/replay,
startup failure, busy, timeout, cancellation, stale, malformed, and
truncated-frame cases. A false match remains a security failure: Howdy does
not provide evaluated spoof resistance, and password fallback prevents
lockout but does not add a second factor.

## Rollback

The runtime package can be removed or replaced as a separate root-owned
deployment. No PAM service was activated, so the exact GDM rollback is the
unchanged file hash above. If `prepare-gdm-pam.sh --apply` is later used, its
printed backup path is the only rollback input; do not edit generated
`password-auth` by hand.
