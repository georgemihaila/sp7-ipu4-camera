#ifndef SP7_CAMERA_CONTROLLER_H
#define SP7_CAMERA_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "media-backend.h"

typedef enum {
	CAMERA_FRONT = 0,
	CAMERA_REAR = 1,
	CAMERA_COUNT = 2,
	CAMERA_NONE = -1,
} CameraKey;

typedef enum {
	CONTROLLER_IDLE,
	CONTROLLER_STARTING,
	CONTROLLER_STREAMING,
	CONTROLLER_STOPPING,
	CONTROLLER_RETRY_WAIT,
} ControllerState;

typedef enum {
	WIREPLUMBER_ALREADY_INACTIVE,
	WIREPLUMBER_STOPPED,
	WIREPLUMBER_STOP_FAILED,
} WirePlumberStopResult;

typedef struct {
	void *context;
	uint64_t (*now_ms)(void *context);
	unsigned (*consumer_mask)(void *context);
	int (*query_format)(void *context, CameraKey camera, MediaFormat *format,
		char *error, unsigned error_size);
	int (*backend_start)(void *context, CameraKey camera, bool filler,
		MediaFormat format, void **handle, char *error, unsigned error_size);
	int (*backend_poll)(void *context, void *handle, char *error,
		unsigned error_size);
	int (*backend_stop)(void *context, void *handle, char *error,
		unsigned error_size);
	void (*backend_free)(void *context, void *handle);
	WirePlumberStopResult (*wireplumber_stop)(void *context, char *error,
		unsigned error_size);
	int (*wireplumber_start)(void *context, char *error, unsigned error_size);
} ControllerOps;

typedef struct CameraController CameraController;

CameraController *camera_controller_new(const ControllerOps *ops,
	char *error, unsigned error_size);
int camera_controller_tick(CameraController *controller);
int camera_controller_shutdown(CameraController *controller, char *error,
	unsigned error_size);
void camera_controller_free(CameraController *controller);
ControllerState camera_controller_state(const CameraController *controller);
CameraKey camera_controller_active(const CameraController *controller);
bool camera_controller_owns_wireplumber(const CameraController *controller);

#endif
