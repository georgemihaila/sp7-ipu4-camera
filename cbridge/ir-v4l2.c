#include "ir-v4l2.h"

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <inttypes.h>
#include <linux/media.h>
#include <linux/media-bus-format.h>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifdef SP7_CAMERA_IR_TEST
extern int sp7_camera_ir_test_ioctl(int fd, unsigned long request,
	void *argument);
extern int sp7_camera_ir_test_poll(struct pollfd *fds, nfds_t count,
	int timeout_ms);
extern uint64_t sp7_camera_ir_test_monotonic_ns(void);
#endif

#define IR_MEDIA_MAX_ENTITIES 128U
#define IR_MEDIA_MAX_LINKS 512U
#define IR_CAPTURE_BUFFERS 8U
#define IR_HEADER_WORD 0x40U
#define IR_MAX_ROW_PADDING 64U
#define IR_CAPTURE_MAX_CONSECUTIVE_DISCARDS 5U
#define IR_CAPTURE_MAX_DISCARD_WAIT_NS UINT64_C(2000000000)

typedef struct {
	void *address;
	size_t length;
} IrMappedBuffer;

struct IrCapture {
	int media_fd;
	int csi_fd;
	int video_fd;
	char video_path[64];
	struct v4l2_pix_format_mplane format;
	IrMappedBuffer buffers[IR_CAPTURE_BUFFERS];
	unsigned buffer_count;
	bool streaming;
	bool have_sequence;
	bool discard_error_buffers;
	uint32_t last_sequence;
	struct timeval last_timestamp;
	uint64_t stream_attempt_id;
	uint64_t discard_anchor_monotonic_ns;
	IrCaptureStats stats;
};

static uint64_t monotonic_timestamp_ns(void)
{
#ifdef SP7_CAMERA_IR_TEST
	return sp7_camera_ir_test_monotonic_ns();
#else
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0U;
	return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
		(uint64_t)now.tv_nsec;
#endif
}

static void log_stream_event(const IrCapture *capture, const char *event,
	uint64_t timestamp_ns, const char *result)
{
	fprintf(stderr, "ir_diag event=%s attempt=%" PRIu64
		" monotonic_ns=%" PRIu64 " result=%s\n", event,
		capture->stats.stream_attempt_id, timestamp_ns, result);
}

static void prepare_rejection(IrCapture *capture, uint32_t reasons)
{
	capture->stats.last_rejection_reasons = reasons;
	capture->stats.last_rejection_monotonic_ns = monotonic_timestamp_ns();
	capture->stats.last_requeue_monotonic_ns = 0U;
	capture->stats.last_requeue_result = IR_CAPTURE_REQUEUE_NOT_ATTEMPTED;
}

static void log_rejection(const IrCapture *capture)
{
	const IrCaptureStats *stats = &capture->stats;

	fprintf(stderr, "ir_diag event=rejection attempt=%" PRIu64
		" monotonic_ns=%" PRIu64 " reason_mask=0x%08" PRIx32
		" index=%" PRIu32 " plane_count=%" PRIu32
		" sequence=%" PRIu32 " flags=0x%08" PRIx32
		" timestamp_flags=0x%08" PRIx32
		" timestamp=%" PRIu64 ".%06" PRIu64
		" bytesused=%" PRIu32 " data_offset=%" PRIu32
		" capacity=%" PRIu64 " capacity_available=%u"
		" requeue_monotonic_ns=%" PRIu64 " requeue_result=%u\n",
		stats->stream_attempt_id, stats->last_rejection_monotonic_ns,
		stats->last_rejection_reasons, stats->last_rejection_index,
		stats->last_rejection_plane_count, stats->last_rejection_sequence,
		stats->last_rejection_flags,
		stats->last_rejection_timestamp_flags,
		stats->last_rejection_timestamp_seconds,
		stats->last_rejection_timestamp_usec,
		stats->last_rejection_bytesused, stats->last_rejection_data_offset,
		stats->last_rejection_capacity,
		stats->last_rejection_capacity_available ? 1U : 0U,
		stats->last_requeue_monotonic_ns, (unsigned)stats->last_requeue_result);
}

static uint32_t validate_timestamp(IrCapture *capture,
	const struct timeval *timestamp, char *error, unsigned error_size);

static void publish_stats(const IrCapture *capture, IrCaptureStats *stats)
{
	if (stats != NULL)
		*stats = capture->stats;
}

static void set_error(char *error, unsigned error_size, const char *format, ...)
{
	va_list args;

	if (error == NULL || error_size == 0U)
		return;
	va_start(args, format);
	(void)vsnprintf(error, error_size, format, args);
	va_end(args);
}

static int ioctl_retry(int fd, unsigned long request, void *argument)
{
	int result;

	do {
#ifdef SP7_CAMERA_IR_TEST
		result = sp7_camera_ir_test_ioctl(fd, request, argument);
#else
		result = ioctl(fd, request, argument);
#endif
	} while (result < 0 && errno == EINTR);
	return result;
}

