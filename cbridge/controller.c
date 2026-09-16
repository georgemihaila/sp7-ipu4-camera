#include "controller.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPEN_DEBOUNCE_MS 450U
#define CLOSE_GRACE_MS 1500U
#define RETRY_BASE_MS 2000U
#define RETRY_MAX_MS 30000U

struct CameraController {
	ControllerOps ops;
	ControllerState state;
	CameraKey active;
	CameraKey candidate;
	CameraKey pending;
	uint64_t candidate_since;
	uint64_t idle_since;
	uint64_t capture_retry_after[CAMERA_COUNT];
	uint64_t filler_retry_after[CAMERA_COUNT];
	unsigned capture_retry_exponent[CAMERA_COUNT];
	unsigned filler_retry_exponent[CAMERA_COUNT];
	void *capture;
	void *filler[CAMERA_COUNT];
	MediaFormat filler_format[CAMERA_COUNT];
	bool wireplumber_stopped;
};

static void set_error(char *error, unsigned error_size, const char *format, ...)
{
	va_list args;

	if (error == NULL || error_size == 0U)
		return;
	va_start(args, format);
	(void)vsnprintf(error, error_size, format, args);
	va_end(args);
}

static uint64_t now_ms(const CameraController *controller)
{
	return controller->ops.now_ms(controller->ops.context);
}

static const char *camera_name(CameraKey camera)
{
	return camera == CAMERA_FRONT ? "front" : "rear";
}

static uint64_t retry_delay_ms(unsigned *exponent)
{
	unsigned current_exponent = *exponent;
	uint64_t delay = RETRY_BASE_MS;

	if (current_exponent > 4U)
		current_exponent = 4U;
	delay <<= current_exponent;
	if (delay > RETRY_MAX_MS)
		delay = RETRY_MAX_MS;
	if (*exponent < 5U)
		(*exponent)++;
	return delay;
}

static void schedule_capture_retry(CameraController *controller, CameraKey camera)
{
	uint64_t delay;

	if (camera < CAMERA_FRONT || camera >= CAMERA_COUNT)
		return;
	delay = retry_delay_ms(&controller->capture_retry_exponent[camera]);
	controller->capture_retry_after[camera] = now_ms(controller) + delay;
}

static void schedule_filler_retry(CameraController *controller, CameraKey camera)
{
	uint64_t delay;

	if (camera < CAMERA_FRONT || camera >= CAMERA_COUNT)
		return;
	delay = retry_delay_ms(&controller->filler_retry_exponent[camera]);
	controller->filler_retry_after[camera] = now_ms(controller) + delay;
}

static void clear_capture_retry(CameraController *controller, CameraKey camera)
{
	if (camera < CAMERA_FRONT || camera >= CAMERA_COUNT)
		return;
	controller->capture_retry_after[camera] = 0U;
	controller->capture_retry_exponent[camera] = 0U;
}

static void clear_filler_retry(CameraController *controller, CameraKey camera)
{
	if (camera < CAMERA_FRONT || camera >= CAMERA_COUNT)
		return;
	controller->filler_retry_after[camera] = 0U;
	controller->filler_retry_exponent[camera] = 0U;
}

static int stop_handle(CameraController *controller, void **handle,
	char *error, unsigned error_size)
{
	int result;

	if (*handle == NULL)
		return 0;
	result = controller->ops.backend_stop(controller->ops.context, *handle,
		error, error_size);
	if (result != 0)
		return -1;
	controller->ops.backend_free(controller->ops.context, *handle);
	*handle = NULL;
	return 0;
}

