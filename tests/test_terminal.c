/**
 * @file test_terminal.c
 * @brief Unit and pty-based tests for terminal.c.
 */
#define _DEFAULT_SOURCE /* setitimer, usleep under -std=c11 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <string.h>
#include <pty.h>
#include <unistd.h>
#include "unity.h"
#include "test_terminal.h"
#include "terminal.h"

/** @brief terminal_set_raw_flags() clears the raw-mode flags and sets VMIN/VTIME. */
static void test_terminal_sets_raw_flags(void) {
	struct termios t;
	memset(&t, 0, sizeof(struct termios));
	terminal_set_raw_flags(&t);

	TEST_ASSERT_EQUAL_INT(0, t.c_lflag & (ICANON | ECHO | ISIG));
	TEST_ASSERT_EQUAL_INT(0, t.c_iflag & (IXON | ICRNL));
	TEST_ASSERT_EQUAL_INT(0, t.c_oflag & OPOST);
	TEST_ASSERT_EQUAL_INT(1, t.c_cc[VMIN]);
	TEST_ASSERT_EQUAL_INT(0, t.c_cc[VTIME]);
}

/** @brief terminal_set_raw_flags() leaves unrelated flags untouched. */
static void test_terminal_preserves_unrelated_flags(void) {
	struct termios t;
	memset(&t, 0, sizeof(struct termios));
	t.c_cflag = CS8 | B9600;
	t.c_lflag = ECHOE | ECHOK;
	t.c_iflag = IGNBRK;
	t.c_oflag = ONLCR;

	terminal_set_raw_flags(&t);

	TEST_ASSERT_EQUAL_INT(CS8 | B9600, t.c_cflag);
	TEST_ASSERT_EQUAL_INT(ECHOE | ECHOK, t.c_lflag);
	TEST_ASSERT_EQUAL_INT(IGNBRK, t.c_iflag);
	TEST_ASSERT_EQUAL_INT(ONLCR, t.c_oflag);
}

/** @brief enter/leave on a pty switches to raw mode and restores the original attributes. */
static void test_enter_and_leave_raw_restores_terminal(void) {
	int master, slave;
	TEST_ASSERT_EQUAL_INT(0, openpty(&master, &slave, NULL, NULL, NULL));

	struct termios original;
	TEST_ASSERT_EQUAL_INT(0, tcgetattr(slave, &original));

	/* Enter raw mode */
	terminal_enter_raw(slave);
	struct termios raw;
	TEST_ASSERT_EQUAL_INT(0, tcgetattr(slave, &raw));
	TEST_ASSERT_EQUAL_INT(0, raw.c_lflag & (ICANON | ECHO | ISIG));
	TEST_ASSERT_EQUAL_INT(0, raw.c_iflag & (IXON | ICRNL));
	TEST_ASSERT_EQUAL_INT(0, raw.c_oflag & OPOST);
	TEST_ASSERT_EQUAL_INT(1, raw.c_cc[VMIN]);
	TEST_ASSERT_EQUAL_INT(0, raw.c_cc[VTIME]);

	/* Leave raw mode */
	terminal_leave_raw(slave);
	struct termios restored;
	TEST_ASSERT_EQUAL_INT(0, tcgetattr(slave, &restored));
	TEST_ASSERT_EQUAL_INT(original.c_lflag, restored.c_lflag);
	TEST_ASSERT_EQUAL_INT(original.c_iflag, restored.c_iflag);
	TEST_ASSERT_EQUAL_INT(original.c_oflag, restored.c_oflag);
	TEST_ASSERT_EQUAL_INT(original.c_cflag, restored.c_cflag);
	TEST_ASSERT_EQUAL_INT(original.c_cc[VMIN], restored.c_cc[VMIN]);
	TEST_ASSERT_EQUAL_INT(original.c_cc[VTIME], restored.c_cc[VTIME]);

	close(master);
	close(slave);
}

/** @brief Fill @p t with the attributes currently set on @p fd. */
static void get_attrs(int fd, struct termios *t) {
	TEST_ASSERT_EQUAL_INT(0, tcgetattr(fd, t));
}

