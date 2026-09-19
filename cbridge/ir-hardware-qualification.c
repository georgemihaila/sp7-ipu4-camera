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
#define CAPTURE_STARTUP_ERROR (-2)

static volatile sig_atomic_t stop_requested;
static uint64_t next_stream_attempt_id = 1U;

typedef struct {
	uint64_t timeouts;
	uint64_t startup_errors;
	uint64_t malformed;
	uint64_t metadata_errors;
	uint64_t timestamp_errors;
	uint64_t sequence_errors;
	uint64_t decode_errors;
	uint64_t requeue_errors;
	uint64_t discarded_buffers;
	uint64_t discard_limit_errors;
	uint64_t poll_errors;
	uint64_t dqbuf_errors;
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

static uint64_t monotonic_timestamp_ns(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0U;
	return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
		(uint64_t)now.tv_nsec;
}

static void log_reopen_event(uint64_t after_attempt, uint64_t recovery)
{
	fprintf(stderr, "ir_diag event=reopen after_attempt=%" PRIu64
		" recovery=%" PRIu64 " monotonic_ns=%" PRIu64 "\n",
		after_attempt, recovery, monotonic_timestamp_ns());
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

static void add_capture_stats(QualificationTotals *totals,
	const IrCaptureStats *stats)
{
	if (totals == NULL || stats == NULL)
		return;
	totals->sequence_gaps += stats->sequence_gaps;
	totals->rejected_buffers += stats->rejected_buffers;
	totals->metadata_errors += stats->metadata_errors;
	totals->timestamp_errors += stats->timestamp_errors;
	totals->sequence_errors += stats->sequence_errors;
	totals->decode_errors += stats->decode_errors;
	totals->requeue_errors += stats->requeue_errors;
	totals->discarded_buffers += stats->discarded_buffers;
	totals->discard_limit_errors += stats->discard_limit_errors;
	totals->poll_errors += stats->poll_errors;
	totals->dqbuf_errors += stats->dqbuf_errors;
	totals->malformed += stats->metadata_errors + stats->timestamp_errors +
		stats->sequence_errors + stats->decode_errors;
}

static QualificationTotals totals_with_current(const QualificationTotals *totals,
	const IrCaptureStats *current)
{
	QualificationTotals result = { 0 };

	if (totals != NULL)
		result = *totals;
	if (current != NULL)
		add_capture_stats(&result, current);
	return result;
}

static const char *capture_error_name(IrCaptureError error)
{
	switch (error) {
	case IR_CAPTURE_ERROR_METADATA:
		return "metadata";
	case IR_CAPTURE_ERROR_TIMESTAMP:
		return "timestamp";
	case IR_CAPTURE_ERROR_SEQUENCE:
		return "sequence";
	case IR_CAPTURE_ERROR_DECODE:
		return "decode";
	case IR_CAPTURE_ERROR_REQUEUE:
		return "requeue";
	case IR_CAPTURE_ERROR_DISCARD_LIMIT:
		return "discard-limit";
	case IR_CAPTURE_ERROR_POLL:
		return "poll";
	case IR_CAPTURE_ERROR_DQBUF:
		return "dqbuf";
	case IR_CAPTURE_ERROR_TIMEOUT:
		return "timeout";
	case IR_CAPTURE_ERROR_NONE:
	default:
		return "unknown";
	}
}

static void print_progress(const char *label, double elapsed, uint64_t frames,
	uint64_t changed, const QualificationTotals *totals,
	const IrCaptureStats *current)
{
	QualificationTotals visible = totals_with_current(totals, current);
	IrCaptureStats empty = { 0 };
	const IrCaptureStats *stats = current != NULL ? current : &empty;
	double fps = elapsed > 0.0 ? (double)frames / elapsed : 0.0;

	printf("progress label=%s elapsed=%.3f frames=%" PRIu64
		" fps=%.3f changed=%" PRIu64 " timeouts=%" PRIu64
		" startup_errors=%" PRIu64 " malformed=%" PRIu64
		" metadata_errors=%" PRIu64 " timestamp_errors=%" PRIu64
		" sequence_errors=%" PRIu64 " decode_errors=%" PRIu64
		" requeue_errors=%" PRIu64 " discarded_buffers=%" PRIu64
		" discard_limit_errors=%" PRIu64 " poll_errors=%" PRIu64
		" dqbuf_errors=%" PRIu64 " recoveries=%" PRIu64
		" sequence_gaps=%" PRIu64 " rejected_buffers=%" PRIu64
		" cleanup_failures=%" PRIu64
		" last_sequence=%" PRIu32 " stride=%u data_offset=%u\n",
		label, elapsed, frames, fps, changed, visible.timeouts,
		visible.startup_errors, visible.malformed, visible.metadata_errors,
		visible.timestamp_errors, visible.sequence_errors, visible.decode_errors,
		visible.requeue_errors, visible.discarded_buffers,
		visible.discard_limit_errors, visible.poll_errors, visible.dqbuf_errors,
		visible.recoveries, visible.sequence_gaps, visible.rejected_buffers,
		visible.cleanup_failures, stats->last_sequence, stats->stride,
		stats->last_data_offset);
	fflush(stdout);
}

static int capture_one_frame(IrCapture **capture, uint8_t *frame,
	IrCaptureStats *stats, uint64_t *attempt_id, char *error,
	unsigned error_size, bool discard_error_buffers)
{
	if (*capture == NULL) {
		uint64_t new_attempt = next_stream_attempt_id++;

		if (attempt_id != NULL)
			*attempt_id = new_attempt;
		fprintf(stderr, "ir_diag event=stream_open attempt=%" PRIu64
			" monotonic_ns=%" PRIu64 "\n", new_attempt,
			monotonic_timestamp_ns());
		if (ir_capture_open(capture, error, error_size) != 0) {
			fprintf(stderr, "ir_diag event=stream_start attempt=%" PRIu64
			" monotonic_ns=%" PRIu64 " result=open-failure\n",
				new_attempt, monotonic_timestamp_ns());
			return CAPTURE_STARTUP_ERROR;
		}
		ir_capture_set_stream_attempt_id(*capture, new_attempt);
		ir_capture_set_discard_error_buffers(*capture, discard_error_buffers);
		if (ir_capture_start(*capture, error, error_size) != 0) {
			ir_capture_close(*capture);
			*capture = NULL;
			return CAPTURE_STARTUP_ERROR;
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
	add_capture_stats(totals, stats);
	if (ir_capture_stop(*capture, error, error_size) != 0) {
		if (totals != NULL)
			totals->cleanup_failures++;
		fprintf(stderr, "cleanup_error operation=STREAMOFF attempt=%" PRIu64
			" monotonic_ns=%" PRIu64 " error=%s\n",
			stats != NULL ? stats->stream_attempt_id : 0U,
			monotonic_timestamp_ns(), error);
		result = -1;
	}
	ir_capture_close(*capture);
	*capture = NULL;
	return result;
}

static int run_persistent(unsigned duration_seconds, bool baseline,
	bool discard_error_buffers, QualificationTotals *totals)
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
	uint64_t attempt_id = 0U;
	bool have_previous = false;

	while (!stop_requested && monotonic_seconds() - started <
		(double)duration_seconds) {
		int result = capture_one_frame(&capture, frame, &stats, &attempt_id,
			error, sizeof(error), discard_error_buffers);

		if (result == 1) {
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
		} else if (result == IR_CAPTURE_RESULT_DISCARDED) {
			fprintf(stderr, "capture_discarded elapsed=%.3f attempt=%" PRIu64
				" cumulative=%" PRIu64 "\n", monotonic_seconds() - started,
				attempt_id, stats.discarded_buffers);
		} else if (result == 0) {
			totals->timeouts++;
			fprintf(stderr, "capture_timeout elapsed=%.3f\n",
				monotonic_seconds() - started);
			if (!baseline)
				break;
		} else if (result == CAPTURE_STARTUP_ERROR) {
			totals->startup_errors++;
			totals->recoveries++;
			fprintf(stderr, "startup_error elapsed=%.3f attempt=%" PRIu64
				" recovery=%" PRIu64 " error=%s\n",
				monotonic_seconds() - started, attempt_id,
				totals->recoveries, error);
			if (close_capture(&capture, &stats, totals, error,
				sizeof(error)) != 0 && !baseline)
				break;
			if (!baseline)
				break;
			log_reopen_event(attempt_id, totals->recoveries);
			sleep(RECOVERY_DELAY_SECONDS);
		} else {
			totals->recoveries++;
			fprintf(stderr, "capture_error elapsed=%.3f attempt=%" PRIu64
				" recovery=%" PRIu64 " kind=%s error=%s\n",
				monotonic_seconds() - started, attempt_id,
				totals->recoveries, capture_error_name(stats.last_error), error);
			if (close_capture(&capture, &stats, totals, error,
				sizeof(error)) != 0 && !baseline)
				break;
			if (!baseline)
				break;
			log_reopen_event(attempt_id, totals->recoveries);
			sleep(RECOVERY_DELAY_SECONDS);
		}
		if (monotonic_seconds() >= next_report) {
			print_progress("persistent", monotonic_seconds() - started,
				frames, changed, totals, capture != NULL ? &stats : NULL);
			next_report += 10.0;
		}
	}
	(void)close_capture(&capture, &stats, totals, error, sizeof(error));
	{
		double elapsed = monotonic_seconds() - started;
		bool passed = !stop_requested && elapsed >= (double)duration_seconds &&
			frames > 0U && changed > 0U && totals->timeouts == 0U &&
			totals->startup_errors == 0U && totals->malformed == 0U &&
			totals->metadata_errors == 0U && totals->timestamp_errors == 0U &&
			totals->sequence_errors == 0U && totals->decode_errors == 0U &&
			totals->requeue_errors == 0U && totals->poll_errors == 0U &&
			totals->dqbuf_errors == 0U && totals->recoveries == 0U &&
			totals->sequence_gaps == 0U &&
			totals->rejected_buffers == 0U &&
			totals->discarded_buffers == 0U &&
			totals->discard_limit_errors == 0U &&
			totals->cleanup_failures == 0U;

		printf("persistent_summary requested_duration_seconds=%u"
			" actual_duration_seconds=%.3f frames=%" PRIu64
			" fps=%.3f changed=%" PRIu64 " timeouts=%" PRIu64
			" startup_errors=%" PRIu64 " malformed=%" PRIu64
			" metadata_errors=%" PRIu64 " timestamp_errors=%" PRIu64
			" sequence_errors=%" PRIu64 " decode_errors=%" PRIu64
			" requeue_errors=%" PRIu64 " discarded_buffers=%" PRIu64
			" discard_limit_errors=%" PRIu64 " poll_errors=%" PRIu64
			" dqbuf_errors=%" PRIu64 " recoveries=%" PRIu64
			" sequence_gaps=%" PRIu64 " rejected_buffers=%" PRIu64
			" cleanup_failures=%" PRIu64 " gate_result=%s result=%s"
			" stability_pass=%s\n", duration_seconds, elapsed, frames,
			elapsed > 0.0 ? (double)frames / elapsed : 0.0, changed,
			totals->timeouts, totals->startup_errors, totals->malformed,
			totals->metadata_errors, totals->timestamp_errors,
			totals->sequence_errors, totals->decode_errors,
			totals->requeue_errors, totals->discarded_buffers,
			totals->discard_limit_errors, totals->poll_errors,
			totals->dqbuf_errors,
			totals->recoveries, totals->sequence_gaps, totals->rejected_buffers,
			totals->cleanup_failures, passed ? "PASS" : "FAIL",
			baseline ? "BASELINE" : (passed ? "PASS" : "FAIL"),
			baseline ? "NO" : (passed ? "YES" : "NO"));
		return passed ? 0 : 1;
	}
}

static int run_cycles(unsigned cycles, unsigned frames_per_cycle, bool baseline,
	bool discard_error_buffers, QualificationTotals *totals)
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
		uint64_t attempt_id = 0U;
		char error[256];
		unsigned frames = 0U;
		bool cycle_ok = true;
		uint64_t cycle_cleanup_failures_before = totals->cleanup_failures;
		uint64_t cycle_sequence_gaps_before = totals->sequence_gaps;
		uint64_t cycle_rejected_buffers_before = totals->rejected_buffers;
		uint64_t cycle_timeouts_before = totals->timeouts;
		uint64_t cycle_startup_errors_before = totals->startup_errors;
		uint64_t cycle_malformed_before = totals->malformed;
		uint64_t cycle_metadata_errors_before = totals->metadata_errors;
		uint64_t cycle_timestamp_errors_before = totals->timestamp_errors;
		uint64_t cycle_sequence_errors_before = totals->sequence_errors;
		uint64_t cycle_decode_errors_before = totals->decode_errors;
		uint64_t cycle_requeue_errors_before = totals->requeue_errors;
		uint64_t cycle_discarded_buffers_before = totals->discarded_buffers;
		uint64_t cycle_discard_limit_errors_before = totals->discard_limit_errors;
		uint64_t cycle_poll_errors_before = totals->poll_errors;
		uint64_t cycle_dqbuf_errors_before = totals->dqbuf_errors;

		while (frames < frames_per_cycle && !stop_requested) {
			int result = capture_one_frame(&capture, frame, &stats, &attempt_id,
				error, sizeof(error), discard_error_buffers);

			if (result == 1) {
				frames++;
				continue;
			}
			if (result == IR_CAPTURE_RESULT_DISCARDED) {
				fprintf(stderr, "cycle=%u attempt=%" PRIu64
					" discarded_buffer cumulative=%" PRIu64 "\n", cycle,
					attempt_id, stats.discarded_buffers);
				continue;
			}
			if (result == 0) {
				totals->timeouts++;
				cycle_ok = false;
				fprintf(stderr, "cycle=%u capture_timeout\n", cycle);
				break;
			}
			if (result == CAPTURE_STARTUP_ERROR) {
				totals->startup_errors++;
				totals->recoveries++;
				cycle_ok = false;
				fprintf(stderr, "cycle=%u attempt=%" PRIu64
					" startup_error=%s\n", cycle, attempt_id, error);
				break;
			}
			totals->recoveries++;
			cycle_ok = false;
			fprintf(stderr, "cycle=%u attempt=%" PRIu64
				" capture_error kind=%s error=%s\n", cycle, attempt_id,
				capture_error_name(stats.last_error), error);
			break;
		}
		if (close_capture(&capture, &stats, totals, error, sizeof(error)) != 0)
			cycle_ok = false;
		if (totals->sequence_gaps != cycle_sequence_gaps_before ||
			totals->rejected_buffers != cycle_rejected_buffers_before ||
			totals->discarded_buffers != cycle_discarded_buffers_before ||
			totals->discard_limit_errors != cycle_discard_limit_errors_before)
			cycle_ok = false;
		attempted++;
		total_frames += frames;
		if (cycle_ok && frames == frames_per_cycle)
			passed++;
		else
			failed++;
		printf("cycle=%u requested_frames=%u actual_frames=%u frames=%u"
			" timeouts=%" PRIu64 " startup_errors=%" PRIu64
			" malformed=%" PRIu64 " metadata_errors=%" PRIu64
			" timestamp_errors=%" PRIu64 " sequence_errors=%" PRIu64
			" decode_errors=%" PRIu64 " requeue_errors=%" PRIu64
			" discarded_buffers=%" PRIu64 " discard_limit_errors=%" PRIu64
			" poll_errors=%" PRIu64 " dqbuf_errors=%" PRIu64
			" sequence_gaps=%" PRIu64
			" rejected_buffers=%" PRIu64 " cleanup_failures=%" PRIu64
			" cumulative_timeouts=%" PRIu64 " cumulative_malformed=%" PRIu64
			" cumulative_startup_errors=%" PRIu64
			" cumulative_metadata_errors=%" PRIu64
			" cumulative_timestamp_errors=%" PRIu64
			" cumulative_sequence_errors=%" PRIu64
			" cumulative_decode_errors=%" PRIu64
			" cumulative_requeue_errors=%" PRIu64
			" cumulative_discarded_buffers=%" PRIu64
			" cumulative_discard_limit_errors=%" PRIu64
			" cumulative_poll_errors=%" PRIu64
			" cumulative_dqbuf_errors=%" PRIu64
			" cumulative_sequence_gaps=%" PRIu64
			" cumulative_rejected_buffers=%" PRIu64
			" cumulative_cleanup_failures=%" PRIu64 " result=%s\n", cycle,
			frames_per_cycle, frames, frames, totals->timeouts - cycle_timeouts_before,
			totals->startup_errors - cycle_startup_errors_before,
			totals->malformed - cycle_malformed_before,
			totals->metadata_errors - cycle_metadata_errors_before,
			totals->timestamp_errors - cycle_timestamp_errors_before,
			totals->sequence_errors - cycle_sequence_errors_before,
			totals->decode_errors - cycle_decode_errors_before,
			totals->requeue_errors - cycle_requeue_errors_before,
			totals->discarded_buffers - cycle_discarded_buffers_before,
			totals->discard_limit_errors - cycle_discard_limit_errors_before,
			totals->poll_errors - cycle_poll_errors_before,
			totals->dqbuf_errors - cycle_dqbuf_errors_before,
			totals->sequence_gaps - cycle_sequence_gaps_before,
			totals->rejected_buffers - cycle_rejected_buffers_before,
			totals->cleanup_failures - cycle_cleanup_failures_before,
			totals->timeouts, totals->malformed, totals->startup_errors,
			totals->metadata_errors, totals->timestamp_errors,
			totals->sequence_errors, totals->decode_errors,
			totals->requeue_errors, totals->discarded_buffers,
			totals->discard_limit_errors, totals->poll_errors,
			totals->dqbuf_errors,
			totals->sequence_gaps,
			totals->rejected_buffers, totals->cleanup_failures,
			cycle_ok && frames == frames_per_cycle ? "PASS" : "FAIL");
		fflush(stdout);
	}
	{
		bool passed_gate = !stop_requested && failed == 0U && passed == cycles;
		printf("cycles_summary requested=%u attempted=%u passed=%u failed=%u"
			" frames=%" PRIu64 " timeouts=%" PRIu64
			" startup_errors=%" PRIu64 " malformed=%" PRIu64
			" metadata_errors=%" PRIu64 " timestamp_errors=%" PRIu64
			" sequence_errors=%" PRIu64 " decode_errors=%" PRIu64
			" requeue_errors=%" PRIu64 " poll_errors=%" PRIu64
			" discarded_buffers=%" PRIu64 " discard_limit_errors=%" PRIu64
			" dqbuf_errors=%" PRIu64 " recoveries=%" PRIu64
			" sequence_gaps=%" PRIu64
			" rejected_buffers=%" PRIu64 " cleanup_failures=%" PRIu64
			" gate_result=%s result=%s stability_pass=%s\n", cycles,
			attempted, passed, failed, total_frames, totals->timeouts,
			totals->startup_errors, totals->malformed, totals->metadata_errors,
			totals->timestamp_errors, totals->sequence_errors,
			totals->decode_errors, totals->requeue_errors, totals->poll_errors,
			totals->discarded_buffers, totals->discard_limit_errors,
			totals->dqbuf_errors, totals->recoveries, totals->sequence_gaps,
			totals->rejected_buffers, totals->cleanup_failures,
			passed_gate ? "PASS" : "FAIL",
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
	bool discard_error_buffers = false;

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
		} else if (strcmp(argv[index], "--discard-error-buffers") == 0) {
			discard_error_buffers = true;
		} else {
			fprintf(stderr, "usage: %s [--duration seconds] [--cycles count] "
				"[--cycle-frames count] [--baseline] "
				"[--discard-error-buffers]\n", argv[0]);
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
		" requested_cycles=%u cycle_frames=%u discard_error_buffers=%s\n",
		baseline ? "baseline" : "normal", duration, cycles, cycle_frames,
		discard_error_buffers ? "yes" : "no");
	{
		QualificationTotals persistent_totals = { 0 };
		QualificationTotals cycle_totals = { 0 };
		int persistent_result = run_persistent(duration, baseline,
			discard_error_buffers, &persistent_totals);
		int cycles_result = EXIT_FAILURE;

		if (baseline || (persistent_result == 0 && !stop_requested))
			cycles_result = run_cycles(cycles, cycle_frames, baseline,
				discard_error_buffers, &cycle_totals);
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
