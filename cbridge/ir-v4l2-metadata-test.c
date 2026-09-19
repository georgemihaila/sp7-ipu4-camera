#include "ir-v4l2.h"

#include <errno.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

typedef struct {
	uint32_t sequence;
	struct timeval timestamp;
	uint32_t flags;
	uint32_t bytesused;
	uint32_t data_offset;
} MockFrame;

static struct {
	const MockFrame *frames;
	size_t count;
	size_t next;
	unsigned qbuf_calls;
} mock_stream;

int sp7_camera_ir_test_poll(struct pollfd *fds, nfds_t count, int timeout_ms)
{
	(void)timeout_ms;
	if (fds == NULL || count == 0U)
		return -1;
	fds[0].revents = POLLIN;
	return 1;
}

int sp7_camera_ir_test_ioctl(int fd, unsigned long request, void *argument)
{
	struct v4l2_buffer *buffer = argument;

	(void)fd;
	if (request == VIDIOC_DQBUF) {
		const MockFrame *frame;

		if (buffer == NULL || buffer->m.planes == NULL ||
			mock_stream.next >= mock_stream.count) {
			errno = EAGAIN;
			return -1;
		}
		frame = &mock_stream.frames[mock_stream.next++];
		buffer->index = 0U;
		buffer->length = 1U;
		buffer->flags = frame->flags;
		buffer->sequence = frame->sequence;
		buffer->timestamp = frame->timestamp;
		buffer->m.planes[0].bytesused = frame->bytesused;
		buffer->m.planes[0].data_offset = frame->data_offset;
		return 0;
	}
	if (request == VIDIOC_QBUF) {
		if (buffer == NULL || buffer->m.planes == NULL) {
			errno = EINVAL;
			return -1;
		}
		mock_stream.qbuf_calls++;
		buffer->sequence = UINT32_C(0xdeadbeef);
		buffer->timestamp.tv_sec = 0;
		buffer->timestamp.tv_usec = 0;
		buffer->flags = 0U;
		buffer->m.planes[0].bytesused = 1U;
		buffer->m.planes[0].data_offset = 0U;
		return 0;
	}
	errno = EINVAL;
	return -1;
}

static int fail(const char *message)
{
	fprintf(stderr, "test-ir-metadata: %s\n", message);
	return EXIT_FAILURE;
}

static int check(bool condition, const char *message)
{
	return condition ? EXIT_SUCCESS : fail(message);
}

static void reset_mock(const MockFrame *frames, size_t count)
{
	mock_stream.frames = frames;
	mock_stream.count = count;
	mock_stream.next = 0U;
	mock_stream.qbuf_calls = 0U;
}

static IrCapture *new_capture(uint8_t *buffer, size_t buffer_length)
{
	return ir_test_capture_create(9, buffer, buffer_length, 4U, 1U, 9U, 9U);
}

static int next_frame(IrCapture *capture, IrCaptureStats *stats, char *error)
{
	uint8_t output[IR_CAPTURE_OUTPUT_BYTES];

	return ir_capture_next(capture, output, sizeof(output), 1000U, stats,
		error, 256U);
}