/** @brief Assert that @p a and @p b have the same flags and VMIN/VTIME. */
static void assert_same_attrs(const struct termios *a, const struct termios *b) {
	TEST_ASSERT_EQUAL_INT(a->c_lflag, b->c_lflag);
	TEST_ASSERT_EQUAL_INT(a->c_iflag, b->c_iflag);
	TEST_ASSERT_EQUAL_INT(a->c_oflag, b->c_oflag);
	TEST_ASSERT_EQUAL_INT(a->c_cflag, b->c_cflag);
	TEST_ASSERT_EQUAL_INT(a->c_cc[VMIN], b->c_cc[VMIN]);
	TEST_ASSERT_EQUAL_INT(a->c_cc[VTIME], b->c_cc[VTIME]);
}

/** @brief Entering raw mode twice keeps the original attributes for the restore. */
static void test_enter_raw_twice_still_restores_original(void) {
	int master, slave;
	TEST_ASSERT_EQUAL_INT(0, openpty(&master, &slave, NULL, NULL, NULL));
	struct termios original, now;
	get_attrs(slave, &original);

	terminal_enter_raw(slave);
	terminal_enter_raw(slave);
	terminal_leave_raw(slave);

	get_attrs(slave, &now);
	assert_same_attrs(&original, &now);
	close(master);
	close(slave);
}

/** @brief Leaving raw mode without entering it does not touch the terminal. */
static void test_leave_raw_without_enter_is_noop(void) {
	int master, slave;
	TEST_ASSERT_EQUAL_INT(0, openpty(&master, &slave, NULL, NULL, NULL));
	struct termios original, now;
	get_attrs(slave, &original);

	terminal_leave_raw(slave);

	get_attrs(slave, &now);
	assert_same_attrs(&original, &now);
	close(master);
	close(slave);
}

/** @brief A second leave does not undo anything or change the restored attributes. */
static void test_leave_raw_twice_is_noop(void) {
	int master, slave;
	TEST_ASSERT_EQUAL_INT(0, openpty(&master, &slave, NULL, NULL, NULL));
	struct termios original, now;
	get_attrs(slave, &original);

	terminal_enter_raw(slave);
	terminal_leave_raw(slave);
	terminal_leave_raw(slave);

	get_attrs(slave, &now);
	assert_same_attrs(&original, &now);
	close(master);
	close(slave);
}

/** @brief Set the window size of the pty behind @p master. */
static void set_winsize(int master, unsigned short rows, unsigned short cols) {
	struct winsize ws = { .ws_row = rows, .ws_col = cols };
	TEST_ASSERT_EQUAL_INT(0, ioctl(master, TIOCSWINSZ, &ws));
}

/** @brief Assert that terminal_get_size(@p fd) gives @p rows x @p cols. */
static void assert_size(int fd, int rows, int cols) {
	int r = -1, c = -1;
	terminal_get_size(fd, &r, &c);
	TEST_ASSERT_EQUAL_INT(rows, r);
	TEST_ASSERT_EQUAL_INT(cols, c);
}

/** @brief A terminal with a known size reports it. */
static void test_terminal_get_size_reads_pty_size(void) {
	int master, slave;
	TEST_ASSERT_EQUAL_INT(0, openpty(&master, &slave, NULL, NULL, NULL));
	set_winsize(master, 30, 100);
	assert_size(slave, 30, 100);
	close(master);
	close(slave);
}

/** @brief A pty that reports 0x0 falls back to the defaults. */
static void test_terminal_get_size_falls_back_on_zero_size(void) {
	int master, slave;
	TEST_ASSERT_EQUAL_INT(0, openpty(&master, &slave, NULL, NULL, NULL));
	assert_size(slave, TERMINAL_DEFAULT_ROWS, TERMINAL_DEFAULT_COLS);
	close(master);
	close(slave);
}

/** @brief Only the dimension reported as 0 falls back. */
static void test_terminal_get_size_falls_back_per_dimension(void) {
	int master, slave;
	TEST_ASSERT_EQUAL_INT(0, openpty(&master, &slave, NULL, NULL, NULL));
	set_winsize(master, 10, 0);
	assert_size(slave, 10, TERMINAL_DEFAULT_COLS);
	set_winsize(master, 0, 120);
	assert_size(slave, TERMINAL_DEFAULT_ROWS, 120);
	close(master);
	close(slave);
}

