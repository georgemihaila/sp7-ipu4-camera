#!/usr/bin/env python3
"""Measure frame count and simple real-image evidence from a V4L2 stream."""

from __future__ import annotations

import io
import statistics
import sys
import time

from PIL import Image, ImageStat


WIDTH = 1280
HEIGHT = 720
YUYV_FRAME_BYTES = WIDTH * HEIGHT * 2


def image_signal(payload: bytes) -> tuple[float, float, int, int]:
    with Image.open(io.BytesIO(payload)) as image:
        image.load()
        gray = image.convert("L")
        stat = ImageStat.Stat(gray)
        return stat.mean[0], stat.stddev[0], image.width, image.height


def report(fmt: str, frames: int, first_real: int | None, means: list[float],
           stddevs: list[float], width: int, height: int, elapsed: float) -> None:
    rate = frames / elapsed if elapsed > 0 else 0.0
    print(f"frames={frames}")
    print(f"rate={rate:.3f}")
    print(f"first_real_frame={first_real if first_real is not None else 0}")
    print(f"width={width}")
    print(f"height={height}")
    print(f"mean_min={min(means) if means else 0.0:.3f}")
    print(f"mean_max={max(means) if means else 0.0:.3f}")
    print(f"stddev_min={min(stddevs) if stddevs else 0.0:.3f}")
    print(f"stddev_max={max(stddevs) if stddevs else 0.0:.3f}")
    print(f"format={fmt}")


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in {"YUYV", "MJPG", "JPEG"}:
        print(f"usage: {sys.argv[0]} YUYV|MJPG|JPEG", file=sys.stderr)
        return 2
    fmt = sys.argv[1]
    frames = 0
    first_real: int | None = None
    means: list[float] = []
    stddevs: list[float] = []
    width = height = 0
    started = time.monotonic()

    if fmt == "YUYV":
        while True:
            payload = sys.stdin.buffer.read(YUYV_FRAME_BYTES)
            if len(payload) != YUYV_FRAME_BYTES:
                break
            y = payload[0::2]
            values = list(y[::max(1, len(y) // 4096)])
            mean = statistics.fmean(values)
            stddev = statistics.pstdev(values) if len(values) > 1 else 0.0
            frames += 1
            means.append(mean)
            stddevs.append(stddev)
            width, height = WIDTH, HEIGHT
            if first_real is None and (mean > 22.0 or stddev > 3.0):
                first_real = frames
    else:
        buffer = bytearray()
        while chunk := sys.stdin.buffer.read(1024 * 1024):
            buffer.extend(chunk)
            while True:
                start = buffer.find(b"\xff\xd8")
                if start < 0:
                    if len(buffer) > 1:
                        del buffer[:-1]
                    break
                end = buffer.find(b"\xff\xd9", start + 2)
                if end < 0:
                    if start:
                        del buffer[:start]
                    break
                payload = bytes(buffer[start:end + 2])
                del buffer[:end + 2]
                try:
                    mean, stddev, width, height = image_signal(payload)
                except Exception as exc:  # malformed buffer; keep counting later frames
                    print(f"invalid_jpeg={exc}", file=sys.stderr)
                    continue
                frames += 1
                means.append(mean)
                stddevs.append(stddev)
                if first_real is None and (mean > 22.0 or stddev > 3.0):
                    first_real = frames

    report(fmt, frames, first_real, means, stddevs, width, height,
           time.monotonic() - started)
    return 0 if frames else 1


if __name__ == "__main__":
    raise SystemExit(main())
