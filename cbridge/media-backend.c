#include "media-backend.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/base/gstpushsrc.h>

#define VIDEO_WIDTH 1280U
#define VIDEO_HEIGHT 720U
#define START_TIMEOUT_US (10 * G_USEC_PER_SEC)
#define STOP_TIMEOUT_US (5 * G_USEC_PER_SEC)
#define FRAME_DURATION (GST_SECOND / 30)

struct MediaBackend {
	GstElement *pipeline;
	GstBus *bus;
	bool running;
	bool stopped;
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

static int ioctl_retry(int fd, unsigned long request, void *argument)
{
	int result;

	do {
		result = ioctl(fd, request, argument);
	} while (result < 0 && errno == EINTR);
	return result;
}

int media_backend_query_format(const char *device, MediaFormat *format,
	char *error, unsigned error_size)
{
	struct v4l2_format v4l2_format = { 0 };
	int capture_errno;
	int output_errno;
	int fd;

	if (device == NULL || format == NULL) {
		set_error(error, error_size, "device and output format are required");
		return -1;
	}
	fd = open(device, O_RDWR | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0) {
		set_error(error, error_size, "open %s: %s", device, strerror(errno));
		return -1;
	}
	v4l2_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl_retry(fd, VIDIOC_G_FMT, &v4l2_format) < 0) {
		capture_errno = errno;
		/* v4l2loopback with exclusive_caps=1 exposes only OUTPUT until a
		 * producer opens it. */
		v4l2_format = (struct v4l2_format){ 0 };
		v4l2_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		if (ioctl_retry(fd, VIDIOC_G_FMT, &v4l2_format) < 0) {
			output_errno = errno;
			set_error(error, error_size,
				"VIDIOC_G_FMT capture %s: %s; VIDEO_OUTPUT fallback %s: %s",
				device, strerror(capture_errno), device,
				strerror(output_errno));
			(void)close(fd);
			return -1;
		}
	}
	(void)close(fd);
	if (v4l2_format.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) {
		*format = MEDIA_FORMAT_YUYV;
		return 0;
	}
	if (v4l2_format.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG ||
		v4l2_format.fmt.pix.pixelformat == V4L2_PIX_FMT_JPEG) {
		*format = MEDIA_FORMAT_MJPEG;
		return 0;
	}
	set_error(error, error_size, "unsupported V4L2 format 0x%08x on %s",
		v4l2_format.fmt.pix.pixelformat, device);
	return -1;
}

static GstElement *make_element(GstElement *pipeline, const char *factory,
	const char *name, char *error, unsigned error_size)
{
	GstElement *element = gst_element_factory_make(factory, name);

	if (element == NULL) {
		set_error(error, error_size, "GStreamer element is unavailable: %s", factory);
		return NULL;
	}
	if (!gst_bin_add(GST_BIN(pipeline), element)) {
		set_error(error, error_size, "could not add GStreamer element: %s", name);
		gst_object_unref(element);
		return NULL;
	}
	return element;
}

static GstCaps *video_caps(const char *media_type, const char *format,
	char *error, unsigned error_size)
{
	GstCaps *caps;

	if (format != NULL) {
		caps = gst_caps_new_simple(media_type, "format", G_TYPE_STRING, format,
			"width", G_TYPE_INT, (gint)VIDEO_WIDTH,
			"height", G_TYPE_INT, (gint)VIDEO_HEIGHT,
			"framerate", GST_TYPE_FRACTION, 30, 1, NULL);
	} else {
		caps = gst_caps_new_simple(media_type,
			"width", G_TYPE_INT, (gint)VIDEO_WIDTH,
			"height", G_TYPE_INT, (gint)VIDEO_HEIGHT,
			"framerate", GST_TYPE_FRACTION, 30, 1, NULL);
	}
	if (caps == NULL)
		set_error(error, error_size, "could not allocate GStreamer caps");
	return caps;
}

