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
#define RECOVERY_DELAY_SECONDS 1U

static volatile sig_atomic_t stop_requested;

typedef struct {
	uint64_t timeouts;
	uint64_t malformed;
	uint64_t recoveries;
	uint64_t sequence_gaps;
	uint64_t rejected_buffers;
	uint64_t cleanup_failures;
} QualificationTotals;

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

static void accumulate_capture_stats(QualificationTotals *totals,
	const IrCaptureStats *stats)
{
	if (totals == NULL || stats == NULL)
		return;
	totals->sequence_gaps += stats->sequence_gaps;
	totals->rejected_buffers += stats->rejected_buffers;
}

static void print_progress(const char *label, double elapsed, uint64_t frames,
	uint64_t changed, const QualificationTotals *totals,
	const IrCaptureStats *stats)
{
	double fps = elapsed > 0.0 ? (double)frames / elapsed : 0.0;

	printf("progress label=%s elapsed=%.3f frames=%" PRIu64
		" fps=%.3f changed=%" PRIu64 " timeouts=%" PRIu64
		" malformed=%" PRIu64 " recoveries=%" PRIu64
		" sequence_gaps=%" PRIu64 " rejected_buffers=%" PRIu64
		" cleanup_failures=%" PRIu64
		" last_sequence=%" PRIu32 " stride=%u data_offset=%u\n",
		label, elapsed, frames, fps, changed, totals->timeouts,
		totals->malformed, totals->recoveries, totals->sequence_gaps,
		totals->rejected_buffers, totals->cleanup_failures, stats->last_sequence,
		stats->stride, stats->last_data_offset);
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

static int close_capture(IrCapture **capture, IrCaptureStats *stats,
	QualificationTotals *totals, char *error, unsigned error_size)
{
	int result = 0;

	if (*capture == NULL)
		return 0;
	accumulate_capture_stats(totals, stats);
	if (ir_capture_stop(*capture, error, error_size) != 0) {
		if (totals != NULL)
			totals->cleanup_failures++;
		fprintf(stderr, "cleanup_error operation=STREAMOFF error=%s\n", error);
		result = -1;
	}
	ir_capture_close(*capture);
	*capture = NULL;
	return result;
}

static int run_persistent(unsigned duration_seconds, bool baseline,
	QualificationTotals *totals)
{
	uint8_t frame[IR_CAPTURE_OUTPUT_BYTES];
	IrCapture *capture = NULL;
	IrCaptureStats stats = { 0 };
	char error[256];
	double started = monotonic_seconds();
	double next_report = started;
	uint64_t frames = 0U;
	uint64_t changed = 0U;
	uint64_t previous_hash = 0U;
	bool have_previous = false;

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
			if (frames == 1U || frames % 100U == 0U)
				printf("frame frames=%" PRIu64 " hash=0x%016" PRIx64
					" luma_min=%u luma_max=%u bytesused=%u data_offset=%u"
					" sequence=%" PRIu32 " timestamp=%" PRIu64 ".%06" PRIu64 "\n",
					frames, hash, minimum, maximum, stats.last_bytesused,
					stats.last_data_offset, stats.last_dequeued_sequence,
					stats.last_timestamp_seconds, stats.last_timestamp_usec);
		} else if (result == 0) {
			totals->timeouts++;
			fprintf(stderr, "capture_timeout elapsed=%.3f\n",
				monotonic_seconds() - started);
			if (!baseline)
				break;
		} else {
			totals->malformed++;
			totals->recoveries++;
			fprintf(stderr, "capture_error elapsed=%.3f recovery=%" PRIu64
				" malformed=%" PRIu64 " error=%s\n",
				monotonic_seconds() - started, totals->recoveries,
				totals->malformed, error);
			if (close_capture(&capture, &stats, totals, error,
				sizeof(error)) != 0 && !baseline)
				break;
			if (!baseline)
				break;
			sleep(RECOVERY_DELAY_SECONDS);
		}
		if (monotonic_seconds() >= next_report) {
			print_progress("persistent", monotonic_seconds() - started,
				frames, changed, totals, &stats);
			next_report += 10.0;
		}
	}
	(void)close_capture(&capture, &stats, totals, error, sizeof(error));
	{
		double elapsed = monotonic_seconds() - started;
		bool passed = !stop_requested && elapsed >= (double)duration_seconds &&
			frames > 0U && changed > 0U && totals->timeouts == 0U &&
			totals->malformed == 0U && totals->recoveries == 0U &&
			totals->sequence_gaps == 0U &&
			totals->rejected_buffers == 0U &&
			totals->cleanup_failures == 0U;

		printf("persistent_summary requested_duration_seconds=%u"
			" actual_duration_seconds=%.3f frames=%" PRIu64
			" fps=%.3f changed=%" PRIu64 " timeouts=%" PRIu64
			" malformed=%" PRIu64 " recoveries=%" PRIu64
			" sequence_gaps=%" PRIu64 " rejected_buffers=%" PRIu64
			" cleanup_failures=%" PRIu64 " gate_result=%s result=%s"
			" stability_pass=%s\n", duration_seconds, elapsed, frames,
			elapsed > 0.0 ? (double)frames / elapsed : 0.0, changed,
			totals->timeouts, totals->malformed, totals->recoveries,
			totals->sequence_gaps, totals->rejected_buffers,
			totals->cleanup_failures, passed ? "PASS" : "FAIL",
			baseline ? "BASELINE" : (passed ? "PASS" : "FAIL"),
			baseline ? "NO" : (passed ? "YES" : "NO"));
		return passed ? 0 : 1;
	}
}

