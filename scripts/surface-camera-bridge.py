#!/usr/bin/python3
"""Feed named Surface Camera V4L2 endpoints from one IPU4P camera at a time."""

from __future__ import annotations

import logging
import os
import signal
import subprocess
import time
from pathlib import Path


CAMERAS = {
    "front": {
        "device": "/dev/video60",
        # gst-launch parses this as a property string; the doubled backslash
        # becomes the leading backslash in libcamera's ACPI camera id.
        "camera_id": r"\\_SB_.PCI0.I2C2.CAMF",
    },
    "rear": {
        "device": "/dev/video61",
        "camera_id": r"\\_SB_.PCI0.I2C3.CAMR",
    },
}

POLL_SECONDS = 0.20
OPEN_DEBOUNCE_SECONDS = 0.45
CLOSE_GRACE_SECONDS = 1.50
RESTART_DELAY_SECONDS = 2.0
VIDEO_WIDTH = 1280
VIDEO_HEIGHT = 720
VIDEO_FRAMERATE = "30/1"
WIREPLUMBER_UNIT = "wireplumber.service"

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
)
LOG = logging.getLogger("sp7-camera-bridge")


def _effective_uid(pid: str) -> int | None:
    try:
        with open(f"/proc/{pid}/status", encoding="ascii") as status:
            for line in status:
                if line.startswith("Uid:"):
                    return int(line.split()[2])
    except (FileNotFoundError, PermissionError, ProcessLookupError, ValueError):
        return None
    return None


def consumers(exclude_pids: set[int]) -> set[str]:
    """Return camera keys held open by this user's other processes."""
    device_to_camera = {camera["device"]: key for key, camera in CAMERAS.items()}
    found: set[str] = set()
    current_uid = os.getuid()

    try:
        pids = os.listdir("/proc")
    except OSError as exc:
        LOG.warning("cannot scan /proc for camera clients: %s", exc)
        return found

    for pid_text in pids:
        if not pid_text.isdecimal():
            continue
        pid = int(pid_text)
        if pid == os.getpid() or pid in exclude_pids:
            continue
        if _effective_uid(pid_text) != current_uid:
            continue

        try:
            fds = os.listdir(f"/proc/{pid_text}/fd")
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            continue
        for fd in fds:
            try:
                target = os.readlink(f"/proc/{pid_text}/fd/{fd}")
            except (FileNotFoundError, PermissionError, ProcessLookupError):
                continue
            camera = device_to_camera.get(target.removesuffix(" (deleted)"))
            if camera is not None:
                found.add(camera)

    return found


def device_format(camera_key: str) -> str:
    """Return the loopback's current V4L2 pixel format.

    A consumer may set this before the producer opens the device. Keep the
    producer in step with that choice so v4l2sink does not fail negotiation.
    """
    device = CAMERAS[camera_key]["device"]
    result = subprocess.run(
        ["/usr/bin/v4l2-ctl", "-d", device, "--get-fmt-video"],
        check=False,
        capture_output=True,
        text=True,
    )
    for format_name in ("MJPG", "JPEG", "YUYV"):
        if f"'{format_name}'" in result.stdout:
            return format_name
    LOG.warning("could not determine %s format; using YUYV", device)
    return "YUYV"


def output_caps(format_name: str) -> list[str]:
    if format_name in ("MJPG", "JPEG"):
        return [
            "!", f"video/x-raw,format=I420,width={VIDEO_WIDTH},height={VIDEO_HEIGHT},framerate={VIDEO_FRAMERATE}",
            "!", "jpegenc", "quality=85", "!", "jpegparse", "!",
            f"image/jpeg,parsed=true,width={VIDEO_WIDTH},height={VIDEO_HEIGHT},framerate={VIDEO_FRAMERATE}",
        ]
    return [
        "!", "videoconvert", "!",
        f"video/x-raw,format=YUY2,width={VIDEO_WIDTH},height={VIDEO_HEIGHT},framerate={VIDEO_FRAMERATE}",
    ]


