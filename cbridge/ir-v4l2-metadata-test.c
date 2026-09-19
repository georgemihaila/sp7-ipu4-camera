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
	uint32_t index;
	uint32_t plane_count;
	bool custom_buffer_metadata;
} MockFrame;

static struct {
	const MockFrame *frames;
	size_t count;
	size_t next;
	unsigned qbuf_calls;
	bool fail_qbuf;
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
		buffer->index = frame->custom_buffer_metadata ? frame->index : 0U;
		buffer->length = frame->custom_buffer_metadata ? frame->plane_count : 1U;
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
		if (mock_stream.fail_qbuf) {
			errno = EIO;
			return -1;
		}
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
	mock_stream.fail_qbuf = false;
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

static int test_rejection(uint8_t *raw, MockFrame frame, uint32_t reasons,
	bool expect_requeue, bool fail_requeue)
{
	const uint64_t attempt_id = 77U;
	IrCaptureStats stats = { 0 };
	IrCapture *capture;
	char error[256] = { 0 };

	reset_mock(&frame, 1U);
	mock_stream.fail_qbuf = fail_requeue;
	capture = new_capture(raw, 13U);
	if (check(capture != NULL, "rejection capture construction failed") != 0)
		return EXIT_FAILURE;
	ir_capture_set_stream_attempt_id(capture, attempt_id);
	if (check(next_frame(capture, &stats, error) == -1,
		"rejected frame did not preserve failure return") != 0 ||
		check(stats.stream_attempt_id == attempt_id,
			"stream attempt ID was not preserved") != 0 ||
		check(stats.last_rejection_reasons == (reasons |
			(fail_requeue ? IR_CAPTURE_REJECTION_REQUEUE : 0U)),
			"rejection reason mask is wrong") != 0 ||
		check(stats.last_rejection_index == frame.index,
			"rejected index snapshot is wrong") != 0 ||
		check(stats.last_rejection_plane_count ==
			(frame.custom_buffer_metadata ? frame.plane_count : 1U),
			"rejected plane-count snapshot is wrong") != 0 ||
		check(stats.last_rejection_sequence == frame.sequence,
			"rejected sequence snapshot is wrong") != 0 ||
		check(stats.last_rejection_flags == frame.flags,
			"rejected flags snapshot is wrong") != 0 ||
		check(stats.last_rejection_timestamp_flags ==
			(frame.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK),
			"rejected timestamp-flags snapshot is wrong") != 0 ||
		check(stats.last_rejection_bytesused == frame.bytesused &&
			stats.last_rejection_data_offset == frame.data_offset,
			"rejected plane snapshot is wrong") != 0 ||
		check(stats.last_timestamp_seconds ==
			(uint64_t)frame.timestamp.tv_sec &&
			stats.last_timestamp_usec == (uint64_t)frame.timestamp.tv_usec,
			"rejected timestamp snapshot is wrong") != 0 ||
		check(stats.last_rejection_timestamp_seconds ==
			(uint64_t)frame.timestamp.tv_sec &&
			stats.last_rejection_timestamp_usec ==
			(uint64_t)frame.timestamp.tv_usec,
			"dedicated rejected timestamp snapshot is wrong") != 0 ||
		check(stats.last_rejection_capacity_available ==
			(frame.index == 0U),
			"rejected capacity availability is wrong") != 0 ||
		check(stats.last_rejection_capacity ==
			(frame.index == 0U ? 13U : 0U),
			"rejected capacity snapshot is wrong") != 0 ||
		check(stats.last_rejection_monotonic_ns != 0U,
			"rejection timestamp was not recorded") != 0 ||
		check(stats.rejected_buffers == 1U,
			"rejected-buffer count is wrong") != 0 ||
		check((expect_requeue && !fail_requeue) ?
			(stats.last_requeue_result == IR_CAPTURE_REQUEUE_SUCCEEDED &&
				stats.requeue_errors == 0U && mock_stream.qbuf_calls == 1U) :
			(!expect_requeue && !fail_requeue) ?
			(stats.last_requeue_result == IR_CAPTURE_REQUEUE_NOT_ATTEMPTED &&
				stats.requeue_errors == 0U && mock_stream.qbuf_calls == 0U) :
			(stats.last_requeue_result == IR_CAPTURE_REQUEUE_FAILED &&
				stats.requeue_errors == 1U && mock_stream.qbuf_calls == 1U &&
				(stats.last_rejection_reasons & IR_CAPTURE_REJECTION_REQUEUE) != 0U),
			"requeue policy/result accounting is wrong") != 0 ||
		check((expect_requeue && !fail_requeue) ?
			stats.last_requeue_monotonic_ns != 0U :
			fail_requeue ? stats.last_requeue_monotonic_ns != 0U :
			stats.last_requeue_monotonic_ns == 0U,
			"requeue timestamp accounting is wrong") != 0) {
		ir_test_capture_destroy(capture);
		return EXIT_FAILURE;
	}
	ir_test_capture_destroy(capture);
	return EXIT_SUCCESS;
}

static int test_structured_rejections(uint8_t *raw)
{
	const uint32_t valid_timestamp = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	MockFrame frame = { 12U, { 60, 1 }, valid_timestamp, 13U, 4U,
		0U, 1U, false };
	const uint32_t invalid_index = 99U;

	frame.flags = valid_timestamp | V4L2_BUF_FLAG_ERROR;
	if (test_rejection(raw, frame, IR_CAPTURE_REJECTION_ERROR_FLAG, true,
		false) != 0)
		return EXIT_FAILURE;

	frame.flags = 0U;
	if (test_rejection(raw, frame, IR_CAPTURE_REJECTION_TIMESTAMP_FLAGS, true,
		false) != 0)
		return EXIT_FAILURE;

	frame.custom_buffer_metadata = true;
	frame.flags = valid_timestamp;
	frame.index = invalid_index;
	if (test_rejection(raw, frame, IR_CAPTURE_REJECTION_INVALID_INDEX |
		IR_CAPTURE_REJECTION_CAPACITY_UNAVAILABLE, false, false) != 0)
		return EXIT_FAILURE;

	frame.index = 0U;
	frame.plane_count = 2U;
	if (test_rejection(raw, frame, IR_CAPTURE_REJECTION_INVALID_PLANE_COUNT,
		false, false) != 0)
		return EXIT_FAILURE;

	frame.plane_count = 1U;
	frame.data_offset = 13U;
	if (test_rejection(raw, frame,
		IR_CAPTURE_REJECTION_INVALID_PLANE_METADATA, true, false) != 0)
		return EXIT_FAILURE;

	frame.index = invalid_index;
	frame.plane_count = 2U;
	frame.flags = V4L2_BUF_FLAG_ERROR;
	if (test_rejection(raw, frame, IR_CAPTURE_REJECTION_INVALID_INDEX |
		IR_CAPTURE_REJECTION_CAPACITY_UNAVAILABLE |
		IR_CAPTURE_REJECTION_INVALID_PLANE_COUNT |
		IR_CAPTURE_REJECTION_ERROR_FLAG |
		IR_CAPTURE_REJECTION_TIMESTAMP_FLAGS, false, false) != 0)
		return EXIT_FAILURE;

	frame.index = 0U;
	frame.plane_count = 1U;
	frame.flags = valid_timestamp | V4L2_BUF_FLAG_ERROR;
	frame.data_offset = 4U;
	if (test_rejection(raw, frame, IR_CAPTURE_REJECTION_ERROR_FLAG, true,
		true) != 0)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

static int test_consecutive_and_skips(uint8_t *raw)
{
	const MockFrame frames[] = {
		{ 100U, { 10, 1 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U,
			0U, 1U, false },
		{ 101U, { 10, 2 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U,
			0U, 1U, false },
		{ 104U, { 10, 5 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U,
			0U, 1U, false },
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
		{ 10U, { 20, 100 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U,
			0U, 1U, false },
		{ 11U, { 20, 99 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U,
			0U, 1U, false },
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
		check(stats.last_rejection_reasons ==
			IR_CAPTURE_REJECTION_TIMESTAMP_REGRESSION,
			"timestamp regression reason mask is wrong") != 0 ||
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
		{ UINT32_MAX, { 30, 1 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U,
			0U, 1U, false },
		{ 0U, { 30, 2 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U,
			0U, 1U, false },
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
		{ 500U, { 40, 1 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U,
			0U, 1U, false },
	};
	const MockFrame second_stream[] = {
		{ 7U, { 50, 1 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U,
			0U, 1U, false },
		{ 8U, { 50, 2 }, V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC, 13U, 4U,
			0U, 1U, false },
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
		test_stream_restart(raw) != 0 || test_structured_rejections(raw) != 0)
		return EXIT_FAILURE;
	puts("test-ir-metadata: PASS");
	return EXIT_SUCCESS;
}