/** @brief A descriptor that is not a terminal gives the defaults. */
static void test_terminal_get_size_falls_back_when_not_a_tty(void) {
	int fds[2];
	TEST_ASSERT_EQUAL_INT(0, pipe(fds));
	assert_size(fds[0], TERMINAL_DEFAULT_ROWS, TERMINAL_DEFAULT_COLS);
	close(fds[0]);
	close(fds[1]);
}

/** @brief An invalid descriptor gives the defaults. */
static void test_terminal_get_size_falls_back_on_invalid_fd(void) {
	assert_size(-1, TERMINAL_DEFAULT_ROWS, TERMINAL_DEFAULT_COLS);
}

/** @brief Read what is waiting on @p master until 100ms of silence; NUL-terminate; return the length. */
static size_t read_pending(int master, char *buf, size_t size) {
	size_t len = 0;
	struct pollfd pfd = { .fd = master, .events = POLLIN };
	while (len < size - 1 && poll(&pfd, 1, 100) > 0) {
		ssize_t n = read(master, buf + len, size - 1 - len);
		if (n <= 0) break;
		len += (size_t)n;
	}
	buf[len] = '\0';
	return len;
}

/** @brief Entering the alternate screen writes the switch sequence once, however often it is called. */
static void test_terminal_enter_alt_screen_writes_once(void) {
	int master, slave;
	TEST_ASSERT_EQUAL_INT(0, openpty(&master, &slave, NULL, NULL, NULL));
	char first[64], second[64];
	terminal_enter_alt_screen(slave);
	read_pending(master, first, sizeof(first));
	terminal_enter_alt_screen(slave);
	read_pending(master, second, sizeof(second));
	terminal_leave_alt_screen(slave); /* leave the shared state clean before asserting */
	close(master);
	close(slave);
	TEST_ASSERT_EQUAL_STRING("\x1b[?1049h", first);
	TEST_ASSERT_EQUAL_STRING("", second);
}

/** @brief Leaving writes the switch-back sequence once, and only after entering. */
static void test_terminal_leave_alt_screen_writes_once_after_enter(void) {
	int master, slave;
	TEST_ASSERT_EQUAL_INT(0, openpty(&master, &slave, NULL, NULL, NULL));
	char before[64], entered[64], left[64], again[64];
	terminal_leave_alt_screen(slave);
	read_pending(master, before, sizeof(before));
	terminal_enter_alt_screen(slave);
	read_pending(master, entered, sizeof(entered));
	terminal_leave_alt_screen(slave);
	read_pending(master, left, sizeof(left));
	terminal_leave_alt_screen(slave);
	read_pending(master, again, sizeof(again));
	close(master);
	close(slave);
	TEST_ASSERT_EQUAL_STRING("", before);
	TEST_ASSERT_EQUAL_STRING("\x1b[?1049h", entered);
	TEST_ASSERT_EQUAL_STRING("\x1b[?1049l", left);
	TEST_ASSERT_EQUAL_STRING("", again);
}

/** Size of the payloads that must not fit in a pipe (a pipe holds 64 KB on Linux). */
#define BIG_PAYLOAD 1000000

/** @brief The byte at offset @p i of the test payload: a pattern that shows lost, repeated or reordered bytes. */
static char payload_byte(size_t i) {
	return (char)('a' + (i * 7 + i / 251) % 26);
}

/** @brief Fill @p buf with the first @p n bytes of the test payload. */
static void fill_payload(char *buf, size_t n) {
	for (size_t i = 0; i < n; i++) buf[i] = payload_byte(i);
}

/**
 * @brief Fork a reader that waits 100 ms, then reads @p rfd until end of file and checks it is exactly @p n payload bytes.
 *
 * It exits 0 if so and 1 otherwise. The caller closes @p rfd and keeps the write end @p wfd.
 * The delay makes the reader slower than the writer, so the writer meets a full pipe.
 */
