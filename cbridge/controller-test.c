#include "controller.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
	bool filler;
	CameraKey camera;
} FakeHandle;

typedef struct {
	uint64_t now;
	unsigned consumers;
	unsigned start_calls;
	unsigned camera_start_calls[CAMERA_COUNT];
	unsigned filler_start_calls[CAMERA_COUNT];
	unsigned stop_calls;
	unsigned free_calls;
	unsigned wp_stop_calls;
	unsigned wp_start_calls;
	unsigned query_calls[CAMERA_COUNT];
	MediaFormat last_camera_format[CAMERA_COUNT];
	unsigned capture_poll_calls;
	bool fail_camera_start[CAMERA_COUNT];
	bool fail_query[CAMERA_COUNT];
	bool fail_poll;
	unsigned fail_stop_count;
	bool fail_wp_stop;
	bool fail_wp_start;
	bool wire_active;
	MediaFormat formats[CAMERA_COUNT];
} Fake;

static void fail(const char *message)
{
	fprintf(stderr, "controller-test: %s\n", message);
	exit(EXIT_FAILURE);
}

#define CHECK(condition, message) \
	do { \
		if (!(condition)) \
			fail(message); \
	} while (0)

static uint64_t fake_now(void *context)
{
	return ((Fake *)context)->now;
}

static unsigned fake_consumers(void *context)
{
	return ((Fake *)context)->consumers;
}

static int fake_query(void *context, CameraKey camera, MediaFormat *format,
	char *error, unsigned error_size)
{
	Fake *fake = context;

	fake->query_calls[camera]++;
	if (fake->fail_query[camera]) {
		(void)snprintf(error, error_size, "synthetic format failure");
		return -1;
	}
	*format = fake->formats[camera];
	return 0;
}

static int fake_backend_start(void *context, CameraKey camera, bool filler,
	MediaFormat format, void **handle, char *error, unsigned error_size)
{
	Fake *fake = context;
	FakeHandle *fake_handle;

	fake->start_calls++;
	if (filler)
		fake->filler_start_calls[camera]++;
	else {
		fake->camera_start_calls[camera]++;
		fake->last_camera_format[camera] = format;
		if (fake->fail_camera_start[camera]) {
			(void)snprintf(error, error_size, "synthetic camera start failure");
			return -1;
		}
	}
	fake_handle = malloc(sizeof(*fake_handle));
	CHECK(fake_handle != NULL, "fake handle allocation failed");
	fake_handle->filler = filler;
	fake_handle->camera = camera;
	*handle = fake_handle;
	return 0;
}

static int fake_backend_poll(void *context, void *handle, char *error,
	unsigned error_size)
{
	Fake *fake = context;
	FakeHandle *fake_handle = handle;

	if (!fake_handle->filler)
		fake->capture_poll_calls++;
	if (!fake_handle->filler && fake->fail_poll) {
		(void)snprintf(error, error_size, "synthetic stream failure");
		fake->fail_poll = false;
		return -1;
	}
	return 0;
}

static int fake_backend_stop(void *context, void *handle, char *error,
	unsigned error_size)
{
	Fake *fake = context;

	fake->stop_calls++;
	if (fake->fail_stop_count != 0U) {
		fake->fail_stop_count--;
		(void)snprintf(error, error_size, "synthetic stop failure");
		return -1;
	}
	(void)handle;
	return 0;
}

static void fake_backend_free(void *context, void *handle)
{
	Fake *fake = context;

	fake->free_calls++;
	free(handle);
}

static WirePlumberStopResult fake_wp_stop(void *context, char *error,
	unsigned error_size)
{
	Fake *fake = context;

	fake->wp_stop_calls++;
	if (!fake->wire_active)
		return WIREPLUMBER_ALREADY_INACTIVE;
	if (fake->fail_wp_stop) {
		(void)snprintf(error, error_size, "synthetic WirePlumber stop failure");
		return WIREPLUMBER_STOP_FAILED;
	}
	fake->wire_active = false;
	return WIREPLUMBER_STOPPED;
}

static int fake_wp_start(void *context, char *error, unsigned error_size)
{
	Fake *fake = context;

	fake->wp_start_calls++;
	if (fake->fail_wp_start) {
		(void)snprintf(error, error_size, "synthetic WirePlumber start failure");
		return -1;
	}
	fake->wire_active = true;
	return 0;
}

