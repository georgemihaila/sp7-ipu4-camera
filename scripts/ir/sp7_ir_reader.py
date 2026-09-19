"""Howdy recorder adapter for the protected SP7 IR capture helper."""

import cv2
import numpy as np

from recorders.sp7_ir_protocol import (
	CAPTURE_BUDGET_SECONDS,
	DirectCaptureSession,
	EXPECTED_HELPER_PATH,
	HEIGHT,
	MAX_FRAMES,
	Sp7IrBusy,
	Sp7IrCaptureError,
	Sp7IrTimeout,
	WIDTH,
)


class sp7_ir_reader:
	"""A single non-restarting 12-frame helper session per Howdy attempt."""

	def __init__(self, device_path, config=None):
		if device_path != str(EXPECTED_HELPER_PATH):
			raise Sp7IrCaptureError(
				"sp7_ir device_path must be the fixed protected capture helper"
			)
		self.device_path = device_path
		self.config = config
		attempt_deadline = getattr(config, "_sp7_ir_attempt_deadline", None)
		self._session = DirectCaptureSession(
			MAX_FRAMES, CAPTURE_BUDGET_SECONDS, deadline=attempt_deadline
		)
		self._started = False
		self._released = False
		self.exhausted = False
		self.last_error = None
		self.last_freshness = None

	def grab(self):
		if self._released:
			return False
		if not self._started:
			self._session.start()
			self._started = True
		return True

	def read(self):
		if self._released:
			return False, None
		if not self.grab():
			return False, None
		try:
			frame = self._session.read_luma()
		except (Sp7IrBusy, Sp7IrTimeout, Sp7IrCaptureError) as error:
			self.last_error = error
			self.release()
			raise
		if frame is None:
			self.exhausted = True
			return False, None
		self.last_freshness, luma = frame
		gray = np.frombuffer(luma, dtype=np.uint8).reshape((HEIGHT, WIDTH))
		# Howdy's descriptor path expects BGR even when the sensor is luma-only.
		bgr = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
		return True, bgr

	def release(self):
		if self._released:
			return
		self._released = True
		self._session.release()

	def isOpened(self):
		return not self._released and self._started

	def get(self, property_id):
		if property_id == cv2.CAP_PROP_FRAME_WIDTH:
			return float(WIDTH)
		if property_id == cv2.CAP_PROP_FRAME_HEIGHT:
			return float(HEIGHT)
		if property_id == cv2.CAP_PROP_FRAME_COUNT:
			return float(MAX_FRAMES)
		# Unsupported properties are reported as unavailable, not successful.
		return 0.0

	def set(self, property_id, value):
		# The protected helper owns format, exposure, FPS, and orientation.
		# Returning False follows the OpenCV contract and avoids pretending that
		# an unsupported camera control was applied.
		return False

	def open(self, *args, **kwargs):
		# A Howdy attempt owns exactly one helper process; never silently restart.
		return False
