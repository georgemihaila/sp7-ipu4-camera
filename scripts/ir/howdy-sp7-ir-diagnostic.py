#!/usr/bin/env python3
"""Headless capture diagnostic for the installed Howdy sp7_ir recorder."""

import argparse
import configparser
import json
import sys
import time


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--config", default="/etc/howdy/config.ini")
	parser.add_argument("--howdy-python-dir", default="/usr/local/lib/howdy")
	parser.add_argument("--frames", type=int, default=12, choices=range(1, 13))
	args = parser.parse_args()

	sys.path.insert(0, args.howdy_python_dir)
	from recorders.sp7_ir_reader import Sp7IrCaptureError, sp7_ir_reader

	config = configparser.ConfigParser()
	if not config.read(args.config):
		print(json.dumps({"result": "unavailable", "detail": "config is missing"}))
		return 2
	plugin = config.get("video", "recording_plugin", fallback="")
	device_path = config.get("video", "device_path", fallback="")
	if plugin != "sp7_ir":
		print(json.dumps({"result": "unavailable", "detail": "recording_plugin is not sp7_ir"}))
		return 2
	started = time.monotonic()
	reader = None
	frames = 0
	try:
		reader = sp7_ir_reader(device_path, config)
		if not reader.grab():
			print(json.dumps({"result": "unavailable", "detail": "helper did not start"}))
			return 2
		print(json.dumps({"event": "started", "helper": device_path}))
		while frames < args.frames:
			ok, image = reader.read()
			if not ok:
				print(json.dumps({"event": "clean-exhaustion", "frames": frames}))
				break
			frames += 1
			print(json.dumps({
				"event": "frame",
				"frame": frames,
				"freshness": reader.last_freshness,
				"shape": list(image.shape),
				"dtype": str(image.dtype),
			}))
		return 0
	except Sp7IrCaptureError as error:
		print(json.dumps({"result": "unavailable", "detail": str(error), "frames": frames}))
		return 2
	finally:
		if reader is not None:
			reader.release()
		print(json.dumps({
			"event": "released",
			"frames": frames,
			"elapsed_ms": round((time.monotonic() - started) * 1000, 2),
		}))


if __name__ == "__main__":
	try:
		raise SystemExit(main())
	except (ImportError, OSError, ValueError) as error:
		print(json.dumps({"result": "unavailable", "detail": str(error)}))
		raise SystemExit(2)