static int start_filler(CameraController *controller, CameraKey camera)
{
	char error[256];
	MediaFormat format = MEDIA_FORMAT_YUYV;
	void *handle = NULL;

	if (controller->filler[camera] != NULL ||
		now_ms(controller) < controller->filler_retry_after[camera])
		return 0;
	if (controller->ops.query_format(controller->ops.context, camera, &format,
		error, sizeof(error)) != 0) {
		(void)fprintf(stderr, "format query for %s filler failed: %s; using YUYV\n",
			camera_name(camera), error);
		format = MEDIA_FORMAT_YUYV;
	}
	if (controller->ops.backend_start(controller->ops.context, camera, true,
		format, &handle, error, sizeof(error)) != 0) {
		(void)fprintf(stderr, "starting %s filler failed: %s\n",
			camera_name(camera), error);
		schedule_filler_retry(controller, camera);
		return -1;
	}
	controller->filler[camera] = handle;
	controller->filler_format[camera] = format;
	clear_filler_retry(controller, camera);
	return 0;
}

static void poll_filler(CameraController *controller, CameraKey camera)
{
	char error[256];
	int result;

	if (controller->filler[camera] == NULL)
		return;
	result = controller->ops.backend_poll(controller->ops.context,
		controller->filler[camera], error, sizeof(error));
	if (result == 0)
		return;
	(void)fprintf(stderr, "%s filler ended: %s\n", camera_name(camera),
		result < 0 ? error : "end-of-stream");
	if (stop_handle(controller, &controller->filler[camera], error,
		sizeof(error)) != 0)
		(void)fprintf(stderr, "stopping failed %s filler: %s\n",
			camera_name(camera), error);
	schedule_filler_retry(controller, camera);
}

static int restore_wireplumber(CameraController *controller)
{
	char error[256];

	if (!controller->wireplumber_stopped)
		return 0;
	if (controller->ops.wireplumber_start(controller->ops.context, error,
		sizeof(error)) != 0) {
		(void)fprintf(stderr, "restoring WirePlumber failed: %s\n", error);
		return -1;
	}
	controller->wireplumber_stopped = false;
	return 0;
}

static int stop_capture(CameraController *controller, CameraKey retry_camera)
{
	char error[256];

	if (stop_handle(controller, &controller->capture, error, sizeof(error)) != 0) {
		(void)fprintf(stderr, "stopping %s camera failed: %s\n",
			retry_camera == CAMERA_NONE ? "current" : camera_name(retry_camera),
			error);
		if (retry_camera != CAMERA_NONE)
			schedule_capture_retry(controller, retry_camera);
		return -1;
	}
	return 0;
}

static int start_capture(CameraController *controller, CameraKey camera)
{
	char error[256];
	MediaFormat format;
	void *handle = NULL;
	WirePlumberStopResult wireplumber_result;

	controller->state = CONTROLLER_STARTING;
	if (stop_handle(controller, &controller->filler[camera], error,
		sizeof(error)) != 0) {
		(void)fprintf(stderr, "stopping %s filler before capture failed: %s\n",
			camera_name(camera), error);
		schedule_capture_retry(controller, camera);
		controller->state = CONTROLLER_RETRY_WAIT;
		return -1;
	}
	if (controller->wireplumber_stopped) {
		if (restore_wireplumber(controller) != 0) {
			schedule_capture_retry(controller, camera);
			(void)start_filler(controller, camera);
			controller->state = CONTROLLER_RETRY_WAIT;
			return -1;
		}
	}
	wireplumber_result = controller->ops.wireplumber_stop(controller->ops.context,
		error, sizeof(error));
	if (wireplumber_result == WIREPLUMBER_STOP_FAILED) {
		(void)fprintf(stderr, "stopping WirePlumber before %s failed: %s\n",
			camera_name(camera), error);
		schedule_capture_retry(controller, camera);
		(void)start_filler(controller, camera);
		controller->state = CONTROLLER_RETRY_WAIT;
		return -1;
	}
	controller->wireplumber_stopped = wireplumber_result == WIREPLUMBER_STOPPED;
	if (controller->ops.query_format(controller->ops.context, camera, &format,
		error, sizeof(error)) != 0) {
		(void)fprintf(stderr, "format query for %s capture failed: %s\n",
			camera_name(camera), error);
		schedule_capture_retry(controller, camera);
		(void)restore_wireplumber(controller);
		(void)start_filler(controller, camera);
		controller->state = CONTROLLER_RETRY_WAIT;
		return -1;
	}
	if (controller->ops.backend_start(controller->ops.context, camera, false,
		format, &handle, error, sizeof(error)) != 0) {
		(void)fprintf(stderr, "starting %s camera failed: %s\n",
			camera_name(camera), error);
		schedule_capture_retry(controller, camera);
		(void)restore_wireplumber(controller);
		(void)start_filler(controller, camera);
		controller->state = CONTROLLER_RETRY_WAIT;
		return -1;
	}
	controller->capture = handle;
	controller->active = camera;
	controller->pending = CAMERA_NONE;
	controller->state = CONTROLLER_STREAMING;
	clear_capture_retry(controller, camera);
	return 0;
}