def pipeline_for(camera_key: str) -> list[str]:
    camera = CAMERAS[camera_key]
    format_name = device_format(camera_key)
    if format_name in ("MJPG", "JPEG"):
        # v4l2sink tries to renegotiate S_FMT after the consumer has started
        # streaming. v4l2loopback rejects that for compressed formats, which
        # leaves libcamerasrc blocked. filesink writes each complete JPEG
        # buffer to the already configured loopback without another ioctl.
        sink = ["filesink", f"location={camera['device']}"]
    else:
        sink = ["v4l2sink", f"device={camera['device']}", "sync=false"]
    return [
        "/usr/bin/gst-launch-1.0", "-e", "libcamerasrc",
        f"camera-name={camera['camera_id']}", "ae-enable=true", "!",
        # The IPU4P sensor/ISP output is already upright. Keep that orientation
        # for every V4L2 consumer instead of applying a bridge-wide rotation.
        "videoconvert", "!", "videoscale",
        *output_caps(format_name), "!", *sink,
    ]


def filler_for(camera_key: str) -> list[str]:
    """Keep a stable, capturable V4L2 endpoint while its sensor is idle."""
    camera = CAMERAS[camera_key]
    return [
        "/usr/bin/gst-launch-1.0", "-e", "videotestsrc", "is-live=true",
        "pattern=black", "!", "videoconvert", "!", "videoscale",
        *output_caps(device_format(camera_key)), "!", "v4l2sink",
        f"device={camera['device']}", "sync=false",
    ]


def stop_pipeline(process: subprocess.Popen[bytes] | None) -> None:
    if process is None or process.poll() is not None:
        return
    LOG.info("stopping the current camera stream")
    process.send_signal(signal.SIGINT)
    try:
        process.wait(timeout=4)
    except subprocess.TimeoutExpired:
        LOG.warning("camera pipeline did not stop cleanly; terminating it")
        process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def wireplumber_is_active() -> bool:
    return subprocess.run(
        ["systemctl", "--user", "is-active", "--quiet", WIREPLUMBER_UNIT],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    ).returncode == 0


