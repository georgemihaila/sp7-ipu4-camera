#include "media-backend.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <gst/gst.h>

static volatile sig_atomic_t stop_requested;

static void request_stop(int signal_number)
{
	(void)signal_number;
	stop_requested = 1;
}

static const char *camera_id_for(const char *camera)
{
	if (strcmp(camera, "front") == 0)
		return "\\\\_SB_.PCI0.I2C2.CAMF";
	if (strcmp(camera, "rear") == 0)
		return "\\\\_SB_.PCI0.I2C3.CAMR";
	return NULL;
}

static void usage(const char *program)
{
	fprintf(stderr,
		"usage: %s --camera front|rear --device /dev/videoN "
		"[--seconds N] [--cycles N]\n", program);
}

int main(int argc, char **argv)
{
	const char *camera = NULL;
	const char *device = NULL;
	unsigned seconds = 10U;
	unsigned cycles = 1U;
	MediaFormat format;
	char error[512];

	for (int index = 1; index < argc; index++) {
		if (strcmp(argv[index], "--camera") == 0 && index + 1 < argc)
			camera = argv[++index];
		else if (strcmp(argv[index], "--device") == 0 && index + 1 < argc)
			device = argv[++index];
		else if (strcmp(argv[index], "--seconds") == 0 && index + 1 < argc)
			seconds = (unsigned)strtoul(argv[++index], NULL, 10);
		else if (strcmp(argv[index], "--cycles") == 0 && index + 1 < argc)
			cycles = (unsigned)strtoul(argv[++index], NULL, 10);
		else {
			usage(argv[0]);
			return 2;
		}
	}
	if (camera_id_for(camera != NULL ? camera : "") == NULL ||
		device == NULL || seconds == 0U || cycles == 0U) {
		usage(argv[0]);
		return 2;
	}
	if (media_backend_query_format(device, &format, error, sizeof(error)) < 0) {
		fprintf(stderr, "format query failed: %s\n", error);
		return 1;
	}
	printf("camera=%s device=%s format=%s\n", camera, device,
		format == MEDIA_FORMAT_YUYV ? "YUYV" : "MJPEG");

	if (signal(SIGINT, request_stop) == SIG_ERR ||
		signal(SIGTERM, request_stop) == SIG_ERR) {
		perror("signal");
		return 1;
	}
	gst_init(NULL, NULL);
	for (unsigned cycle = 0U; cycle < cycles && !stop_requested; cycle++) {
		MediaBackendConfig config = {
			.camera_id = camera_id_for(camera),
			.device = device,
			.format = format,
			.kind = MEDIA_PIPELINE_CAMERA,
		};
		MediaBackend *backend = media_backend_new();
		gint64 deadline;

		if (backend == NULL) {
			fprintf(stderr, "could not allocate media backend\n");
			return 1;
		}
		printf("cycle=%u starting\n", cycle + 1U);
		if (media_backend_start(backend, &config, error, sizeof(error)) < 0) {
			fprintf(stderr, "startup failed: %s\n", error);
			media_backend_free(backend);
			return 1;
		}
		printf("cycle=%u streaming\n", cycle + 1U);
		deadline = g_get_monotonic_time() + (gint64)seconds * G_USEC_PER_SEC;
		while (!stop_requested && g_get_monotonic_time() < deadline) {
			int poll_result = media_backend_poll(backend, 100U, error,
				sizeof(error));

			if (poll_result != 0) {
				if (poll_result > 0)
					(void)snprintf(error, sizeof(error),
						"pipeline reached EOS while streaming");
				fprintf(stderr, "stream failed: %s\n", error);
				(void)media_backend_stop(backend, error, sizeof(error));
				media_backend_free(backend);
				return 1;
			}
		}
		printf("cycle=%u stopping\n", cycle + 1U);
		if (media_backend_stop(backend, error, sizeof(error)) < 0) {
			fprintf(stderr, "shutdown failed: %s\n", error);
			media_backend_free(backend);
			return 1;
		}
		media_backend_free(backend);
	}
	return 0;
}
