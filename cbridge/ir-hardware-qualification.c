#include "ir-v4l2.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_DURATION_SECONDS 600U
#define DEFAULT_CYCLES 20U
#define DEFAULT_CYCLE_FRAMES 5U
#define CAPTURE_TIMEOUT_MS 1000U
#define MAX_RECOVERIES 5U
#define RECOVERY_DELAY_SECONDS 1U

static volatile sig_atomic_t stop_requested;

static void request_stop(int signal_number)
{
	(void)signal_number;
	stop_requested = 1;
}

static double monotonic_seconds(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0.0;
	return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static uint64_t frame_hash_and_range(const uint8_t *frame, size_t size,
	uint8_t *minimum, uint8_t *maximum)
{
	uint64_t hash = UINT64_C(1469598103934665603);
	uint8_t min_value = UINT8_MAX;
	uint8_t max_value = 0U;

	for (size_t index = 0U; index < size; index += 2U) {
		uint8_t value = frame[index];

		if (value < min_value)
			min_value = value;
		if (value > max_value)
			max_value = value;
		hash ^= value;
		hash *= UINT64_C(1099511628211);
	}
	*minimum = min_value;
	*maximum = max_value;
	return hash;
}

static void print_progress(const char *label, double elapsed, uint64_t frames,
	uint64_t changed, uint64_t timeouts, uint64_t malformed,
	uint64_t recoveries, const IrCaptureStats *stats)
{
	double fps = elapsed > 0.0 ? (double)frames / elapsed : 0.0;

	printf("progress label=%s elapsed=%.3f frames=%" PRIu64
		" fps=%.3f changed=%" PRIu64 " timeouts=%" PRIu64
		" malformed=%" PRIu64 " recoveries=%" PRIu64
		" sequence_gaps=%" PRIu64 " rejected_buffers=%" PRIu64
		" last_sequence=%" PRIu32 " stride=%u data_offset=%u\n",
		label, elapsed, frames, fps, changed, timeouts, malformed,
		recoveries, stats->sequence_gaps, stats->rejected_buffers,
		stats->last_sequence, stats->stride, stats->last_data_offset);
	fflush(stdout);
}

static int capture_one_frame(IrCapture **capture, uint8_t *frame,
	IrCaptureStats *stats, char *error, unsigned error_size)
{
	if (*capture == NULL) {
		if (ir_capture_open(capture, error, error_size) != 0)
			return -1;
		if (ir_capture_start(*capture, error, error_size) != 0) {
			ir_capture_close(*capture);
			*capture = NULL;
			return -1;
		}
	}
	return ir_capture_next(*capture, frame, IR_CAPTURE_OUTPUT_BYTES,
		CAPTURE_TIMEOUT_MS, stats, error, error_size);
}

static void close_capture(IrCapture **capture, char *error, unsigned error_size)
{
	if (*capture == NULL)
		return;
	if (ir_capture_stop(*capture, error, error_size) != 0)
		fprintf(stderr, "warning: STREAMOFF failed: %s\n", error);
	ir_capture_close(*capture);
	*capture = NULL;
}

static int run_persistent(unsigned duration_seconds)
{
	uint8_t frame[IR_CAPTURE_OUTPUT_BYTES];
	IrCapture *capture = NULL;
	IrCaptureStats stats = { 0 };
	char error[256];
	double started = monotonic_seconds();
	double next_report = started;
	uint64_t frames = 0U;
	uint64_t changed = 0U;
	uint64_t timeouts = 0U;
	uint64_t malformed = 0U;
	uint64_t recoveries = 0U;
	uint64_t previous_hash = 0U;
	bool have_previous = false;
	unsigned consecutive_recoveries = 0U;

	while (!stop_requested && monotonic_seconds() - started <
		(double)duration_seconds) {
		int result = capture_one_frame(&capture, frame, &stats, error,
			sizeof(error));

		if (result > 0) {
			uint8_t minimum;
			uint8_t maximum;
			uint64_t hash = frame_hash_and_range(frame, sizeof(frame),
				&minimum, &maximum);

			if (have_previous && hash != previous_hash)
				changed++;
			previous_hash = hash;
			have_previous = true;
			frames++;
			consecutive_recoveries = 0U;
			if (frames == 1U || frames % 100U == 0U)
				printf("frame frames=%" PRIu64 " hash=0x%016" PRIx64
					" luma_min=%u luma_max=%u bytesused=%u data_offset=%u"
					" sequence=%" PRIu32 " timestamp=%" PRIu64 ".%06" PRIu64 "\n",
					frames, hash, minimum, maximum, stats.last_bytesused,
					stats.last_data_offset, stats.last_dequeued_sequence,
					stats.last_timestamp_seconds, stats.last_timestamp_usec);
		} else if (result == 0) {
			timeouts++;
		} else {
			malformed++;
			recoveries++;
			consecutive_recoveries++;
			fprintf(stderr, "capture_error elapsed=%.3f recovery=%" PRIu64
				" malformed=%" PRIu64 " error=%s\n",
				monotonic_seconds() - started, recoveries, malformed, error);
			close_capture(&capture, error, sizeof(error));
			if (consecutive_recoveries >= MAX_RECOVERIES)
				break;
			sleep(RECOVERY_DELAY_SECONDS);
		}
		if (monotonic_seconds() >= next_report) {
			print_progress("persistent", monotonic_seconds() - started,
				frames, changed, timeouts, malformed, recoveries, &stats);
			next_report += 10.0;
		}
	}
	close_capture(&capture, error, sizeof(error));
	{
		double elapsed = monotonic_seconds() - started;
		bool passed = !stop_requested && elapsed >= (double)duration_seconds &&
			frames > 0U && changed > 0U && timeouts == 0U &&
			malformed == 0U && recoveries == 0U &&
			stats.sequence_gaps == 0U && stats.rejected_buffers == 0U;

		printf("persistent_summary duration_seconds=%.3f frames=%" PRIu64
			" fps=%.3f changed=%" PRIu64 " timeouts=%" PRIu64
			" malformed=%" PRIu64 " recoveries=%" PRIu64
			" sequence_gaps=%" PRIu64 " rejected_buffers=%" PRIu64
			" result=%s\n", elapsed, frames,
			elapsed > 0.0 ? (double)frames / elapsed : 0.0, changed,
			timeouts, malformed, recoveries, stats.sequence_gaps,
			stats.rejected_buffers, passed ? "PASS" : "FAIL");
		return passed ? 0 : 1;
	}
}

static int run_cycles(unsigned cycles, unsigned frames_per_cycle)
{
	uint8_t frame[IR_CAPTURE_OUTPUT_BYTES];
	unsigned passed = 0U;
	unsigned failed = 0U;

	for (unsigned cycle = 1U; cycle <= cycles && !stop_requested; cycle++) {
		IrCapture *capture = NULL;
		IrCaptureStats stats = { 0 };
		char error[256];
		unsigned frames = 0U;
		unsigned timeouts = 0U;
		unsigned malformed = 0U;
		bool cycle_ok = true;

		while (frames < frames_per_cycle && !stop_requested) {
			int result = capture_one_frame(&capture, frame, &stats, error,
				sizeof(error));

			if (result > 0) {
				frames++;
				continue;
			}
			if (result == 0) {
				timeouts++;
				cycle_ok = false;
				break;
			}
			malformed++;
			cycle_ok = false;
			fprintf(stderr, "cycle=%u capture_error=%s\n", cycle, error);
			break;
		}
		if (stats.sequence_gaps != 0U || stats.rejected_buffers != 0U)
			cycle_ok = false;
		close_capture(&capture, error, sizeof(error));
		if (cycle_ok && frames == frames_per_cycle)
			passed++;
		else
			failed++;
		printf("cycle=%u frames=%u timeouts=%u malformed=%u sequence_gaps=%" PRIu64
			" rejected_buffers=%" PRIu64 " result=%s\n", cycle, frames,
			timeouts, malformed, stats.sequence_gaps, stats.rejected_buffers,
			cycle_ok && frames == frames_per_cycle ? "PASS" : "FAIL");
		fflush(stdout);
	}
	printf("cycles_summary requested=%u passed=%u failed=%u result=%s\n",
		cycles, passed, failed, failed == 0U && passed == cycles ? "PASS" :
		"FAIL");
	return failed == 0U && passed == cycles ? 0 : 1;
}

static int parse_unsigned(const char *value, unsigned *result)
{
	char *end = NULL;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(value, &end, 10);
	if (errno != 0 || end == value || *end != '\0' || parsed > UINT_MAX)
		return -1;
	*result = (unsigned)parsed;
	return 0;
}

int main(int argc, char **argv)
{
	unsigned duration = DEFAULT_DURATION_SECONDS;
	unsigned cycles = DEFAULT_CYCLES;
	unsigned cycle_frames = DEFAULT_CYCLE_FRAMES;

	for (int index = 1; index < argc; index++) {
		if (strcmp(argv[index], "--duration") == 0 && index + 1 < argc) {
			if (parse_unsigned(argv[++index], &duration) != 0)
				return EXIT_FAILURE;
		} else if (strcmp(argv[index], "--cycles") == 0 && index + 1 < argc) {
			if (parse_unsigned(argv[++index], &cycles) != 0)
				return EXIT_FAILURE;
		} else if (strcmp(argv[index], "--cycle-frames") == 0 &&
			index + 1 < argc) {
			if (parse_unsigned(argv[++index], &cycle_frames) != 0)
				return EXIT_FAILURE;
		} else {
			fprintf(stderr, "usage: %s [--duration seconds] [--cycles count] "
				"[--cycle-frames count]\n", argv[0]);
			return EXIT_FAILURE;
		}
	}
	(void)signal(SIGINT, request_stop);
	(void)signal(SIGTERM, request_stop);
	printf("qualification_start duration=%u cycles=%u cycle_frames=%u\n",
		duration, cycles, cycle_frames);
	if (run_persistent(duration) != 0 || stop_requested)
		return EXIT_FAILURE;
	return run_cycles(cycles, cycle_frames) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
