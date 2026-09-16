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
#define WORKER_START_TIMEOUT_MS 10000U
#define WORKER_STOP_TIMEOUT_MS 5000U
#define WORKER_REAP_POLL_MS 50U
#define ACTIVE_CONSUMER_SCAN_MS 200U
#define IDLE_CONSUMER_SCAN_MS 1000U
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
	pid_t worker_pid;
	int status_fd;
} LiveBackendHandle;

static volatile sig_atomic_t stop_requested;

static void set_error(char *error, unsigned error_size, const char *message)
{
	if (error != NULL && error_size != 0U)
		(void)snprintf(error, error_size, "%s", message);
}

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

static int write_worker_status(int fd, char status)
{
	ssize_t written;

	do {
		written = write(fd, &status, sizeof(status));
	} while (written < 0 && errno == EINTR);
	return written == (ssize_t)sizeof(status) ? 0 : -1;
}

static void worker_process(const MediaBackendConfig *config, int status_fd)
{
	MediaBackend *backend;
	char error[512];
	int result;

	if (setpgid(0, 0) != 0) {
		(void)write_worker_status(status_fd, 'F');
		(void)close(status_fd);
		_exit(EXIT_FAILURE);
	}
	/* GStreamer and the media backend are intentionally initialized only here. */
	gst_init(NULL, NULL);
	backend = media_backend_new();
	if (backend == NULL) {
		(void)fprintf(stderr, "worker could not allocate media backend\n");
		(void)write_worker_status(status_fd, 'F');
		(void)close(status_fd);
		_exit(EXIT_FAILURE);
	}
	result = media_backend_start(backend, config, error, sizeof(error));
	if (result != 0) {
		(void)fprintf(stderr, "worker media backend start failed: %s\n", error);
		media_backend_free(backend);
		(void)write_worker_status(status_fd, 'F');
		(void)close(status_fd);
		_exit(EXIT_FAILURE);
	}
	if (write_worker_status(status_fd, 'R') != 0) {
		media_backend_stop(backend, error, sizeof(error));
		media_backend_free(backend);
		(void)close(status_fd);
		_exit(EXIT_FAILURE);
	}
	(void)close(status_fd);
	while (!stop_requested) {
		result = media_backend_poll(backend, WORKER_REAP_POLL_MS, error,
			sizeof(error));
		if (result != 0) {
			(void)fprintf(stderr, "worker media backend ended: %s\n",
				result < 0 ? error : "end-of-stream");
			break;
		}
	}
	if (media_backend_stop(backend, error, sizeof(error)) != 0)
		(void)fprintf(stderr, "worker media backend stop failed: %s\n", error);
	media_backend_free(backend);
	_exit(result < 0 ? EXIT_FAILURE : EXIT_SUCCESS);
}

static int reap_worker(pid_t worker_pid, unsigned timeout_ms, int *status)
{
	uint64_t deadline = monotonic_ms() + timeout_ms;
	pid_t result;

	for (;;) {
		result = waitpid(worker_pid, status, WNOHANG);
		if (result == worker_pid)
			return 0;
		if (result < 0) {
			if (errno == EINTR)
				continue;
			if (errno == ECHILD)
				return 0;
			return -1;
		}
		if (monotonic_ms() >= deadline)
			return 1;
		(void)poll(NULL, 0, (int)WORKER_REAP_POLL_MS);
	}
}

static int terminate_worker(LiveBackendHandle *live_handle, char *error,
	unsigned error_size)
{
	bool failed = false;
	int status = 0;
	int result;
	pid_t worker_pid;

	if (live_handle == NULL)
		return 0;
	worker_pid = live_handle->worker_pid;
	if (worker_pid <= 0)
		return 0;
	if (kill(-worker_pid, SIGTERM) != 0 && errno != ESRCH) {
		if (error != NULL && error_size != 0U)
			(void)snprintf(error, error_size,
				"could not stop media worker: %s", strerror(errno));
		failed = true;
	}
	result = reap_worker(worker_pid, WORKER_STOP_TIMEOUT_MS, &status);
	if (result == 1) {
		if (kill(-worker_pid, SIGKILL) != 0 && errno != ESRCH) {
			if (error != NULL && error_size != 0U)
				(void)snprintf(error, error_size,
					"could not kill media worker: %s", strerror(errno));
			failed = true;
		}
		result = reap_worker(worker_pid, WORKER_STOP_TIMEOUT_MS, &status);
		if (result == 1) {
			/* SIGKILL cannot be ignored; finish the reap before returning. */
			do {
				result = waitpid(worker_pid, &status, 0);
			} while (result < 0 && errno == EINTR);
		}
	}
	if (result < 0) {
		if (errno == ECHILD) {
			live_handle->worker_pid = 0;
			return failed ? -1 : 0;
		}
		if (error != NULL && error_size != 0U)
			(void)snprintf(error, error_size,
				"could not reap media worker: %s", strerror(errno));
		return -1;
	}
	live_handle->worker_pid = 0;
	return failed ? -1 : 0;
}

