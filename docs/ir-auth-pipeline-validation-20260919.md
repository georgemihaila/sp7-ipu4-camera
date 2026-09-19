# IR authentication demo: dependency and pipeline validation

Date: 2026-09-19<br>
Status: **PASS for offline dependency/pipeline validation only**

This record does not qualify the camera, face authentication, PAM, GNOME
login, lock screen, boot, resume, liveness, or spoof resistance. No PAM
package was installed, no PAM configuration was edited, and no desktop login
path was changed.

## Environment

The actual lazy imports in `scripts/ir/howdy-direct-demo.py` are:

```text
cv2
dlib
numpy
```

The isolated environment is `/home/george/repos/sp7-ipu4-camera/.venv-auth`.
It was prepared by `scripts/ir/prepare-auth-env.sh` with these runtime pins:

```text
numpy==2.4.6
opencv-python-headless==4.13.0.92
dlib==20.0.1
```

The environment contains only those three runtime packages plus the virtual
environment's pip. It contains no Howdy package, PAM binding, V4L2 Python
adapter, GStreamer binding, or FFmpeg binding. OpenCV is the headless wheel;
the demo does not open a camera through it.

The dlib build used the pinned source release because no matching Python 3.14
binary wheel was available. The installed environment reported:

```text
Python 3.14.7
numpy 2.4.6
opencv 4.13.0
dlib 20.0.1
```

## Offline test inputs and protection

The offline harness is `scripts/ir/validate-auth-demo-offline.py`. It imports
the production demo module but deliberately bypasses `capture_frames()`.
It uses a public OpenCV sample image only as a dependency fixture, generates
disjoint transformed frame sets in memory, and never invokes
`/usr/local/libexec/sp7-camera-auth-capture`, `/dev/video62`, or any V4L2
node.

Protected validation data is outside the repository:

```text
/home/george/.local/share/sp7-camera-auth-validation/       0700
.../dlib-data/                                              0700
.../*.dat and lena.jpg                                      0600
.../run-20260919/                                           0700
.../run-20260919/*.json                                     0600
```

The dlib files are the standard five-point landmark and face-recognition
models from [dlib-models](https://github.com/davisking/dlib-models). Recorded
SHA-256 values are:

```text
55533b28a95800a551ba546ba62fe69625c7e95a7061c338adffead08719da30  dlib_face_recognition_resnet_model_v1.dat
c4b1e9804792707d3a405c2c16a80a20269e6675021f64a41d30fffafbc41888  shape_predictor_5_face_landmarks.dat
7de7ed51a1594fff247f4cae2301eceacf5313d6011e37b4a4c8733f7bb72c07  lena.jpg
```

The JSON model written by the harness is a protected test model from the
public fixture, not an enrollment for a person. No protected user enrollment
data is committed to the repository.

## Offline results

The run was:

```text
./.venv-auth/bin/python scripts/ir/validate-auth-demo-offline.py \
  --data-dir /home/george/.local/share/sp7-camera-auth-validation/dlib-data \
  --fixture /home/george/.local/share/sp7-camera-auth-validation/lena.jpg \
  --output-dir /home/george/.local/share/sp7-camera-auth-validation/run-20260919
```

Results from the protected JSON record:

| Check | Result |
| --- | --- |
| dependency imports | PASS |
| offline enrollment face frames | 4/4 |
| independently transformed matching face frames | 4/4 |
| best distance at threshold 0.35 | 0.033853 |
| offline no-face blank frames | PASS: no face |
| model write/read protection | PASS: 0600 model under 0700 directory |
| total offline pipeline measurement | 2398.42 ms |
| dlib engine load | 556.63 ms |
| enrollment description | 771.60 ms |
| matching description | 785.18 ms |
| no-face description | 283.11 ms |

These timings include ordinary offline host processing and are not a camera
availability or authentication decision-time measurement.

## Hardware and authentication qualification status

The requested live tests remain intentionally **NOT TESTED** because this
milestone did not authorize a hardware session and the direct OV7251 path has
not been re-qualified here.

| Requested evidence | Status |
| --- | --- |
| fresh valid OV7251 capture availability | NOT TESTED |
| time to usable frames across repeated starts | NOT TESTED |
| independent IR enrollment and match | NOT TESTED |
| camera startup failure | NOT TESTED |
| live no-face behavior | NOT TESTED |
| three-second deadline while cleanup continues | NOT TESTED live; enforced by the protected helper in source/static tests |
| overlapping attempts blocked | NOT TESTED live; lock path is enforced by the protected helper in source/static tests |
| late result cannot authorize | NOT TESTED live; no PAM consumer exists |
| password-fallback timing | NOT TESTED; PAM is unchanged |
| exposure, orientation, glasses, normal login distance | NOT TESTED |
| adequate-light hardware recognition | NOT TESTED |
| controlled IR illumination and darkness | UNCLAIMED |
| photo/replay resistance or liveness | UNCLAIMED |

When hardware testing is explicitly authorized, the next session must enroll
only from fresh valid frames from one prepared IR session, then use a separate
capture session for matching. Capture availability, time to decision, match
result, and password fallback must be recorded as separate measurements. A
timeout must remain a non-authorizing result even if worker cleanup finishes
later, and a second attempt must remain blocked until the first worker has
actually released ownership.

The security limitation remains material: Howdy's own documentation warns
that face authentication can be fooled by lookalikes or photographs and
should not be treated as equivalent to a password. This demo has no liveness
or evaluated spoof-resistance mechanism; password fallback avoids lockout but
does not turn a false face match into strong authentication.

## Rollback

This milestone has no system rollback because it made no system or PAM
changes. To remove only the local validation artifacts, delete the explicitly
listed `.venv-auth` and `/home/george/.local/share/sp7-camera-auth-validation`
directories after preserving any desired result record. The repository
changes can be reverted by commit if required; no push was performed.
