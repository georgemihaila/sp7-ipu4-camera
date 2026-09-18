#ifndef SP7_CAMERA_MEDIA_BACKEND_H
#define SP7_CAMERA_MEDIA_BACKEND_H

#include <stdbool.h>

typedef enum {
	MEDIA_FORMAT_YUYV,
	MEDIA_FORMAT_MJPEG,
} MediaFormat;

typedef enum {
	MEDIA_PIPELINE_CAMERA,
	MEDIA_PIPELINE_FILLER,
} MediaPipelineKind;

typedef struct {
	const char *camera_id;
	const char *device;
	MediaFormat format;
	MediaPipelineKind kind;
} MediaBackendConfig;

typedef struct MediaBackend MediaBackend;

MediaBackend *media_backend_new(void);
void media_backend_free(MediaBackend *backend);

int media_backend_query_format(const char *device, MediaFormat *format,
	char *error, unsigned error_size);
int media_backend_start(MediaBackend *backend, const MediaBackendConfig *config,
	char *error, unsigned error_size);
int media_backend_poll(MediaBackend *backend, unsigned timeout_ms,
	char *error, unsigned error_size);
int media_backend_stop(MediaBackend *backend, char *error,
	unsigned error_size);

#endif