static GstCaps *camera_source_caps(const char *camera_id, char *error,
	unsigned error_size)
{
	unsigned width = 2560U;
	unsigned height = 1600U;
	GstCaps *caps;

	if (camera_id != NULL && strcmp(camera_id, "\\_SB_.PCI0.I2C3.CAMR") == 0) {
		width = VIDEO_WIDTH;
		height = VIDEO_HEIGHT;
	}
	caps = gst_caps_new_simple("video/x-raw",
		"width", G_TYPE_INT, (gint)width,
		"height", G_TYPE_INT, (gint)height,
		"framerate", GST_TYPE_FRACTION, 30, 1, NULL);
	if (caps == NULL)
		set_error(error, error_size, "could not allocate camera source caps");
	return caps;
}

typedef struct _CachedFillerSource CachedFillerSource;
typedef struct _CachedFillerSourceClass CachedFillerSourceClass;

struct _CachedFillerSource {
	GstPushSrc parent;
	GstBuffer *buffer;
	GstCaps *caps;
	GstClockTime frame_duration;
	uint64_t frame_number;
};

struct _CachedFillerSourceClass {
	GstPushSrcClass parent_class;
};

#define SP7_TYPE_CACHED_FILLER_SOURCE (sp7_cached_filler_source_get_type())
G_DEFINE_TYPE(CachedFillerSource, sp7_cached_filler_source, GST_TYPE_PUSH_SRC)

static gboolean cached_filler_start(GstBaseSrc *base_source)
{
	CachedFillerSource *source = (CachedFillerSource *)base_source;

	source->frame_number = 0U;
	return TRUE;
}

static gboolean cached_filler_stop(GstBaseSrc *base_source)
{
	(void)base_source;
	return TRUE;
}

static GstCaps *cached_filler_get_caps(GstBaseSrc *base_source,
	GstCaps *filter)
{
	CachedFillerSource *source = (CachedFillerSource *)base_source;

	if (filter == NULL)
		return gst_caps_ref(source->caps);
	return gst_caps_intersect_full(source->caps, filter,
		GST_CAPS_INTERSECT_FIRST);
}

static GstFlowReturn cached_filler_create(GstPushSrc *push_source,
	GstBuffer **buffer)
{
	CachedFillerSource *source = (CachedFillerSource *)push_source;
	GstBuffer *frame;
	GstClockTime timestamp;

	frame = gst_buffer_copy_region(source->buffer, GST_BUFFER_COPY_MEMORY, 0,
		gst_buffer_get_size(source->buffer));
	if (frame == NULL)
		return GST_FLOW_ERROR;
	timestamp = source->frame_number * source->frame_duration;
	GST_BUFFER_PTS(frame) = timestamp;
	GST_BUFFER_DTS(frame) = timestamp;
	GST_BUFFER_DURATION(frame) = source->frame_duration;
	source->frame_number++;
	*buffer = frame;
	return GST_FLOW_OK;
}

static void cached_filler_finalize(GObject *object)
{
	CachedFillerSource *source = (CachedFillerSource *)object;

	if (source->buffer != NULL)
		gst_buffer_unref(source->buffer);
	if (source->caps != NULL)
		gst_caps_unref(source->caps);
	G_OBJECT_CLASS(sp7_cached_filler_source_parent_class)->finalize(object);
}

static void sp7_cached_filler_source_class_init(CachedFillerSourceClass *klass)
{
	static GstStaticPadTemplate source_template = GST_STATIC_PAD_TEMPLATE(
		"src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GstBaseSrcClass *base_class = GST_BASE_SRC_CLASS(klass);
	GstPushSrcClass *push_class = GST_PUSH_SRC_CLASS(klass);

	gst_element_class_add_static_pad_template(GST_ELEMENT_CLASS(klass),
		&source_template);
	object_class->finalize = cached_filler_finalize;
	base_class->start = cached_filler_start;
	base_class->stop = cached_filler_stop;
	base_class->get_caps = cached_filler_get_caps;
	push_class->create = cached_filler_create;
}

static void sp7_cached_filler_source_init(CachedFillerSource *source)
{
	gst_base_src_set_live(GST_BASE_SRC(source), TRUE);
	gst_base_src_set_format(GST_BASE_SRC(source), GST_FORMAT_TIME);
	gst_base_src_set_do_timestamp(GST_BASE_SRC(source), FALSE);
}

static GstBuffer *make_black_yuyv_buffer(char *error, unsigned error_size)
{
	GstBuffer *buffer;
	GstMapInfo map;
	gsize offset;

	buffer = gst_buffer_new_allocate(NULL,
		(gsize)VIDEO_WIDTH * (gsize)VIDEO_HEIGHT * 2U, NULL);
	if (buffer == NULL) {
		set_error(error, error_size, "could not allocate cached YUYV filler");
		return NULL;
	}
	if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
		set_error(error, error_size, "could not map cached YUYV filler");
		gst_buffer_unref(buffer);
		return NULL;
	}
	for (offset = 0; offset < map.size; offset += 4U) {
		map.data[offset] = 16U;
		map.data[offset + 1U] = 128U;
		map.data[offset + 2U] = 16U;
		map.data[offset + 3U] = 128U;
	}
	gst_buffer_unmap(buffer, &map);
	return buffer;
}

