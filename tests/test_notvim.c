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
#include "drawfmt.h"

/** Size of the pty of the last spawn_notvim_size() or set_size(): screen() draws for it (a full row gets no erase, a short screen is erased below). */
static unsigned short term_rows = 24, term_cols = 80;

/**
 * @brief Start ./notvim on a new pty of @p rows rows by @p cols columns.
 * @param file          Argument for notvim, or NULL for none.
 * @param rows          Terminal height.
 * @param cols          Terminal width.
 * @param nonblock_out  Non-zero to make the child's stdout non-blocking before it starts (the pty slave is shared by stdin and stdout).
 * @param master        Receives the pty master descriptor.
 * @return The child's pid.
 */
static pid_t spawn_notvim_full(const char *file, unsigned short rows, unsigned short cols, int nonblock_out, int *master) {
	struct winsize ws = { .ws_row = rows, .ws_col = cols };
	term_rows = rows;
	term_cols = cols;
	pid_t pid = forkpty(master, NULL, NULL, &ws);
	if (pid == 0) {
		if (nonblock_out) fcntl(STDOUT_FILENO, F_SETFL, fcntl(STDOUT_FILENO, F_GETFL) | O_NONBLOCK);
		execl("./notvim", "./notvim", file, (char *)NULL);
		_exit(127);
	}
	return pid;
}

/** @brief Start ./notvim on a new pty of @p rows rows by @p cols columns; see spawn_notvim_full(). */
static pid_t spawn_notvim_size(const char *file, unsigned short rows, unsigned short cols, int *master) {
	return spawn_notvim_full(file, rows, cols, 0, master);
}

