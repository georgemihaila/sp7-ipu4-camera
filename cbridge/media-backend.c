#include "media-backend.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <gst/gst.h>

#define VIDEO_WIDTH 1280U
#define VIDEO_HEIGHT 720U
#define START_TIMEOUT_US (10 * G_USEC_PER_SEC)
#define STOP_TIMEOUT_US (5 * G_USEC_PER_SEC)

struct MediaBackend {
	GstElement *pipeline;
	GstBus *bus;
	bool running;
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
		set_error(error, error_size, "VIDIOC_G_FMT %s: %s", device,
			strerror(errno));
		(void)close(fd);
		return -1;
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
		caps = gst_caps_new_simple(media_type,
			"format", G_TYPE_STRING, format,
			"width", G_TYPE_INT, (gint)VIDEO_WIDTH,
			"height", G_TYPE_INT, (gint)VIDEO_HEIGHT,
			"framerate", GST_TYPE_FRACTION, 30, 1,
			NULL);
	} else {
		caps = gst_caps_new_simple(media_type,
			"width", G_TYPE_INT, (gint)VIDEO_WIDTH,
			"height", G_TYPE_INT, (gint)VIDEO_HEIGHT,
			"framerate", GST_TYPE_FRACTION, 30, 1,
			NULL);
	}

	if (caps == NULL)
		set_error(error, error_size, "could not allocate GStreamer caps");
	return caps;
}

static GstElement *build_pipeline(const MediaBackendConfig *config,
	char *error, unsigned error_size)
{
	GstElement *pipeline;
	GstElement *source;
	GstElement *source_caps_filter;
	GstElement *convert;
	GstElement *scale;
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
	source = make_element(pipeline,
		config->kind == MEDIA_PIPELINE_CAMERA ? "libcamerasrc" : "videotestsrc",
		config->kind == MEDIA_PIPELINE_CAMERA ? "camera-source" : "filler-source",
		error, error_size);
	source_caps_filter = NULL;
	convert = NULL;
	scale = NULL;
	if (source == NULL)
		goto fail;
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
		caps = video_caps("video/x-raw", NULL, error, error_size);
		if (caps == NULL)
			goto fail;
		g_object_set(source_caps_filter, "caps", caps, NULL);
		gst_caps_unref(caps);
	} else {
		g_object_set(source, "is-live", TRUE, "pattern", 2, NULL);
	}

	if (config->format == MEDIA_FORMAT_MJPEG) {
		caps_filter = make_element(pipeline, "capsfilter", "raw-caps", error,
			error_size);
		jpegenc = make_element(pipeline, "jpegenc", "jpeg-encoder", error,
			error_size);
		jpegparse = make_element(pipeline, "jpegparse", "jpeg-parser", error,
			error_size);
		sink = make_element(pipeline,
			config->kind == MEDIA_PIPELINE_CAMERA ? "filesink" : "v4l2sink",
			"loopback-sink", error, error_size);
		if (caps_filter == NULL || jpegenc == NULL || jpegparse == NULL ||
			sink == NULL)
			goto fail;
		caps = video_caps("video/x-raw", "I420", error, error_size);
		if (caps == NULL)
			goto fail;
		g_object_set(caps_filter, "caps", caps, NULL);
		gst_caps_unref(caps);
		g_object_set(jpegenc, "quality", 85, NULL);
		caps = video_caps("image/jpeg", NULL, error, error_size);
		if (caps == NULL)
			goto fail;
		gst_caps_set_simple(caps, "parsed", G_TYPE_BOOLEAN, TRUE, NULL);
		/* The compressed sink path deliberately avoids v4l2sink renegotiation. */
		if (config->kind == MEDIA_PIPELINE_CAMERA)
			g_object_set(sink, "location", config->device, NULL);
		else
			g_object_set(sink, "device", config->device, "sync", FALSE, NULL);
		gst_caps_unref(caps);
		if ((config->kind == MEDIA_PIPELINE_CAMERA &&
			!gst_element_link_many(source, source_caps_filter, convert, scale,
				caps_filter, jpegenc, jpegparse, sink, NULL)) ||
			(config->kind == MEDIA_PIPELINE_FILLER &&
			!gst_element_link_many(source, caps_filter, jpegenc, jpegparse, sink,
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
	if (backend->pipeline != NULL && backend->bus != NULL)
		(void)media_backend_stop(backend, ignored_error, sizeof(ignored_error));
	else if (backend->pipeline != NULL)
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

	if (backend == NULL || backend->pipeline == NULL)
		return 0;
	backend->running = false;
	(void)gst_element_set_state(backend->pipeline, GST_STATE_NULL);
	deadline = g_get_monotonic_time() + STOP_TIMEOUT_US;
	while (g_get_monotonic_time() < deadline) {
		GstMessage *message = gst_bus_timed_pop_filtered(backend->bus,
			100 * GST_MSECOND, GST_MESSAGE_ERROR | GST_MESSAGE_WARNING |
			GST_MESSAGE_STATE_CHANGED);

		if (message != NULL) {
			int result = handle_message(backend, message, error, error_size);
			gst_message_unref(message);
			if (result < 0)
				return -1;
		}
		(void)gst_element_get_state(backend->pipeline, &state, NULL, 0);
		if (state == GST_STATE_NULL)
			return 0;
	}
	set_error(error, error_size, "pipeline did not reach NULL within %u seconds",
		(unsigned)(STOP_TIMEOUT_US / G_USEC_PER_SEC));
	return -1;
}