static CameraKey requested_camera(const CameraController *controller,
	unsigned mask)
{
	if (controller->active != CAMERA_NONE &&
		(mask & (1U << (unsigned)controller->active)) != 0U)
		return controller->active;
	if ((mask & (1U << CAMERA_REAR)) != 0U)
		return CAMERA_REAR;
	if ((mask & (1U << CAMERA_FRONT)) != 0U)
		return CAMERA_FRONT;
	return CAMERA_NONE;
}

static int transition(CameraController *controller, CameraKey target)
{
	CameraKey old = controller->active;

	controller->state = CONTROLLER_STOPPING;
	controller->pending = target;
	if (old != CAMERA_NONE) {
		if (stop_capture(controller, target == CAMERA_NONE ? old : target) != 0) {
			controller->state = CONTROLLER_RETRY_WAIT;
			return -1;
		}
		controller->active = CAMERA_NONE;
		clear_capture_retry(controller, old);
		(void)start_filler(controller, old);
		if (restore_wireplumber(controller) != 0) {
			schedule_capture_retry(controller, target == CAMERA_NONE ? old : target);
			controller->state = CONTROLLER_RETRY_WAIT;
			return -1;
		}
	}
	if (target == CAMERA_NONE) {
		controller->pending = CAMERA_NONE;
		controller->state = CONTROLLER_IDLE;
		return 0;
	}
	if (start_capture(controller, target) != 0)
		return -1;
	return 0;
}

static void capture_failed(CameraController *controller)
{
	CameraKey failed = controller->active;

	(void)fprintf(stderr, "%s camera producer failed\n", camera_name(failed));
	if (stop_capture(controller, failed) != 0) {
		controller->state = CONTROLLER_RETRY_WAIT;
		return;
	}
	controller->active = CAMERA_NONE;
	(void)start_filler(controller, failed);
	(void)restore_wireplumber(controller);
	schedule_capture_retry(controller, failed);
	controller->state = CONTROLLER_RETRY_WAIT;
}

CameraController *camera_controller_new(const ControllerOps *ops,
	char *error, unsigned error_size)
{
	CameraController *controller;

	if (ops == NULL || ops->now_ms == NULL || ops->consumer_mask == NULL ||
		ops->query_format == NULL || ops->backend_start == NULL ||
		ops->backend_poll == NULL || ops->backend_stop == NULL ||
		ops->backend_free == NULL || ops->wireplumber_stop == NULL ||
		ops->wireplumber_start == NULL) {
		set_error(error, error_size, "controller operations are incomplete");
		return NULL;
	}
	controller = calloc(1U, sizeof(*controller));
	if (controller == NULL) {
		set_error(error, error_size, "out of memory creating controller");
		return NULL;
	}
	controller->ops = *ops;
	controller->active = CAMERA_NONE;
	controller->candidate = CAMERA_NONE;
	controller->pending = CAMERA_NONE;
	controller->state = CONTROLLER_IDLE;
	for (CameraKey camera = CAMERA_FRONT; camera < CAMERA_COUNT; camera++) {
		controller->filler_format[camera] = MEDIA_FORMAT_YUYV;
		(void)start_filler(controller, camera);
	}
	return controller;
}

