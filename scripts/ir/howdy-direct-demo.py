#!/usr/bin/env python3
"""Standalone Howdy-style enrollment/matching over the protected IR helper.

This intentionally does not import or configure PAM.  It uses the same dlib
detector/landmark/descriptor flow as Howdy, but reads the helper's private
fresh-frame protocol instead of opening a V4L2 node or /dev/video62.
"""

import argparse
import json
import os
from pathlib import Path
import stat
import struct
import subprocess
import sys
import tempfile
import time


HELPER_PATH = Path("/usr/local/libexec/sp7-camera-auth-capture")
MAGIC = b"SP7IRF01"
HEADER = struct.Struct("<8sIIIIIQQ")
WIDTH = 640
HEIGHT = 480
MAX_FRAMES = 12


class CaptureError(RuntimeError):
    """The direct capture session did not produce a usable frame stream."""


def resolve_helper() -> Path:
    try:
        info = HELPER_PATH.stat()
    except OSError as error:
        raise CaptureError(f"protected helper is unavailable: {HELPER_PATH}") from error
    if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
        raise CaptureError("protected helper is not a root-owned, non-writable regular file")
    if not os.access(HELPER_PATH, os.X_OK):
        raise CaptureError("protected helper is not executable")
    return HELPER_PATH


def read_exact(stream, length):
    data = bytearray()
    while len(data) < length:
        chunk = stream.read(length - len(data))
        if not chunk:
            if not data:
                return None
            raise CaptureError("direct capture ended in the middle of a frame")
        data.extend(chunk)
    return bytes(data)


def capture_frames(requested):
    helper = resolve_helper()
    process = subprocess.Popen(
        [str(helper), "--frames", str(requested)],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=None,
        close_fds=True,
    )
    frames = []
    seen = set()
    try:
        while len(frames) < requested:
            raw_header = read_exact(process.stdout, HEADER.size)
            if raw_header is None:
                break
            magic, version, width, height, payload, sequence, seconds, usec = HEADER.unpack(raw_header)
            if (magic != MAGIC or version != 1 or width != WIDTH or height != HEIGHT or
                    payload != WIDTH * HEIGHT):
                raise CaptureError("direct capture emitted an invalid frame header")
            raw_frame = read_exact(process.stdout, payload)
            if raw_frame is None:
                raise CaptureError("direct capture ended before the frame payload")
            freshness = (sequence, seconds, usec)
            if freshness in seen or (frames and freshness <= frames[-1][0]):
                raise CaptureError("direct capture emitted a stale or regressed frame")
            seen.add(freshness)
            frames.append((freshness, raw_frame))
    finally:
        if process.stdout is not None:
            process.stdout.close()
        try:
            status = process.wait(timeout=4)
        except subprocess.TimeoutExpired as error:
            raise CaptureError("direct capture helper did not finish cleanup") from error
    if status != 0:
        if status == 124:
            raise CaptureError("direct capture exceeded the three-second budget")
        if status == 2:
            raise CaptureError("direct capture is already owned by another worker")
        raise CaptureError(f"direct capture failed with status {status}")
    if len(frames) != requested:
        raise CaptureError(f"only {len(frames)} fresh frames arrived; needed {requested}")
    return frames


def load_engine(data_dir, use_cnn):
    try:
        import cv2
        import dlib
        import numpy as np
    except ImportError as error:
        raise CaptureError("Howdy's dlib, OpenCV, and NumPy dependencies are required") from error

    data_dir = Path(data_dir)
    detector_path = data_dir / "mmod_human_face_detector.dat"
    predictor_path = data_dir / "shape_predictor_5_face_landmarks.dat"
    encoder_path = data_dir / "dlib_face_recognition_resnet_model_v1.dat"
    missing = [str(path) for path in (predictor_path, encoder_path) if not path.is_file()]
    if use_cnn and not detector_path.is_file():
        missing.append(str(detector_path))
    if missing:
        raise CaptureError("missing Howdy dlib data: " + ", ".join(missing))
    detector = (dlib.cnn_face_detection_model_v1(str(detector_path)) if use_cnn
                else dlib.get_frontal_face_detector())
    predictor = dlib.shape_predictor(str(predictor_path))
    encoder = dlib.face_recognition_model_v1(str(encoder_path))
    return cv2, np, detector, predictor, encoder