static int run_cycles(unsigned cycles, unsigned frames_per_cycle, bool baseline,
	QualificationTotals *totals)
{
	uint8_t frame[IR_CAPTURE_OUTPUT_BYTES];
	unsigned passed = 0U;
	unsigned failed = 0U;
	unsigned attempted = 0U;
	uint64_t total_frames = 0U;

	for (unsigned cycle = 1U; cycle <= cycles && !stop_requested &&
		(baseline || failed == 0U); cycle++) {
		IrCapture *capture = NULL;
		IrCaptureStats stats = { 0 };
		char error[256];
		unsigned frames = 0U;
		bool cycle_ok = true;
		uint64_t cycle_cleanup_failures_before = totals->cleanup_failures;
		uint64_t cycle_sequence_gaps_before = totals->sequence_gaps;
		uint64_t cycle_rejected_buffers_before = totals->rejected_buffers;
		uint64_t cycle_timeouts_before = totals->timeouts;
		uint64_t cycle_malformed_before = totals->malformed;

		while (frames < frames_per_cycle && !stop_requested) {
			int result = capture_one_frame(&capture, frame, &stats, error,
				sizeof(error));

			if (result > 0) {
				frames++;
				continue;
			}
			if (result == 0) {
				totals->timeouts++;
				cycle_ok = false;
				fprintf(stderr, "cycle=%u capture_timeout\n", cycle);
				break;
			}
			totals->malformed++;
			cycle_ok = false;
			fprintf(stderr, "cycle=%u capture_error=%s\n", cycle, error);
			break;
		}
		if (close_capture(&capture, &stats, totals, error, sizeof(error)) != 0)
			cycle_ok = false;
		if (totals->sequence_gaps != cycle_sequence_gaps_before ||
			totals->rejected_buffers != cycle_rejected_buffers_before)
			cycle_ok = false;
		attempted++;
		total_frames += frames;
		if (cycle_ok && frames == frames_per_cycle)
			passed++;
		else
			failed++;
		printf("cycle=%u requested_frames=%u actual_frames=%u frames=%u"
			" timeouts=%" PRIu64
			" malformed=%" PRIu64 " sequence_gaps=%" PRIu64
			" rejected_buffers=%" PRIu64 " cleanup_failures=%" PRIu64
			" cumulative_timeouts=%" PRIu64 " cumulative_malformed=%" PRIu64
			" cumulative_sequence_gaps=%" PRIu64
			" cumulative_rejected_buffers=%" PRIu64
			" cumulative_cleanup_failures=%" PRIu64 " result=%s\n", cycle,
			frames_per_cycle, frames, frames, totals->timeouts - cycle_timeouts_before,
			totals->malformed - cycle_malformed_before,
			totals->sequence_gaps - cycle_sequence_gaps_before,
			totals->rejected_buffers - cycle_rejected_buffers_before,
			totals->cleanup_failures - cycle_cleanup_failures_before,
			totals->timeouts, totals->malformed, totals->sequence_gaps,
			totals->rejected_buffers, totals->cleanup_failures,
			cycle_ok && frames == frames_per_cycle ? "PASS" : "FAIL");
		fflush(stdout);
	}
	{
		bool passed_gate = !stop_requested && failed == 0U && passed == cycles;
		printf("cycles_summary requested=%u attempted=%u passed=%u failed=%u"
			" frames=%" PRIu64 " timeouts=%" PRIu64
			" malformed=%" PRIu64 " sequence_gaps=%" PRIu64
			" rejected_buffers=%" PRIu64 " cleanup_failures=%" PRIu64
			" gate_result=%s result=%s stability_pass=%s\n", cycles,
			attempted, passed, failed, total_frames, totals->timeouts,
			totals->malformed, totals->sequence_gaps, totals->rejected_buffers,
			totals->cleanup_failures, passed_gate ? "PASS" : "FAIL",
			baseline ? "BASELINE" : (passed_gate ? "PASS" : "FAIL"),
			baseline ? "NO" : (passed_gate ? "YES" : "NO"));
		return passed_gate ? 0 : 1;
	}
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
	bool baseline = false;

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
		} else if (strcmp(argv[index], "--baseline") == 0) {
			baseline = true;
		} else {
			fprintf(stderr, "usage: %s [--duration seconds] [--cycles count] "
				"[--cycle-frames count] [--baseline]\n", argv[0]);
			return EXIT_FAILURE;
		}
	}
	if (duration == 0U || cycles == 0U || cycle_frames == 0U) {
		fprintf(stderr, "duration, cycles, and cycle-frames must be nonzero\n");
		return EXIT_FAILURE;
	}
	(void)signal(SIGINT, request_stop);
	(void)signal(SIGTERM, request_stop);
	printf("qualification_start mode=%s requested_duration_seconds=%u"
		" requested_cycles=%u cycle_frames=%u\n", baseline ? "baseline" :
		"normal", duration, cycles, cycle_frames);
	{
		QualificationTotals persistent_totals = { 0 };
		QualificationTotals cycle_totals = { 0 };
		int persistent_result = run_persistent(duration, baseline,
			&persistent_totals);
		int cycles_result = EXIT_FAILURE;

		if (baseline || (persistent_result == 0 && !stop_requested))
			cycles_result = run_cycles(cycles, cycle_frames, baseline,
				&cycle_totals);
		else
			printf("cycles_summary requested=%u attempted=0 passed=0 failed=0"
				" gate_result=SKIPPED result=SKIPPED stability_pass=NO\n",
				cycles);
		printf("qualification_summary mode=%s persistent_gate=%s"
			" cycles_gate=%s cleanup_failures=%" PRIu64
			" result=%s stability_pass=%s\n", baseline ? "baseline" : "normal",
			persistent_result == 0 ? "PASS" : "FAIL",
			cycles_result == 0 ? "PASS" :
			(baseline ? "FAIL" : (persistent_result != 0 ? "SKIPPED" :
			"FAIL")),
			persistent_totals.cleanup_failures + cycle_totals.cleanup_failures,
			baseline ? "BASELINE" :
			(persistent_result == 0 && cycles_result == 0 ? "PASS" : "FAIL"),
			baseline ? "NO" :
			(persistent_result == 0 && cycles_result == 0 ? "YES" : "NO"));
		return persistent_result == 0 && cycles_result == 0 ? EXIT_SUCCESS :
			EXIT_FAILURE;
	}
}