static GstBuffer *make_black_mjpeg_buffer(char *error, unsigned error_size)
{
	GstElement *pipeline = NULL;
	GstElement *source = NULL;
	GstElement *caps_filter = NULL;
	GstElement *jpegenc = NULL;
	GstElement *jpegparse = NULL;
	GstElement *sink = NULL;
	GstCaps *caps = NULL;
	GstSample *sample = NULL;
	GstBuffer *buffer = NULL;
	GstBuffer *copy = NULL;

	pipeline = gst_pipeline_new("sp7-black-jpeg");
	source = gst_element_factory_make("videotestsrc", "black-source");
	caps_filter = gst_element_factory_make("capsfilter", "black-caps");
	jpegenc = gst_element_factory_make("jpegenc", "black-jpegenc");
	jpegparse = gst_element_factory_make("jpegparse", "black-jpegparse");
	sink = gst_element_factory_make("appsink", "black-sink");
	if (pipeline == NULL || source == NULL || caps_filter == NULL ||
		jpegenc == NULL || jpegparse == NULL || sink == NULL) {
		set_error(error, error_size, "could not create one-shot black JPEG pipeline");
		goto done;
	}
	caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "I420",
		"width", G_TYPE_INT, (gint)VIDEO_WIDTH,
		"height", G_TYPE_INT, (gint)VIDEO_HEIGHT,
		"framerate", GST_TYPE_FRACTION, 1, 1, NULL);
	if (caps == NULL) {
		set_error(error, error_size, "could not allocate black JPEG caps");
		goto done;
	}
	g_object_set(source, "num-buffers", 1, "pattern", 2, NULL);
	g_object_set(caps_filter, "caps", caps, NULL);
	g_object_set(jpegenc, "quality", 85, NULL);
	g_object_set(sink, "sync", FALSE, "emit-signals", FALSE,
		"wait-on-eos", FALSE, NULL);
	gst_bin_add_many(GST_BIN(pipeline), source, caps_filter, jpegenc, jpegparse,
		sink, NULL);
	if (!gst_element_link_many(source, caps_filter, jpegenc, jpegparse, sink,
		NULL)) {
		set_error(error, error_size, "could not link one-shot black JPEG pipeline");
		goto done;
	}
	if (gst_element_set_state(pipeline, GST_STATE_PLAYING) ==
		GST_STATE_CHANGE_FAILURE) {
		set_error(error, error_size, "could not start one-shot black JPEG pipeline");
		goto done;
	}
	sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink),
		10 * GST_SECOND);
	if (sample == NULL) {
		set_error(error, error_size, "one-shot black JPEG pipeline produced no frame");
		goto done;
	}
	buffer = gst_sample_get_buffer(sample);
	if (buffer == NULL) {
		set_error(error, error_size, "one-shot black JPEG sample had no buffer");
		goto done;
	}
	copy = gst_buffer_copy_deep(buffer);
	if (copy == NULL)
		set_error(error, error_size, "could not cache black JPEG frame");

done:
	if (caps != NULL)
		gst_caps_unref(caps);
	if (sample != NULL)
		gst_sample_unref(sample);
	if (pipeline != NULL) {
		(void)gst_element_set_state(pipeline, GST_STATE_NULL);
		gst_object_unref(pipeline);
	}
	return copy;
}