int camera_controller_tick(CameraController *controller)
{
	unsigned mask;
	CameraKey requested;
	uint64_t current;
	char error[256];

	if (controller == NULL)
		return -1;
	current = now_ms(controller);
	for (CameraKey camera = CAMERA_FRONT; camera < CAMERA_COUNT; camera++) {
		poll_filler(controller, camera);
		(void)start_filler(controller, camera);
	}
	if (controller->capture != NULL && controller->state == CONTROLLER_STREAMING) {
		int result = controller->ops.backend_poll(controller->ops.context,
			controller->capture, error, sizeof(error));

		if (result != 0) {
			capture_failed(controller);
			return result < 0 ? -1 : 0;
		}
	}
	mask = controller->ops.consumer_mask(controller->ops.context);
	requested = requested_camera(controller, mask);
	if (requested == CAMERA_NONE) {
		controller->candidate = CAMERA_NONE;
		if (controller->active == CAMERA_NONE) {
			controller->idle_since = 0U;
			if (controller->state == CONTROLLER_RETRY_WAIT &&
				controller->pending != CAMERA_NONE)
				return 0;
			controller->state = CONTROLLER_IDLE;
			return 0;
		}
		if (controller->idle_since == 0U)
			controller->idle_since = current;
		if (current - controller->idle_since >= CLOSE_GRACE_MS &&
			(controller->state == CONTROLLER_STREAMING ||
			 controller->state == CONTROLLER_RETRY_WAIT))
			return transition(controller, CAMERA_NONE);
		return 0;
	}
	controller->idle_since = 0U;
	if (requested != controller->candidate) {
		controller->candidate = requested;
		controller->candidate_since = current;
	}
	if (requested == controller->active &&
		controller->state == CONTROLLER_STREAMING)
		return 0;
	if (current < controller->capture_retry_after[requested])
		return 0;
	if (requested != controller->active &&
		current - controller->candidate_since < OPEN_DEBOUNCE_MS)
		return 0;
	return transition(controller, requested);
}

int camera_controller_shutdown(CameraController *controller, char *error,
	unsigned error_size)
{
	char local_error[256];
	int result = 0;

	if (controller == NULL)
		return 0;
	if (controller->capture != NULL &&
		stop_handle(controller, &controller->capture, local_error,
			sizeof(local_error)) != 0) {
		set_error(error, error_size, "%s", local_error);
		result = -1;
	}
	for (CameraKey camera = CAMERA_FRONT; camera < CAMERA_COUNT; camera++) {
		if (stop_handle(controller, &controller->filler[camera], local_error,
			sizeof(local_error)) != 0 && result == 0) {
			set_error(error, error_size, "%s filler: %s", camera_name(camera),
				local_error);
			result = -1;
		}
	}
	if (restore_wireplumber(controller) != 0 && result == 0) {
		set_error(error, error_size, "WirePlumber restore failed");
		result = -1;
	}
	controller->active = CAMERA_NONE;
	controller->state = CONTROLLER_IDLE;
	return result;
}

void camera_controller_free(CameraController *controller)
{
	if (controller == NULL)
		return;
	(void)camera_controller_shutdown(controller, NULL, 0U);
	free(controller);
}

ControllerState camera_controller_state(const CameraController *controller)
{
	return controller != NULL ? controller->state : CONTROLLER_IDLE;
}

CameraKey camera_controller_active(const CameraController *controller)
{
	return controller != NULL ? controller->active : CAMERA_NONE;
}

bool camera_controller_owns_wireplumber(const CameraController *controller)
{
	return controller != NULL && controller->wireplumber_stopped;
}