static int wait_for_worker_ready(LiveBackendHandle *live_handle, char *error,
	unsigned error_size)
{
	struct pollfd descriptor;
	uint64_t deadline = monotonic_ms() + WORKER_START_TIMEOUT_MS;
	char status;
	ssize_t received;
	int timeout;
	int wait_status;
	pid_t result;

	descriptor.fd = live_handle->status_fd;
	descriptor.events = POLLIN | POLLHUP;
	descriptor.revents = 0;
	for (;;) {
		uint64_t remaining = deadline - monotonic_ms();

		if (monotonic_ms() >= deadline)
			break;
		timeout = remaining > 100U ? 100 : (int)remaining;
		result = poll(&descriptor, 1, timeout);
		if (result < 0) {
			if (errno == EINTR)
				continue;
			set_error(error, error_size, "could not wait for media worker");
			return -1;
		}
		if (result > 0 && (descriptor.revents & (POLLIN | POLLHUP)) != 0) {
			do {
				received = read(live_handle->status_fd, &status,
					sizeof(status));
			} while (received < 0 && errno == EINTR);
			if (received == (ssize_t)sizeof(status) && status == 'R')
				return 0;
			set_error(error, error_size, "media worker failed during startup");
			return -1;
		}
		result = waitpid(live_handle->worker_pid, &wait_status, WNOHANG);
		if (result == live_handle->worker_pid) {
			set_error(error, error_size, "media worker exited during startup");
			return -1;
		}
		if (result < 0 && errno != EINTR) {
			set_error(error, error_size, "could not monitor media worker");
			return -1;
		}
	}
	set_error(error, error_size, "media worker startup timed out");
	return -1;
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
	int status_pipe[2];
	pid_t worker_pid;

	if (handle == NULL || endpoint == NULL || !endpoint->present) {
		set_error(error, error_size, "camera endpoint is unavailable");
		return -1;
	}
	live_handle = calloc(1U, sizeof(*live_handle));
	if (live_handle == NULL) {
		set_error(error, error_size, "out of memory for media worker");
		return -1;
	}
	live_handle->worker_pid = 0;
	live_handle->status_fd = -1;
	config.camera_id = endpoint->camera_id;
	config.device = endpoint->device;
	config.format = format;
	config.kind = filler ? MEDIA_PIPELINE_FILLER : MEDIA_PIPELINE_CAMERA;
	if (pipe2(status_pipe, O_CLOEXEC) != 0) {
		set_error(error, error_size, "could not create media worker status pipe");
		free(live_handle);
		return -1;
	}
	worker_pid = fork();
	if (worker_pid < 0) {
		(void)close(status_pipe[0]);
		(void)close(status_pipe[1]);
		set_error(error, error_size, "could not fork media worker");
		free(live_handle);
		return -1;
	}
	if (worker_pid == 0) {
		(void)close(status_pipe[0]);
		worker_process(&config, status_pipe[1]);
		_exit(EXIT_FAILURE);
	}
	(void)close(status_pipe[1]);
	live_handle->worker_pid = worker_pid;
	live_handle->status_fd = status_pipe[0];
	if (setpgid(worker_pid, worker_pid) != 0 && errno != EACCES &&
		errno != ESRCH) {
		set_error(error, error_size, "could not create media worker process group");
		(void)terminate_worker(live_handle, error, error_size);
		(void)close(live_handle->status_fd);
		free(live_handle);
		return -1;
	}
	if (wait_for_worker_ready(live_handle, error, error_size) != 0) {
		(void)terminate_worker(live_handle, error, error_size);
		(void)close(live_handle->status_fd);
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
	int status;

	(void)opaque;
	if (live_handle == NULL || live_handle->worker_pid <= 0) {
		set_error(error, error_size, "media worker handle is invalid");
		return -1;
	}
	if (waitpid(live_handle->worker_pid, &status, WNOHANG) == 0)
		return 0;
	if (errno == EINTR)
		return 0;
	set_error(error, error_size, "media worker exited unexpectedly");
	return -1;
}

static int backend_stop(void *opaque, void *handle, char *error,
	unsigned error_size)
{
	LiveBackendHandle *live_handle = handle;
	int result;

	(void)opaque;
	if (live_handle == NULL)
		return 0;
	result = terminate_worker(live_handle, error, error_size);
	if (live_handle->status_fd >= 0) {
		(void)close(live_handle->status_fd);
		live_handle->status_fd = -1;
	}
	return result;
}

static void backend_free(void *opaque, void *handle)
{
	LiveBackendHandle *live_handle = handle;

	(void)opaque;
	if (live_handle == NULL)
		return;
	if (live_handle->worker_pid > 0) {
		char error[256];

		(void)terminate_worker(live_handle, error, sizeof(error));
	}
	if (live_handle->status_fd >= 0)
		(void)close(live_handle->status_fd);
	free(live_handle);
}

static WirePlumberStopResult wireplumber_stop(void *opaque, char *error,
	unsigned error_size)
{
	(void)opaque;
	(void)error;
	(void)error_size;
	/*
	 * The installed WirePlumber profile disables the libcamera monitor but
	 * keeps WirePlumber running for PipeWire's V4L2 loopback targets. Stopping
	 * it here would destroy the target that the application is opening.
	 */
	return WIREPLUMBER_ALREADY_INACTIVE;
}

static int wireplumber_start(void *opaque, char *error, unsigned error_size)
{
	(void)opaque;
	(void)error;
	(void)error_size;
	/* WirePlumber remains active for the lifetime of the bridge service. */
	return 0;
}

static int initialize_context(LiveContext *context)
{
	const char *devices[CAMERA_COUNT] = {
		CAMERA_FRONT_DEVICE,
		CAMERA_REAR_DEVICE,
	};
	const char *camera_ids[CAMERA_COUNT] = {
		"\\_SB_.PCI0.I2C2.CAMF",
		"\\_SB_.PCI0.I2C3.CAMR",
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