/** @brief Start ./notvim on a new pty with @p rows rows and 80 columns. */
static pid_t spawn_notvim(const char *file, unsigned short rows, int *master) {
	return spawn_notvim_size(file, rows, 80, master);
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

/** @brief Write the screen notvim should draw, for the current pty size, for the rows @p text (NULL for none) with the cursor at row @p row, column @p col (1-based). */
static void screen(char *buf, size_t size, const char *text, int row, int col) {
	draw_expected(buf, size, text, term_rows, term_cols, row, col);
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
	term_rows = rows;
	term_cols = cols;
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

/** @brief A file with an escape sequence is shown as text, and there is no clear-screen sequence in the output at all. */
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
	TEST_ASSERT_NULL_MESSAGE(strstr(out, "\x1b[2J"), "notvim never clears the screen, so a file cannot either");
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
	first_screen(expected, sizeof(expected), NULL, 1, 1); /* an empty editor draws no row */
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
	TEST_ASSERT_TRUE(n >= 7 + 6);
	moved[n - 6] = '\0'; /* what follows the cursor position is the show-cursor sequence */
	TEST_ASSERT_EQUAL_STRING("\x1b[11;1H", moved + n - 6 - 7); /* the cursor was on line 11 before the resize */
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

/** @brief Number of elements of an array. */
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/** @brief Append @p count copies of @p unit to @p dst. */
static void append_repeated(char *dst, const char *unit, int count) {
	for (int i = 0; i < count; i++) strcat(dst, unit);
}

/** @brief A line of 100 e-acutes on 80 columns shows exactly 80 characters (the exact bytes, so no half character). */
static void test_notvim_clips_a_wide_utf8_line_by_characters(void) {
	char content[512] = "", text[512] = "";
	append_repeated(content, "\xc3\xa9", 100);
	strcat(content, "\nx\n");
	append_repeated(text, "\xc3\xa9", 80);
	strcat(text, "\r\nx");
	const char *path = tmpdir_write("wide.txt", content);
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[2048], expected[2048];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	first_screen(expected, sizeof(expected), text, 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, first);
}

/** @brief Invalid bytes and C1 controls (including U+009B, a CSI) reach the terminal as '?'. */
static void test_notvim_draws_invalid_bytes_and_c1_controls_as_question_marks(void) {
	const char *path = tmpdir_write("bad.txt", "a\xff" "b\xc2\x9b[2Jc\xe2\x82" "z\xe2\x1b[2Jq\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[512], expected[512];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	first_screen(expected, sizeof(expected), "a?b?[2Jc??z?^[[2Jq", 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, first);
}

/** Room for a screen of 24 lines of 80 four-byte characters, and for the file that holds it. */
#define SCREEN_BYTES 16384

/** @brief Run notvim on a file of 30 lines of 80 copies of @p unit, starting with @p start_rows rows, and compare the full 24x80 screen. */
static void assert_full_multibyte_screen(const char *unit, unsigned short start_rows) {
	static char content[SCREEN_BYTES], text[SCREEN_BYTES], expected[SCREEN_BYTES * 2], got[SCREEN_BYTES * 2], first[SCREEN_BYTES * 2];
	content[0] = text[0] = '\0';
	for (int i = 0; i < 30; i++) {
		append_repeated(content, unit, 80);
		strcat(content, "\n");
	}
	for (int i = 0; i < 24; i++) {
		if (i > 0) strcat(text, "\r\n");
		append_repeated(text, unit, 80);
	}
	const char *path = tmpdir_write("full.txt", content);
	TEST_ASSERT_NOT_NULL(path);
	int master;
	pid_t pid = spawn_notvim(path, start_rows, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	if (start_rows != 24) {
		set_size(master, 24, 80);
		read_output(master, got, sizeof(got));
	}
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	if (start_rows == 24) {
		first_screen(expected, sizeof(expected), text, 1, 1);
		TEST_ASSERT_EQUAL_STRING(expected, first);
	} else {
		screen(expected, sizeof(expected), text, 1, 1);
		TEST_ASSERT_EQUAL_STRING(expected, got);
	}
}

/** @brief A full 24x80 screen of 2-byte characters is drawn completely at startup. */
static void test_notvim_full_screen_of_two_byte_text_is_not_truncated(void) {
	assert_full_multibyte_screen("\xc3\xa9", 24);
}

/** @brief A full screen of 3-byte characters (5.8 KB) is drawn completely: a buffer of 2 bytes per column is too small. */
static void test_notvim_full_screen_of_three_byte_text_is_not_truncated(void) {
	assert_full_multibyte_screen("\xe2\x82\xac", 24);
}

/** @brief A full screen of 4-byte characters (7.7 KB) is drawn completely: a buffer of 3 bytes per column is too small. */
static void test_notvim_full_screen_of_four_byte_text_is_not_truncated(void) {
	assert_full_multibyte_screen("\xf0\x9f\x98\x80", 24);
}

/** @brief Growing from 2 rows to 24 draws the full screen of 2-byte characters on the resize path. */
static void test_notvim_resize_draws_a_full_screen_of_two_byte_text(void) {
	assert_full_multibyte_screen("\xc3\xa9", 2);
}

/** @brief Growing from 2 rows to 24 draws the full screen of 3-byte characters on the resize path. */
static void test_notvim_resize_draws_a_full_screen_of_three_byte_text(void) {
	assert_full_multibyte_screen("\xe2\x82\xac", 2);
}

/** @brief Growing from 2 rows to 24 draws the full screen of 4-byte characters on the resize path. */
static void test_notvim_resize_draws_a_full_screen_of_four_byte_text(void) {
	assert_full_multibyte_screen("\xf0\x9f\x98\x80", 2);
}

/** @brief h, j, k and the arrow keys move by character, and a vertical move into a character snaps to its start. */
static void test_notvim_navigates_by_character_with_keys_and_arrows(void) {
	const char *path = tmpdir_write("nav.txt", "abcdef\n\xc3\xa9\xc3\xa9\xc3\xa9\nabcdef\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	pid_t pid = spawn_notvim(path, 24, &master);
	/* spawned first: screen() draws for the size of the pty that was just created */
	const char *text = "abcdef\r\n\xc3\xa9\xc3\xa9\xc3\xa9\r\nabcdef";
	struct { const char *keys; int row, col; } steps[] = {
		{ "l", 1, 2 }, { "l", 1, 3 }, { "l", 1, 4 },
		{ "j", 2, 3 },        /* wanted column 3 is past the last e-acute (columns 0 to 2): the last character, byte 4, column 3 */
		{ "\x1b[C", 2, 3 },   /* right arrow: already on the last character, it does not move and keeps the wanted column 3 */
		{ "\x1b[A", 1, 4 },   /* up: column 3 of the ASCII line, byte 3 */
		{ "\x1b[D", 1, 3 },   /* left arrow: a real move, the wanted column is now 2 */
		{ "j", 2, 3 },        /* wanted column 2: the third e-acute, byte 4 */
		{ "h", 2, 2 },        /* left: the second e-acute, wanted column 1 */
		{ "k", 1, 2 },
	};
	char keys[64] = "", expected[4096] = "";
	for (size_t i = 0; i < COUNT(steps); i++) {
		char one[512];
		strcat(keys, steps[i].keys);
		screen(one, sizeof(one), text, steps[i].row, steps[i].col);
		strcat(expected, one);
	}
	char first[1024], got[4096];
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	/* every key redraws once and in order, so the keys can go in one write and the redraws come back concatenated */
	send_and_read(master, keys, got, sizeof(got));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING(expected, got);
}

/** @brief Arrow keys and h j k l over uneven lines (short, empty, tab, multi-byte) remember the column, with the exact screens. */
static void test_notvim_remembers_the_column_over_uneven_lines(void) {
	const char *path = tmpdir_write("uneven.txt", "abcdefgh\nab\n\nabcdefgh\n\xc3\xa9\t\xe2\x82\xac" "x\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	pid_t pid = spawn_notvim(path, 24, &master);
	/* spawned first: screen() draws for the size of the pty that was just created */
	/* the tab of the last line is drawn as seven spaces: e-acute at column 0, tab 1 to 7, euro sign at 8, x at 9 */
	const char *text = "abcdefgh\r\nab\r\n\r\nabcdefgh\r\n\xc3\xa9       \xe2\x82\xac" "x";
	struct { const char *keys; int row, col; } steps[] = {
		{ "l", 1, 2 }, { "l", 1, 3 }, { "l", 1, 4 }, { "l", 1, 5 }, { "l", 1, 6 },   /* wanted column 5 */
		{ "\x1b[B", 2, 2 },   /* "ab": the last character */
		{ "l", 2, 2 },        /* does not move, so the wanted column 5 is kept (Vim: `5l j l j` ends on column 5) */
		{ "\x1b[B", 3, 1 },   /* the empty line */
		{ "h", 3, 1 },        /* does not move either */
		{ "\x1b[B", 4, 6 },   /* back to column 5 */
		{ "\x1b[B", 5, 2 },   /* column 5 is inside the tab (columns 1 to 7): the tab starts at column 1 */
		{ "\x1b[A", 4, 6 },
		{ "\x1b[B", 5, 2 },
		{ "\x1b[C", 5, 9 },   /* right arrow: the euro sign, column 8: the wanted column is now 8 */
		{ "\x1b[A", 4, 8 },   /* the line has 8 columns: its last character */
		{ "k", 3, 1 },
		{ "k", 2, 2 },
		{ "k", 1, 8 },        /* column 8 on the first line: its last character, column 7 */
		{ "h", 1, 7 },        /* the wanted column is now 6 */
		{ "j", 2, 2 },
		{ "j", 3, 1 },
		{ "j", 4, 7 },
	};
	char keys[256] = "", expected[16384] = "";
	for (size_t i = 0; i < COUNT(steps); i++) {
		char one[512];
		strcat(keys, steps[i].keys);
		screen(one, sizeof(one), text, steps[i].row, steps[i].col);
		strcat(expected, one);
	}
	char first[1024], got[16384];
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, keys, got, sizeof(got)); /* one write: every key redraws once, in order */
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING(expected, got);
}

/** @brief The remembered column survives a scroll: down past the bottom of a 4-row window and back up. */
static void test_notvim_remembers_the_column_across_a_scroll(void) {
	const char *lines[] = { "abcdefghij", "ab", "ab", "ab", "ab", "abcdefghij" };
	const char *path = tmpdir_write("scroll.txt", "abcdefghij\nab\nab\nab\nab\nabcdefghij\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	pid_t pid = spawn_notvim(path, 4, &master);
	/* spawned first: screen() draws for the size of the pty that was just created */
	char keys[64] = "lllll", expected[16384] = "";
	int cy = 0, rowoff = 0, cx = 5;
	/* five l, five j (the window scrolls by two rows), five k: every key redraws once */
	for (int i = 0; i < 15; i++) {
		char key = i < 5 ? 'l' : (i < 10 ? 'j' : 'k');
		if (i >= 5) strncat(keys, &key, 1);
		if (key == 'l') cx = i + 1;
		if (key == 'j') cy++;
		if (key == 'k') cy--;
		if (cy >= rowoff + 4) rowoff = cy - 3;
		if (cy < rowoff) rowoff = cy;
		int len = (int)strlen(lines[cy]);
		int col = cx < len ? cx : len - 1;
		char text[256] = "", one[512];
		for (int r = rowoff; r < rowoff + 4; r++) {
			if (r > rowoff) strcat(text, "\r\n");
			strcat(text, lines[r]);
		}
		screen(one, sizeof(one), text, cy - rowoff + 1, col + 1);
		strcat(expected, one);
	}
	char first[1024], got[16384];
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, keys, got, sizeof(got));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING(expected, got);
}

/** @brief A CRLF file is shown without ^M, and the cursor stops on the last character (no hidden CR column). */
static void test_notvim_shows_a_crlf_file_without_marks(void) {
	const char *path = tmpdir_write("crlf.txt", "ab\r\ncd\r\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[2048], right1[2048], right2[2048], expected[2048];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "l", right1, sizeof(right1));
	send_and_read(master, "l", right2, sizeof(right2));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	first_screen(expected, sizeof(expected), "ab\r\ncd", 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, first);
	screen(expected, sizeof(expected), "ab\r\ncd", 1, 2);
	TEST_ASSERT_EQUAL_STRING(expected, right1);
	TEST_ASSERT_EQUAL_STRING(expected, right2); /* already on the b: the second l redraws the same screen */
}

/** @brief A mixed file keeps its CRs, shown as ^M. */
static void test_notvim_shows_a_mixed_file_with_marks(void) {
	const char *path = tmpdir_write("mixed.txt", "one\r\ntwo\nthree\r\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[2048], expected[2048];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	first_screen(expected, sizeof(expected), "one^M\r\ntwo\r\nthree^M", 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, first);
}

/** @brief Count the non-overlapping occurrences of @p needle in @p hay. */
static int count_of(const char *hay, const char *needle) {
	int n = 0;
	for (const char *p = hay; (p = strstr(p, needle)) != NULL; p += strlen(needle)) n++;
	return n;
}

/**
 * @brief Redraws after key presses never clear the screen: no ESC[2J, and each one hides the cursor, shows it again and erases the screen below the rows.
 * The first draw needs no clear because the alternate screen starts blank; the scroll and resize paths are covered by the scroll and resize tests, which compare exact screens.
 */
static void test_notvim_redraws_never_clear_the_screen(void) {
	const char *path = tmpdir_write("noclear.txt", "abc\ndef\nghi\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], moves[4096], expected[4096] = "", one[512];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "jlkh", moves, sizeof(moves)); /* four consecutive redraws */
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_NULL(strstr(first, "\x1b[2J"));
	TEST_ASSERT_NULL(strstr(moves, "\x1b[2J"));
	TEST_ASSERT_EQUAL_INT(1, count_of(first, "\x1b[?25l"));
	TEST_ASSERT_EQUAL_INT(4, count_of(moves, "\x1b[?25l"));
	TEST_ASSERT_EQUAL_INT(4, count_of(moves, "\x1b[?25h"));
	TEST_ASSERT_EQUAL_INT(4, count_of(moves, "\x1b[J"));
	const int cursors[4][2] = { { 2, 1 }, { 2, 2 }, { 1, 2 }, { 1, 1 } };
	for (int i = 0; i < 4; i++) {
		screen(one, sizeof(one), "abc\r\ndef\r\nghi", cursors[i][0], cursors[i][1]);
		strcat(expected, one);
	}
	TEST_ASSERT_EQUAL_STRING(expected, moves);
}

/** Room for the biggest screens below (300 rows of 200 four-byte characters is 240 KB), and for the file that holds them. */
#define HUGE_BYTES 400000

/**
 * @brief Run notvim on @p file_lines lines of @p per_line copies of @p unit and compare the first screen it draws, or, if
 *        @p resize is set, the one it draws after growing from 2 rows by 80 columns to @p rows by @p cols.
 *
 * The expected screen has @p rows rows (or fewer if the file is shorter) of @p per_line characters each, so a row
 * is full width when @p per_line == @p cols and gets its erase when it is shorter.
 */
static void assert_uniform_screen(const char *unit, int per_line, int file_lines, unsigned short rows, unsigned short cols,
                                  int resize, int nonblock_out, size_t pause_ms) {
	static char content[HUGE_BYTES], text[HUGE_BYTES], expected[HUGE_BYTES + 4096], got[HUGE_BYTES + 4096];
	content[0] = text[0] = '\0';
	for (int i = 0; i < file_lines; i++) {
		append_repeated(content, unit, per_line);
		strcat(content, "\n");
	}
	for (int i = 0; i < file_lines && i < rows; i++) {
		if (i > 0) strcat(text, "\r\n");
		append_repeated(text, unit, per_line);
	}
	const char *path = tmpdir_write("uniform.txt", content);
	TEST_ASSERT_NOT_NULL(path);
	int master;
	pid_t pid = resize ? spawn_notvim_size(path, 2, 80, &master) : spawn_notvim_full(path, rows, cols, nonblock_out, &master);
	int raw = wait_until_raw(master);
	if (resize) {
		read_output(master, got, sizeof(got));
		set_size(master, rows, cols);
	} else if (pause_ms) {
		usleep((useconds_t)pause_ms * 1000); /* the child's first redraw meets a full pty buffer; the check is on the content */
	}
	read_output(master, got, sizeof(got));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	if (resize) screen(expected, sizeof(expected), text, 1, 1);
	else first_screen(expected, sizeof(expected), text, 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, got);
}

/** @brief A tall narrow terminal (100 rows by 20 columns) of 4-byte characters, rows one column short of full: nothing is dropped. */
static void test_notvim_tall_narrow_screen_of_four_byte_text_is_complete(void) {
	assert_uniform_screen("\xf0\x9f\x98\x80", 19, 100, 100, 20, 0, 0, 0);
}

/** @brief The same screen with full-width rows (no erase at the end of a row). */
static void test_notvim_tall_narrow_screen_of_full_width_four_byte_rows_is_complete(void) {
	assert_uniform_screen("\xf0\x9f\x98\x80", 20, 100, 100, 20, 0, 0, 0);
}

/** @brief The tall narrow terminal reached by a resize (from 2 rows by 80 columns): the buffer is reallocated for it. */
static void test_notvim_resize_to_a_tall_narrow_screen_of_four_byte_text_is_complete(void) {
	assert_uniform_screen("\xf0\x9f\x98\x80", 20, 100, 100, 20, 1, 0, 0);
}

/** @brief A short wide terminal (2 rows by 200 columns) of 4-byte characters, full width and one short. */
static void test_notvim_short_wide_screen_of_four_byte_text_is_complete(void) {
	assert_uniform_screen("\xf0\x9f\x98\x80", 200, 5, 2, 200, 0, 0, 0);
	assert_uniform_screen("\xf0\x9f\x98\x80", 199, 5, 2, 200, 0, 0, 0);
}

/** @brief The short wide terminal reached by a resize. */
static void test_notvim_resize_to_a_short_wide_screen_of_four_byte_text_is_complete(void) {
	assert_uniform_screen("\xf0\x9f\x98\x80", 199, 5, 2, 200, 1, 0, 0);
}

/**
 * @brief With a non-blocking stdout and a reader that is 200 ms late, a 240 KB redraw still arrives whole:
 *        main writes with terminal_write_all(), which waits and continues; a plain write() would lose the rest.
 */
static void test_notvim_redraw_survives_a_non_blocking_stdout_and_a_slow_reader(void) {
	assert_uniform_screen("\xf0\x9f\x98\x80", 199, 300, 300, 200, 0, 1, 200);
}

/** @brief When the terminal can no longer be written to, a redraw in the loop makes notvim exit with status 1 (as a failed first draw does), not 0. */
static void test_notvim_exits_1_when_a_redraw_fails(void) {
	const char *path = tmpdir_write("gone.txt", "abc\ndef\n");
	TEST_ASSERT_NOT_NULL(path);
	int out_master, out_slave, in_master, in_slave;
	TEST_ASSERT_EQUAL_INT(0, openpty(&out_master, &out_slave, NULL, NULL, NULL));
	TEST_ASSERT_EQUAL_INT(0, openpty(&in_master, &in_slave, NULL, NULL, NULL));
	/* keys come from one pty, the screen goes to another whose master we then close: its writes fail with EIO */
	int all[4] = { out_master, out_slave, in_master, in_slave };
	for (int i = 0; i < 4; i++) fcntl(all[i], F_SETFD, FD_CLOEXEC); /* the child must not keep a master open */
	pid_t pid = spawn_notvim_fds(path, in_slave, out_slave, out_slave);
	int raw = wait_until_raw(in_master);
	char first[2048];
	read_output(out_master, first, sizeof(first));
	close(out_master);
	close(out_slave);
	close(in_slave);
	ssize_t sent = write(in_master, "j", 1);
	int status = wait_exit(pid);
	close(in_master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(1, (int)sent);
	TEST_ASSERT_EQUAL_INT(1, status);
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
	RUN_TEST(test_notvim_clips_a_wide_utf8_line_by_characters);
	RUN_TEST(test_notvim_draws_invalid_bytes_and_c1_controls_as_question_marks);
	RUN_TEST(test_notvim_full_screen_of_two_byte_text_is_not_truncated);
	RUN_TEST(test_notvim_full_screen_of_three_byte_text_is_not_truncated);
	RUN_TEST(test_notvim_full_screen_of_four_byte_text_is_not_truncated);
	RUN_TEST(test_notvim_resize_draws_a_full_screen_of_two_byte_text);
	RUN_TEST(test_notvim_resize_draws_a_full_screen_of_three_byte_text);
	RUN_TEST(test_notvim_resize_draws_a_full_screen_of_four_byte_text);
	RUN_TEST(test_notvim_navigates_by_character_with_keys_and_arrows);
	RUN_TEST(test_notvim_shows_a_crlf_file_without_marks);
	RUN_TEST(test_notvim_shows_a_mixed_file_with_marks);
	RUN_TEST(test_notvim_redraws_never_clear_the_screen);
	RUN_TEST(test_notvim_tall_narrow_screen_of_four_byte_text_is_complete);
	RUN_TEST(test_notvim_tall_narrow_screen_of_full_width_four_byte_rows_is_complete);
	RUN_TEST(test_notvim_resize_to_a_tall_narrow_screen_of_four_byte_text_is_complete);
	RUN_TEST(test_notvim_short_wide_screen_of_four_byte_text_is_complete);
	RUN_TEST(test_notvim_resize_to_a_short_wide_screen_of_four_byte_text_is_complete);
	RUN_TEST(test_notvim_redraw_survives_a_non_blocking_stdout_and_a_slow_reader);
	RUN_TEST(test_notvim_exits_1_when_a_redraw_fails);
	RUN_TEST(test_notvim_remembers_the_column_over_uneven_lines);
	RUN_TEST(test_notvim_remembers_the_column_across_a_scroll);
}