static bool name_has(const char *name, const char *needle)
{
	return name != NULL && needle != NULL && strstr(name, needle) != NULL;
}

static bool name_is(const char *name, const char *expected)
{
	return name != NULL && expected != NULL && strcmp(name, expected) == 0;
}

static int enumerate_entities(int fd, struct media_entity_desc *entities,
	unsigned *count, char *error, unsigned error_size)
{
	unsigned found = 0U;
	uint32_t id = MEDIA_ENT_ID_FLAG_NEXT;

	while (found < IR_MEDIA_MAX_ENTITIES) {
		struct media_entity_desc entity = { 0 };

		entity.id = id;
		if (ioctl_retry(fd, MEDIA_IOC_ENUM_ENTITIES, &entity) < 0) {
			if (errno == EINVAL)
				break;
			set_error(error, error_size, "MEDIA_IOC_ENUM_ENTITIES: %s",
				strerror(errno));
			return -1;
		}
		entities[found++] = entity;
		id = entity.id | MEDIA_ENT_ID_FLAG_NEXT;
	}
	if (found == IR_MEDIA_MAX_ENTITIES) {
		set_error(error, error_size, "media graph has too many entities");
		return -1;
	}
	*count = found;
	return 0;
}

static int link_route_enabled(int fd, const struct media_entity_desc *entities,
	unsigned count, uint32_t source_id, uint32_t sink_id, bool *enabled,
	char *error, unsigned error_size)
{
	bool found = false;

	*enabled = false;
	for (unsigned index = 0U; index < count; index++) {
		const struct media_entity_desc *entity = &entities[index];
		struct media_pad_desc *pads = NULL;
		struct media_link_desc *links = NULL;
		struct media_links_enum enumeration = { 0 };

		if (entity->links > IR_MEDIA_MAX_LINKS) {
			set_error(error, error_size, "entity %s has too many links",
				entity->name);
			return -1;
		}
		pads = calloc(entity->pads == 0U ? 1U : entity->pads, sizeof(*pads));
		links = calloc(entity->links == 0U ? 1U : entity->links,
			sizeof(*links));
		if (pads == NULL || links == NULL) {
			free(pads);
			free(links);
			set_error(error, error_size, "out of memory enumerating media links");
			return -1;
		}
		enumeration.entity = entity->id;
		enumeration.pads = pads;
		enumeration.links = links;
		if (ioctl_retry(fd, MEDIA_IOC_ENUM_LINKS, &enumeration) < 0) {
			int saved_errno = errno;

			free(pads);
			free(links);
			set_error(error, error_size, "MEDIA_IOC_ENUM_LINKS for %s: %s",
				entity->name, strerror(saved_errno));
			return -1;
		}
		for (unsigned link = 0U; link < entity->links; link++) {
			const struct media_link_desc *descriptor = &links[link];

			if (descriptor->source.entity == source_id &&
				descriptor->sink.entity == sink_id) {
				found = true;
				*enabled = (descriptor->flags & MEDIA_LNK_FL_ENABLED) != 0U;
			}
		}
		free(pads);
		free(links);
	}
	if (!found) {
		set_error(error, error_size,
			"media graph route is absent (%u -> %u)", source_id, sink_id);
		return -1;
	}
	return 0;
}

static int find_entity_node(const struct media_entity_desc *entity,
	const char *pattern, char *path, size_t path_size, char *error,
	unsigned error_size)
{
	glob_t matches = { 0 };
	dev_t expected = makedev(entity->dev.major, entity->dev.minor);

	if (glob(pattern, 0, NULL, &matches) != 0) {
		set_error(error, error_size, "no video nodes exist for %s", entity->name);
		return -1;
	}
	for (size_t index = 0U; index < matches.gl_pathc; index++) {
		struct stat status;

		if (stat(matches.gl_pathv[index], &status) == 0 &&
			S_ISCHR(status.st_mode) && status.st_rdev == expected) {
			if (snprintf(path, path_size, "%s", matches.gl_pathv[index]) < 0 ||
				strlen(matches.gl_pathv[index]) >= path_size) {
				globfree(&matches);
				set_error(error, error_size, "video path is too long");
				return -1;
			}
			globfree(&matches);
			return 0;
		}
	}
	globfree(&matches);
	set_error(error, error_size, "media entity %s has no video device", entity->name);
	return -1;
}

