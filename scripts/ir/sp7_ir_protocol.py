"""Bounded reader for the protected SP7 direct-capture frame protocol."""

import os
from pathlib import Path
import selectors
import stat
import struct
import subprocess
import time


EXPECTED_HELPER_PATH = Path("/usr/local/libexec/sp7-camera-auth-capture")
EXPECTED_ILLUMINATOR_PARAM = Path(
    "/sys/module/ov7251/parameters/experimental_strobe_output"
)
MAGIC = b"SP7IRF01"
HEADER = struct.Struct("<8sIIIIIQQ")
WIDTH = 640
HEIGHT = 480
MAX_FRAMES = 12
CAPTURE_BUDGET_SECONDS = 3.0
CLEANUP_GRACE_SECONDS = 0.25
MAX_FRAME_AGE_NS = 2_000_000_000


class Sp7IrCaptureError(RuntimeError):
    """The helper did not provide a valid bounded capture session."""


class Sp7IrBusy(Sp7IrCaptureError):
    """Another protected capture session owns the camera."""


class Sp7IrTimeout(Sp7IrCaptureError):
    """The helper or its frame stream exceeded the capture budget."""


class Sp7IrExhausted(Sp7IrCaptureError):
    """The fixed per-attempt frame budget was consumed cleanly."""


def resolve_helper(path=None):
    path = EXPECTED_HELPER_PATH if path is None else Path(path)
    if path != EXPECTED_HELPER_PATH:
        raise Sp7IrCaptureError("sp7_ir requires the fixed protected helper path")
    try:
        info = path.stat()
    except OSError as error:
        raise Sp7IrCaptureError(f"protected helper is unavailable: {path}") from error
    if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
        raise Sp7IrCaptureError("protected helper is not a root-owned, non-writable regular file")
    if not os.access(path, os.X_OK):
        raise Sp7IrCaptureError("protected helper is not executable")
    return path


def require_illuminator():
    """Require the patched OV7251 driver to enable illumination on stream-on."""
    try:
        state = EXPECTED_ILLUMINATOR_PARAM.read_text(encoding="ascii").strip().lower()
    except OSError as error:
        raise Sp7IrCaptureError(
            "OV7251 illuminator control is unavailable"
        ) from error
    if state not in ("y", "1", "true"):
        raise Sp7IrCaptureError("OV7251 illuminator is not enabled")


def _remaining(deadline):
    return max(0.0, deadline - time.monotonic())


def _fresh_timestamp(seconds, usec):
    if usec >= 1_000_000:
        return False
    timestamp_ns = seconds * 1_000_000_000 + usec * 1_000
    now_ns = time.monotonic_ns()
    return timestamp_ns <= now_ns and now_ns - timestamp_ns <= MAX_FRAME_AGE_NS


class DirectCaptureSession:
    """One helper process and one non-restarting, deadline-aware frame stream."""

    def __init__(self, requested_frames=MAX_FRAMES, budget=CAPTURE_BUDGET_SECONDS,
                 deadline=None):
        if not 1 <= requested_frames <= MAX_FRAMES:
            raise ValueError("requested frame count is outside the fixed limit")
        self.requested_frames = requested_frames
        self.budget = budget
        self.external_deadline = deadline
        self.process = None
        self._fd = None
        self._deadline = None
        self._frames_read = 0
        self._last_freshness = None
        self.exhausted = False
        self._closed = False

    def start(self):
        if self._closed:
            raise Sp7IrCaptureError("capture session was released")
        if self.process is not None:
            return
        started = time.monotonic()
        if self.external_deadline is not None and started >= self.external_deadline:
            raise Sp7IrTimeout("face attempt deadline expired before direct capture")
        helper = resolve_helper()
        require_illuminator()
        self.process = subprocess.Popen(
            [str(helper), "--frames", str(self.requested_frames)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=None,
            close_fds=True,
        )
        self._fd = self.process.stdout.fileno()
        os.set_blocking(self._fd, False)
        self._deadline = started + self.budget
        if self.external_deadline is not None:
            self._deadline = min(self._deadline, self.external_deadline)

    def _read_exact(self, length):
        data = bytearray()
        selector = selectors.DefaultSelector()
        selector.register(self._fd, selectors.EVENT_READ)
        try:
            while len(data) < length:
                remaining = _remaining(self._deadline)
                if remaining <= 0.0:
                    raise Sp7IrTimeout("direct capture exceeded the three-second budget")
                events = selector.select(remaining)
                if not events:
                    raise Sp7IrTimeout("direct capture exceeded the three-second budget")
                try:
                    chunk = os.read(self._fd, length - len(data))
                except BlockingIOError:
                    continue
                except InterruptedError:
                    continue
                if not chunk:
                    if data:
                        raise Sp7IrCaptureError("direct capture ended in a truncated frame")
                    return None
                data.extend(chunk)
        finally:
            selector.close()
        return bytes(data)

    def _process_failure(self):
        status = self.process.poll()
        if status == 124:
            raise Sp7IrTimeout("direct capture exceeded the three-second budget")
        if status == 2:
            raise Sp7IrBusy("direct capture is already owned by another worker")
        raise Sp7IrCaptureError(f"direct capture failed with status {status}")

    def read_luma(self):
        self.start()
        if self._frames_read >= self.requested_frames:
            self.exhausted = True
            return None
        raw_header = self._read_exact(HEADER.size)
        if raw_header is None:
            self._process_failure()
        magic, version, width, height, payload, sequence, seconds, usec = HEADER.unpack(raw_header)
        if (magic != MAGIC or version != 1 or width != WIDTH or height != HEIGHT or
                payload != WIDTH * HEIGHT):
            raise Sp7IrCaptureError("direct capture emitted an invalid frame header")
        raw_frame = self._read_exact(payload)
        if raw_frame is None:
            raise Sp7IrCaptureError("direct capture ended before the frame payload")
        freshness = (sequence, seconds, usec)
        if (self._last_freshness is not None and freshness <= self._last_freshness):
            raise Sp7IrCaptureError("direct capture emitted a stale or regressed frame")
        if not _fresh_timestamp(seconds, usec):
            raise Sp7IrCaptureError("direct capture emitted a stale frame timestamp")
        self._last_freshness = freshness
        self._frames_read += 1
        return freshness, raw_frame

    def release(self):
        if self._closed:
            return
        self._closed = True
        process = self.process
        if process is None:
            return
        try:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=CLEANUP_GRACE_SECONDS)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=CLEANUP_GRACE_SECONDS)
        finally:
            if process.stdout is not None:
                process.stdout.close()


def capture_frames(requested):
    session = DirectCaptureSession(requested)
    frames = []
    try:
        while len(frames) < requested:
            frame = session.read_luma()
            if frame is None:
                raise Sp7IrCaptureError(
                    f"only {len(frames)} fresh frames arrived; needed {requested}"
                )
            frames.append(frame)
        try:
            status = session.process.wait(timeout=_remaining(session._deadline))
        except subprocess.TimeoutExpired as error:
            raise Sp7IrTimeout("direct capture exceeded the three-second budget") from error
        if status != 0:
            session._process_failure()
        return frames
    finally:
        session.release()
