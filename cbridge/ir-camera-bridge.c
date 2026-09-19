#include "ir-v4l2.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/videodev2.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define IR_OUTPUT_DEVICE "/dev/video62"
#define IR_MAX_RECOVERIES 5U
#define IR_RECOVERY_DELAY_SECONDS 1U

static volatile sig_atomic_t stop_requested;
static uint64_t next_stream_attempt_id = 1U;

static void request_stop(int signal_number)
{
	(void)signal_number;
	stop_requested = 1;
}

static uint64_t monotonic_timestamp_ns(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0U;
	return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
		(uint64_t)now.tv_nsec;
}

static void log_reopen_event(uint64_t after_attempt)
{
	fprintf(stderr, "ir_diag event=reopen after_attempt=%" PRIu64
		" monotonic_ns=%" PRIu64 "\n", after_attempt,
		monotonic_timestamp_ns());
}

static int set_output_format(int fd, char *error, unsigned error_size)
{
	struct v4l2_format format = { 0 };

	format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	format.fmt.pix.width = IR_CAPTURE_WIDTH;
	format.fmt.pix.height = IR_CAPTURE_HEIGHT;
	format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	format.fmt.pix.field = V4L2_FIELD_NONE;
	format.fmt.pix.bytesperline = IR_CAPTURE_WIDTH * 2U;
	format.fmt.pix.sizeimage = IR_CAPTURE_OUTPUT_BYTES;
	if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
		(void)snprintf(error, error_size, "VIDIOC_S_FMT on IR loopback: %s",
			strerror(errno));
		return -1;
	}
	if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV ||
		format.fmt.pix.width != IR_CAPTURE_WIDTH ||
		format.fmt.pix.height != IR_CAPTURE_HEIGHT) {
		(void)snprintf(error, error_size, "IR loopback rejected YUYV 640x480");
		return -1;
	}
	return 0;
}

static int start_capture(IrCapture **capture, uint64_t attempt_id,
	char *error, unsigned error_size)
{
	fprintf(stderr, "ir_diag event=stream_open attempt=%" PRIu64
		" monotonic_ns=%" PRIu64 "\n", attempt_id,
		monotonic_timestamp_ns());
	if (ir_capture_open(capture, error, error_size) != 0) {
		fprintf(stderr, "ir_diag event=stream_start attempt=%" PRIu64
			" monotonic_ns=%" PRIu64 " result=open-failure\n", attempt_id,
			monotonic_timestamp_ns());
		return -1;
	}
	ir_capture_set_stream_attempt_id(*capture, attempt_id);
	if (ir_capture_start(*capture, error, error_size) != 0) {
		ir_capture_close(*capture);
		*capture = NULL;
		return -1;
	}
	return 0;
}

int main(void)
{
	const char *output_path = getenv("SP7_IR_OUTPUT");
	uint8_t frame[IR_CAPTURE_OUTPUT_BYTES];
	IrCapture *capture = NULL;
	IrCaptureStats stats;
	char error[256];
	int output_fd;
	int exit_code = EXIT_SUCCESS;
	unsigned recoveries = 0U;
	uint64_t attempt_id = 0U;

	if (output_path == NULL || *output_path == '\0')
		output_path = IR_OUTPUT_DEVICE;
	(void)signal(SIGINT, request_stop);
	(void)signal(SIGTERM, request_stop);
	output_fd = open(output_path, O_WRONLY | O_CLOEXEC);
	if (output_fd < 0 || set_output_format(output_fd, error, sizeof(error)) != 0) {
		fprintf(stderr, "IR loopback startup failed: %s\n", output_fd < 0 ?
			strerror(errno) : error);
		if (output_fd >= 0)
			(void)close(output_fd);
		return EXIT_FAILURE;
	}
	while (!stop_requested) {
		if (capture == NULL) {
			attempt_id = next_stream_attempt_id++;
			if (start_capture(&capture, attempt_id, error, sizeof(error)) != 0) {
				fprintf(stderr, "IR capture startup failed attempt=%" PRIu64
					": %s\n", attempt_id, error);
				if (recoveries >= IR_MAX_RECOVERIES) {
					exit_code = EXIT_FAILURE;
					break;
				}
				recoveries++;
				log_reopen_event(attempt_id);
				sleep(IR_RECOVERY_DELAY_SECONDS);
				continue;
			}
		}
		int result = ir_capture_next(capture, frame, sizeof(frame), 1000U,
			&stats, error, sizeof(error));

		if (result == 0)
			continue;
		if (result == IR_CAPTURE_RESULT_DISCARDED)
			continue;
		if (result < 0) {
			fprintf(stderr, "IR capture stopped attempt=%" PRIu64 ": %s\n",
				attempt_id, error);
			(void)ir_capture_stop(capture, error, sizeof(error));
			ir_capture_close(capture);
			capture = NULL;
			if (recoveries >= IR_MAX_RECOVERIES) {
				exit_code = EXIT_FAILURE;
				break;
			}
			recoveries++;
			log_reopen_event(attempt_id);
			sleep(IR_RECOVERY_DELAY_SECONDS);
			continue;
		}
		recoveries = 0U;
		{
			size_t written = 0U;

			while (written < sizeof(frame)) {
				ssize_t count = write(output_fd, frame + written,
					sizeof(frame) - written);

				if (count < 0 && errno == EINTR)
					continue;
				if (count <= 0) {
					fprintf(stderr, "IR loopback write failed: %s\n",
						strerror(errno));
					stop_requested = 1;
					break;
				}
				written += (size_t)count;
			}
		}
	}
	(void)close(output_fd);
	if (capture != NULL && ir_capture_stop(capture, error, sizeof(error)) != 0) {
		fprintf(stderr, "IR capture shutdown warning: %s\n", error);
		exit_code = EXIT_FAILURE;
	}
	ir_capture_close(capture);
	return exit_code;
}
