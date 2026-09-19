#ifndef SP7_CAMERA_IR_V4L2_H
#define SP7_CAMERA_IR_V4L2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IR_CAPTURE_WIDTH 640U
#define IR_CAPTURE_HEIGHT 480U
#define IR_CAPTURE_HEADER_BYTES 4U
#define IR_CAPTURE_RAW10_BYTES (IR_CAPTURE_WIDTH * 10U / 8U)
#define IR_CAPTURE_MIN_STRIDE 832U
#define IR_CAPTURE_OUTPUT_BYTES (IR_CAPTURE_WIDTH * IR_CAPTURE_HEIGHT * 2U)

typedef struct IrCapture IrCapture;

typedef enum {
	IR_CAPTURE_ERROR_NONE = 0,
	IR_CAPTURE_ERROR_TIMEOUT,
	IR_CAPTURE_ERROR_POLL,
	IR_CAPTURE_ERROR_DQBUF,
	IR_CAPTURE_ERROR_METADATA,
	IR_CAPTURE_ERROR_TIMESTAMP,
	IR_CAPTURE_ERROR_SEQUENCE,
	IR_CAPTURE_ERROR_DECODE,
	IR_CAPTURE_ERROR_REQUEUE,
} IrCaptureError;

typedef enum {
	IR_CAPTURE_REJECTION_NONE = 0,
	IR_CAPTURE_REJECTION_INVALID_INDEX = 1U << 0,
	IR_CAPTURE_REJECTION_INVALID_PLANE_COUNT = 1U << 1,
	IR_CAPTURE_REJECTION_ERROR_FLAG = 1U << 2,
	IR_CAPTURE_REJECTION_TIMESTAMP_FLAGS = 1U << 3,
	IR_CAPTURE_REJECTION_CAPACITY_UNAVAILABLE = 1U << 4,
	IR_CAPTURE_REJECTION_INVALID_PLANE_METADATA = 1U << 5,
	IR_CAPTURE_REJECTION_TIMESTAMP_EMPTY = 1U << 6,
	IR_CAPTURE_REJECTION_TIMESTAMP_REGRESSION = 1U << 7,
	IR_CAPTURE_REJECTION_SEQUENCE = 1U << 8,
	IR_CAPTURE_REJECTION_DECODE = 1U << 9,
	IR_CAPTURE_REJECTION_REQUEUE = 1U << 10,
} IrCaptureRejectionReason;

typedef enum {
	IR_CAPTURE_REQUEUE_NOT_ATTEMPTED = 0,
	IR_CAPTURE_REQUEUE_SUCCEEDED,
	IR_CAPTURE_REQUEUE_FAILED,
} IrCaptureRequeueResult;

typedef struct {
	unsigned width;
	unsigned height;
	unsigned stride;
	unsigned sizeimage;
	unsigned data_offset;
	unsigned last_bytesused;
	unsigned last_data_offset;
	uint32_t last_sequence;
	uint32_t last_dequeued_sequence;
	uint64_t last_timestamp_seconds;
	uint64_t last_timestamp_usec;
	uint64_t stream_attempt_id;
	uint64_t stream_start_monotonic_ns;
	uint64_t last_rejection_monotonic_ns;
	uint64_t last_requeue_monotonic_ns;
	uint64_t stream_stop_monotonic_ns;
	uint32_t last_rejection_reasons;
	uint32_t last_rejection_index;
	uint32_t last_rejection_plane_count;
	uint32_t last_rejection_sequence;
	uint32_t last_rejection_flags;
	uint32_t last_rejection_timestamp_flags;
	uint64_t last_rejection_timestamp_seconds;
	uint64_t last_rejection_timestamp_usec;
	uint32_t last_rejection_bytesused;
	uint32_t last_rejection_data_offset;
	uint64_t last_rejection_capacity;
	bool last_rejection_capacity_available;
	IrCaptureRequeueResult last_requeue_result;
	uint64_t frames;
	uint64_t sequence_gaps;
	uint64_t rejected_buffers;
	uint64_t metadata_errors;
	uint64_t timestamp_errors;
	uint64_t sequence_errors;
	uint64_t decode_errors;
	uint64_t requeue_errors;
	uint64_t poll_errors;
	uint64_t dqbuf_errors;
	IrCaptureError last_error;
} IrCaptureStats;

/* Discover the OV7251 -> source-6 -> capture route and prepare MMAP buffers. */
int ir_capture_open(IrCapture **capture, char *error, unsigned error_size);
void ir_capture_set_stream_attempt_id(IrCapture *capture, uint64_t attempt_id);
int ir_capture_start(IrCapture *capture, char *error, unsigned error_size);

/* Returns 1 for a decoded frame, 0 for a poll timeout, and -1 on bad input. */
int ir_capture_next(IrCapture *capture, uint8_t *yuyv, size_t yuyv_size,
	unsigned timeout_ms, IrCaptureStats *stats, char *error,
	unsigned error_size);

int ir_capture_stop(IrCapture *capture, char *error, unsigned error_size);
void ir_capture_close(IrCapture *capture);

/* Decode one complete direct-tap packed RAW10 frame into neutral-chroma YUYV. */
int ir_decode_raw10_to_yuyv(const uint8_t *buffer, size_t buffer_size,
	unsigned width, unsigned height, unsigned stride, unsigned data_offset,
	uint8_t *yuyv, size_t yuyv_size, char *error, unsigned error_size);

#ifdef SP7_CAMERA_IR_TEST
/* Test-only construction for mocked V4L2 dequeue/requeue regression tests. */
IrCapture *ir_test_capture_create(int video_fd, void *buffer,
	size_t buffer_length, unsigned width, unsigned height, unsigned stride,
	unsigned sizeimage);
void ir_test_capture_destroy(IrCapture *capture);
#endif

#endif
