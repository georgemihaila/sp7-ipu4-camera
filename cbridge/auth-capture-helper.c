#include "ir-v4l2.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define AUTH_LOCK_PATH "/run/lock/sp7-camera-auth-capture.lock"
#define AUTH_CAPTURE_BUDGET_NS UINT64_C(3000000000)
#define AUTH_DEFAULT_FRAMES 5U
#define AUTH_MAX_FRAMES 12U
#define AUTH_FRAME_MAGIC "SP7IRF01"
#define AUTH_FRAME_HEADER_BYTES 44U
#define AUTH_FRAME_PAYLOAD_BYTES (IR_CAPTURE_WIDTH * IR_CAPTURE_HEIGHT)
#define AUTH_MAX_FRAME_AGE_NS UINT64_C(2000000000)
#define AUTH_EXIT_FAILURE 1
#define AUTH_EXIT_BUSY 2
#define AUTH_EXIT_TIMEOUT 124
#define AUTH_CHILD_POLL_MS 50
#define AUTH_FRAME_POLL_MS 100

static volatile sig_atomic_t stop_requested;
#ifdef AUTH_CAPTURE_SUPERVISOR_TEST
static pid_t auth_test_worker_pid = -1;
static int auth_test_late_status_fd = -1;
#endif

static uint64_t monotonic_timestamp_ns(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0U;
	return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
		(uint64_t)now.tv_nsec;
}

static void request_stop(int signal_number)
{
	(void)signal_number;
	stop_requested = 1;
}

static void install_worker_signals(void)
{
	struct sigaction action = { 0 };

	action.sa_handler = request_stop;
	(void)sigemptyset(&action.sa_mask);
	/* Do not use SA_RESTART: a blocked pipe write must notice shutdown. */
	(void)sigaction(SIGTERM, &action, NULL);
	(void)sigaction(SIGINT, &action, NULL);
	(void)signal(SIGPIPE, SIG_IGN);
}

static void report_error(const char *message)
{
	fprintf(stderr, "auth_capture result=error detail=%s\n", message);
}

#ifndef AUTH_CAPTURE_SUPERVISOR_TEST
static int write_all(int fd, const void *data, size_t length)
{
	const uint8_t *bytes = data;
	size_t written = 0U;

	while (written < length) {
		ssize_t count;

		if (stop_requested)
			return -1;
		count = write(fd, bytes + written, length - written);
		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			return -1;
		written += (size_t)count;
	}
	return 0;
}

static void put_u32_le(uint8_t *destination, uint32_t value)
{
	destination[0] = (uint8_t)(value & UINT32_C(0xff));
	destination[1] = (uint8_t)((value >> 8U) & UINT32_C(0xff));
	destination[2] = (uint8_t)((value >> 16U) & UINT32_C(0xff));
	destination[3] = (uint8_t)((value >> 24U) & UINT32_C(0xff));
}

static void put_u64_le(uint8_t *destination, uint64_t value)
{
	for (unsigned byte = 0U; byte < 8U; byte++)
		destination[byte] = (uint8_t)((value >> (byte * 8U)) & UINT64_C(0xff));
}

static int write_frame(int output_fd, const uint8_t *yuyv,
	const IrCaptureStats *stats)
{
	uint8_t header[AUTH_FRAME_HEADER_BYTES] = { 0 };
	uint8_t luma[AUTH_FRAME_PAYLOAD_BYTES];

	memcpy(header, AUTH_FRAME_MAGIC, sizeof(AUTH_FRAME_MAGIC) - 1U);
	put_u32_le(header + 8U, 1U);
	put_u32_le(header + 12U, IR_CAPTURE_WIDTH);
	put_u32_le(header + 16U, IR_CAPTURE_HEIGHT);
	put_u32_le(header + 20U, AUTH_FRAME_PAYLOAD_BYTES);
	put_u32_le(header + 24U, stats->last_sequence);
	put_u64_le(header + 28U, stats->last_timestamp_seconds);
	put_u64_le(header + 36U, stats->last_timestamp_usec);
	for (size_t pixel = 0U; pixel < AUTH_FRAME_PAYLOAD_BYTES; pixel++)
		luma[pixel] = yuyv[pixel * 2U];
	if (write_all(output_fd, header, sizeof(header)) != 0 ||
		write_all(output_fd, luma, sizeof(luma)) != 0)
		return -1;
	fprintf(stderr, "auth_capture result=frame sequence=%" PRIu32
		" timestamp=%" PRIu64 ".%06" PRIu64 "\n", stats->last_sequence,
		stats->last_timestamp_seconds, stats->last_timestamp_usec);
	return 0;
}