static int test_consecutive_and_skips(uint8_t *raw)
{
	const MockFrame frames[] = {
		{ 100U, { 10, 1 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U },
		{ 101U, { 10, 2 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U },
		{ 104U, { 10, 5 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U },
	};
	IrCaptureStats stats = { 0 };
	IrCapture *capture;
	char error[256] = { 0 };

	reset_mock(frames, sizeof(frames) / sizeof(frames[0]));
	capture = new_capture(raw, 13U);
	if (check(capture != NULL, "consecutive capture construction failed") != 0)
		return EXIT_FAILURE;
	if (check(next_frame(capture, &stats, error) == 1,
		"first consecutive frame was rejected") != 0 ||
		check(stats.sequence_gaps == 0U, "first frame created a sequence gap") != 0 ||
		check(next_frame(capture, &stats, error) == 1,
		"second consecutive frame was rejected") != 0 ||
		check(stats.sequence_gaps == 0U, "consecutive frames created a gap") != 0 ||
		check(next_frame(capture, &stats, error) == 1,
		"skipped frame was rejected") != 0 ||
		check(stats.sequence_gaps == 2U, "genuine sequence skip was not counted") != 0 ||
		check(stats.last_sequence == 104U && stats.last_dequeued_sequence == 104U,
			"preserved sequence metadata is wrong") != 0 ||
		check(stats.last_timestamp_seconds == 10U &&
			stats.last_timestamp_usec == 5U, "preserved timestamp metadata is wrong") != 0 ||
		check(stats.last_bytesused == 13U && stats.last_data_offset == 4U,
			"preserved plane metadata is wrong") != 0 ||
		check(mock_stream.qbuf_calls == 3U, "each frame was not requeued") != 0) {
		ir_test_capture_destroy(capture);
		return EXIT_FAILURE;
	}
	ir_test_capture_destroy(capture);
	return EXIT_SUCCESS;
}

static int test_timestamp_regression(uint8_t *raw)
{
	const MockFrame frames[] = {
		{ 10U, { 20, 100 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U },
		{ 11U, { 20, 99 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U },
	};
	IrCaptureStats stats = { 0 };
	IrCapture *capture;
	char error[256] = { 0 };

	reset_mock(frames, sizeof(frames) / sizeof(frames[0]));
	capture = new_capture(raw, 13U);
	if (check(capture != NULL, "timestamp capture construction failed") != 0)
		return EXIT_FAILURE;
	if (check(next_frame(capture, &stats, error) == 1,
		"timestamp baseline frame was rejected") != 0 ||
		check(next_frame(capture, &stats, error) == -1,
		"timestamp regression was accepted") != 0 ||
		check(strstr(error, "timestamps") != NULL,
			"timestamp regression error was not reported") != 0 ||
		check(stats.timestamp_errors == 1U && stats.rejected_buffers == 1U,
			"timestamp error accounting is wrong") != 0 ||
		check(stats.sequence_gaps == 0U, "timestamp regression became a sequence gap") != 0) {
		ir_test_capture_destroy(capture);
		return EXIT_FAILURE;
	}
	ir_test_capture_destroy(capture);
	return EXIT_SUCCESS;
}

static int test_wraparound(uint8_t *raw)
{
	const MockFrame frames[] = {
		{ UINT32_MAX, { 30, 1 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U },
		{ 0U, { 30, 2 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U },
	};
	IrCaptureStats stats = { 0 };
	IrCapture *capture;
	char error[256] = { 0 };

	reset_mock(frames, sizeof(frames) / sizeof(frames[0]));
	capture = new_capture(raw, 13U);
	if (check(capture != NULL, "wraparound capture construction failed") != 0)
		return EXIT_FAILURE;
	if (check(next_frame(capture, &stats, error) == 1,
		"pre-wrap frame was rejected") != 0 ||
		check(next_frame(capture, &stats, error) == 1,
			"post-wrap frame was rejected") != 0 ||
		check(stats.sequence_gaps == 0U && stats.last_sequence == 0U,
			"sequence wraparound was miscounted") != 0) {
		ir_test_capture_destroy(capture);
		return EXIT_FAILURE;
	}
	ir_test_capture_destroy(capture);
	return EXIT_SUCCESS;
}

static int test_stream_restart(uint8_t *raw)
{
	const MockFrame first_stream[] = {
		{ 500U, { 40, 1 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U },
	};
	const MockFrame second_stream[] = {
		{ 7U, { 50, 1 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U },
		{ 8U, { 50, 2 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U },
	};
	IrCaptureStats stats = { 0 };
	IrCapture *capture;
	char error[256] = { 0 };

	reset_mock(first_stream, sizeof(first_stream) / sizeof(first_stream[0]));
	capture = new_capture(raw, 13U);
	if (check(capture != NULL, "first restart capture construction failed") != 0)
		return EXIT_FAILURE;
	if (check(next_frame(capture, &stats, error) == 1,
		"first stream frame was rejected") != 0) {
		ir_test_capture_destroy(capture);
		return EXIT_FAILURE;
	}
	ir_test_capture_destroy(capture);

	stats = (IrCaptureStats){ 0 };
	reset_mock(second_stream, sizeof(second_stream) / sizeof(second_stream[0]));
	capture = new_capture(raw, 13U);
	if (check(capture != NULL, "second restart capture construction failed") != 0)
		return EXIT_FAILURE;
	if (check(next_frame(capture, &stats, error) == 1,
		"first post-restart frame was rejected") != 0 ||
		check(next_frame(capture, &stats, error) == 1,
			"second post-restart frame was rejected") != 0 ||
		check(stats.sequence_gaps == 0U && stats.last_sequence == 8U,
			"stream restart sequence domain was not reset") != 0) {
		ir_test_capture_destroy(capture);
		return EXIT_FAILURE;
	}
	ir_test_capture_destroy(capture);
	return EXIT_SUCCESS;
}

int main(void)
{
	uint8_t raw[13] = { 0 };

	raw[4] = 0x40U;
	if (test_consecutive_and_skips(raw) != 0 ||
		test_timestamp_regression(raw) != 0 || test_wraparound(raw) != 0 ||
		test_stream_restart(raw) != 0)
		return EXIT_FAILURE;
	puts("test-ir-metadata: PASS");
	return EXIT_SUCCESS;
}