static CameraController *new_controller(Fake *fake)
{
	ControllerOps ops = {
		.context = fake,
		.now_ms = fake_now,
		.consumer_mask = fake_consumers,
		.query_format = fake_query,
		.backend_start = fake_backend_start,
		.backend_poll = fake_backend_poll,
		.backend_stop = fake_backend_stop,
		.backend_free = fake_backend_free,
		.wireplumber_stop = fake_wp_stop,
		.wireplumber_start = fake_wp_start,
	};
	char error[256];
	CameraController *controller = camera_controller_new(&ops, error,
		sizeof(error));

	if (controller == NULL)
		fail(error);
	return controller;
}

static Fake new_fake(void)
{
	Fake fake = {
		.now = 1000U,
		.wire_active = true,
		.formats = { MEDIA_FORMAT_YUYV, MEDIA_FORMAT_MJPEG },
	};

	return fake;
}

static void open_camera(CameraController *controller, Fake *fake,
	CameraKey camera)
{
	fake->consumers = 1U << (unsigned)camera;
	CHECK(camera_controller_tick(controller) == 0,
		"initial consumer tick failed");
	fake->now += 450U;
	(void)camera_controller_tick(controller);
	(void)camera;
}

static void test_priority_switch_and_close(void)
{
	Fake fake = new_fake();
	CameraController *controller = new_controller(&fake);
	unsigned rear_starts;

	CHECK(fake.filler_start_calls[CAMERA_FRONT] == 1U &&
		fake.filler_start_calls[CAMERA_REAR] == 1U,
		"controller did not start both fillers");
	fake.consumers = (1U << CAMERA_FRONT) | (1U << CAMERA_REAR);
	CHECK(camera_controller_tick(controller) == 0,
		"priority candidate tick failed");
	fake.now += 449U;
	CHECK(camera_controller_tick(controller) == 0,
		"priority debounce tick failed");
	CHECK(camera_controller_active(controller) == CAMERA_NONE,
		"camera started before debounce completed");
	fake.now++;
	CHECK(camera_controller_tick(controller) == 0,
		"priority transition failed");
	CHECK(camera_controller_active(controller) == CAMERA_REAR,
		"rear was not preferred when both consumers were present");
	CHECK(fake.last_camera_format[CAMERA_REAR] == MEDIA_FORMAT_MJPEG,
		"rear capture did not receive its negotiated MJPEG format");
	rear_starts = fake.camera_start_calls[CAMERA_REAR];

	fake.consumers = (1U << CAMERA_FRONT) | (1U << CAMERA_REAR);
	fake.now += 1000U;
	CHECK(camera_controller_tick(controller) == 0,
		"held-camera tick failed");
	CHECK(fake.camera_start_calls[CAMERA_REAR] == rear_starts,
		"held rear camera was restarted");

	fake.consumers = 1U << CAMERA_FRONT;
	fake.now += 1U;
	CHECK(camera_controller_tick(controller) == 0,
		"switch candidate tick failed");
	fake.now += 449U;
	CHECK(camera_controller_tick(controller) == 0,
		"switch debounce tick failed");
	CHECK(camera_controller_active(controller) == CAMERA_REAR,
		"camera switched before debounce completed");
	fake.now++;
	CHECK(camera_controller_tick(controller) == 0,
		"front switch failed");
	CHECK(camera_controller_active(controller) == CAMERA_FRONT,
		"front did not become active after rear release");
	CHECK(fake.last_camera_format[CAMERA_FRONT] == MEDIA_FORMAT_YUYV,
		"front capture did not receive its negotiated YUYV format");

	fake.consumers = 0U;
	fake.now++;
	CHECK(camera_controller_tick(controller) == 0,
		"close-grace start tick failed");
	fake.now += 1499U;
	CHECK(camera_controller_tick(controller) == 0,
		"close-grace wait tick failed");
	CHECK(camera_controller_active(controller) == CAMERA_FRONT,
		"camera stopped before close grace elapsed");
	fake.now++;
	CHECK(camera_controller_tick(controller) == 0,
		"close transition failed");
	CHECK(camera_controller_active(controller) == CAMERA_NONE &&
		camera_controller_state(controller) == CONTROLLER_IDLE,
		"controller did not return to idle after close grace");
	CHECK(fake.wire_active && !camera_controller_owns_wireplumber(controller),
		"WirePlumber ownership was not restored");

	camera_controller_free(controller);
	CHECK(fake.free_calls == fake.stop_calls,
		"a stopped backend handle was not freed");
}

