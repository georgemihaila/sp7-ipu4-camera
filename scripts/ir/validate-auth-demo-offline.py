#!/usr/bin/env python3
"""Validate the direct-demo dependency and matcher pipeline without hardware.

This deliberately bypasses capture_frames(). It uses a local public fixture
and local dlib data only, so a PASS here is dependency/pipeline validation,
not camera or authentication qualification.
"""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import stat
import time


ROOT = Path(__file__).resolve().parents[2]
DEMO_PATH = ROOT / "scripts/ir/howdy-direct-demo.py"


def load_demo():
    spec = importlib.util.spec_from_file_location("sp7_howdy_direct_demo", DEMO_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {DEMO_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def protected_directory(path):
    path = Path(path)
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(path, 0o700)
    return path


def write_result(path, result):
    path = Path(path)
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    try:
        with temporary.open("w", encoding="utf-8") as output:
            os.fchmod(output.fileno(), 0o600)
            json.dump(result, output, indent=2, sort_keys=True)
            output.write("\n")
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def fixture_frames(demo, cv2, np, fixture):
    image = cv2.imread(str(fixture), cv2.IMREAD_GRAYSCALE)
    if image is None:
        raise RuntimeError(f"could not read fixture image: {fixture}")
    image = cv2.resize(image, (demo.WIDTH, demo.HEIGHT), interpolation=cv2.INTER_AREA)
    frames = []
    for index, (alpha, beta, dx, angle) in enumerate(
            ((1.00, 0, 0, 0), (1.02, 2, 1, 0), (0.98, 4, -1, 0),
             (1.04, -2, 1, 0), (0.96, 3, -1, 0), (1.01, 5, 2, 1.0),
             (0.97, 1, -2, -1.0), (1.03, -4, 2, -1.5)), start=1):
        transformed = cv2.convertScaleAbs(image, alpha=alpha, beta=beta)
        matrix = cv2.getRotationMatrix2D((demo.WIDTH / 2, demo.HEIGHT / 2), angle, 1.0)
        matrix[0, 2] += dx
        transformed = cv2.warpAffine(
            transformed, matrix, (demo.WIDTH, demo.HEIGHT),
            borderMode=cv2.BORDER_REPLICATE,
        )
        freshness = (index, 1_800_000_000 + index, index * 1000)
        frames.append((freshness, transformed.tobytes()))
    return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", required=True)
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--threshold", type=float, default=0.35)
    args = parser.parse_args()

    import cv2
    import dlib  # noqa: F401 - prove the direct demo's actual import works
    import numpy as np

    demo = load_demo()
    output_dir = protected_directory(args.output_dir)
    fixture = Path(args.fixture)
    if not fixture.is_file():
        raise RuntimeError(f"fixture is not a regular file: {fixture}")
    fixture_mode = stat.S_IMODE(fixture.stat().st_mode)

    frames = fixture_frames(demo, cv2, np, fixture)
    blank = np.zeros((demo.HEIGHT, demo.WIDTH), dtype=np.uint8).tobytes()
    no_face_frames = [((index, 1_900_000_000 + index, index * 1000), blank)
                      for index in range(1, 4)]

    timings = {}
    started = time.monotonic()
    engine_started = time.monotonic()
    engine = demo.load_engine(args.data_dir, False)
    timings["engine_load_ms"] = round((time.monotonic() - engine_started) * 1000, 2)

    enroll_started = time.monotonic()
    enroll_descriptions = demo.describe_faces(frames[:4], engine)
    timings["enroll_description_ms"] = round((time.monotonic() - enroll_started) * 1000, 2)
    match_started = time.monotonic()
    match_descriptions = demo.describe_faces(frames[4:], engine)
    timings["match_description_ms"] = round((time.monotonic() - match_started) * 1000, 2)
    no_face_started = time.monotonic()
    no_face_descriptions = demo.describe_faces(no_face_frames, engine)
    timings["no_face_ms"] = round((time.monotonic() - no_face_started) * 1000, 2)

    if len(enroll_descriptions) < 3:
        raise RuntimeError(f"fixture enrollment detected only {len(enroll_descriptions)} faces")
    if len(match_descriptions) < 3:
        raise RuntimeError(f"fixture matching detected only {len(match_descriptions)} faces")
    if no_face_descriptions:
        raise RuntimeError("blank frames unexpectedly produced a face")

    model_path = output_dir / "offline-validation-model.json"
    demo.write_model(model_path, [descriptor for _, descriptor in enroll_descriptions],
                     "offline-public-fixture")
    encodings = demo.load_model(model_path)
    known = np.asarray(encodings)
    distances = []
    for _, descriptor in match_descriptions:
        distances.append(float(np.min(np.linalg.norm(known - descriptor, axis=1))))
    matching_frames = sum(0 < distance < args.threshold for distance in distances)
    result = matching_frames >= 2
    timings["decision_ms"] = round((time.monotonic() - started) * 1000, 2)

    if not result:
        raise RuntimeError(
            f"offline matcher rejected the independently transformed fixture: "
            f"{matching_frames}/{len(distances)} frames, best={min(distances):.6f}"
        )

    output = {
        "classification": "dependency/pipeline validation only",
        "hardware_capture": "not used",
        "pam_or_login_configuration": "not used",
        "fixture": str(fixture),
        "fixture_mode": oct(fixture_mode),
        "data_dir": str(Path(args.data_dir)),
        "model": str(model_path),
        "enrollment_face_frames": len(enroll_descriptions),
        "match_face_frames": len(match_descriptions),
        "matching_frames": matching_frames,
        "match_result": "match",
        "best_distance": min(distances),
        "threshold": args.threshold,
        "no_face_result": "no-face",
        "timings_ms": timings,
    }
    result_path = output_dir / "offline-validation-result.json"
    write_result(result_path, output)
    print(json.dumps(output, sort_keys=True))
    print(f"result_file={result_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, ValueError) as error:
        print(f"offline validation failed: {error}")
        raise SystemExit(2)