static bool frame_timestamp_is_fresh(const IrCaptureStats *stats)
{
	uint64_t now = monotonic_timestamp_ns();
	uint64_t timestamp = stats->last_timestamp_seconds * UINT64_C(1000000000) +
		stats->last_timestamp_usec * UINT64_C(1000);

	return now != 0U && timestamp <= now && now - timestamp <= AUTH_MAX_FRAME_AGE_NS;
}
#endif

#ifdef AUTH_CAPTURE_SUPERVISOR_TEST
static int worker_main(int output_fd, unsigned requested_frames)
{
	const char late_output[] = "late";
	char result;
	ssize_t written;

	(void)requested_frames;
	install_worker_signals();
	/* Model an ioctl that remains blocked after the supervisor deadline. */
	(void)signal(SIGTERM, SIG_IGN);
	sleep(4U);
	errno = 0;
	written = write(output_fd, late_output, sizeof(late_output) - 1U);
	result = written < 0 && errno == EPIPE ? 'E' : 'W';
	if (auth_test_late_status_fd >= 0)
		(void)write(auth_test_late_status_fd, &result, sizeof(result));
	for (;;)
		pause();
	return AUTH_EXIT_FAILURE;
}
#else
static int worker_main(int output_fd, unsigned requested_frames)
{
	IrCapture *capture = NULL;
	IrCaptureStats stats = { 0 };
	char error[256];
	uint8_t yuyv[IR_CAPTURE_OUTPUT_BYTES];
	unsigned frames = 0U;
	int result = AUTH_EXIT_FAILURE;

	install_worker_signals();
	if (ir_capture_open(&capture, error, sizeof(error)) != 0) {
		report_error(error);
		return AUTH_EXIT_FAILURE;
	}
	/* The authentication path never writes a loopback or accepts a filler. */
	ir_capture_set_discard_error_buffers(capture, true);
	ir_capture_set_stream_attempt_id(capture, (uint64_t)getpid());
	if (ir_capture_start(capture, error, sizeof(error)) != 0) {
		report_error(error);
		(void)ir_capture_stop(capture, NULL, 0U);
		ir_capture_close(capture);
		return AUTH_EXIT_FAILURE;
	}
	fprintf(stderr, "auth_capture result=streaming frames=%u\n",
		requested_frames);
	while (!stop_requested && (requested_frames == 0U ||
		frames < requested_frames)) {
		int capture_result = ir_capture_next(capture, yuyv, sizeof(yuyv),
			AUTH_FRAME_POLL_MS, &stats, error, sizeof(error));

		if (capture_result == 0 || capture_result == IR_CAPTURE_RESULT_DISCARDED)
			continue;
		if (capture_result < 0) {
			report_error(error);
			break;
		}
		if (!frame_timestamp_is_fresh(&stats)) {
			report_error("decoded frame timestamp is stale");
			break;
		}
		if (write_frame(output_fd, yuyv, &stats) != 0) {
			fprintf(stderr, "auth_capture result=output-closed\n");
			stop_requested = 1;
			break;
		}
		frames++;
	}
	if (ir_capture_stop(capture, error, sizeof(error)) != 0) {
		report_error(error);
	} else if (!stop_requested || frames == requested_frames) {
		result = frames == requested_frames || requested_frames == 0U ?
			EXIT_SUCCESS : AUTH_EXIT_FAILURE;
	}
	ir_capture_close(capture);
	fprintf(stderr, "auth_capture result=cleanup frames=%u status=%d\n",
		frames, result);
	return result;
}
#endif