static int discover_graph(int *media_fd, char *csi_path, size_t csi_path_size,
	char *video_path, size_t video_path_size, char *error, unsigned error_size)
{
	glob_t matches = { 0 };

	if (glob("/dev/media*", 0, NULL, &matches) != 0) {
		set_error(error, error_size, "no media-controller nodes exist");
		return -1;
	}
	for (size_t media = 0U; media < matches.gl_pathc; media++) {
		struct media_entity_desc entities[IR_MEDIA_MAX_ENTITIES];
		unsigned count = 0U;
		int sensor = -1;
		int csi = -1;
		int capture = -1;
		int fd;

		fd = open(matches.gl_pathv[media], O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		if (enumerate_entities(fd, entities, &count, error, error_size) != 0) {
			(void)close(fd);
			continue;
		}
		for (unsigned index = 0U; index < count; index++) {
			if (name_has(entities[index].name, "ov7251"))
				sensor = (int)index;
			/* The capture child also contains the CSI entity name. */
			if (name_is(entities[index].name, "Intel IPU4 CSI-2 1"))
				csi = (int)index;
			if (name_has(entities[index].name, "CSI-2 1 capture 0"))
				capture = (int)index;
		}
		if (sensor >= 0 && csi >= 0 && capture >= 0) {
			bool sensor_csi;
			bool csi_capture;

			if (link_route_enabled(fd, entities, count, entities[sensor].id,
				entities[csi].id, &sensor_csi, error, error_size) == 0 &&
				link_route_enabled(fd, entities, count, entities[csi].id,
				entities[capture].id, &csi_capture, error, error_size) == 0 &&
				sensor_csi && csi_capture &&
				find_entity_node(&entities[csi], "/dev/v4l-subdev*", csi_path,
					csi_path_size, error, error_size) == 0 &&
				find_entity_node(&entities[capture], "/dev/video*", video_path,
					video_path_size, error, error_size) == 0) {
				*media_fd = fd;
				globfree(&matches);
				return 0;
			}
		}
		(void)close(fd);
	}
	globfree(&matches);
	set_error(error, error_size,
		"enabled OV7251 source-6 media route was not found");
	return -1;
}

static int configure_csi_format(int fd, char *error, unsigned error_size)
{
	for (unsigned pad = 0U; pad <= 1U; pad++) {
		struct v4l2_subdev_format format = { 0 };

		format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		format.pad = pad;
		format.format.width = IR_CAPTURE_WIDTH;
		format.format.height = IR_CAPTURE_HEIGHT;
		format.format.code = MEDIA_BUS_FMT_Y10_1X10;
		format.format.field = V4L2_FIELD_NONE;
		if (ioctl_retry(fd, VIDIOC_SUBDEV_S_FMT, &format) < 0) {
			set_error(error, error_size, "VIDIOC_SUBDEV_S_FMT pad %u: %s",
				pad, strerror(errno));
			return -1;
		}
		if (format.format.width != IR_CAPTURE_WIDTH ||
			format.format.height != IR_CAPTURE_HEIGHT ||
			format.format.code != MEDIA_BUS_FMT_Y10_1X10) {
			set_error(error, error_size,
				"CSI pad %u negotiated an unsupported Y10 layout (%ux%u code=0x%x)",
				pad, format.format.width, format.format.height,
				format.format.code);
			return -1;
		}
	}
	return 0;
}

static int negotiate_format(IrCapture *capture, char *error, unsigned error_size)
{
	struct v4l2_format format = { 0 };
	struct v4l2_pix_format_mplane *pixel;

	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	pixel = &format.fmt.pix_mp;
	pixel->width = IR_CAPTURE_WIDTH;
	pixel->height = IR_CAPTURE_HEIGHT;
	pixel->pixelformat = V4L2_PIX_FMT_Y10;
	pixel->field = V4L2_FIELD_NONE;
	pixel->num_planes = 1U;
	if (ioctl_retry(capture->video_fd, VIDIOC_S_FMT, &format) < 0) {
		set_error(error, error_size, "VIDIOC_S_FMT on %s: %s",
			capture->video_path, strerror(errno));
		return -1;
	}
	if (ioctl_retry(capture->video_fd, VIDIOC_G_FMT, &format) < 0) {
		set_error(error, error_size, "VIDIOC_G_FMT on %s: %s",
			capture->video_path, strerror(errno));
		return -1;
	}
	pixel = &format.fmt.pix_mp;
	if (pixel->width != IR_CAPTURE_WIDTH || pixel->height != IR_CAPTURE_HEIGHT ||
		pixel->pixelformat != V4L2_PIX_FMT_Y10 || pixel->num_planes != 1U ||
		pixel->field != V4L2_FIELD_NONE) {
		set_error(error, error_size,
			"source-6 negotiated an unsupported Y10 layout (%ux%u planes=%u field=%u)",
			pixel->width, pixel->height, pixel->num_planes, pixel->field);
		return -1;
	}
	if (pixel->plane_fmt[0].bytesperline < IR_CAPTURE_HEADER_BYTES +
		IR_CAPTURE_RAW10_BYTES || pixel->plane_fmt[0].bytesperline <
		IR_CAPTURE_MIN_STRIDE || pixel->plane_fmt[0].bytesperline -
		(IR_CAPTURE_HEADER_BYTES + IR_CAPTURE_RAW10_BYTES) > IR_MAX_ROW_PADDING) {
		set_error(error, error_size,
			"negotiated Y10 stride %u is not packed RAW10 source-6",
			pixel->plane_fmt[0].bytesperline);
		return -1;
	}
	if (pixel->plane_fmt[0].sizeimage < pixel->plane_fmt[0].bytesperline *
		IR_CAPTURE_HEIGHT) {
		set_error(error, error_size, "negotiated sizeimage is smaller than the frame");
		return -1;
	}
	capture->format = *pixel;
	capture->stats.width = pixel->width;
	capture->stats.height = pixel->height;
	capture->stats.stride = pixel->plane_fmt[0].bytesperline;
	capture->stats.sizeimage = pixel->plane_fmt[0].sizeimage;
	return 0;
}

static int map_buffers(IrCapture *capture, char *error, unsigned error_size)
{
	struct v4l2_requestbuffers request = { 0 };

	request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	request.memory = V4L2_MEMORY_MMAP;
	request.count = IR_CAPTURE_BUFFERS;
	if (ioctl_retry(capture->video_fd, VIDIOC_REQBUFS, &request) < 0 ||
		request.count < 2U) {
		set_error(error, error_size, "VIDIOC_REQBUFS: %s", strerror(errno));
		return -1;
	}
	capture->buffer_count = request.count > IR_CAPTURE_BUFFERS ?
		IR_CAPTURE_BUFFERS : request.count;
	for (unsigned index = 0U; index < capture->buffer_count; index++) {
		struct v4l2_buffer buffer = { 0 };
		struct v4l2_plane plane = { 0 };

		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buffer.memory = V4L2_MEMORY_MMAP;
		buffer.index = index;
		buffer.length = 1U;
		buffer.m.planes = &plane;
		if (ioctl_retry(capture->video_fd, VIDIOC_QUERYBUF, &buffer) < 0) {
			set_error(error, error_size, "VIDIOC_QUERYBUF %u: %s", index,
				strerror(errno));
			return -1;
		}
		capture->buffers[index].length = plane.length;
		capture->buffers[index].address = mmap(NULL, plane.length,
			PROT_READ, MAP_SHARED, capture->video_fd, plane.m.mem_offset);
		if (capture->buffers[index].address == MAP_FAILED) {
			capture->buffers[index].address = NULL;
			set_error(error, error_size, "mmap buffer %u: %s", index,
				strerror(errno));
			return -1;
		}
	}
	return 0;
}

static void unmap_buffers(IrCapture *capture)
{
	for (unsigned index = 0U; index < capture->buffer_count; index++) {
		if (capture->buffers[index].address != NULL)
			(void)munmap(capture->buffers[index].address,
				capture->buffers[index].length);
		capture->buffers[index].address = NULL;
		capture->buffers[index].length = 0U;
	}
	capture->buffer_count = 0U;
}

int ir_capture_open(IrCapture **capture, char *error, unsigned error_size)
{
	IrCapture *result;

	if (capture == NULL) {
		set_error(error, error_size, "capture output is required");
		return -1;
	}
	result = calloc(1U, sizeof(*result));
	if (result == NULL) {
		set_error(error, error_size, "out of memory creating IR capture");
		return -1;
	}
	result->media_fd = -1;
	result->csi_fd = -1;
	result->video_fd = -1;
	{
		char csi_path[64];

		if (discover_graph(&result->media_fd, csi_path, sizeof(csi_path),
			result->video_path, sizeof(result->video_path), error,
			error_size) != 0)
			goto fail;
		result->csi_fd = open(csi_path, O_RDWR | O_CLOEXEC);
		if (result->csi_fd < 0) {
			set_error(error, error_size, "open %s: %s", csi_path,
				strerror(errno));
			goto fail;
		}
		if (configure_csi_format(result->csi_fd, error, error_size) != 0)
			goto fail;
	}
	result->video_fd = open(result->video_path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
	if (result->video_fd < 0) {
		set_error(error, error_size, "open %s: %s", result->video_path,
			strerror(errno));
		goto fail;
	}
	if (negotiate_format(result, error, error_size) != 0 ||
		map_buffers(result, error, error_size) != 0)
		goto fail;
	*capture = result;
	return 0;
fail:
	ir_capture_close(result);
	return -1;
}

void ir_capture_set_stream_attempt_id(IrCapture *capture, uint64_t attempt_id)
{
	if (capture == NULL)
		return;
	capture->stream_attempt_id = attempt_id;
	capture->stats.stream_attempt_id = attempt_id;
}

void ir_capture_set_discard_error_buffers(IrCapture *capture, bool enabled)
{
	if (capture == NULL)
		return;
	capture->discard_error_buffers = enabled;
}

#ifdef SP7_CAMERA_IR_TEST
IrCapture *ir_test_capture_create(int video_fd, void *buffer,
	size_t buffer_length, unsigned width, unsigned height, unsigned stride,
	unsigned sizeimage)
{
	IrCapture *capture;

	if (buffer == NULL || buffer_length == 0U || width == 0U || height == 0U ||
		stride == 0U || sizeimage == 0U)
		return NULL;
	capture = calloc(1U, sizeof(*capture));
	if (capture == NULL)
		return NULL;
	capture->media_fd = -1;
	capture->csi_fd = -1;
	capture->video_fd = video_fd;
	capture->buffer_count = 1U;
	capture->streaming = true;
	capture->buffers[0].address = buffer;
	capture->buffers[0].length = buffer_length;
	capture->format.width = width;
	capture->format.height = height;
	capture->format.pixelformat = V4L2_PIX_FMT_Y10;
	capture->format.num_planes = 1U;
	capture->format.plane_fmt[0].bytesperline = stride;
	capture->format.plane_fmt[0].sizeimage = sizeimage;
	capture->stats.width = width;
	capture->stats.height = height;
	capture->stats.stride = stride;
	capture->stats.sizeimage = sizeimage;
	return capture;
}

void ir_test_capture_destroy(IrCapture *capture)
{
	free(capture);
}
#endif

int ir_capture_start(IrCapture *capture, char *error, unsigned error_size)
{
	uint64_t start_timestamp_ns;

	if (capture == NULL || capture->video_fd < 0) {
		set_error(error, error_size, "capture is not open");
		return -1;
	}
	start_timestamp_ns = monotonic_timestamp_ns();
	capture->stats.stream_start_monotonic_ns = start_timestamp_ns;
	capture->discard_anchor_monotonic_ns = 0U;
	capture->stats.consecutive_discards = 0U;
	for (unsigned index = 0U; index < capture->buffer_count; index++) {
		struct v4l2_buffer buffer = { 0 };
		struct v4l2_plane plane = { 0 };

		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buffer.memory = V4L2_MEMORY_MMAP;
		buffer.index = index;
		buffer.length = 1U;
		buffer.m.planes = &plane;
		if (ioctl_retry(capture->video_fd, VIDIOC_QBUF, &buffer) < 0) {
			set_error(error, error_size, "VIDIOC_QBUF %u: %s", index,
				strerror(errno));
			log_stream_event(capture, "stream_start", start_timestamp_ns,
				"qbuf-failure");
			return -1;
		}
	}
	{
		enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

		if (ioctl_retry(capture->video_fd, VIDIOC_STREAMON, &type) < 0) {
			set_error(error, error_size, "VIDIOC_STREAMON: %s", strerror(errno));
			log_stream_event(capture, "stream_start", start_timestamp_ns,
				"streamon-failure");
			return -1;
		}
	}
	capture->streaming = true;
	log_stream_event(capture, "stream_start", start_timestamp_ns, "success");
	return 0;
}

static uint32_t validate_timestamp(IrCapture *capture,
	const struct timeval *timestamp,
	char *error, unsigned error_size)
{
	if (timestamp->tv_sec == 0 && timestamp->tv_usec == 0) {
		set_error(error, error_size, "dequeued buffer has an empty timestamp");
		return IR_CAPTURE_REJECTION_TIMESTAMP_EMPTY;
	}
	if (capture->have_sequence &&
		(timestamp->tv_sec < capture->last_timestamp.tv_sec ||
		(timestamp->tv_sec == capture->last_timestamp.tv_sec &&
			timestamp->tv_usec <= capture->last_timestamp.tv_usec))) {
		set_error(error, error_size, "dequeued timestamps are not monotonic");
		return IR_CAPTURE_REJECTION_TIMESTAMP_REGRESSION;
	}
	return IR_CAPTURE_REJECTION_NONE;
}

static bool discard_limit_reached(const IrCapture *capture,
	uint64_t now_monotonic_ns)
{
	if (capture == NULL || !capture->discard_error_buffers)
		return false;
	if (capture->stats.consecutive_discards >=
		IR_CAPTURE_MAX_CONSECUTIVE_DISCARDS)
		return true;
	return capture->discard_anchor_monotonic_ns != 0U &&
		now_monotonic_ns >= capture->discard_anchor_monotonic_ns &&
		now_monotonic_ns - capture->discard_anchor_monotonic_ns >=
		IR_CAPTURE_MAX_DISCARD_WAIT_NS;
}

static int handle_capture_timeout(IrCapture *capture, IrCaptureStats *stats,
	char *error, unsigned error_size)
{
	if (discard_limit_reached(capture, monotonic_timestamp_ns())) {
		set_error(error, error_size,
			"discard continuation limit reached without a valid frame");
		capture->stats.discard_limit_errors++;
		capture->stats.last_error = IR_CAPTURE_ERROR_DISCARD_LIMIT;
		publish_stats(capture, stats);
		return -1;
	}
	capture->stats.last_error = IR_CAPTURE_ERROR_TIMEOUT;
	publish_stats(capture, stats);
	return 0;
}

int ir_decode_raw10_to_yuyv(const uint8_t *buffer, size_t buffer_size,
	unsigned width, unsigned height, unsigned stride, unsigned data_offset,
	uint8_t *yuyv, size_t yuyv_size, char *error, unsigned error_size)
{
	const size_t payload = (size_t)width * 10U / 8U;
	const size_t required = (size_t)data_offset + (size_t)(height - 1U) * stride +
		IR_CAPTURE_HEADER_BYTES + payload;

	if (buffer == NULL || yuyv == NULL || width == 0U || height == 0U ||
		(width % 4U) != 0U || stride < IR_CAPTURE_HEADER_BYTES + payload ||
		data_offset >= buffer_size || buffer_size < required ||
		yuyv_size < (size_t)width * height * 2U) {
		set_error(error, error_size, "truncated or invalid packed RAW10 buffer");
		return -1;
	}
	for (unsigned row = 0U; row < height; row++) {
		const uint8_t *line = buffer + data_offset + (size_t)row * stride;
		const uint8_t *packed = line + IR_CAPTURE_HEADER_BYTES;
		uint32_t header = (uint32_t)line[0] | ((uint32_t)line[1] << 8U) |
			((uint32_t)line[2] << 16U) | ((uint32_t)line[3] << 24U);

		if ((header & 0xffU) != IR_HEADER_WORD) {
			set_error(error, error_size,
				"invalid source-6 line header at row %u: 0x%08x", row,
				header);
			return -1;
		}
		for (unsigned group = 0U; group < width / 4U; group++) {
			const uint8_t *source = packed + (size_t)group * 5U;
			uint16_t pixel[4];
			size_t output = ((size_t)row * width + group * 4U) * 2U;

			for (unsigned component = 0U; component < 4U; component++)
				pixel[component] = (uint16_t)(((uint16_t)source[component] << 2U) |
					((source[4] >> (component * 2U)) & 0x03U));
			for (unsigned component = 0U; component < 4U; component++) {
				yuyv[output + component * 2U] = (uint8_t)(pixel[component] >> 2U);
				yuyv[output + component * 2U + 1U] = 0x80U;
			}
		}
	}
	return 0;
}

int ir_capture_next(IrCapture *capture, uint8_t *yuyv, size_t yuyv_size,
	unsigned timeout_ms, IrCaptureStats *stats, char *error, unsigned error_size)
{
	struct pollfd descriptor;
	struct v4l2_buffer buffer = { 0 };
	struct v4l2_plane plane = { 0 };
	bool can_requeue;
	int result;

	if (capture == NULL || !capture->streaming) {
		set_error(error, error_size, "capture is not streaming");
		return -1;
	}
	descriptor.fd = capture->video_fd;
	descriptor.events = POLLIN | POLLERR;
	descriptor.revents = 0;
#ifdef SP7_CAMERA_IR_TEST
	result = sp7_camera_ir_test_poll(&descriptor, 1, (int)timeout_ms);
#else
	result = poll(&descriptor, 1, (int)timeout_ms);
#endif
	if (result == 0) {
		return handle_capture_timeout(capture, stats, error, error_size);
	}
	if (result < 0) {
		if (errno == EINTR) {
			return handle_capture_timeout(capture, stats, error, error_size);
		}
		set_error(error, error_size, "poll source-6 capture: %s", strerror(errno));
		capture->stats.poll_errors++;
		capture->stats.last_error = IR_CAPTURE_ERROR_POLL;
		publish_stats(capture, stats);
		return -1;
	}
	if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0U) {
		set_error(error, error_size, "source-6 capture poll reported error");
		capture->stats.poll_errors++;
		capture->stats.last_error = IR_CAPTURE_ERROR_POLL;
		publish_stats(capture, stats);
		return -1;
	}
	buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buffer.memory = V4L2_MEMORY_MMAP;
	buffer.length = 1U;
	buffer.m.planes = &plane;
	if (ioctl_retry(capture->video_fd, VIDIOC_DQBUF, &buffer) < 0) {
		if (errno == EAGAIN) {
			return handle_capture_timeout(capture, stats, error, error_size);
		}
		set_error(error, error_size, "VIDIOC_DQBUF: %s", strerror(errno));
		capture->stats.dqbuf_errors++;
		capture->stats.last_error = IR_CAPTURE_ERROR_DQBUF;
		publish_stats(capture, stats);
		return -1;
	}
	{
		const uint32_t dequeued_index = buffer.index;
		const uint32_t dequeued_length = buffer.length;
		const uint32_t dequeued_flags = buffer.flags;
		const uint32_t dequeued_sequence = buffer.sequence;
		const struct timeval dequeued_timestamp = buffer.timestamp;
		const uint32_t dequeued_bytesused = plane.bytesused;
		const uint32_t dequeued_data_offset = plane.data_offset;
		const bool index_valid = dequeued_index < capture->buffer_count;
		const bool plane_count_valid = dequeued_length == 1U;
		const bool capacity_available = index_valid &&
			capture->buffers[dequeued_index].address != NULL &&
			capture->buffers[dequeued_index].length != 0U;
		const uint64_t capacity = capacity_available ?
			(uint64_t)capture->buffers[dequeued_index].length : 0U;
		uint32_t metadata_reasons = IR_CAPTURE_REJECTION_NONE;
		uint32_t plane_metadata_reasons = IR_CAPTURE_REJECTION_NONE;
		uint32_t timestamp_reasons;

		capture->stats.last_bytesused = dequeued_bytesused;
		capture->stats.last_data_offset = dequeued_data_offset;
		capture->stats.last_dequeued_sequence = dequeued_sequence;
		capture->stats.last_timestamp_seconds =
			(uint64_t)dequeued_timestamp.tv_sec;
		capture->stats.last_timestamp_usec =
			(uint64_t)dequeued_timestamp.tv_usec;
		capture->stats.last_rejection_index = dequeued_index;
		capture->stats.last_rejection_plane_count = dequeued_length;
		capture->stats.last_rejection_sequence = dequeued_sequence;
		capture->stats.last_rejection_flags = dequeued_flags;
		capture->stats.last_rejection_timestamp_flags =
			dequeued_flags & V4L2_BUF_FLAG_TIMESTAMP_MASK;
		capture->stats.last_rejection_timestamp_seconds =
			(uint64_t)dequeued_timestamp.tv_sec;
		capture->stats.last_rejection_timestamp_usec =
			(uint64_t)dequeued_timestamp.tv_usec;
		capture->stats.last_rejection_bytesused = dequeued_bytesused;
		capture->stats.last_rejection_data_offset = dequeued_data_offset;
		capture->stats.last_rejection_capacity = capacity;
		capture->stats.last_rejection_capacity_available = capacity_available;
		can_requeue = index_valid && plane_count_valid;
		if (!index_valid)
			metadata_reasons |= IR_CAPTURE_REJECTION_INVALID_INDEX |
				IR_CAPTURE_REJECTION_CAPACITY_UNAVAILABLE;
		if (!plane_count_valid)
			metadata_reasons |= IR_CAPTURE_REJECTION_INVALID_PLANE_COUNT;
		if ((dequeued_flags & V4L2_BUF_FLAG_ERROR) != 0U)
			metadata_reasons |= IR_CAPTURE_REJECTION_ERROR_FLAG;
		if ((dequeued_flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) !=
			V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC)
			metadata_reasons |= IR_CAPTURE_REJECTION_TIMESTAMP_FLAGS;
		if (index_valid && plane_count_valid && !capacity_available)
			metadata_reasons |= IR_CAPTURE_REJECTION_CAPACITY_UNAVAILABLE;
		if (index_valid && plane_count_valid && capacity_available &&
			(dequeued_data_offset >= dequeued_bytesused ||
			dequeued_bytesused > capacity))
			plane_metadata_reasons |= IR_CAPTURE_REJECTION_INVALID_PLANE_METADATA;
		timestamp_reasons = validate_timestamp(capture, &dequeued_timestamp,
			error, error_size);
		if (metadata_reasons != IR_CAPTURE_REJECTION_NONE) {
			prepare_rejection(capture, metadata_reasons |
				plane_metadata_reasons | timestamp_reasons);
			set_error(error, error_size,
				"dequeued source-6 buffer metadata/timestamp flags are invalid");
			capture->stats.metadata_errors++;
			capture->stats.last_error = IR_CAPTURE_ERROR_METADATA;
			goto requeue_fail;
		}
		if (timestamp_reasons != IR_CAPTURE_REJECTION_NONE) {
			prepare_rejection(capture, timestamp_reasons |
				plane_metadata_reasons);
			capture->stats.timestamp_errors++;
			capture->stats.last_error = IR_CAPTURE_ERROR_TIMESTAMP;
			goto requeue_fail;
		}
		if (capture->have_sequence) {
			uint32_t delta = dequeued_sequence - capture->last_sequence;

			if (delta == 0U || delta > UINT32_MAX / 2U) {
				set_error(error, error_size,
					"source-6 sequence regressed/repeated: %u",
					dequeued_sequence);
				prepare_rejection(capture, IR_CAPTURE_REJECTION_SEQUENCE |
					plane_metadata_reasons);
				capture->stats.sequence_errors++;
				capture->stats.last_error = IR_CAPTURE_ERROR_SEQUENCE;
				goto requeue_fail;
			}
			if (delta > 1U)
				capture->stats.sequence_gaps += (uint64_t)delta - 1U;
		}
		if (plane_metadata_reasons != IR_CAPTURE_REJECTION_NONE) {
			set_error(error, error_size,
				"dequeued source-6 plane metadata is invalid");
			prepare_rejection(capture, plane_metadata_reasons);
			capture->stats.metadata_errors++;
			capture->stats.last_error = IR_CAPTURE_ERROR_METADATA;
			goto requeue_fail;
		}
		if (ir_decode_raw10_to_yuyv(capture->buffers[dequeued_index].address,
			dequeued_bytesused, capture->format.width, capture->format.height,
			capture->format.plane_fmt[0].bytesperline, dequeued_data_offset, yuyv,
			yuyv_size, error, error_size) != 0) {
			prepare_rejection(capture, IR_CAPTURE_REJECTION_DECODE);
			capture->stats.decode_errors++;
			capture->stats.last_error = IR_CAPTURE_ERROR_DECODE;
			goto requeue_fail;
		}
		if (ioctl_retry(capture->video_fd, VIDIOC_QBUF, &buffer) < 0) {
			set_error(error, error_size, "VIDIOC_QBUF after decode: %s",
				strerror(errno));
			capture->stats.requeue_errors++;
			capture->stats.last_error = IR_CAPTURE_ERROR_REQUEUE;
			publish_stats(capture, stats);
			return -1;
		}
		capture->last_sequence = dequeued_sequence;
		capture->last_timestamp = dequeued_timestamp;
		capture->have_sequence = true;
		capture->discard_anchor_monotonic_ns = 0U;
		capture->stats.last_sequence = dequeued_sequence;
		capture->stats.data_offset = dequeued_data_offset;
		capture->stats.frames++;
		capture->stats.consecutive_discards = 0U;
		publish_stats(capture, stats);
		return 1;
	}

requeue_fail:
	capture->stats.rejected_buffers++;
	if (can_requeue) {
		if (ioctl_retry(capture->video_fd, VIDIOC_QBUF, &buffer) < 0) {
			char previous_error[256];

			(void)snprintf(previous_error, sizeof(previous_error), "%s",
				error != NULL ? error : "invalid source-6 buffer");
			set_error(error, error_size, "%s; VIDIOC_QBUF recovery: %s",
				previous_error, strerror(errno));
			capture->stats.last_rejection_reasons |=
				IR_CAPTURE_REJECTION_REQUEUE;
			capture->stats.last_requeue_monotonic_ns =
				monotonic_timestamp_ns();
			capture->stats.last_requeue_result = IR_CAPTURE_REQUEUE_FAILED;
			capture->stats.requeue_errors++;
			capture->stats.last_error = IR_CAPTURE_ERROR_REQUEUE;
		} else {
			capture->stats.last_requeue_monotonic_ns =
				monotonic_timestamp_ns();
			capture->stats.last_requeue_result = IR_CAPTURE_REQUEUE_SUCCEEDED;
		}
	}
	if (capture->discard_error_buffers &&
		capture->stats.last_rejection_reasons == IR_CAPTURE_REJECTION_ERROR_FLAG &&
		capture->stats.last_requeue_result == IR_CAPTURE_REQUEUE_SUCCEEDED) {
		uint64_t now_monotonic_ns = capture->stats.last_requeue_monotonic_ns;

		if (capture->discard_anchor_monotonic_ns == 0U)
			capture->discard_anchor_monotonic_ns = now_monotonic_ns;
		if (discard_limit_reached(capture, now_monotonic_ns)) {
			set_error(error, error_size,
				"discard continuation limit reached without a valid frame");
			capture->stats.discard_limit_errors++;
			capture->stats.last_error = IR_CAPTURE_ERROR_DISCARD_LIMIT;
		} else {
			capture->stats.discarded_buffers++;
			capture->stats.consecutive_discards++;
			log_rejection(capture);
			publish_stats(capture, stats);
			return IR_CAPTURE_RESULT_DISCARDED;
		}
	}
	log_rejection(capture);
	publish_stats(capture, stats);
	return -1;
}

int ir_capture_stop(IrCapture *capture, char *error, unsigned error_size)
{
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	uint64_t stop_timestamp_ns;

	if (capture == NULL || !capture->streaming)
		return 0;
	stop_timestamp_ns = monotonic_timestamp_ns();
	capture->stats.stream_stop_monotonic_ns = stop_timestamp_ns;
	if (ioctl_retry(capture->video_fd, VIDIOC_STREAMOFF, &type) < 0) {
		set_error(error, error_size, "VIDIOC_STREAMOFF: %s", strerror(errno));
		log_stream_event(capture, "stream_stop", stop_timestamp_ns,
			"streamoff-failure");
		return -1;
	}
	capture->streaming = false;
	log_stream_event(capture, "stream_stop", stop_timestamp_ns, "success");
	return 0;
}

void ir_capture_close(IrCapture *capture)
{
	if (capture == NULL)
		return;
	if (capture->streaming) {
		enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		uint64_t stop_timestamp_ns = monotonic_timestamp_ns();

		capture->stats.stream_stop_monotonic_ns = stop_timestamp_ns;
		if (ioctl_retry(capture->video_fd, VIDIOC_STREAMOFF, &type) < 0)
			log_stream_event(capture, "stream_stop", stop_timestamp_ns,
				"close-streamoff-failure");
		else
			log_stream_event(capture, "stream_stop", stop_timestamp_ns,
				"close-streamoff");
	}
	if (capture->video_fd >= 0)
		(void)close(capture->video_fd);
	if (capture->csi_fd >= 0)
		(void)close(capture->csi_fd);
	unmap_buffers(capture);
	if (capture->media_fd >= 0)
		(void)close(capture->media_fd);
	free(capture);
}
