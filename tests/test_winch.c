/**
 * @file test_winch.c
 * @brief Unit tests for winch.c. SIGWINCH is raised in this process, so the
 *        handler must be removed again before a test ends.
 */
#define _POSIX_C_SOURCE 200809L /* sigaction under -std=c11 */
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "unity.h"
#include "test_winch.h"
#include "winch.h"

/** @brief Whether @p fd becomes readable within 200 ms. */
static int readable(int fd) {
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	return poll(&pfd, 1, 200) > 0 && (pfd.revents & POLLIN);
}

/** @brief The descriptor is valid, quiet at first, and no resize is reported. */
static void test_winch_install_returns_a_quiet_descriptor(void) {
	int fd = winch_install();
	int is_readable = fd >= 0 && readable(fd);
	int flags = fd >= 0 ? fcntl(fd, F_GETFL) : 0;
	int fdflags = fd >= 0 ? fcntl(fd, F_GETFD) : 0;
	/* a blocking read end would hang here, so the flags are read first and checked first */
	int resized = (flags & O_NONBLOCK) ? winch_drain() : 0;
	winch_remove();
	TEST_ASSERT_TRUE(fd >= 0);
	TEST_ASSERT_TRUE_MESSAGE(flags & O_NONBLOCK, "drain would block on an empty pipe");
	TEST_ASSERT_TRUE(fdflags & FD_CLOEXEC);
	TEST_ASSERT_FALSE(is_readable);
	TEST_ASSERT_EQUAL_INT(0, resized);
}

/** @brief SIGWINCH is caught: the process lives and the pipe wakes up. */
static void test_winch_catches_sigwinch_and_wakes_the_pipe(void) {
	int fd = winch_install();
	raise(SIGWINCH);
	int woke = fd >= 0 && readable(fd);
	winch_remove();
	TEST_ASSERT_TRUE(fd >= 0);
	TEST_ASSERT_TRUE(woke);
}

/** @brief The drain reports a resize once, then nothing, and leaves the pipe quiet. */
static void test_winch_drain_reports_a_resize_once(void) {
	int fd = winch_install();
	raise(SIGWINCH);
	int first = winch_drain();
	int quiet_after = fd >= 0 && !readable(fd);
	int second = winch_drain();
	winch_remove();
	TEST_ASSERT_TRUE(fd >= 0);
	TEST_ASSERT_EQUAL_INT(1, first);
	TEST_ASSERT_TRUE(quiet_after);
	TEST_ASSERT_EQUAL_INT(0, second);
}

/** @brief A burst of resizes is one report: one drain empties the pipe. */
static void test_winch_a_burst_coalesces_into_one_report(void) {
	int fd = winch_install();
	for (int i = 0; i < 5; i++) raise(SIGWINCH);
	int first = winch_drain();
	int quiet_after = fd >= 0 && !readable(fd);
	winch_remove();
	TEST_ASSERT_EQUAL_INT(1, first);
	TEST_ASSERT_TRUE(quiet_after);
}

/** @brief Installing twice gives the same descriptor. */
static void test_winch_install_twice_returns_the_same_descriptor(void) {
	int first = winch_install();
	int second = winch_install();
	winch_remove();
	TEST_ASSERT_TRUE(first >= 0);
	TEST_ASSERT_EQUAL_INT(first, second);
}

/** @brief Installing twice does not save its own handler: remove still restores the original. */
static void test_winch_install_twice_then_remove_restores_the_original_handler(void) {
	struct sigaction mine, saved, now;
	memset(&mine, 0, sizeof(mine));
	mine.sa_handler = SIG_IGN;
	sigaction(SIGWINCH, &mine, &saved);
	winch_install();
	winch_install();
	winch_remove();
	sigaction(SIGWINCH, &saved, &now);
	TEST_ASSERT_TRUE(now.sa_handler == SIG_IGN);
}

/** @brief Removing puts back the handler that was set before, and forgets the resize. */
static void test_winch_remove_restores_the_previous_handler(void) {
	struct sigaction mine, saved, now;
	memset(&mine, 0, sizeof(mine));
	mine.sa_handler = SIG_IGN;
	sigaction(SIGWINCH, &mine, &saved);

	int fd = winch_install();
	raise(SIGWINCH);
	winch_remove();

	sigaction(SIGWINCH, &saved, &now); /* read what is set now, and put the original back */
	TEST_ASSERT_TRUE(fd >= 0);
	TEST_ASSERT_TRUE(now.sa_handler == SIG_IGN);
	TEST_ASSERT_EQUAL_INT(0, winch_drain());
}

/** @brief Draining or removing without installing, or removing twice, does nothing. */
static void test_winch_is_safe_when_not_installed(void) {
	TEST_ASSERT_EQUAL_INT(0, winch_drain());
	winch_remove();
	int fd = winch_install();
	winch_remove();
	winch_remove();
	TEST_ASSERT_TRUE(fd >= 0);
	TEST_ASSERT_EQUAL_INT(0, winch_drain());
}

/** @brief Register every test in this file with Unity. */
void test_winch_suite(void) {
	RUN_TEST(test_winch_install_returns_a_quiet_descriptor);
	RUN_TEST(test_winch_catches_sigwinch_and_wakes_the_pipe);
	RUN_TEST(test_winch_drain_reports_a_resize_once);
	RUN_TEST(test_winch_a_burst_coalesces_into_one_report);
	RUN_TEST(test_winch_install_twice_returns_the_same_descriptor);
	RUN_TEST(test_winch_install_twice_then_remove_restores_the_original_handler);
	RUN_TEST(test_winch_remove_restores_the_previous_handler);
	RUN_TEST(test_winch_is_safe_when_not_installed);
}