def wireplumber_stop() -> bool:
    """Release libcamera's shared V4L2 backend before direct capture."""
    if not wireplumber_is_active():
        return False
    LOG.info("stopping WirePlumber to release the shared IPU4P camera backend")
    result = subprocess.run(
        ["systemctl", "--user", "stop", WIREPLUMBER_UNIT],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        LOG.error("could not stop WirePlumber: %s", result.stderr.strip())
        return False
    # libcamera's manager closes the V4L2 node during service shutdown. Give
    # it a short settling period before opening the same backend directly.
    time.sleep(0.8)
    return True


def wireplumber_start() -> None:
    LOG.info("starting WirePlumber after releasing the IPU4P camera backend")
    result = subprocess.run(
        ["systemctl", "--user", "start", WIREPLUMBER_UNIT],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        LOG.error("could not start WirePlumber: %s", result.stderr.strip())


def _handle_shutdown(_signum: int, _frame: object) -> None:
    raise KeyboardInterrupt


def main() -> int:
    for camera in CAMERAS.values():
        if not Path(camera["device"]).exists():
            LOG.error("missing virtual camera device %s", camera["device"])
            return 1

    signal.signal(signal.SIGTERM, _handle_shutdown)
    signal.signal(signal.SIGINT, _handle_shutdown)

    capture_process: subprocess.Popen[bytes] | None = None
    filler_processes: dict[str, subprocess.Popen[bytes]] = {}
    active: str | None = None
    candidate: str | None = None
    candidate_since = time.monotonic()
    idle_since: float | None = None
    retry_after: dict[str, float] = {}
    wireplumber_was_active = False
    LOG.info("watching for consumers of the front and rear camera devices")

    try:
        for camera_key in CAMERAS:
            LOG.info("starting idle V4L2 signal for %s", CAMERAS[camera_key]["device"])
            filler_processes[camera_key] = subprocess.Popen(filler_for(camera_key))

        while True:
            now = time.monotonic()
            for camera_key, idle_process in list(filler_processes.items()):
                if idle_process.poll() is not None and camera_key != active:
                    LOG.warning("idle signal exited for %s; restarting it", CAMERAS[camera_key]["device"])
                    filler_processes[camera_key] = subprocess.Popen(filler_for(camera_key))

            excluded = {
                process.pid
                for process in [capture_process, *filler_processes.values()]
                if process is not None and process.poll() is None
            }
            open_cameras = consumers(excluded)

            # Most applications open only the selected camera. If an
            # application probes both, keep the current route; when no route
            # is active, retain the current rear-camera default.
            if active in open_cameras:
                requested = active
            elif "rear" in open_cameras:
                requested = "rear"
            elif "front" in open_cameras:
                requested = "front"
            else:
                requested = None

            if requested is None:
                candidate = None
                if active is not None:
                    if idle_since is None:
                        idle_since = now
                    elif now - idle_since >= CLOSE_GRACE_SECONDS:
                        stopped_camera = active
                        stop_pipeline(capture_process)
                        capture_process = None
                        active = None
                        idle_since = None
                        retry_after.pop(stopped_camera, None)
                        LOG.info("restoring idle signal for %s", CAMERAS[stopped_camera]["device"])
                        filler_processes[stopped_camera] = subprocess.Popen(
                            filler_for(stopped_camera)
                        )
                        if wireplumber_was_active:
                            wireplumber_start()
                            wireplumber_was_active = False
                time.sleep(POLL_SECONDS)
                continue

            idle_since = None
            if requested != candidate:
                candidate = requested
                candidate_since = now
            if requested != active and now - candidate_since < OPEN_DEBOUNCE_SECONDS:
                time.sleep(POLL_SECONDS)
                continue

            if requested != active:
                if now < retry_after.get(requested, 0.0):
                    time.sleep(POLL_SECONDS)
                    continue
                if active is not None:
                    old_camera = active
                    stop_pipeline(capture_process)
                    capture_process = None
                    active = None
                    LOG.info("restoring idle signal for %s", CAMERAS[old_camera]["device"])
                    filler_processes[old_camera] = subprocess.Popen(filler_for(old_camera))

                idle_process = filler_processes.pop(requested, None)
                stop_pipeline(idle_process)
                active = None
                if not wireplumber_was_active:
                    wireplumber_was_active = wireplumber_stop()
                # Let IPU4P tear down the old route before setting up the
                # other sensor on the shared backend input.
                time.sleep(0.35)
                try:
                    LOG.info("starting %s camera for %s", requested, CAMERAS[requested]["device"])
                    capture_process = subprocess.Popen(pipeline_for(requested))
                    active = requested
                except OSError as exc:
                    LOG.error("could not start GStreamer camera pipeline: %s", exc)
                    retry_after[requested] = time.monotonic() + RESTART_DELAY_SECONDS
                    filler_processes[requested] = subprocess.Popen(filler_for(requested))
            elif capture_process is not None and capture_process.poll() is not None:
                status = capture_process.returncode
                LOG.warning("%s camera pipeline exited with status %s", active, status)
                failed_camera = active
                capture_process = None
                active = None
                retry_after[failed_camera] = time.monotonic() + RESTART_DELAY_SECONDS
                LOG.info("restoring idle signal for %s", CAMERAS[failed_camera]["device"])
                filler_processes[failed_camera] = subprocess.Popen(filler_for(failed_camera))
                if wireplumber_was_active:
                    wireplumber_start()
                    wireplumber_was_active = False

            time.sleep(POLL_SECONDS)
    except KeyboardInterrupt:
        LOG.info("shutting down")
    finally:
        stop_pipeline(capture_process)
        for process in filler_processes.values():
            stop_pipeline(process)
        if wireplumber_was_active:
            wireplumber_start()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