static pid_t spawn_slow_reader(int rfd, int wfd, size_t n) {
	pid_t pid = fork();
	if (pid == 0) {
		close(wfd);
		usleep(100000);
		size_t got = 0;
		char chunk[4096];
		ssize_t r;
		while ((r = read(rfd, chunk, sizeof(chunk))) > 0) {
			for (ssize_t i = 0; i < r; i++) {
				if (got + (size_t)i >= n || chunk[i] != payload_byte(got + (size_t)i)) _exit(1);
			}
			got += (size_t)r;
		}
		_exit(r == 0 && got == n ? 0 : 1);
	}
	close(rfd);
	return pid;
}

/** @brief Wait for @p pid and return its exit status, or -1 if it did not exit normally. */
static int exit_status_of(pid_t pid) {
	int status = 0;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/** @brief A short payload goes through whole and can be read back. */
static void test_terminal_write_all_writes_a_short_payload(void) {
	int fds[2];
	TEST_ASSERT_EQUAL_INT(0, pipe(fds));
	int rc = terminal_write_all(fds[1], "hello\x1b[K", 8);
	close(fds[1]); /* so that the read below ends even if nothing was written */
	char got[16] = "";
	ssize_t n = read(fds[0], got, sizeof(got) - 1);
	close(fds[0]);
	TEST_ASSERT_EQUAL_INT(0, rc);
	TEST_ASSERT_EQUAL_INT(8, (int)n);
	TEST_ASSERT_EQUAL_MEMORY("hello\x1b[K", got, 8);
}

/** @brief Writing nothing succeeds and writes nothing, even to a descriptor that is not valid. */
static void test_terminal_write_all_of_nothing_succeeds(void) {
	TEST_ASSERT_EQUAL_INT(0, terminal_write_all(-1, "", 0));
	TEST_ASSERT_EQUAL_INT(0, terminal_write_all(-1, NULL, 0));
}

/** @brief A payload far bigger than the pipe, read slowly by another process, arrives whole and in order. */
static void test_terminal_write_all_fills_a_pipe_for_a_slow_reader(void) {
	static char payload[BIG_PAYLOAD];
	fill_payload(payload, sizeof(payload));
	int fds[2];
	TEST_ASSERT_EQUAL_INT(0, pipe(fds));
	pid_t reader = spawn_slow_reader(fds[0], fds[1], sizeof(payload));
	int rc = terminal_write_all(fds[1], payload, sizeof(payload));
	close(fds[1]);
	int status = exit_status_of(reader);
	TEST_ASSERT_EQUAL_INT(0, rc);
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, status, "the reader did not get every byte, in order");
}

/** @brief A non-blocking pipe takes part of the payload and then says EAGAIN: the rest is written once the reader makes room. */
static void test_terminal_write_all_continues_after_a_partial_write_and_eagain(void) {
	static char payload[BIG_PAYLOAD];
	fill_payload(payload, sizeof(payload));
	int fds[2];
	TEST_ASSERT_EQUAL_INT(0, pipe(fds));
	fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL) | O_NONBLOCK);
	pid_t reader = spawn_slow_reader(fds[0], fds[1], sizeof(payload));
	int rc = terminal_write_all(fds[1], payload, sizeof(payload));
	int saved = errno;
	close(fds[1]);
	int status = exit_status_of(reader);
	char msg[64];
	snprintf(msg, sizeof(msg), "rc %d, errno %d", rc, saved);
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, msg);
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, status, "the reader did not get every byte, in order");
}

/** @brief Signal handler that does nothing: it only has to interrupt the system call. */
static void on_alarm(int sig) {
	(void)sig;
}

/** @brief A signal that interrupts a blocked write (EINTR, or a short count) does not lose or repeat any byte. */
static void test_terminal_write_all_survives_signals_interrupting_the_write(void) {
	static char payload[BIG_PAYLOAD];
	fill_payload(payload, sizeof(payload));
	int fds[2];
	TEST_ASSERT_EQUAL_INT(0, pipe(fds));
	struct sigaction sa, old;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_alarm; /* no SA_RESTART: the write really is interrupted */
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, &old);
	pid_t reader = spawn_slow_reader(fds[0], fds[1], sizeof(payload));
	struct itimerval tick = { .it_interval = { 0, 5000 }, .it_value = { 0, 5000 } };
	setitimer(ITIMER_REAL, &tick, NULL); /* every 5 ms, while the reader is still asleep */
	int rc = terminal_write_all(fds[1], payload, sizeof(payload));
	struct itimerval off = { { 0, 0 }, { 0, 0 } };
	setitimer(ITIMER_REAL, &off, NULL);
	sigaction(SIGALRM, &old, NULL);
	close(fds[1]);
	int status = exit_status_of(reader);
	TEST_ASSERT_EQUAL_INT(0, rc);
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, status, "the reader did not get every byte, in order");
}

