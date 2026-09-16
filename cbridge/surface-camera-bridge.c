#include "controller.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <gst/gst.h>

#define POLL_INTERVAL_MS 200U
#define ACTIVE_CONSUMER_SCAN_MS 200U
#define IDLE_CONSUMER_SCAN_MS 1000U
#define SYSTEMCTL_TIMEOUT_MS 5000U
#define CAMERA_FRONT_DEVICE "/dev/video60"
#define CAMERA_REAR_DEVICE "/dev/video61"

typedef struct {
	const char *device;
	const char *camera_id;
	dev_t device_number;
	bool present;
} CameraEndpoint;

typedef struct {
	CameraEndpoint endpoints[CAMERA_COUNT];
	uid_t uid;
	CameraController *controller;
	uint64_t next_consumer_scan_ms;
	unsigned cached_consumer_mask;
	unsigned consumer_scans;
	bool consumer_scan_valid;
} LiveContext;

typedef struct {
	MediaBackend *backend;
} LiveBackendHandle;

static volatile sig_atomic_t stop_requested;

static void request_stop(int signal_number)
{
	(void)signal_number;
	stop_requested = 1;
}

static uint64_t monotonic_ms(void)
{
	struct timespec time;

	if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
		return 0U;
	return (uint64_t)time.tv_sec * 1000U + (uint64_t)time.tv_nsec / 1000000U;
}

static const CameraEndpoint *endpoint_for(const LiveContext *context,
	CameraKey camera)
{
	if (camera < CAMERA_FRONT || camera >= CAMERA_COUNT)
		return NULL;
	return &context->endpoints[camera];
}

static bool decimal_pid(const char *text, pid_t *pid)
{
	unsigned long value = 0U;

	if (text == NULL || *text == '\0')
		return false;
	for (const unsigned char *cursor = (const unsigned char *)text;
		*cursor != '\0'; cursor++) {
		if (*cursor < '0' || *cursor > '9')
			return false;
		value = value * 10U + (unsigned long)(*cursor - '0');
		if (value > (unsigned long)INT_MAX)
			return false;
	}
	*pid = (pid_t)value;
	return true;
}

static uid_t process_uid(const char *pid_text, bool *available)
{
	char path[64];
	char line[256];
	FILE *status;

	*available = false;
	if (snprintf(path, sizeof(path), "/proc/%s/status", pid_text) < 0)
		return (uid_t)-1;
	status = fopen(path, "r");
	if (status == NULL)
		return (uid_t)-1;
	while (fgets(line, sizeof(line), status) != NULL) {
		unsigned long uid_value;

		if (sscanf(line, "Uid:\t%lu", &uid_value) == 1) {
			(void)fclose(status);
			*available = true;
			return (uid_t)uid_value;
		}
	}
	(void)fclose(status);
	return (uid_t)-1;
}

static bool same_device(const char *target, const CameraEndpoint *endpoint)
{
	struct stat status;

	if (stat(target, &status) != 0)
		return false;
	return S_ISCHR(status.st_mode) && status.st_rdev == endpoint->device_number;
}

static bool process_uses_endpoint(const char *pid_text,
	const CameraEndpoint *endpoint)
{
	char path[96];
	DIR *directory;
	struct dirent *entry;
	bool found = false;

	if (snprintf(path, sizeof(path), "/proc/%s/fd", pid_text) < 0)
		return false;
	directory = opendir(path);
	if (directory == NULL)
		return false;
	while ((entry = readdir(directory)) != NULL) {
		char fd_path[160];
		char target[PATH_MAX];
		ssize_t length;

		if (entry->d_name[0] == '.')
			continue;
		if (snprintf(fd_path, sizeof(fd_path), "/proc/%s/fd/%s", pid_text,
			entry->d_name) < 0)
			continue;
		length = readlink(fd_path, target, sizeof(target) - 1U);
		if (length < 0)
			continue;
		target[length] = '\0';
		if (length >= 10 && strcmp(target + length - 10, " (deleted)") == 0)
			target[length - 10] = '\0';
		if (same_device(target, endpoint)) {
			found = true;
			break;
		}
	}
	(void)closedir(directory);
	return found;
}

static unsigned scan_consumer_mask(const LiveContext *context)
{
	DIR *proc;
	struct dirent *entry;
	unsigned mask = 0U;
	pid_t own_pid = getpid();

	proc = opendir("/proc");
	if (proc == NULL)
		return 0U;
	while ((entry = readdir(proc)) != NULL) {
		pid_t pid;
		bool available;
		uid_t uid;

		if (!decimal_pid(entry->d_name, &pid) || pid == own_pid)
			continue;
		uid = process_uid(entry->d_name, &available);
		if (!available || uid != context->uid)
			continue;
		for (CameraKey camera = CAMERA_FRONT; camera < CAMERA_COUNT; camera++) {
			if (context->endpoints[camera].present &&
				process_uses_endpoint(entry->d_name, &context->endpoints[camera]))
				mask |= 1U << (unsigned)camera;
		}
	}
	(void)closedir(proc);
	return mask;
}

