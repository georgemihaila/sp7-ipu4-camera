#define AUTH_CAPTURE_SUPERVISOR_TEST
#define main auth_capture_helper_main
#include "auth-capture-helper.c"
#undef main

#include <fcntl.h>
#include <sys/file.h>

static int fail(const char *message)
{
	fprintf(stderr, "auth-capture-supervisor-test: %s\n", message);
	if (auth_test_worker_pid > 0) {
		(void)kill(auth_test_worker_pid, SIGKILL);
		(void)waitpid(auth_test_worker_pid, NULL, 0);
	}
	return EXIT_FAILURE;
}

int main(void)
{
	char lock_path[] = "/var/tmp/sp7-auth-lock-test-XXXXXX";
	int lock_fd = -1;
	int late_pipe[2] = { -1, -1 };
	int stdout_pipe[2] = { -1, -1 };
	int saved_stdout = -1;
	struct pollfd descriptor;
	char result;
	char output[8];
	int second_fd = -1;
	int status;

	lock_fd = mkstemp(lock_path);
	if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0)
		return fail("could not acquire test lock");
	if (pipe(late_pipe) != 0 || pipe(stdout_pipe) != 0)
		return fail("could not create test pipes");
	auth_test_late_status_fd = late_pipe[1];
	saved_stdout = dup(STDOUT_FILENO);
	if (saved_stdout < 0 || dup2(stdout_pipe[1], STDOUT_FILENO) < 0)
		return fail("could not redirect test stdout");
	(void)close(stdout_pipe[1]);
	stdout_pipe[1] = -1;

	status = supervise_worker(lock_fd, 1U);
	lock_fd = -1;
	if (dup2(saved_stdout, STDOUT_FILENO) < 0)
		return fail("could not restore test stdout");
	(void)close(saved_stdout);
	saved_stdout = -1;
	if (status != AUTH_EXIT_TIMEOUT)
		return fail("blocked worker did not produce the timeout status");

	second_fd = open(lock_path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
	if (second_fd < 0 || flock(second_fd, LOCK_EX | LOCK_NB) == 0)
		return fail("overlapping capture was not blocked by the live worker");
	(void)close(second_fd);
	second_fd = -1;

	descriptor.fd = stdout_pipe[0];
	descriptor.events = POLLIN | POLLHUP | POLLERR;
	descriptor.revents = 0;
	if (poll(&descriptor, 1, 500) <= 0 || (descriptor.revents & POLLHUP) == 0U)
		return fail("worker retained the caller stdout descriptor");
	if (read(stdout_pipe[0], output, sizeof(output)) != 0)
		return fail("late caller-visible stdout was not closed");

	descriptor.fd = late_pipe[0];
	descriptor.events = POLLIN | POLLHUP | POLLERR;
	descriptor.revents = 0;
	if (poll(&descriptor, 1, 2500) <= 0 || read(late_pipe[0], &result, 1) != 1 ||
		result != 'E')
		return fail("late frame output was not rejected after supervisor exit");

	(void)kill(auth_test_worker_pid, SIGKILL);
	if (waitpid(auth_test_worker_pid, NULL, 0) < 0)
		return fail("blocked worker cleanup could not be reaped");
	auth_test_worker_pid = -1;
	(void)close(stdout_pipe[0]);
	(void)close(late_pipe[0]);
	(void)close(late_pipe[1]);
	(void)unlink(lock_path);
	printf("auth-capture-supervisor-test: PASS\n");
	return EXIT_SUCCESS;
}