static GstElement *make_filler_source(MediaFormat format, char *error,
	unsigned error_size)
{
	CachedFillerSource *source;
	GstBuffer *buffer;
	GstCaps *caps;

	buffer = format == MEDIA_FORMAT_MJPEG ?
		make_black_mjpeg_buffer(error, error_size) :
		make_black_yuyv_buffer(error, error_size);
	if (buffer == NULL)
		return NULL;
	source = g_object_new(SP7_TYPE_CACHED_FILLER_SOURCE, NULL);
	if (source == NULL) {
		set_error(error, error_size, "could not allocate cached filler source");
		gst_buffer_unref(buffer);
		return NULL;
	}
	source->buffer = buffer;
	source->frame_duration = FRAME_DURATION;
	caps = format == MEDIA_FORMAT_MJPEG ?
		gst_caps_new_simple("image/jpeg", "width", G_TYPE_INT,
			(gint)VIDEO_WIDTH, "height", G_TYPE_INT, (gint)VIDEO_HEIGHT,
			"framerate", GST_TYPE_FRACTION, 30, 1,
			"parsed", G_TYPE_BOOLEAN, TRUE, NULL) :
		gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "YUY2",
			"width", G_TYPE_INT, (gint)VIDEO_WIDTH, "height", G_TYPE_INT,
			(gint)VIDEO_HEIGHT, "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
	if (caps == NULL) {
		set_error(error, error_size, "could not allocate filler caps");
		gst_object_unref(source);
		return NULL;
	}
	source->caps = caps;
	return GST_ELEMENT(source);
}

static GstElement *build_pipeline(const MediaBackendConfig *config,
	char *error, unsigned error_size)
{
	GstElement *pipeline;
	GstElement *source;
	GstElement *source_caps_filter = NULL;
	GstElement *convert = NULL;
	GstElement *scale = NULL;
	GstElement *caps_filter;
	GstElement *sink;
	GstElement *jpegenc;
	GstElement *jpegparse;
	GstCaps *caps;

	pipeline = gst_pipeline_new("sp7-camera-pipeline");
	if (pipeline == NULL) {
		set_error(error, error_size, "could not allocate GStreamer pipeline");
		return NULL;
	}
	source = config->kind == MEDIA_PIPELINE_CAMERA ?
		make_element(pipeline, "libcamerasrc", "camera-source", error,
			error_size) : make_filler_source(config->format, error, error_size);
	if (source == NULL)
		goto fail;
	if (config->kind == MEDIA_PIPELINE_FILLER &&
		!gst_bin_add(GST_BIN(pipeline), source)) {
		set_error(error, error_size, "could not add cached filler source");
		gst_object_unref(source);
		goto fail;
	}
	if (config->kind == MEDIA_PIPELINE_CAMERA) {
		source_caps_filter = make_element(pipeline, "capsfilter", "camera-caps",
			error, error_size);
		convert = make_element(pipeline, "videoconvert", "convert", error,
			error_size);
		scale = make_element(pipeline, "videoscale", "scale", error, error_size);
		if (source_caps_filter == NULL || convert == NULL || scale == NULL)
			goto fail;
		g_object_set(source, "camera-name", config->camera_id, "ae-enable", TRUE,
			NULL);
		caps = camera_source_caps(config->camera_id, error, error_size);
		if (caps == NULL)
			goto fail;
		g_object_set(source_caps_filter, "caps", caps, NULL);
		gst_caps_unref(caps);
	}

	if (config->format == MEDIA_FORMAT_MJPEG) {
		caps_filter = make_element(pipeline, "capsfilter", "output-caps", error,
			error_size);
		jpegenc = config->kind == MEDIA_PIPELINE_CAMERA ?
			make_element(pipeline, "jpegenc", "jpeg-encoder", error,
				error_size) : NULL;
		jpegparse = make_element(pipeline, "jpegparse", "jpeg-parser", error,
			error_size);
		sink = make_element(pipeline,
			config->kind == MEDIA_PIPELINE_CAMERA ? "filesink" : "v4l2sink",
			"loopback-sink", error, error_size);
		if (caps_filter == NULL || jpegparse == NULL ||
			(config->kind == MEDIA_PIPELINE_CAMERA && jpegenc == NULL) ||
			sink == NULL)
			goto fail;
		caps = config->kind == MEDIA_PIPELINE_CAMERA ?
			video_caps("video/x-raw", "I420", error, error_size) :
			video_caps("image/jpeg", NULL, error, error_size);
		if (caps == NULL)
			goto fail;
		if (config->kind == MEDIA_PIPELINE_FILLER)
			gst_caps_set_simple(caps, "parsed", G_TYPE_BOOLEAN, TRUE, NULL);
		g_object_set(caps_filter, "caps", caps, NULL);
		gst_caps_unref(caps);
		if (jpegenc != NULL)
			g_object_set(jpegenc, "quality", 85, NULL);
		if (config->kind == MEDIA_PIPELINE_CAMERA)
			g_object_set(sink, "location", config->device, NULL);
		else
			g_object_set(sink, "device", config->device, "sync", FALSE, NULL);
		if ((config->kind == MEDIA_PIPELINE_CAMERA &&
			!gst_element_link_many(source, source_caps_filter, convert, scale,
				caps_filter, jpegenc, jpegparse, sink, NULL)) ||
			(config->kind == MEDIA_PIPELINE_FILLER &&
			!gst_element_link_many(source, caps_filter, jpegparse, sink,
				NULL))) {
			set_error(error, error_size, "could not link MJPEG GStreamer pipeline");
			goto fail;
		}
	} else {
		caps_filter = make_element(pipeline, "capsfilter", "yuyv-caps", error,
			error_size);
		sink = make_element(pipeline, "v4l2sink", "loopback-sink", error,
			error_size);
		if (caps_filter == NULL || sink == NULL)
			goto fail;
		caps = video_caps("video/x-raw", "YUY2", error, error_size);
		if (caps == NULL)
			goto fail;
		g_object_set(caps_filter, "caps", caps, NULL);
		gst_caps_unref(caps);
		g_object_set(sink, "device", config->device, "sync", FALSE, NULL);
		if ((config->kind == MEDIA_PIPELINE_CAMERA &&
			!gst_element_link_many(source, source_caps_filter, convert, scale,
				caps_filter, sink, NULL)) ||
			(config->kind == MEDIA_PIPELINE_FILLER &&
			!gst_element_link_many(source, caps_filter, sink, NULL))) {
			set_error(error, error_size, "could not link YUYV GStreamer pipeline");
			goto fail;
		}
	}
	return pipeline;

fail:
	gst_object_unref(pipeline);
	return NULL;
}