static unsigned consumer_mask(void *opaque)
{
	LiveContext *context = opaque;
	uint64_t current = monotonic_ms();
	unsigned interval = ACTIVE_CONSUMER_SCAN_MS;

	if (context->controller != NULL &&
		camera_controller_state(context->controller) == CONTROLLER_IDLE &&
		context->cached_consumer_mask == 0U)
		interval = IDLE_CONSUMER_SCAN_MS;
	if (context->consumer_scan_valid && current < context->next_consumer_scan_ms)
		return context->cached_consumer_mask;
	context->cached_consumer_mask = scan_consumer_mask(context);
	context->consumer_scan_valid = true;
	context->consumer_scans++;
	context->next_consumer_scan_ms = current + interval;
	return context->cached_consumer_mask;
}

static uint64_t live_now(void *opaque)
{
	(void)opaque;
	return monotonic_ms();
}

static int query_format(void *opaque, CameraKey camera, MediaFormat *format,
	char *error, unsigned error_size)
{
	const LiveContext *context = opaque;
	const CameraEndpoint *endpoint = endpoint_for(context, camera);

	if (endpoint == NULL || !endpoint->present) {
		if (error != NULL && error_size != 0U)
			(void)snprintf(error, error_size, "camera endpoint is unavailable");
		return -1;
	}
	return media_backend_query_format(endpoint->device, format, error,
		error_size);
}

static int backend_start(void *opaque, CameraKey camera, bool filler,
	MediaFormat format, void **handle, char *error, unsigned error_size)
{
	const LiveContext *context = opaque;
	const CameraEndpoint *endpoint = endpoint_for(context, camera);
	LiveBackendHandle *live_handle;
	MediaBackendConfig config;

	if (handle == NULL || endpoint == NULL || !endpoint->present) {
		if (error != NULL && error_size != 0U)
			(void)snprintf(error, error_size, "camera endpoint is unavailable");
		return -1;
	}
	live_handle = calloc(1U, sizeof(*live_handle));
	if (live_handle == NULL) {
		if (error != NULL && error_size != 0U)
			(void)snprintf(error, error_size, "out of memory for media backend");
		return -1;
	}
	live_handle->backend = media_backend_new();
	if (live_handle->backend == NULL) {
		free(live_handle);
		if (error != NULL && error_size != 0U)
			(void)snprintf(error, error_size, "out of memory for media backend");
		return -1;
	}
	config.camera_id = endpoint->camera_id;
	config.device = endpoint->device;
	config.format = format;
	config.kind = filler ? MEDIA_PIPELINE_FILLER : MEDIA_PIPELINE_CAMERA;
	if (media_backend_start(live_handle->backend, &config, error, error_size) != 0) {
		media_backend_free(live_handle->backend);
		free(live_handle);
		return -1;
	}
	*handle = live_handle;
	return 0;
}

static int backend_poll(void *opaque, void *handle, char *error,
	unsigned error_size)
{
	LiveBackendHandle *live_handle = handle;

	(void)opaque;
	if (live_handle == NULL || live_handle->backend == NULL) {
		if (error != NULL && error_size != 0U)
			(void)snprintf(error, error_size, "media backend handle is invalid");
		return -1;
	}
	return media_backend_poll(live_handle->backend, 0U, error, error_size);
}

static int backend_stop(void *opaque, void *handle, char *error,
	unsigned error_size)
{
	LiveBackendHandle *live_handle = handle;

	(void)opaque;
	if (live_handle == NULL || live_handle->backend == NULL)
		return 0;
	return media_backend_stop(live_handle->backend, error, error_size);
}

static void backend_free(void *opaque, void *handle)
{
	LiveBackendHandle *live_handle = handle;

	(void)opaque;
	if (live_handle == NULL)
		return;
	media_backend_free(live_handle->backend);
	free(live_handle);
}

static int run_systemctl(const char *verb, bool quiet, char *error,
	unsigned error_size)
{
	pid_t child;
	int status;
	uint64_t deadline;

	child = fork();
	if (child < 0) {
		if (error != NULL && error_size != 0U)
			(void)snprintf(error, error_size, "fork systemctl: %s", strerror(errno));
		return -1;
	}
	if (child == 0) {
		if (quiet)
			execl("/usr/bin/systemctl", "systemctl", "--user", verb,
				"--quiet", "wireplumber.service", (char *)NULL);
		else
			execl("/usr/bin/systemctl", "systemctl", "--user", verb,
				"wireplumber.service", (char *)NULL);
		_exit(127);
	}
	deadline = monotonic_ms() + SYSTEMCTL_TIMEOUT_MS;
	for (;;) {
		pid_t result = waitpid(child, &status, WNOHANG);

		if (result == child) {
			if (WIFEXITED(status))
				return WEXITSTATUS(status);
			if (error != NULL && error_size != 0U)
				(void)snprintf(error, error_size, "systemctl terminated by signal");
			return -1;
		}
		if (result < 0 && errno != EINTR) {
			if (error != NULL && error_size != 0U)
				(void)snprintf(error, error_size, "wait systemctl: %s",
					strerror(errno));
			return -1;
		}
		if (monotonic_ms() >= deadline) {
			(void)kill(child, SIGTERM);
			while (waitpid(child, &status, 0) < 0 && errno == EINTR)
				;
			if (error != NULL && error_size != 0U)
				(void)snprintf(error, error_size,
					"systemctl %s timed out after %u ms", verb,
					SYSTEMCTL_TIMEOUT_MS);
			return -1;
		}
		{
			struct timespec pause = { .tv_sec = 0, .tv_nsec = 20000000L };
			(void)nanosleep(&pause, NULL);
		}
	}
}