/**
 * @brief Run terminal_write_all() on @p fd in a child with a 3 s alarm (so a function that retries forever fails
 *        the test instead of hanging it) and check that it returned -1 with errno @p expected_errno.
 * @note SIGPIPE is ignored in the child only, so that EPIPE is returned instead of killing it.
 * @return 0 if it did, 1 if it returned something else, -1 if the child was killed by the alarm.
 */
static int failing_write_status(int fd, int expected_errno) {
	pid_t pid = fork();
	if (pid == 0) {
		signal(SIGPIPE, SIG_IGN);
		alarm(3);
		int rc = terminal_write_all(fd, "data", 4);
		_exit(rc == -1 && errno == expected_errno ? 0 : 1);
	}
	return exit_status_of(pid);
}

/** @brief A descriptor that is not open gives -1 with EBADF, at once. */
static void test_terminal_write_all_reports_a_bad_descriptor(void) {
	int fds[2];
	TEST_ASSERT_EQUAL_INT(0, pipe(fds));
	int fd = fds[1];
	close(fds[0]);
	close(fds[1]);
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, failing_write_status(fd, EBADF), "expected -1 with EBADF, and no retry loop");
}

/** @brief A pipe nobody reads any more gives -1 with EPIPE (SIGPIPE ignored), and does not retry. */
static void test_terminal_write_all_reports_a_closed_reader(void) {
	int fds[2];
	TEST_ASSERT_EQUAL_INT(0, pipe(fds));
	close(fds[0]);
	int status = failing_write_status(fds[1], EPIPE);
	close(fds[1]);
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, status, "expected -1 with EPIPE, and no retry loop");
}

/** @brief The reader goes away halfway through a big write: -1 with EPIPE, not a hang. */
static void test_terminal_write_all_reports_a_reader_that_quits_midway(void) {
	int fds[2];
	TEST_ASSERT_EQUAL_INT(0, pipe(fds));
	pid_t pid = fork();
	if (pid == 0) {
		close(fds[0]); /* this child must not keep the read end open */
		signal(SIGPIPE, SIG_IGN);
		alarm(3);
		static char payload[BIG_PAYLOAD];
		fill_payload(payload, sizeof(payload));
		/* the parent reads a little and closes its end */
		int rc = terminal_write_all(fds[1], payload, sizeof(payload));
		_exit(rc == -1 && errno == EPIPE ? 0 : 1);
	}
	close(fds[1]); /* only the child writes; if it stops early the read below sees the end of file */
	char chunk[1024];
	ssize_t n = read(fds[0], chunk, sizeof(chunk));
	close(fds[0]);
	TEST_ASSERT_TRUE(n > 0);
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, exit_status_of(pid), "expected -1 with EPIPE, and no retry loop");
}

/** @brief Fork a reader that waits 100 ms, drains @p rfd to the end and exits 0 only if the last bytes it saw are @p tail. */
static pid_t spawn_tail_checker(int rfd, int wfd, const char *tail) {
	pid_t pid = fork();
	if (pid == 0) {
		close(wfd);
		usleep(100000);
		char last[16] = "";
		size_t tail_len = strlen(tail);
		char chunk[4096];
		ssize_t r;
		while ((r = read(rfd, chunk, sizeof(chunk))) > 0) {
			for (ssize_t i = 0; i < r; i++) {
				memmove(last, last + 1, tail_len - 1);
				last[tail_len - 1] = chunk[i];
			}
		}
		_exit(r == 0 && memcmp(last, tail, tail_len) == 0 ? 0 : 1);
	}
	close(rfd);
	return pid;
}