def describe_faces(frames, engine):
    cv2, np, detector, predictor, encoder = engine
    clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))
    descriptions = []
    for freshness, raw_frame in frames:
        gray = np.frombuffer(raw_frame, dtype=np.uint8).reshape((HEIGHT, WIDTH))
        color = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
        locations = detector(clahe.apply(gray), 1)
        if len(locations) != 1:
            continue
        location = locations[0].rect if hasattr(locations[0], "rect") else locations[0]
        landmarks = predictor(color, location)
        descriptor = np.asarray(encoder.compute_face_descriptor(color, landmarks, 1))
        descriptions.append((freshness, descriptor))
    return descriptions


def write_model(path, descriptors, label):
    model = [{
        "id": 0,
        "label": label,
        "time": int(time.time()),
        "data": [descriptor.tolist() for descriptor in descriptors],
    }]
    path = Path(path)
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent, text=True)
    try:
        os.fchmod(handle, 0o600)
        with os.fdopen(handle, "w", encoding="utf-8") as output:
            json.dump(model, output)
            output.write("\n")
        os.replace(temporary, path)
    except Exception:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def load_model(path):
    try:
        with Path(path).open(encoding="utf-8") as source:
            models = json.load(source)
    except (OSError, ValueError) as error:
        raise CaptureError(f"could not load model {path}: {error}") from error
    if not isinstance(models, list):
        raise CaptureError("model must contain a list of face models")
    encodings = []
    for model in models:
        encodings.extend(model.get("data", []))
    if not encodings:
        raise CaptureError("model contains no face descriptors")
    return encodings


def default_data_dir():
    candidates = (
        Path("/usr/local/share/howdy/dlib-data"),
        Path("/usr/share/howdy/dlib-data"),
        Path("/usr/etc/howdy/dlib-data"),
        Path("/lib/security/howdy/dlib-data"),
    )
    return next((path for path in candidates if path.is_dir()), candidates[0])


def main():
    parser = argparse.ArgumentParser(description="Standalone direct-capture Howdy-style demo")
    parser.add_argument("mode", choices=("enroll", "match"))
    parser.add_argument("--model", required=True, help="demo model JSON path")
    parser.add_argument("--data-dir", default=str(default_data_dir()), help="Howdy dlib-data directory")
    parser.add_argument("--frames", type=int, default=5, choices=range(1, MAX_FRAMES + 1))
    parser.add_argument("--min-face-frames", type=int, default=3, choices=range(1, MAX_FRAMES + 1))
    parser.add_argument("--min-matches", type=int, default=2, choices=range(1, MAX_FRAMES + 1))
    parser.add_argument("--threshold", type=float, default=0.35)
    parser.add_argument("--label", default="direct-capture-demo")
    parser.add_argument("--cnn", action="store_true")
    args = parser.parse_args()
    if args.min_face_frames > args.frames or args.min_matches > args.frames:
        parser.error("minimum frame counts cannot exceed --frames")
    try:
        frames = capture_frames(args.frames)
        engine = load_engine(args.data_dir, args.cnn)
        descriptions = describe_faces(frames, engine)
        if args.mode == "enroll":
            if len(descriptions) < args.min_face_frames:
                raise CaptureError(
                    f"enrollment rejected: only {len(descriptions)} of {args.frames} frames had one face"
                )
            write_model(args.model, [descriptor for _, descriptor in descriptions], args.label)
            print(json.dumps({"result": "enrolled", "fresh_frames": len(frames),
                              "face_frames": len(descriptions), "model": args.model}))
            return 0
        encodings = load_model(args.model)
        import numpy as np
        known = np.asarray(encodings)
        matches = 0
        best = None
        for _, descriptor in descriptions:
            distance = float(np.min(np.linalg.norm(known - descriptor, axis=1)))
            best = distance if best is None else min(best, distance)
            if 0 < distance < args.threshold:
                matches += 1
        result = matches >= args.min_matches
        print(json.dumps({"result": "match" if result else "no-match",
                          "fresh_frames": len(frames), "face_frames": len(descriptions),
                          "matching_frames": matches, "best_distance": best,
                          "threshold": args.threshold}))
        return 0 if result else 1
    except CaptureError as error:
        print(json.dumps({"result": "unavailable", "detail": str(error)}))
        return 2


if __name__ == "__main__":
    sys.exit(main())