static void test_start_failure_retry_and_capture_failure(void)
{
	Fake fake = new_fake();
	CameraController *controller = new_controller(&fake);

	fake.fail_camera_start[CAMERA_FRONT] = true;
	open_camera(controller, &fake, CAMERA_FRONT);
	CHECK(camera_controller_active(controller) == CAMERA_NONE &&
		camera_controller_state(controller) == CONTROLLER_RETRY_WAIT,
		"camera start failure did not enter retry state");
	CHECK(fake.wire_active && !camera_controller_owns_wireplumber(controller),
		"failed start left WirePlumber stopped");
	fake.fail_camera_start[CAMERA_FRONT] = false;
	fake.now += 1999U;
	CHECK(camera_controller_tick(controller) == 0,
		"retry fired before its deadline");
	CHECK(camera_controller_active(controller) == CAMERA_NONE,
		"camera started before retry deadline");
	fake.now++;
	CHECK(camera_controller_tick(controller) == 0,
		"camera retry failed");
	CHECK(camera_controller_active(controller) == CAMERA_FRONT,
		"camera did not recover on retry");

	fake.fail_poll = true;
	CHECK(camera_controller_tick(controller) < 0,
		"capture failure was not reported");
	CHECK(camera_controller_active(controller) == CAMERA_NONE &&
		camera_controller_state(controller) == CONTROLLER_RETRY_WAIT,
		"capture failure did not enter retry state");
	CHECK(fake.wire_active,
		"capture failure did not restore WirePlumber");
	camera_controller_free(controller);
}

static void test_format_wireplumber_and_stop_failures(void)
{
	Fake fake = new_fake();
	CameraController *controller = new_controller(&fake);

	fake.fail_query[CAMERA_FRONT] = true;
	open_camera(controller, &fake, CAMERA_FRONT);
	CHECK(camera_controller_active(controller) == CAMERA_NONE &&
		camera_controller_state(controller) == CONTROLLER_RETRY_WAIT,
		"format failure did not enter retry state");
	CHECK(fake.wire_active,
		"format failure did not restore WirePlumber");
	camera_controller_free(controller);

	fake = new_fake();
	controller = new_controller(&fake);
	fake.fail_wp_stop = true;
	open_camera(controller, &fake, CAMERA_FRONT);
	CHECK(camera_controller_active(controller) == CAMERA_NONE &&
		camera_controller_state(controller) == CONTROLLER_RETRY_WAIT,
		"WirePlumber stop failure did not enter retry state");
	fake.fail_wp_stop = false;
	fake.now += 2000U;
	CHECK(camera_controller_tick(controller) == 0,
		"WirePlumber retry failed");
	CHECK(camera_controller_active(controller) == CAMERA_FRONT,
		"WirePlumber retry did not start camera");

	fake.consumers = 1U << CAMERA_REAR;
	fake.now += 450U;
	CHECK(camera_controller_tick(controller) == 0,
		"stop-failure candidate tick failed");
	fake.now += 450U;
	fake.fail_stop_count = 1U;
	CHECK(camera_controller_tick(controller) < 0,
		"backend stop failure was not reported");
	CHECK(camera_controller_active(controller) == CAMERA_FRONT &&
		camera_controller_state(controller) == CONTROLLER_RETRY_WAIT,
		"backend stop failure allowed a new camera to start");
	fake.now += 2000U;
	CHECK(camera_controller_tick(controller) == 0,
		"backend stop retry failed");
	CHECK(camera_controller_active(controller) == CAMERA_REAR,
		"backend stop retry did not switch cameras");

	CHECK(camera_controller_shutdown(controller, NULL, 0U) == 0,
		"controller shutdown failed");
	CHECK(camera_controller_state(controller) == CONTROLLER_IDLE &&
		camera_controller_active(controller) == CAMERA_NONE,
		"shutdown did not leave idle state");
	camera_controller_free(controller);
}

int main(void)
{
	test_priority_switch_and_close();
	test_start_failure_retry_and_capture_failure();
	test_format_wireplumber_and_stop_failures();
	puts("controller-test: all deterministic state-machine scenarios passed");
	return EXIT_SUCCESS;
}