static int open_auth_lock(void)
{
	struct stat status;
	int fd;

	if (geteuid() != 0) {
		report_error("root privileges are required");
		return -1;
	}
	fd = open(AUTH_LOCK_PATH, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0) {
		fprintf(stderr, "auth_capture result=unavailable detail=open-lock:%s\n",
			strerror(errno));
		return -1;
	}
	if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
		status.st_uid != 0U || fchmod(fd, 0600) != 0) {
		report_error("authentication lock is not a root-owned regular file");
		(void)close(fd);
		return -1;
	}
	if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
		if (errno == EWOULDBLOCK || errno == EAGAIN)
			fprintf(stderr, "auth_capture result=busy\n");
		else
			fprintf(stderr, "auth_capture result=unavailable detail=lock:%s\n",
				strerror(errno));
		(void)close(fd);
		return errno == EWOULDBLOCK || errno == EAGAIN ? AUTH_EXIT_BUSY : -1;
	}
	return fd;
}

static int remaining_poll_ms(uint64_t deadline_ns)
{
	uint64_t now = monotonic_timestamp_ns();
	uint64_t remaining;

	if (now == 0U || now >= deadline_ns)
		return 0;
	remaining = deadline_ns - now;
	return (int)((remaining + UINT64_C(999999)) / UINT64_C(1000000));
}

static int forward_bytes(const uint8_t *buffer, size_t length,
	uint64_t deadline_ns)
{
	size_t written = 0U;

	while (written < length) {
		struct pollfd descriptor = {
			.fd = STDOUT_FILENO,
			.events = POLLOUT,
		};
		int timeout = remaining_poll_ms(deadline_ns);
		int ready = poll(&descriptor, 1, timeout);

		if (ready <= 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0U)
			return -1;
		{
			ssize_t count = write(STDOUT_FILENO, buffer + written, length - written);

			if (count < 0 && errno == EINTR)
				continue;
			if (count <= 0)
				return -1;
			written += (size_t)count;
		}
	}
	return 0;
}