static int handle_message(MediaBackend *backend, GstMessage *message,
	char *error, unsigned error_size)
{
	if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
		GError *gst_error = NULL;
		gchar *debug = NULL;

		gst_message_parse_error(message, &gst_error, &debug);
		set_error(error, error_size, "GStreamer error: %s%s%s%s",
			gst_error != NULL ? gst_error->message : "unknown",
			debug != NULL ? " (" : "", debug != NULL ? debug : "",
			debug != NULL ? ")" : "");
		if (gst_error != NULL)
			g_error_free(gst_error);
		g_free(debug);
		return -1;
	}
	if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS)
		return 1;
	if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_STATE_CHANGED &&
		GST_MESSAGE_SRC(message) == GST_OBJECT(backend->pipeline)) {
		GstState old_state;
		GstState new_state;
		GstState pending_state;

		gst_message_parse_state_changed(message, &old_state, &new_state,
			&pending_state);
		g_message("pipeline state: %s -> %s%s",
			gst_element_state_get_name(old_state),
			gst_element_state_get_name(new_state),
			pending_state != GST_STATE_VOID_PENDING ? " (pending)" : "");
	}
	if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_WARNING) {
		GError *warning = NULL;
		gchar *debug = NULL;

		gst_message_parse_warning(message, &warning, &debug);
		g_printerr("GStreamer warning: %s%s%s\n",
			warning != NULL ? warning->message : "unknown",
			debug != NULL ? " (" : "", debug != NULL ? debug : "");
		if (warning != NULL)
			g_error_free(warning);
		g_free(debug);
	}
	(void)backend;
	return 0;
}

MediaBackend *media_backend_new(void)
{
	return g_new0(MediaBackend, 1);
}

void media_backend_free(MediaBackend *backend)
{
	char ignored_error[128];

	if (backend == NULL)
		return;
	if (backend->pipeline != NULL && !backend->stopped)
		(void)media_backend_stop(backend, ignored_error, sizeof(ignored_error));
	else if (backend->pipeline != NULL && backend->bus == NULL)
		(void)gst_element_set_state(backend->pipeline, GST_STATE_NULL);
	if (backend->bus != NULL)
		gst_object_unref(backend->bus);
	if (backend->pipeline != NULL)
		gst_object_unref(backend->pipeline);
	g_free(backend);
}