/** @brief Fill the non-blocking write end @p fd until the pipe takes no more (EAGAIN). */
static void fill_pipe(int fd) {
	char chunk[4096];
	memset(chunk, 'x', sizeof(chunk));
	while (write(fd, chunk, sizeof(chunk)) > 0) { }
	while (write(fd, "x", 1) > 0) { }
}

/** @brief Leaving the alternate screen on a non-blocking stdout whose buffer is full waits and still sends the whole sequence. */
static void test_terminal_leave_alt_screen_survives_a_full_non_blocking_descriptor(void) {
	int fds[2];
	TEST_ASSERT_EQUAL_INT(0, pipe(fds));
	fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL) | O_NONBLOCK);
	terminal_enter_alt_screen(fds[1]);
	fill_pipe(fds[1]);
	pid_t reader = spawn_tail_checker(fds[0], fds[1], "\x1b[?1049l");
	terminal_leave_alt_screen(fds[1]);
	close(fds[1]);
	int status = exit_status_of(reader);
	/* put the module back to "not active" whatever happened, so that the other tests are not affected */
	int scratch[2];
	TEST_ASSERT_EQUAL_INT(0, pipe(scratch));
	terminal_leave_alt_screen(scratch[1]);
	close(scratch[0]);
	close(scratch[1]);
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, status, "the output did not end with the switch back from the alternate screen");
}

/** @brief A failed switch leaves the screen state as it was: not active after a failed enter, still active after a failed leave. */
static void test_terminal_alt_screen_state_follows_what_was_written(void) {
	int fds[2];
	TEST_ASSERT_EQUAL_INT(0, pipe(fds));
	close(fds[0]);
	signal(SIGPIPE, SIG_IGN);
	terminal_enter_alt_screen(fds[1]); /* fails: nobody reads */
	signal(SIGPIPE, SIG_DFL);
	close(fds[1]);
	int ok[2];
	TEST_ASSERT_EQUAL_INT(0, pipe(ok));
	terminal_leave_alt_screen(ok[1]); /* the enter failed, so this must write nothing */
	close(ok[1]);
	char got[16] = "";
	ssize_t n = read(ok[0], got, sizeof(got));
	close(ok[0]);
	TEST_ASSERT_EQUAL_INT(0, (int)n);
}

/** @brief Register every test in this file with Unity. */
void test_terminal_suite(void) {
	RUN_TEST(test_terminal_sets_raw_flags);
	RUN_TEST(test_terminal_preserves_unrelated_flags);
	RUN_TEST(test_enter_and_leave_raw_restores_terminal);
	RUN_TEST(test_enter_raw_twice_still_restores_original);
	RUN_TEST(test_leave_raw_without_enter_is_noop);
	RUN_TEST(test_leave_raw_twice_is_noop);
	RUN_TEST(test_terminal_get_size_reads_pty_size);
	RUN_TEST(test_terminal_get_size_falls_back_on_zero_size);
	RUN_TEST(test_terminal_get_size_falls_back_per_dimension);
	RUN_TEST(test_terminal_get_size_falls_back_when_not_a_tty);
	RUN_TEST(test_terminal_get_size_falls_back_on_invalid_fd);
	RUN_TEST(test_terminal_enter_alt_screen_writes_once);
	RUN_TEST(test_terminal_leave_alt_screen_writes_once_after_enter);
	RUN_TEST(test_terminal_write_all_writes_a_short_payload);
	RUN_TEST(test_terminal_write_all_of_nothing_succeeds);
	RUN_TEST(test_terminal_write_all_fills_a_pipe_for_a_slow_reader);
	RUN_TEST(test_terminal_write_all_continues_after_a_partial_write_and_eagain);
	RUN_TEST(test_terminal_write_all_survives_signals_interrupting_the_write);
	RUN_TEST(test_terminal_write_all_reports_a_bad_descriptor);
	RUN_TEST(test_terminal_write_all_reports_a_closed_reader);
	RUN_TEST(test_terminal_write_all_reports_a_reader_that_quits_midway);
	RUN_TEST(test_terminal_leave_alt_screen_survives_a_full_non_blocking_descriptor);
	RUN_TEST(test_terminal_alt_screen_state_follows_what_was_written);
}
