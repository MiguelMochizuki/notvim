/**
 * @file test_notvim.c
 * @brief End-to-end tests of the notvim binary on a pty.
 */
#define _DEFAULT_SOURCE /* usleep, kill under -std=c11 */
#include <errno.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include "unity.h"
#include "test_notvim.h"
#include "tmpdir.h"

/**
 * @brief Start ./notvim on a new pty with @p rows rows and 80 columns.
 * @param file   Argument for notvim, or NULL for none.
 * @param rows   Terminal height.
 * @param master Receives the pty master descriptor.
 * @return The child's pid.
 */
static pid_t spawn_notvim(const char *file, unsigned short rows, int *master) {
	struct winsize ws = { .ws_row = rows, .ws_col = 80 };
	pid_t pid = forkpty(master, NULL, NULL, &ws);
	if (pid == 0) {
		execl("./notvim", "./notvim", file, (char *)NULL);
		_exit(127);
	}
	return pid;
}

/** @brief Wait up to ~2s for the terminal on @p master to enter raw mode; 1 if it did. */
static int wait_until_raw(int master) {
	struct termios t;
	for (int i = 0; i < 100; i++) {
		if (tcgetattr(master, &t) == 0 && !(t.c_lflag & (ICANON | ECHO | ISIG))) return 1;
		usleep(20000);
	}
	return 0;
}

/** @brief Read what the child writes until 300ms of silence or EOF; NUL-terminate; return the length. */
static size_t read_output(int master, char *buf, size_t size) {
	size_t len = 0;
	struct pollfd pfd = { .fd = master, .events = POLLIN };
	while (len < size - 1 && poll(&pfd, 1, 300) > 0) {
		ssize_t n = read(master, buf + len, size - 1 - len);
		if (n <= 0) break;
		len += (size_t)n;
	}
	buf[len] = '\0';
	return len;
}

/** @brief Wait up to ~2s for @p pid to exit; return its exit status, or -1 (after SIGKILL) otherwise. */
static int wait_exit(pid_t pid) {
	int status = 0;
	for (int i = 0; i < 100; i++) {
		if (waitpid(pid, &status, WNOHANG) == pid) return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
		usleep(20000);
	}
	kill(pid, SIGKILL);
	waitpid(pid, &status, 0);
	return -1;
}

/** @brief Send Ctrl+Q to the child and return wait_exit() of it. */
static int quit_and_wait(int master, pid_t pid) {
	char ctrl_q = 0x11;
	if (write(master, &ctrl_q, 1) != 1) kill(pid, SIGKILL);
	return wait_exit(pid);
}

/** @brief With no argument, notvim enters raw mode and quits on Ctrl+Q with status 0. */
static void test_notvim_binary_enters_raw_and_quits_on_ctrl_q(void) {
	int master;
	pid_t pid = spawn_notvim(NULL, 24, &master);
	int raw = wait_until_raw(master);
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE_MESSAGE(raw, "notvim never entered raw mode");
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, status, "notvim did not exit 0 on Ctrl+Q");
}

/** @brief notvim file shows the file's lines, separated by CRLF. */
static void test_notvim_shows_file_lines(void) {
	const char *path = tmpdir_write("three.txt", "line1\nline2\nline3\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char out[256];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, out, sizeof(out));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING("line1\r\nline2\r\nline3", out);
}

/** @brief A file taller than the terminal is cut to the terminal height. */
static void test_notvim_shows_only_the_rows_that_fit(void) {
	const char *path = tmpdir_write("four.txt", "a\nb\nc\nd\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char out[256];
	pid_t pid = spawn_notvim(path, 2, &master);
	int raw = wait_until_raw(master);
	read_output(master, out, sizeof(out));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING("a\r\nb", out);
}

/** @brief A screenful of long lines (more than 1 KB) is shown whole. */
static void test_notvim_shows_a_full_screen_of_long_lines(void) {
	char content[2500] = "";
	char expected[2500] = "";
	char line[80];
	for (int i = 0; i < 30; i++) {
		memset(line, 'a' + i % 26, 70);
		line[70] = '\0';
		strcat(content, line);
		strcat(content, "\n");
		if (i < 24) {
			if (i > 0) strcat(expected, "\r\n");
			strcat(expected, line);
		}
	}
	const char *path = tmpdir_write("big.txt", content);
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char out[4096];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, out, sizeof(out));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/** @brief A missing file starts an empty editor, writes nothing and is not created. */
static void test_notvim_missing_file_starts_empty_and_is_not_created(void) {
	const char *path = tmpdir_path("new.txt");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char out[256];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	size_t n = read_output(master, out, sizeof(out));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_UINT(0, n);
	struct stat st;
	TEST_ASSERT_EQUAL_INT(-1, stat(path, &st));
	TEST_ASSERT_EQUAL_INT(ENOENT, errno);
}

/** @brief A path that can't be loaded prints "notvim: <path>: ..." and exits 1. */
static void test_notvim_load_error_reports_and_exits_1(void) {
	const char *path = tmpdir_path("."); /* a directory: fopen works, reading fails */
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char out[512];
	pid_t pid = spawn_notvim(path, 24, &master);
	read_output(master, out, sizeof(out));
	int status = wait_exit(pid);
	close(master);
	TEST_ASSERT_EQUAL_INT(1, status);
	TEST_ASSERT_EQUAL_INT(0, strncmp(out, "notvim: ", 8));
	TEST_ASSERT_NOT_NULL(strstr(out, path));
}

/** @brief Register every test in this file with Unity. */
void test_notvim_suite(void) {
	RUN_TEST(test_notvim_binary_enters_raw_and_quits_on_ctrl_q);
	RUN_TEST(test_notvim_shows_file_lines);
	RUN_TEST(test_notvim_shows_only_the_rows_that_fit);
	RUN_TEST(test_notvim_shows_a_full_screen_of_long_lines);
	RUN_TEST(test_notvim_missing_file_starts_empty_and_is_not_created);
	RUN_TEST(test_notvim_load_error_reports_and_exits_1);
}
