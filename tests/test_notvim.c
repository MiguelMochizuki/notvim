/**
 * @file test_notvim.c
 * @brief End-to-end test of the notvim binary on a pty.
 */
#define _DEFAULT_SOURCE /* usleep, kill under -std=c11 */
#include <termios.h>
#include <pty.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "unity.h"
#include "test_notvim.h"

/** @brief Run ./notvim on a pty: it enters raw mode and quits on Ctrl+Q with status 0. */
static void test_notvim_binary_enters_raw_and_quits_on_ctrl_q(void) {
	int master;
	pid_t pid = forkpty(&master, NULL, NULL, NULL);
	TEST_ASSERT_TRUE(pid >= 0);
	if (pid == 0) {
		execl("./notvim", "./notvim", (char *)NULL);
		_exit(127);
	}

	struct termios t;
	int raw = 0;
	for (int i = 0; i < 100 && !raw; i++) { /* up to ~2s */
		TEST_ASSERT_EQUAL_INT(0, tcgetattr(master, &t));
		raw = !(t.c_lflag & (ICANON | ECHO | ISIG));
		if (!raw) usleep(20000);
	}
	if (!raw) kill(pid, SIGKILL);
	TEST_ASSERT_TRUE_MESSAGE(raw, "notvim never entered raw mode");

	char ctrl_q = 0x11;
	TEST_ASSERT_EQUAL_INT(1, (int)write(master, &ctrl_q, 1));

	int status = 0;
	pid_t done = 0;
	for (int i = 0; i < 100 && done == 0; i++) {
		done = waitpid(pid, &status, WNOHANG);
		if (done == 0) usleep(20000);
	}
	if (done == 0) kill(pid, SIGKILL);
	TEST_ASSERT_EQUAL_INT_MESSAGE(pid, done, "notvim did not exit on Ctrl+Q");
	TEST_ASSERT_TRUE(WIFEXITED(status));
	TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));

	close(master);
}

/** @brief Register every test in this file with Unity. */
void test_notvim_suite(void) {
	RUN_TEST(test_notvim_binary_enters_raw_and_quits_on_ctrl_q);
}