int media_backend_start(MediaBackend *backend, const MediaBackendConfig *config,
	char *error, unsigned error_size)
{
	GstState state = GST_STATE_NULL;
	gint64 deadline;

	if (backend == NULL || config == NULL || config->camera_id == NULL ||
		config->device == NULL) {
		set_error(error, error_size, "incomplete media backend configuration");
		return -1;
	}
	backend->pipeline = build_pipeline(config, error, error_size);
	if (backend->pipeline == NULL)
		return -1;
	backend->stopped = false;
	backend->bus = gst_element_get_bus(backend->pipeline);
	if (backend->bus == NULL) {
		set_error(error, error_size, "could not acquire GStreamer bus");
		return -1;
	}
	if (gst_element_set_state(backend->pipeline, GST_STATE_PLAYING) ==
		GST_STATE_CHANGE_FAILURE) {
		set_error(error, error_size, "could not set GStreamer pipeline to PLAYING");
		return -1;
	}
	deadline = g_get_monotonic_time() + START_TIMEOUT_US;
	while (g_get_monotonic_time() < deadline) {
		GstMessage *message = gst_bus_timed_pop_filtered(backend->bus,
			100 * GST_MSECOND, GST_MESSAGE_ERROR | GST_MESSAGE_WARNING |
			GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ASYNC_DONE);
		int message_result = 0;

		if (message != NULL) {
			message_result = handle_message(backend, message, error, error_size);
			gst_message_unref(message);
			if (message_result < 0)
				return -1;
			if (message_result > 0) {
				set_error(error, error_size, "pipeline reached EOS during startup");
				return -1;
			}
		}
		(void)gst_element_get_state(backend->pipeline, &state, NULL, 0);
		if (state == GST_STATE_PLAYING) {
			backend->running = true;
			return 0;
		}
	}
	set_error(error, error_size, "pipeline did not reach PLAYING within %u seconds",
		(unsigned)(START_TIMEOUT_US / G_USEC_PER_SEC));
	return -1;
}

int media_backend_poll(MediaBackend *backend, unsigned timeout_ms,
	char *error, unsigned error_size)
{
	GstMessage *message;
	int result;

	if (backend == NULL || backend->bus == NULL || !backend->running) {
		set_error(error, error_size, "media backend is not running");
		return -1;
	}
	message = gst_bus_timed_pop_filtered(backend->bus,
		(GstClockTime)timeout_ms * GST_MSECOND,
		GST_MESSAGE_ERROR | GST_MESSAGE_WARNING | GST_MESSAGE_EOS);
	if (message == NULL)
		return 0;
	result = handle_message(backend, message, error, error_size);
	gst_message_unref(message);
	if (result < 0)
		return -1;
	if (result > 0) {
		backend->running = false;
		return 1;
	}
	return 0;
}

int media_backend_stop(MediaBackend *backend, char *error, unsigned error_size)
{
	GstState state = GST_STATE_PLAYING;
	gint64 deadline;

	if (backend == NULL || backend->pipeline == NULL || backend->stopped)
		return 0;
	backend->running = false;
	(void)gst_element_set_state(backend->pipeline, GST_STATE_NULL);
	deadline = g_get_monotonic_time() + STOP_TIMEOUT_US;
	while (g_get_monotonic_time() < deadline) {
		GstMessage *message = gst_bus_timed_pop_filtered(backend->bus,
			100 * GST_MSECOND, GST_MESSAGE_ERROR | GST_MESSAGE_STATE_CHANGED);

		if (message != NULL) {
			int result = handle_message(backend, message, error, error_size);
			gst_message_unref(message);
			if (result < 0)
				return -1;
		}
		(void)gst_element_get_state(backend->pipeline, &state, NULL, 0);
		if (state == GST_STATE_NULL) {
			backend->stopped = true;
			return 0;
		}
	}
	set_error(error, error_size, "pipeline did not reach NULL within %u seconds",
		(unsigned)(STOP_TIMEOUT_US / G_USEC_PER_SEC));
	return -1;
}
