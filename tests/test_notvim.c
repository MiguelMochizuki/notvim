/**
 * @file test_notvim.c
 * @brief End-to-end tests of the notvim binary on a pty.
 */
#define _DEFAULT_SOURCE /* usleep, kill under -std=c11 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
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

/**
 * @brief Start ./notvim with the given descriptors as stdin, stdout and stderr.
 * @param file   Argument for notvim.
 * @param in_fd  Descriptor to use as stdin.
 * @param out_fd Descriptor to use as stdout.
 * @param err_fd Descriptor to use as stderr.
 * @return The child's pid.
 */
static pid_t spawn_notvim_fds(const char *file, int in_fd, int out_fd, int err_fd) {
	pid_t pid = fork();
	if (pid == 0) {
		dup2(in_fd, STDIN_FILENO);
		dup2(out_fd, STDOUT_FILENO);
		dup2(err_fd, STDERR_FILENO);
		execl("./notvim", "./notvim", file, (char *)NULL);
		_exit(127);
	}
	return pid;
}

/** @brief Make both ends of a pipe close on exec, so a child does not keep them open. */
static void pipe_cloexec(int fds[2]) {
	TEST_ASSERT_EQUAL_INT(0, pipe(fds));
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(fds[1], F_SETFD, FD_CLOEXEC);
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

/** @brief Write the screen notvim should draw for @p text with the cursor at row @p row, column @p col (1-based). */
static void screen(char *buf, size_t size, const char *text, int row, int col) {
	snprintf(buf, size, "\x1b[H\x1b[2J%s\x1b[%d;%dH", text, row, col);
}

/** Sequences notvim writes when it enters and leaves the alternate screen. */
#define ALT_ENTER "\x1b[?1049h"
#define ALT_LEAVE "\x1b[?1049l"

/** @brief Like screen(), preceded by the switch to the alternate screen: what notvim writes first. */
static void first_screen(char *buf, size_t size, const char *text, int row, int col) {
	size_t n = (size_t)snprintf(buf, size, "%s", ALT_ENTER);
	screen(buf + n, size - n, text, row, col);
}

/** @brief Send @p keys to the child, then read what it draws in answer; return the length. */
static size_t send_and_read(int master, const char *keys, char *out, size_t size) {
	if (write(master, keys, strlen(keys)) < 0) {
		out[0] = '\0';
		return 0;
	}
	return read_output(master, out, size);
}

/** @brief Set the pty size; the kernel then sends SIGWINCH to the child. */
static void set_size(int master, unsigned short rows, unsigned short cols) {
	struct winsize ws = { .ws_row = rows, .ws_col = cols };
	ioctl(master, TIOCSWINSZ, &ws);
}

/**
 * @brief Write the lines of the 30-line resize file, numbered from 1 (two digits, then 'x' up to 40 bytes).
 * @param content Receives the file content.
 * @param size    Size of @p content.
 */
static void resize_file_content(char *content, size_t size) {
	content[0] = '\0';
	for (int i = 1; i <= 30; i++) {
		char line[64];
		snprintf(line, sizeof(line), "%02dxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n", i);
		strncat(content, line, size - strlen(content) - 1);
	}
}

/**
 * @brief Start notvim on the 30-line resize file with @p rows rows, wait for raw mode and read the first screen.
 * @param master Receives the pty master descriptor.
 * @param raw    Receives whether the terminal entered raw mode (the caller asserts it after cleanup).
 * @return The child's pid.
 */
static pid_t spawn_resize_notvim(unsigned short rows, int *master, int *raw) {
	char content[2048], first[2048];
	resize_file_content(content, sizeof(content));
	const char *path = tmpdir_write("resize.txt", content);
	TEST_ASSERT_NOT_NULL(path); /* before the spawn: no child to clean up yet */
	pid_t pid = spawn_notvim(path, rows, master);
	*raw = wait_until_raw(*master);
	read_output(*master, first, sizeof(first));
	return pid;
}

/**
 * @brief Write the screen for lines @p first to @p last of the resize file, clipped to @p cols columns.
 * @param row Cursor row on the screen (1-based); the cursor is in column 1.
 * @note @p cols must be at most 40, the width of a line of the resize file.
 */
static void resize_screen(char *buf, size_t size, int first, int last, int cols, int row) {
	char text[2048] = "";
	for (int i = first; i <= last; i++) {
		char line[64];
		snprintf(line, sizeof(line), "%02dxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", i);
		line[cols] = '\0';
		if (i > first) strcat(text, "\r\n");
		strcat(text, line);
	}
	screen(buf, size, text, row, 1);
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
	char expected[256];
	first_screen(expected, sizeof(expected), "line1\r\nline2\r\nline3", 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, out);
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
	char expected[256];
	first_screen(expected, sizeof(expected), "a\r\nb", 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/** @brief A screenful of long lines (more than 1 KB) is shown whole. */
static void test_notvim_shows_a_full_screen_of_long_lines(void) {
	char content[2500] = "";
	char text[2500] = "";
	char line[80];
	for (int i = 0; i < 30; i++) {
		memset(line, 'a' + i % 26, 70);
		line[70] = '\0';
		strcat(content, line);
		strcat(content, "\n");
		if (i < 24) {
			if (i > 0) strcat(text, "\r\n");
			strcat(text, line);
		}
	}
	const char *path = tmpdir_write("big.txt", content);
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char out[4096], expected[4096];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, out, sizeof(out));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	first_screen(expected, sizeof(expected), text, 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/** @brief Lines wider than the terminal are clipped, so nothing wraps and scrolls the screen. */
static void test_notvim_clips_long_lines_to_the_terminal_width(void) {
	char content[512] = "";
	char text[512] = "";
	char line[128], clipped[128];
	for (int i = 0; i < 3; i++) {
		memset(line, 'a' + i, 100);
		line[100] = '\0';
		memset(clipped, 'a' + i, 80); /* the terminal is 80 columns wide */
		clipped[80] = '\0';
		strcat(content, line);
		strcat(content, "\n");
		if (i > 0) strcat(text, "\r\n");
		strcat(text, clipped);
	}
	const char *path = tmpdir_write("wide.txt", content);
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char out[1024], expected[1024];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, out, sizeof(out));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	first_screen(expected, sizeof(expected), text, 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/** @brief A file with an escape sequence is shown as text and cannot clear the screen. */
static void test_notvim_shows_escape_sequences_in_a_file_as_text(void) {
	const char *path = tmpdir_write("danger.txt", "a\x1b[2Jb\nline2\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char out[256], expected[256];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, out, sizeof(out));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	first_screen(expected, sizeof(expected), "a^[[2Jb\r\nline2", 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, out);
	/* the only clear-screen sequence is the one notvim itself sends */
	const char *first_clear = strstr(out, "\x1b[2J");
	TEST_ASSERT_NOT_NULL(first_clear);
	TEST_ASSERT_NULL(strstr(first_clear + 1, "\x1b[2J"));
}

/** @brief Lines full of tabs fill exactly the screen: they no longer wrap and scroll it. */
static void test_notvim_tabs_do_not_overflow_the_screen(void) {
	char content[1024] = "";
	char text[2048] = "";
	char blanks[81];
	memset(blanks, ' ', 80);
	blanks[80] = '\0';
	for (int i = 0; i < 24; i++) {
		strcat(content, "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\tx\n"); /* 20 tabs: 160 columns */
		if (i > 0) strcat(text, "\r\n");
		strcat(text, blanks); /* clipped to the 80 columns of the terminal */
	}
	const char *path = tmpdir_write("tabs.txt", content);
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char out[4096], expected[4096];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, out, sizeof(out));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	first_screen(expected, sizeof(expected), text, 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/** @brief A missing file starts an empty editor, draws an empty screen and is not created. */
static void test_notvim_missing_file_starts_empty_and_is_not_created(void) {
	const char *path = tmpdir_path("new.txt");
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
	char expected[256];
	first_screen(expected, sizeof(expected), "", 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, out);
	struct stat st;
	TEST_ASSERT_EQUAL_INT(-1, stat(path, &st));
	TEST_ASSERT_EQUAL_INT(ENOENT, errno);
}

/** @brief Arrow keys move the cursor and each one redraws the screen. */
static void test_notvim_arrow_keys_move_the_cursor_and_redraw(void) {
	const char *path = tmpdir_write("arrows.txt", "abc\ndef\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[256], down[256], right[256], up[256], left[256];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "\x1b[B", down, sizeof(down));
	send_and_read(master, "\x1b[C", right, sizeof(right));
	send_and_read(master, "\x1b[A", up, sizeof(up));
	send_and_read(master, "\x1b[D", left, sizeof(left));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	char expected[256];
	first_screen(expected, sizeof(expected), "abc\r\ndef", 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, first);
	screen(expected, sizeof(expected), "abc\r\ndef", 2, 1);
	TEST_ASSERT_EQUAL_STRING(expected, down);
	screen(expected, sizeof(expected), "abc\r\ndef", 2, 2);
	TEST_ASSERT_EQUAL_STRING(expected, right);
	screen(expected, sizeof(expected), "abc\r\ndef", 1, 2);
	TEST_ASSERT_EQUAL_STRING(expected, up);
	screen(expected, sizeof(expected), "abc\r\ndef", 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, left);
}

/** @brief j, l, k and h move the cursor down, right, up and left, like the arrow keys. */
static void test_notvim_hjkl_move_the_cursor_and_redraw(void) {
	const char *path = tmpdir_write("hjkl.txt", "abc\ndef\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[256], down[256], right[256], up[256], left[256];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "j", down, sizeof(down));
	send_and_read(master, "l", right, sizeof(right));
	send_and_read(master, "k", up, sizeof(up));
	send_and_read(master, "h", left, sizeof(left));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	char expected[256];
	screen(expected, sizeof(expected), "abc\r\ndef", 2, 1);
	TEST_ASSERT_EQUAL_STRING(expected, down);
	screen(expected, sizeof(expected), "abc\r\ndef", 2, 2);
	TEST_ASSERT_EQUAL_STRING(expected, right);
	screen(expected, sizeof(expected), "abc\r\ndef", 1, 2);
	TEST_ASSERT_EQUAL_STRING(expected, up);
	screen(expected, sizeof(expected), "abc\r\ndef", 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, left);
}

/** @brief Uppercase H, J, K and L are not movement keys and do not redraw. */
static void test_notvim_uppercase_hjkl_do_nothing(void) {
	const char *path = tmpdir_write("upper.txt", "abc\ndef\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[256], after[256];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	size_t n = send_and_read(master, "HJKL", after, sizeof(after));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_UINT(0, n);
}

/** @brief An escape sequence that is not an arrow (Delete) is ignored and does not redraw. */
static void test_notvim_ignored_escape_sequence_does_not_redraw(void) {
	const char *path = tmpdir_write("ignored.txt", "abc\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[256], after[256];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	size_t n = send_and_read(master, "\x1b[3~", after, sizeof(after));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_UINT(0, n);
}

/** @brief Moving the cursor past the last visible row scrolls the screen, and back up scrolls it back. */
static void test_notvim_scrolls_when_the_cursor_leaves_the_screen(void) {
	const char *path = tmpdir_write("scroll.txt", "a\nb\nc\nd\ne\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[256], d1[256], d2[256], d3[256], u1[256], u3[256];
	pid_t pid = spawn_notvim(path, 3, &master); /* a 3-row terminal */
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "j", d1, sizeof(d1));
	send_and_read(master, "j", d2, sizeof(d2));
	send_and_read(master, "j", d3, sizeof(d3));
	send_and_read(master, "k", u1, sizeof(u1));
	send_and_read(master, "kkk", u3, sizeof(u3)); /* up to the first line again */
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	char expected[256];
	first_screen(expected, sizeof(expected), "a\r\nb\r\nc", 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, first);
	screen(expected, sizeof(expected), "a\r\nb\r\nc", 2, 1);
	TEST_ASSERT_EQUAL_STRING(expected, d1);
	screen(expected, sizeof(expected), "a\r\nb\r\nc", 3, 1);
	TEST_ASSERT_EQUAL_STRING(expected, d2);
	screen(expected, sizeof(expected), "b\r\nc\r\nd", 3, 1); /* scrolled by one line */
	TEST_ASSERT_EQUAL_STRING(expected, d3);
	screen(expected, sizeof(expected), "b\r\nc\r\nd", 2, 1); /* still the same window */
	TEST_ASSERT_EQUAL_STRING(expected, u1);
	/* kkk is three redraws: line 2 in the same window, then line 1 scrolls back, then it stays */
	char same_window[128], top[128], three[384];
	screen(same_window, sizeof(same_window), "b\r\nc\r\nd", 1, 1);
	screen(top, sizeof(top), "a\r\nb\r\nc", 1, 1);
	snprintf(three, sizeof(three), "%s%s%s", same_window, top, top);
	TEST_ASSERT_EQUAL_STRING(three, u3);
}

/** @brief A lone Esc does not redraw and does not swallow the key that comes after it. */
static void test_notvim_lone_escape_does_not_swallow_the_next_key(void) {
	const char *path = tmpdir_write("esc.txt", "a\nb\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[256], after_esc[256], after_j[256];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	/* send_and_read waits 300 ms for output, far longer than the Esc timeout */
	size_t n = send_and_read(master, "\x1b", after_esc, sizeof(after_esc));
	send_and_read(master, "j", after_j, sizeof(after_j));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_UINT(0, n);
	char expected[256];
	screen(expected, sizeof(expected), "a\r\nb", 2, 1);
	TEST_ASSERT_EQUAL_STRING(expected, after_j);
}

/** @brief An arrow key whose bytes arrive a few milliseconds apart is still an arrow. */
static void test_notvim_arrow_split_across_writes_still_works(void) {
	const char *path = tmpdir_write("split.txt", "a\nb\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[256], after[256];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	ssize_t w1 = write(master, "\x1b", 1);
	usleep(10000); /* well inside the Esc timeout */
	ssize_t w2 = write(master, "[B", 2);
	read_output(master, after, sizeof(after));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_INT(1, (int)w1);
	TEST_ASSERT_EQUAL_INT(2, (int)w2);
	char expected[256];
	screen(expected, sizeof(expected), "a\r\nb", 2, 1);
	TEST_ASSERT_EQUAL_STRING(expected, after);
}

/** @brief An ordinary key does nothing yet and does not redraw. */
static void test_notvim_ordinary_key_does_not_redraw(void) {
	const char *path = tmpdir_write("plain.txt", "abc\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[256], after[256];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	size_t n = send_and_read(master, "a", after, sizeof(after));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_UINT(0, n);
}

/** @brief Quitting switches back from the alternate screen and writes nothing else. */
static void test_notvim_leaves_the_alternate_screen_on_exit(void) {
	const char *path = tmpdir_write("leave.txt", "abc\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[256], last[256];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	char ctrl_q = 0x11;
	ssize_t sent = write(master, &ctrl_q, 1);
	read_output(master, last, sizeof(last));
	int status = wait_exit(pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(1, (int)sent);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING(ALT_LEAVE, last);
}

/** @brief A binary file is refused with a clear message on the normal screen and exit status 1. */
static void test_notvim_refuses_a_binary_file(void) {
	const char data[] = { 'a', '\0', 'b', '\n' };
	const char *path = tmpdir_write_bytes("binary.bin", data, sizeof(data));
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
	TEST_ASSERT_NOT_NULL(strstr(out, "binary file"));
	TEST_ASSERT_NULL(strstr(out, "\x1b[?1049"));
}

/** @brief With stdin not a terminal, notvim refuses to start, says so, and writes nothing to the terminal. */
static void test_notvim_refuses_when_input_is_not_a_terminal(void) {
	const char *path = tmpdir_write("plain.txt", "hello\n");
	TEST_ASSERT_NOT_NULL(path);
	int master, slave, err[2];
	TEST_ASSERT_EQUAL_INT(0, openpty(&master, &slave, NULL, NULL, NULL));
	pipe_cloexec(err);
	int devnull = open("/dev/null", O_RDONLY);
	TEST_ASSERT_TRUE(devnull >= 0);
	pid_t pid = spawn_notvim_fds(path, devnull, slave, err[1]);
	close(err[1]);
	char message[256], terminal[256];
	read_output(err[0], message, sizeof(message));
	size_t written = read_output(master, terminal, sizeof(terminal));
	int status = wait_exit(pid);
	close(err[0]); close(master); close(slave); close(devnull);
	TEST_ASSERT_EQUAL_INT(1, status);
	TEST_ASSERT_EQUAL_STRING("notvim: input is not a terminal\n", message);
	TEST_ASSERT_EQUAL_UINT(0, written);
}

/** @brief With stdout not a terminal, notvim refuses to start and writes nothing into the pipe. */
static void test_notvim_refuses_when_output_is_not_a_terminal(void) {
	const char *path = tmpdir_write("plain.txt", "hello\n");
	TEST_ASSERT_NOT_NULL(path);
	int master, slave, out[2], err[2];
	TEST_ASSERT_EQUAL_INT(0, openpty(&master, &slave, NULL, NULL, NULL));
	pipe_cloexec(out);
	pipe_cloexec(err);
	pid_t pid = spawn_notvim_fds(path, slave, out[1], err[1]);
	close(out[1]); close(err[1]);
	char piped[256], message[256];
	size_t written = read_output(out[0], piped, sizeof(piped));
	read_output(err[0], message, sizeof(message));
	int status = wait_exit(pid);
	close(out[0]); close(err[0]); close(master); close(slave);
	TEST_ASSERT_EQUAL_INT(1, status);
	TEST_ASSERT_EQUAL_STRING("notvim: output is not a terminal\n", message);
	TEST_ASSERT_EQUAL_UINT(0, written);
}

/**
 * @brief Send @p sig to a running notvim and check that it restores the terminal and exits with 128 + @p sig.
 */
static void assert_restores_the_terminal_on_signal(int sig) {
	const char *path = tmpdir_write("signal.txt", "abc\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[256], last[256];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	kill(pid, sig);
	read_output(master, last, sizeof(last));
	int status = wait_exit(pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_STRING(ALT_LEAVE, last);
	TEST_ASSERT_EQUAL_INT(128 + sig, status);
}

/** @brief SIGTERM switches back from the alternate screen and exits with 143. */
static void test_notvim_restores_the_terminal_on_sigterm(void) {
	assert_restores_the_terminal_on_signal(SIGTERM);
}

/** @brief SIGHUP, as sent when the terminal closes, does the same and exits with 129. */
static void test_notvim_restores_the_terminal_on_sighup(void) {
	assert_restores_the_terminal_on_signal(SIGHUP);
}

/** @brief SIGINT sent by kill (Ctrl+C is only a byte in raw mode) does the same and exits with 130. */
static void test_notvim_restores_the_terminal_on_sigint(void) {
	assert_restores_the_terminal_on_signal(SIGINT);
}

/** @brief Shrinking the terminal to 5 rows by 30 columns redraws for the new size. */
static void test_notvim_redraws_for_the_new_size_after_a_shrink(void) {
	int master, raw;
	char after[2048], expected[2048];
	pid_t pid = spawn_resize_notvim(24, &master, &raw);
	set_size(master, 5, 30);
	read_output(master, after, sizeof(after));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	resize_screen(expected, sizeof(expected), 1, 5, 30, 1);
	TEST_ASSERT_EQUAL_STRING(expected, after);
}

/** @brief Shrinking below the cursor row scrolls the window so the cursor stays visible. */
static void test_notvim_scrolls_to_keep_the_cursor_visible_after_a_shrink(void) {
	int master, raw;
	char moved[16384], after[2048], expected[2048];
	pid_t pid = spawn_resize_notvim(24, &master, &raw);
	/* ten redraws of about 1 KB: the buffer holds them all, so none is left to be read as "after" */
	size_t n = send_and_read(master, "jjjjjjjjjj", moved, sizeof(moved));
	set_size(master, 5, 80);
	read_output(master, after, sizeof(after));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_TRUE(n >= 7);
	TEST_ASSERT_EQUAL_STRING("\x1b[11;1H", moved + n - 7); /* the cursor was on line 11 before the resize */
	resize_screen(expected, sizeof(expected), 7, 11, 40, 5); /* lines 7 to 11, cursor on the last row */
	TEST_ASSERT_EQUAL_STRING(expected, after);
}

/** @brief Growing the terminal shows more rows. */
static void test_notvim_shows_more_rows_after_the_terminal_grows(void) {
	int master, raw;
	char after[2048], expected[2048];
	pid_t pid = spawn_resize_notvim(5, &master, &raw);
	set_size(master, 8, 80);
	read_output(master, after, sizeof(after));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	resize_screen(expected, sizeof(expected), 1, 8, 40, 1);
	TEST_ASSERT_EQUAL_STRING(expected, after);
}

/** @brief After a burst of resizes the last size wins: the output ends with the redraw for it. */
static void test_notvim_last_size_wins_after_a_burst_of_resizes(void) {
	int master, raw;
	char after[8192], expected[2048];
	pid_t pid = spawn_resize_notvim(24, &master, &raw);
	set_size(master, 10, 80);
	set_size(master, 7, 80);
	set_size(master, 4, 80);
	size_t n = read_output(master, after, sizeof(after));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	resize_screen(expected, sizeof(expected), 1, 4, 40, 1);
	size_t len = strlen(expected);
	TEST_ASSERT_TRUE(n >= len);
	TEST_ASSERT_EQUAL_STRING(expected, after + n - len);
}

/** @brief Growing from 2 rows to 8 shows full-width lines: the draw buffer is reallocated for the new size. */
static void test_notvim_draw_buffer_grows_with_the_terminal(void) {
	char content[4096] = "";
	for (int i = 1; i <= 30; i++) {
		char line[128];
		snprintf(line, sizeof(line), "%02dyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy\n", i);
		strcat(content, line);
	}
	const char *path = tmpdir_write("wide-grow.txt", content);
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[2048], after[4096], expected[4096], text[4096] = "";
	pid_t pid = spawn_notvim(path, 2, &master); /* a buffer for 2 rows only */
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	set_size(master, 8, 80);
	read_output(master, after, sizeof(after));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	for (int i = 1; i <= 8; i++) {
		char line[128];
		snprintf(line, sizeof(line), "%02dyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy", i);
		line[80] = '\0';
		if (i > 1) strcat(text, "\r\n");
		strcat(text, line);
	}
	screen(expected, sizeof(expected), text, 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, after);
}

/** @brief Resizing to a tall, narrow terminal fits the buffer: it needs rows * (cols + 2), not cols * (rows + 2). */
static void test_notvim_draw_buffer_fits_a_tall_narrow_terminal(void) {
	int master, raw;
	char after[4096], expected[4096];
	pid_t pid = spawn_resize_notvim(24, &master, &raw);
	set_size(master, 30, 10);
	read_output(master, after, sizeof(after));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	resize_screen(expected, sizeof(expected), 1, 30, 10, 1);
	TEST_ASSERT_EQUAL_STRING(expected, after);
}

/** @brief A resize inside an escape sequence redraws and does not break the sequence: ESC, resize, [B is still Down. */
static void test_notvim_resize_inside_an_escape_sequence_keeps_the_sequence(void) {
	int master, raw;
	char after[4096], expected[4096], down[2048];
	pid_t pid = spawn_resize_notvim(24, &master, &raw);
	ssize_t w1 = write(master, "\x1b", 1);
	usleep(10000); /* all pauses stay well inside the Esc timeout */
	set_size(master, 5, 80);
	usleep(10000);
	ssize_t w2 = write(master, "[B", 2);
	read_output(master, after, sizeof(after));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_INT(1, (int)w1);
	TEST_ASSERT_EQUAL_INT(2, (int)w2);
	resize_screen(expected, sizeof(expected), 1, 5, 40, 1);
	resize_screen(down, sizeof(down), 1, 5, 40, 2);
	strcat(expected, down);
	TEST_ASSERT_EQUAL_STRING(expected, after);
}

/** @brief SIGTERM after a resize still restores the terminal and exits with 143. */
static void test_notvim_sigterm_after_a_resize_still_exits_cleanly(void) {
	int master, raw;
	char resized[2048], last[256];
	pid_t pid = spawn_resize_notvim(24, &master, &raw);
	set_size(master, 5, 80);
	size_t n = read_output(master, resized, sizeof(resized));
	kill(pid, SIGTERM);
	read_output(master, last, sizeof(last));
	int status = wait_exit(pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_TRUE(n > 0);
	TEST_ASSERT_EQUAL_STRING(ALT_LEAVE, last);
	TEST_ASSERT_EQUAL_INT(128 + SIGTERM, status);
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
	TEST_ASSERT_NULL_MESSAGE(strstr(out, "\x1b[?1049"), "the error must stay on the normal screen");
}

/** @brief Register every test in this file with Unity. */
void test_notvim_suite(void) {
	RUN_TEST(test_notvim_binary_enters_raw_and_quits_on_ctrl_q);
	RUN_TEST(test_notvim_shows_file_lines);
	RUN_TEST(test_notvim_shows_only_the_rows_that_fit);
	RUN_TEST(test_notvim_shows_a_full_screen_of_long_lines);
	RUN_TEST(test_notvim_arrow_keys_move_the_cursor_and_redraw);
	RUN_TEST(test_notvim_hjkl_move_the_cursor_and_redraw);
	RUN_TEST(test_notvim_uppercase_hjkl_do_nothing);
	RUN_TEST(test_notvim_ignored_escape_sequence_does_not_redraw);
	RUN_TEST(test_notvim_scrolls_when_the_cursor_leaves_the_screen);
	RUN_TEST(test_notvim_lone_escape_does_not_swallow_the_next_key);
	RUN_TEST(test_notvim_arrow_split_across_writes_still_works);
	RUN_TEST(test_notvim_ordinary_key_does_not_redraw);
	RUN_TEST(test_notvim_clips_long_lines_to_the_terminal_width);
	RUN_TEST(test_notvim_shows_escape_sequences_in_a_file_as_text);
	RUN_TEST(test_notvim_tabs_do_not_overflow_the_screen);
	RUN_TEST(test_notvim_missing_file_starts_empty_and_is_not_created);
	RUN_TEST(test_notvim_leaves_the_alternate_screen_on_exit);
	RUN_TEST(test_notvim_restores_the_terminal_on_sigterm);
	RUN_TEST(test_notvim_restores_the_terminal_on_sighup);
	RUN_TEST(test_notvim_restores_the_terminal_on_sigint);
	RUN_TEST(test_notvim_refuses_a_binary_file);
	RUN_TEST(test_notvim_refuses_when_input_is_not_a_terminal);
	RUN_TEST(test_notvim_refuses_when_output_is_not_a_terminal);
	RUN_TEST(test_notvim_load_error_reports_and_exits_1);
	RUN_TEST(test_notvim_redraws_for_the_new_size_after_a_shrink);
	RUN_TEST(test_notvim_scrolls_to_keep_the_cursor_visible_after_a_shrink);
	RUN_TEST(test_notvim_shows_more_rows_after_the_terminal_grows);
	RUN_TEST(test_notvim_last_size_wins_after_a_burst_of_resizes);
	RUN_TEST(test_notvim_draw_buffer_grows_with_the_terminal);
	RUN_TEST(test_notvim_draw_buffer_fits_a_tall_narrow_terminal);
	RUN_TEST(test_notvim_resize_inside_an_escape_sequence_keeps_the_sequence);
	RUN_TEST(test_notvim_sigterm_after_a_resize_still_exits_cleanly);
}