static WirePlumberStopResult wireplumber_stop(void *opaque, char *error,
	unsigned error_size)
{
	int status;

	(void)opaque;
	status = run_systemctl("is-active", true, error, error_size);
	if (status == 3)
		return WIREPLUMBER_ALREADY_INACTIVE;
	if (status != 0) {
		if (status >= 0 && error != NULL && error_size != 0U)
			(void)snprintf(error, error_size,
				"WirePlumber is-active returned status %d", status);
		return WIREPLUMBER_STOP_FAILED;
	}
	status = run_systemctl("stop", false, error, error_size);
	if (status != 0) {
		if (status >= 0 && error != NULL && error_size != 0U)
			(void)snprintf(error, error_size,
				"WirePlumber stop returned status %d", status);
		return WIREPLUMBER_STOP_FAILED;
	}
	return WIREPLUMBER_STOPPED;
}

static int wireplumber_start(void *opaque, char *error, unsigned error_size)
{
	int status;

	(void)opaque;
	status = run_systemctl("start", false, error, error_size);
	if (status == 0)
		return 0;
	if (status >= 0 && error != NULL && error_size != 0U)
		(void)snprintf(error, error_size,
			"WirePlumber start returned status %d", status);
	return -1;
}

static int initialize_context(LiveContext *context)
{
	const char *devices[CAMERA_COUNT] = {
		CAMERA_FRONT_DEVICE,
		CAMERA_REAR_DEVICE,
	};
	const char *camera_ids[CAMERA_COUNT] = {
		"\\\\_SB_.PCI0.I2C2.CAMF",
		"\\\\_SB_.PCI0.I2C3.CAMR",
	};

	memset(context, 0, sizeof(*context));
	context->uid = getuid();
	for (CameraKey camera = CAMERA_FRONT; camera < CAMERA_COUNT; camera++) {
		struct stat status;

		context->endpoints[camera].device = devices[camera];
		context->endpoints[camera].camera_id = camera_ids[camera];
		if (stat(devices[camera], &status) != 0 || !S_ISCHR(status.st_mode)) {
			fprintf(stderr, "missing character device %s\n", devices[camera]);
			return -1;
		}
		context->endpoints[camera].device_number = status.st_rdev;
		context->endpoints[camera].present = true;
	}
	return 0;
}

int main(void)
{
	LiveContext context;
	ControllerOps ops;
	CameraController *controller;
	char error[512];
	int result = 0;

	if (initialize_context(&context) != 0)
		return EXIT_FAILURE;
	if (signal(SIGINT, request_stop) == SIG_ERR ||
		signal(SIGTERM, request_stop) == SIG_ERR) {
		perror("signal");
		return EXIT_FAILURE;
	}
	gst_init(NULL, NULL);
	memset(&ops, 0, sizeof(ops));
	ops.context = &context;
	ops.now_ms = live_now;
	ops.consumer_mask = consumer_mask;
	ops.query_format = query_format;
	ops.backend_start = backend_start;
	ops.backend_poll = backend_poll;
	ops.backend_stop = backend_stop;
	ops.backend_free = backend_free;
	ops.wireplumber_stop = wireplumber_stop;
	ops.wireplumber_start = wireplumber_start;
	controller = camera_controller_new(&ops, error, sizeof(error));
	if (controller == NULL) {
		fprintf(stderr, "could not create camera controller: %s\n", error);
		return EXIT_FAILURE;
	}
	context.controller = controller;
	fprintf(stderr, "watching front and rear camera consumers\n");
	while (!stop_requested) {
		if (camera_controller_tick(controller) < 0)
			fprintf(stderr, "camera controller recovered from a stream error\n");
		(void)poll(NULL, 0, (int)POLL_INTERVAL_MS);
	}
	fprintf(stderr, "shutting down camera controller\n");
	fprintf(stderr, "consumer scans performed: %u\n", context.consumer_scans);
	if (camera_controller_shutdown(controller, error, sizeof(error)) != 0) {
		fprintf(stderr, "camera controller shutdown failed: %s\n", error);
		result = EXIT_FAILURE;
	}
	camera_controller_free(controller);
	return result;
}