static int supervise_worker(int lock_fd, unsigned requested_frames)
{
	int pipe_fds[2];
	pid_t worker;
	uint64_t deadline_ns = monotonic_timestamp_ns() + AUTH_CAPTURE_BUDGET_NS;
	bool pipe_eof = false;
	int worker_status = AUTH_EXIT_FAILURE;
	bool worker_done = false;

	if (pipe2(pipe_fds, O_CLOEXEC) != 0) {
		report_error("could not create capture pipe");
		return AUTH_EXIT_FAILURE;
	}
	worker = fork();
	if (worker < 0) {
		report_error("could not create capture worker");
		(void)close(pipe_fds[0]);
		(void)close(pipe_fds[1]);
		return AUTH_EXIT_FAILURE;
	}
	if (worker == 0) {
		int child_result;

		(void)close(pipe_fds[0]);
		/* The worker must not inherit the caller's outward stdout pipe. It
		 * retains pipe_fds[1] and the cleanup lock until worker_main returns. */
		if (pipe_fds[1] != STDOUT_FILENO)
			(void)close(STDOUT_FILENO);
		child_result = worker_main(pipe_fds[1], requested_frames);
		(void)close(pipe_fds[1]);
		_exit(child_result);
	}
#ifdef AUTH_CAPTURE_SUPERVISOR_TEST
	auth_test_worker_pid = worker;
#endif
	(void)close(pipe_fds[1]);
	{
		int flags = fcntl(pipe_fds[0], F_GETFL, 0);

		if (flags >= 0)
			(void)fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);
	}
	fprintf(stderr, "auth_capture result=started budget_ms=3000 worker=%ld\n",
		(long)worker);
	while (!worker_done || !pipe_eof) {
		uint8_t buffer[65536];
		struct pollfd descriptor = {
			.fd = pipe_fds[0],
			.events = POLLIN | POLLHUP | POLLERR,
		};
		int ready;
		pid_t waited;

		waited = waitpid(worker, &worker_status, WNOHANG);
		if (waited == worker)
			worker_done = true;
		else if (waited < 0 && errno != EINTR) {
			report_error("waitpid failed");
			worker_done = true;
		}
		if (monotonic_timestamp_ns() >= deadline_ns) {
			(void)kill(worker, SIGTERM);
			(void)close(pipe_fds[0]);
			(void)close(lock_fd);
			fprintf(stderr, "auth_capture result=timeout cleanup=background\n");
			return AUTH_EXIT_TIMEOUT;
		}
		ready = poll(&descriptor, 1,
			remaining_poll_ms(deadline_ns) > AUTH_CHILD_POLL_MS ?
			AUTH_CHILD_POLL_MS : remaining_poll_ms(deadline_ns));
		if (ready < 0 && errno == EINTR)
			continue;
		if (ready < 0) {
			report_error("capture pipe poll failed");
			(void)kill(worker, SIGTERM);
			(void)close(pipe_fds[0]);
			(void)close(lock_fd);
			return AUTH_EXIT_FAILURE;
		}
		if (ready == 0)
			continue;
		if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0U) {
			(void)kill(worker, SIGTERM);
			(void)close(pipe_fds[0]);
			(void)close(lock_fd);
			return AUTH_EXIT_FAILURE;
		}
		if ((descriptor.revents & (POLLIN | POLLHUP)) != 0U) {
			ssize_t count = read(pipe_fds[0], buffer, sizeof(buffer));

			if (count == 0) {
				pipe_eof = true;
			} else if (count > 0) {
				if (forward_bytes(buffer, (size_t)count, deadline_ns) != 0) {
					(void)kill(worker, SIGTERM);
					(void)close(pipe_fds[0]);
					(void)close(lock_fd);
					return AUTH_EXIT_FAILURE;
				}
			} else if (errno != EAGAIN && errno != EINTR) {
				(void)kill(worker, SIGTERM);
				(void)close(pipe_fds[0]);
				(void)close(lock_fd);
				return AUTH_EXIT_FAILURE;
			}
		}
	}
	(void)close(pipe_fds[0]);
	(void)close(lock_fd);
	if (WIFEXITED(worker_status))
		return WEXITSTATUS(worker_status);
	return AUTH_EXIT_FAILURE;
}

static int parse_frames(const char *text, unsigned *frames)
{
	char *end = NULL;
	unsigned long value;

	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || value > AUTH_MAX_FRAMES)
		return -1;
	*frames = (unsigned)value;
	return 0;
}

static void usage(const char *program)
{
	fprintf(stderr,
		"Usage: %s [--frames N]\n"
		"Capture fresh validated OV7251 frames to stdout. N is 1..%u; "
		"the fixed supervisor budget is three seconds.\n",
		program, AUTH_MAX_FRAMES);
}

int main(int argc, char **argv)
{
	unsigned requested_frames = AUTH_DEFAULT_FRAMES;
	int lock_fd;

	/* A closed consumer must become a handled output failure, not a supervisor
	 * death that leaves the worker without an explicit stop request. */
	(void)signal(SIGPIPE, SIG_IGN);

	for (int argument = 1; argument < argc; argument++) {
		if (strcmp(argv[argument], "--help") == 0 ||
			strcmp(argv[argument], "-h") == 0) {
			usage(argv[0]);
			return EXIT_SUCCESS;
		}
		if (strcmp(argv[argument], "--frames") == 0 && argument + 1 < argc &&
			parse_frames(argv[++argument], &requested_frames) == 0 &&
			requested_frames > 0U)
			continue;
		usage(argv[0]);
		return AUTH_EXIT_FAILURE;
	}
	lock_fd = open_auth_lock();
	if (lock_fd == AUTH_EXIT_BUSY)
		return AUTH_EXIT_BUSY;
	if (lock_fd < 0)
		return AUTH_EXIT_FAILURE;
	return supervise_worker(lock_fd, requested_frames);
}
