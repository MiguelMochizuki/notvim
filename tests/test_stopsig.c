/**
 * @file test_stopsig.c
 * @brief Unit tests for stopsig.c. The signals are raised in this process, so the
 *        handlers must be removed again before a test ends.
 */
#define _POSIX_C_SOURCE 200809L /* sigaction under -std=c11 */
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "unity.h"
#include "test_stopsig.h"
#include "stopsig.h"

/** @brief Whether @p fd becomes readable within 200 ms. */
static int readable(int fd) {
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	return poll(&pfd, 1, 200) > 0 && (pfd.revents & POLLIN);
}

/** @brief The descriptor is valid, quiet at first, and no signal is reported. */
static void test_stopsig_install_returns_a_quiet_descriptor(void) {
	int fd = stopsig_install();
	int is_readable = fd >= 0 && readable(fd);
	int received = stopsig_received();
	stopsig_remove();
	TEST_ASSERT_TRUE(fd >= 0);
	TEST_ASSERT_FALSE(is_readable);
	TEST_ASSERT_EQUAL_INT(0, received);
}

/** @brief SIGTERM, SIGHUP and SIGINT are caught: the process lives, the pipe wakes up, the number is recorded. */
static void test_stopsig_catches_term_hup_and_int(void) {
	const int signals[] = { SIGTERM, SIGHUP, SIGINT };
	int woke[3], received[3];
	for (int i = 0; i < 3; i++) {
		int fd = stopsig_install();
		raise(signals[i]);
		woke[i] = fd >= 0 && readable(fd);
		received[i] = stopsig_received();
		stopsig_remove();
	}
	for (int i = 0; i < 3; i++) {
		TEST_ASSERT_TRUE(woke[i]);
		TEST_ASSERT_EQUAL_INT(signals[i], received[i]);
	}
}

/** @brief Installing twice gives the same descriptor. */
static void test_stopsig_install_twice_returns_the_same_descriptor(void) {
	int first = stopsig_install();
	int second = stopsig_install();
	stopsig_remove();
	TEST_ASSERT_TRUE(first >= 0);
	TEST_ASSERT_EQUAL_INT(first, second);
}

/** @brief Removing puts back the handler that was set before, and forgets the signal. */
static void test_stopsig_remove_restores_the_previous_handler(void) {
	struct sigaction mine, saved, now;
	memset(&mine, 0, sizeof(mine));
	mine.sa_handler = SIG_IGN;
	sigaction(SIGTERM, &mine, &saved);

	stopsig_install();
	raise(SIGTERM);
	stopsig_remove();

	sigaction(SIGTERM, &saved, &now); /* read what is set now, and put the original back */
	TEST_ASSERT_TRUE(now.sa_handler == SIG_IGN);
	TEST_ASSERT_EQUAL_INT(0, stopsig_received());
}

/** @brief Removing without installing, or twice, does nothing. */
static void test_stopsig_remove_is_safe_when_not_installed(void) {
	stopsig_remove();
	stopsig_install();
	stopsig_remove();
	stopsig_remove();
	TEST_ASSERT_EQUAL_INT(0, stopsig_received());
}

/** @brief Register every test in this file with Unity. */
void test_stopsig_suite(void) {
	RUN_TEST(test_stopsig_install_returns_a_quiet_descriptor);
	RUN_TEST(test_stopsig_catches_term_hup_and_int);
	RUN_TEST(test_stopsig_install_twice_returns_the_same_descriptor);
	RUN_TEST(test_stopsig_remove_restores_the_previous_handler);
	RUN_TEST(test_stopsig_remove_is_safe_when_not_installed);
}
