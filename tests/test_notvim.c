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
/** Name notvim shows in its status line: the file given to the last spawn_notvim_full(), or "[No Name]". A copy: tmpdir_write() reuses its buffer. */
static char shown_name[1024] = "[No Name]";
/** Whether the file of the last spawn is shown as a CRLF file: a test that loads one sets it after the spawn. */
static int shown_dos;
/** Whether the status line shows [+]: a test that types sets it before building the expected screens; reset by each spawn. */
static int shown_modified;
/** Mode label notvim shows in its status line: "NORMAL" after each spawn; a test that enters insert mode sets it before building the expected screens. */
static const char *shown_mode = "NORMAL";

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
	snprintf(shown_name, sizeof(shown_name), "%s", file ? file : "[No Name]");
	shown_dos = 0;
	shown_modified = 0;
	shown_mode = "NORMAL";
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

/**
 * @brief Read what the child writes; NUL-terminate; return the length.
 *
 * Waits up to 300 ms for the first byte (so "nothing is drawn" is still checked), then stops at 100 ms of silence or EOF:
 * a draw arrives within one burst, so there is no need to wait the full 300 ms after it.
 */
static size_t read_output(int master, char *buf, size_t size) {
	size_t len = 0;
	struct pollfd pfd = { .fd = master, .events = POLLIN };
	while (len < size - 1 && poll(&pfd, 1, len ? 100 : 300) > 0) {
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

/** @brief Quit a notvim in normal mode with unsaved changes (Ctrl+Q would be refused): ":q!" and wait_exit(). */
static int discard_and_wait(int master, pid_t pid) {
	if (write(master, ":q!\r", 4) != 4) kill(pid, SIGKILL);
	return wait_exit(pid);
}

/** @brief Send Ctrl+Q to the child and return wait_exit() of it. */
static int quit_and_wait(int master, pid_t pid) {
	char ctrl_q = 0x11;
	if (write(master, &ctrl_q, 1) != 1) kill(pid, SIGKILL);
	return wait_exit(pid);
}

/**
 * @brief Write the screen notvim should draw, for the current pty size, for the text rows @p text (NULL for none) with the cursor at row @p row, column @p col (1-based).
 *
 * The last row is the status line (none on a 1-row terminal) and @p text must fit the other rows.
 * @p line and @p fcol are the position the status line shows: the line of the file (1-based) and the display column (1-based) of the cursor.
 * The name is the one of the last spawn.
 */
static void screen_at(char *buf, size_t size, const char *text, int row, int col, int line, int fcol) {
	char status[1024];
	if (term_rows < 2) {
		draw_expected(buf, size, text, term_rows, term_cols, row, col);
		return;
	}
	status_expected(status, sizeof(status), shown_name, shown_dos, shown_modified, shown_mode, (size_t)line, (size_t)fcol, term_cols);
	draw_expected_status(buf, size, text, (size_t)term_rows - 1, term_cols, status, term_rows, row, col);
}

/** @brief screen_at() for a window that shows the file from its first line and text where a column is a character: the status line shows the row and column of the cursor. */
static void screen(char *buf, size_t size, const char *text, int row, int col) {
	screen_at(buf, size, text, row, col, row, col);
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
	screen_at(buf, size, text, row, 1, first + row - 1, 1);
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
	char out[1024];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, out, sizeof(out));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	char expected[1024];
	first_screen(expected, sizeof(expected), "line1\r\nline2\r\nline3", 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/** @brief A file taller than the terminal is cut to the terminal height. */
static void test_notvim_shows_only_the_rows_that_fit(void) {
	const char *path = tmpdir_write("four.txt", "a\nb\nc\nd\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char out[1024];
	pid_t pid = spawn_notvim(path, 3, &master); /* two text rows and the status line */
	int raw = wait_until_raw(master);
	read_output(master, out, sizeof(out));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	char expected[1024];
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
		if (i < 23) { /* the 24th row is the status line */
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
	char out[1024], expected[1024];
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
		if (i > 0 && i < 23) strcat(text, "\r\n");
		if (i < 23) strcat(text, blanks); /* clipped to the 80 columns of the terminal; 23 text rows and the status line */
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
	char out[1024];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, out, sizeof(out));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	char expected[1024];
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
	char first[1024], down[1024], right[1024], up[1024], left[1024];
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
	char expected[1024];
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

/** @brief 0 ^ $ w b e move the cursor on a pty and each redraws the screen with the new cursor and status position. */
static void test_notvim_line_and_word_motions_move_the_cursor(void) {
	const char *path = tmpdir_write("motions.txt", "foo bar.baz\n  qux\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], out[8][1024];
	const char *keys = "wweb$0j^";
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	for (int i = 0; i < 8; i++) send_and_read(master, (char[]){ keys[i], '\0' }, out[i], sizeof(out[i]));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	const int cursors[8][2] = { { 1, 5 }, { 1, 8 }, { 1, 11 }, { 1, 9 }, { 1, 11 }, { 1, 1 }, { 2, 1 }, { 2, 3 } };
	for (int i = 0; i < 8; i++) {
		char expected[1024];
		screen(expected, sizeof(expected), "foo bar.baz\r\n  qux", cursors[i][0], cursors[i][1]);
		TEST_ASSERT_EQUAL_STRING(expected, out[i]);
	}
}

/** @brief Counts, "G" and "gg" on a pty: "3j", "G", "2G", "gg" and "4l" each redraw once, with the new cursor and status position; the digit and the first "g" draw nothing. */
static void test_notvim_counts_gg_and_g_move_the_cursor(void) {
	const char *path = tmpdir_write("counts.txt", "abcdef\n  b2\nc3\nd4\ne5\nf6\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], out[5][1024], quiet[64];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	const char *keys[5] = { "3j", "G", "2G", "gg", "4l" };
	for (int i = 0; i < 5; i++) send_and_read(master, keys[i], out[i], sizeof(out[i]));
	size_t digit = send_and_read(master, "3", quiet, sizeof(quiet)); /* nothing to draw */
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_UINT(0, digit);
	const int cursors[5][2] = { { 4, 1 }, { 6, 1 }, { 2, 3 }, { 1, 1 }, { 1, 5 } };
	for (int i = 0; i < 5; i++) {
		char expected[1024];
		screen(expected, sizeof(expected), "abcdef\r\n  b2\r\nc3\r\nd4\r\ne5\r\nf6", cursors[i][0], cursors[i][1]);
		TEST_ASSERT_EQUAL_STRING(expected, out[i]);
	}
}

/** @brief Ctrl+f and Ctrl+d scroll by pages in the real program, and after a resize the next page uses the new window height. */
static void test_notvim_page_keys_scroll_and_follow_a_resize(void) {
	int master, raw;
	char first[4096], resized[4096], second[4096], half[4096], expected_first[2048];
	pid_t pid = spawn_resize_notvim(11, &master, &raw); /* 10 text rows */
	send_and_read(master, "\x06", first, sizeof(first));
	resize_screen(expected_first, sizeof(expected_first), 9, 18, 40, 1); /* the window moved 8 lines: 2 lines of overlap; built before the pty size changes */
	set_size(master, 6, 80); /* 5 text rows */
	read_output(master, resized, sizeof(resized));
	send_and_read(master, "\x06", second, sizeof(second));
	send_and_read(master, "\x04", half, sizeof(half));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	char expected[2048];
	TEST_ASSERT_EQUAL_STRING(expected_first, first);
	resize_screen(expected, sizeof(expected), 9, 13, 40, 1);
	TEST_ASSERT_EQUAL_STRING(expected, resized);
	resize_screen(expected, sizeof(expected), 12, 16, 40, 1); /* a page of 3 lines now */
	TEST_ASSERT_EQUAL_STRING(expected, second);
	resize_screen(expected, sizeof(expected), 14, 18, 40, 1); /* half of 5 lines is 2 */
	TEST_ASSERT_EQUAL_STRING(expected, half);
}

/** @brief "}", "{" and "%" move the cursor in the real program. */
static void test_notvim_paragraph_and_bracket_motions_move_the_cursor(void) {
	const char *path = tmpdir_write("blocks.txt", "a(b\nc)\n\nd\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char out[2048], expected[2048];
	pid_t pid = spawn_notvim(path, 6, &master);
	int raw = wait_until_raw(master);
	read_output(master, out, sizeof(out));
	char at_match[2048], at_blank[2048];
	send_and_read(master, "%", at_match, sizeof(at_match));
	send_and_read(master, "}", at_blank, sizeof(at_blank));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	screen_at(expected, sizeof(expected), "a(b\r\nc)\r\n\r\nd", 2, 2, 2, 2);
	TEST_ASSERT_EQUAL_STRING(expected, at_match);
	screen_at(expected, sizeof(expected), "a(b\r\nc)\r\n\r\nd", 3, 1, 3, 1);
	TEST_ASSERT_EQUAL_STRING(expected, at_blank);
}

/** @brief "f", "t", ";" and "," move the cursor in the real program, also onto a multibyte character sent as one write. */
static void test_notvim_character_search_moves_the_cursor(void) {
	const char *path = tmpdir_write("find.txt", "ab,cd,e\nx\xc3\xa9y\xc3\xa9z\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char out[2048], expected[2048];
	pid_t pid = spawn_notvim(path, 6, &master);
	int raw = wait_until_raw(master);
	read_output(master, out, sizeof(out));
	char after_f[2048], after_semi[2048], after_comma[2048], after_t[2048], after_j[2048], after_zero[2048], after_accent[2048], after_again[2048];
	send_and_read(master, "f,", after_f, sizeof(after_f));
	send_and_read(master, ";", after_semi, sizeof(after_semi));
	send_and_read(master, ",", after_comma, sizeof(after_comma));
	send_and_read(master, "t,", after_t, sizeof(after_t)); /* the cursor is on a comma: "t," goes to the character before the next one */
	send_and_read(master, "j", after_j, sizeof(after_j));
	send_and_read(master, "0", after_zero, sizeof(after_zero));
	send_and_read(master, "f\xc3\xa9", after_accent, sizeof(after_accent));
	send_and_read(master, ";", after_again, sizeof(after_again));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	const char *text = "ab,cd,e\r\nx\xc3\xa9y\xc3\xa9z";
	screen(expected, sizeof(expected), text, 1, 3);
	TEST_ASSERT_EQUAL_STRING(expected, after_f);
	screen(expected, sizeof(expected), text, 1, 6);
	TEST_ASSERT_EQUAL_STRING(expected, after_semi);
	screen(expected, sizeof(expected), text, 1, 3);
	TEST_ASSERT_EQUAL_STRING(expected, after_comma);
	screen(expected, sizeof(expected), text, 1, 5);
	TEST_ASSERT_EQUAL_STRING(expected, after_t);
	screen_at(expected, sizeof(expected), text, 2, 5, 2, 5);
	TEST_ASSERT_EQUAL_STRING(expected, after_j);
	screen(expected, sizeof(expected), text, 2, 1);
	TEST_ASSERT_EQUAL_STRING(expected, after_zero);
	screen(expected, sizeof(expected), text, 2, 2);
	TEST_ASSERT_EQUAL_STRING(expected, after_accent);
	screen(expected, sizeof(expected), text, 2, 4);
	TEST_ASSERT_EQUAL_STRING(expected, after_again);
}

/** @brief After "$", j keeps to the end of each line on a pty. */
static void test_notvim_dollar_then_j_keeps_to_the_end(void) {
	const char *path = tmpdir_write("eol.txt", "abcdef\nab\nabcdefgh\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], out[3][1024];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "$", out[0], sizeof(out[0]));
	send_and_read(master, "j", out[1], sizeof(out[1]));
	send_and_read(master, "j", out[2], sizeof(out[2]));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	const int cursors[3][2] = { { 1, 6 }, { 2, 2 }, { 3, 8 } };
	for (int i = 0; i < 3; i++) {
		char expected[1024];
		screen(expected, sizeof(expected), "abcdef\r\nab\r\nabcdefgh", cursors[i][0], cursors[i][1]);
		TEST_ASSERT_EQUAL_STRING(expected, out[i]);
	}
}

/** @brief j, l, k and h move the cursor down, right, up and left, like the arrow keys. */
static void test_notvim_hjkl_move_the_cursor_and_redraw(void) {
	const char *path = tmpdir_write("hjkl.txt", "abc\ndef\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], down[1024], right[1024], up[1024], left[1024];
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
	char expected[1024];
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
	char first[1024], after[1024];
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

/** @brief i enters insert mode (status INSERT, cursor kept) and Esc leaves it, one character to the left. */
static void test_notvim_i_and_esc_switch_modes(void) {
	const char *path = tmpdir_write("modes.txt", "abc\ndef\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], ins[1024], esc[1024];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "l", ins, sizeof(ins));
	send_and_read(master, "i", ins, sizeof(ins));
	send_and_read(master, "\x1b", esc, sizeof(esc));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	char expected[1024];
	shown_mode = "INSERT";
	screen(expected, sizeof(expected), "abc\r\ndef", 1, 2);
	TEST_ASSERT_EQUAL_STRING(expected, ins);
	shown_mode = "NORMAL";
	screen(expected, sizeof(expected), "abc\r\ndef", 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, esc);
}

/** @brief The arrows work in insert mode, up to the end of the line; Esc then lands on the last character. */
static void test_notvim_arrows_in_insert_mode_reach_the_end_of_the_line(void) {
	const char *path = tmpdir_write("insarrows.txt", "abc\ndef\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], r1[1024], r2[1024], r3[1024], r4[1024], down[1024], up[1024], esc[1024], left[1024];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "i", r1, sizeof(r1));
	send_and_read(master, "\x1b[C", r1, sizeof(r1));
	send_and_read(master, "\x1b[C", r2, sizeof(r2));
	send_and_read(master, "\x1b[C", r3, sizeof(r3));
	size_t n = send_and_read(master, "\x1b[C", r4, sizeof(r4));
	send_and_read(master, "\x1b[B", down, sizeof(down));
	send_and_read(master, "\x1b[A", up, sizeof(up));
	send_and_read(master, "\x1b[D", left, sizeof(left));
	send_and_read(master, "\x1b", esc, sizeof(esc));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	char expected[1024];
	shown_mode = "INSERT";
	screen(expected, sizeof(expected), "abc\r\ndef", 1, 3);
	TEST_ASSERT_EQUAL_STRING(expected, r2);
	screen(expected, sizeof(expected), "abc\r\ndef", 1, 4); /* after the c */
	TEST_ASSERT_EQUAL_STRING(expected, r3);
	TEST_ASSERT_EQUAL_STRING(expected, r4); /* right at the end: nothing moves, the screen is drawn again */
	TEST_ASSERT_TRUE(n > 0);
	screen(expected, sizeof(expected), "abc\r\ndef", 2, 4);
	TEST_ASSERT_EQUAL_STRING(expected, down);
	screen(expected, sizeof(expected), "abc\r\ndef", 1, 4);
	TEST_ASSERT_EQUAL_STRING(expected, up);
	screen(expected, sizeof(expected), "abc\r\ndef", 1, 3);
	TEST_ASSERT_EQUAL_STRING(expected, left);
	shown_mode = "NORMAL";
	screen(expected, sizeof(expected), "abc\r\ndef", 1, 2); /* from column 2 (after Left) one left */
	TEST_ASSERT_EQUAL_STRING(expected, esc);
}

/** @brief Typing in insert mode (h, j, k, l included) and an accented letter in two writes: the exact screen, with [+] in the status. */
static void test_notvim_typing_in_insert_mode(void) {
	const char *path = tmpdir_write("typing.txt", "abc\ndef\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], ins[1024], typed[1024], half[1024], acc[1024], esc[1024];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "i", ins, sizeof(ins));
	send_and_read(master, "hj", typed, sizeof(typed));
	send_and_read(master, "\t", typed, sizeof(typed));
	size_t n = send_and_read(master, "\xc3", half, sizeof(half));
	send_and_read(master, "\xa9", acc, sizeof(acc));
	send_and_read(master, "\x1b", esc, sizeof(esc));
	int status = discard_and_wait(master, pid); /* ends in normal mode, modified */
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_UINT(0, n); /* half a character: nothing to draw */
	char expected[1024];
	shown_mode = "INSERT";
	shown_modified = 1;
	screen_at(expected, sizeof(expected), "hj      abc\r\ndef", 1, 9, 1, 9);
	TEST_ASSERT_EQUAL_STRING(expected, typed);
	screen_at(expected, sizeof(expected), "hj      \xc3\xa9" "abc\r\ndef", 1, 10, 1, 10);
	TEST_ASSERT_EQUAL_STRING(expected, acc);
	shown_mode = "NORMAL";
	screen_at(expected, sizeof(expected), "hj      \xc3\xa9" "abc\r\ndef", 1, 9, 1, 9);
	TEST_ASSERT_EQUAL_STRING(expected, esc);
}

/** @brief Enter, Backspace and Delete in insert mode split and join lines: the exact screen after each key. */
static void test_notvim_enter_backspace_and_delete_split_and_join_lines(void) {
	const char *path = tmpdir_write("editing.txt", "abc\ndef\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], out[8][1024];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "i", out[7], sizeof(out[7]));
	send_and_read(master, "\r", out[0], sizeof(out[0]));      /* split at 0: "", abc, def */
	send_and_read(master, "\x7f", out[1], sizeof(out[1]));    /* join back */
	send_and_read(master, "\x1b[C", out[7], sizeof(out[7])); /* one key per write: each draws once */
	send_and_read(master, "\x1b[C", out[7], sizeof(out[7]));
	send_and_read(master, "\r", out[2], sizeof(out[2]));      /* ab, c, def */
	send_and_read(master, "\x1b[3~", out[3], sizeof(out[3])); /* ab, "", def */
	send_and_read(master, "\x1b[3~", out[4], sizeof(out[4])); /* ab, def: join with the next line */
	send_and_read(master, "\x08", out[5], sizeof(out[5]));    /* ab, def -> abdef */
	send_and_read(master, "\r", out[6], sizeof(out[6]));      /* split at 2: ab, def */
	send_and_read(master, "\x1b", out[7], sizeof(out[7]));
	int status = discard_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	char expected[1024];
	shown_mode = "INSERT";
	shown_modified = 1;
	screen(expected, sizeof(expected), "\r\nabc\r\ndef", 2, 1);
	TEST_ASSERT_EQUAL_STRING(expected, out[0]);
	screen(expected, sizeof(expected), "abc\r\ndef", 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, out[1]);
	screen(expected, sizeof(expected), "ab\r\nc\r\ndef", 2, 1);
	TEST_ASSERT_EQUAL_STRING(expected, out[2]);
	screen(expected, sizeof(expected), "ab\r\n\r\ndef", 2, 1);
	TEST_ASSERT_EQUAL_STRING(expected, out[3]);
	screen(expected, sizeof(expected), "ab\r\ndef", 2, 1);
	TEST_ASSERT_EQUAL_STRING(expected, out[4]);
	screen(expected, sizeof(expected), "abdef", 1, 3);
	TEST_ASSERT_EQUAL_STRING(expected, out[5]);
	screen(expected, sizeof(expected), "ab\r\ndef", 2, 1);
	TEST_ASSERT_EQUAL_STRING(expected, out[6]);
}

/** @brief The status line says "[No Name] INSERT" in insert mode, literally, and Ctrl+Q quits from insert mode. */
static void test_notvim_status_says_insert_and_ctrl_q_quits_in_insert_mode(void) {
	int master;
	char first[1024], ins[1024];
	pid_t pid = spawn_notvim(NULL, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "i", ins, sizeof(ins));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_NOT_NULL(strstr(ins, "\x1b[24;1H\x1b[7m[No Name] INSERT"));
}

/** @brief An escape sequence that is neither an arrow nor Delete (Page Up) is ignored and does not redraw. */
static void test_notvim_ignored_escape_sequence_does_not_redraw(void) {
	const char *path = tmpdir_write("ignored.txt", "abc\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], after[1024];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	size_t n = send_and_read(master, "\x1b[5~", after, sizeof(after));
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
	char first[1024], d1[1024], d2[1024], d3[1024], u1[1024], u3[1024];
	pid_t pid = spawn_notvim(path, 4, &master); /* 4 rows: a text window of 3 rows and the status line */
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
	char expected[1024];
	first_screen(expected, sizeof(expected), "a\r\nb\r\nc", 1, 1);
	TEST_ASSERT_EQUAL_STRING(expected, first);
	screen(expected, sizeof(expected), "a\r\nb\r\nc", 2, 1);
	TEST_ASSERT_EQUAL_STRING(expected, d1);
	screen(expected, sizeof(expected), "a\r\nb\r\nc", 3, 1);
	TEST_ASSERT_EQUAL_STRING(expected, d2);
	screen_at(expected, sizeof(expected), "b\r\nc\r\nd", 3, 1, 4, 1); /* scrolled by one line: line 4 on the last text row */
	TEST_ASSERT_EQUAL_STRING(expected, d3);
	screen_at(expected, sizeof(expected), "b\r\nc\r\nd", 2, 1, 3, 1); /* still the same window */
	TEST_ASSERT_EQUAL_STRING(expected, u1);
	/* kkk is three redraws: line 2 in the same window, then line 1 scrolls back, then it stays */
	char same_window[1024], top[1024], three[3072];
	screen_at(same_window, sizeof(same_window), "b\r\nc\r\nd", 1, 1, 2, 1);
	screen(top, sizeof(top), "a\r\nb\r\nc", 1, 1);
	snprintf(three, sizeof(three), "%s%s%s", same_window, top, top);
	TEST_ASSERT_EQUAL_STRING(three, u3);
}

/** @brief A lone Esc does not redraw and does not swallow the key that comes after it. */
static void test_notvim_lone_escape_does_not_swallow_the_next_key(void) {
	const char *path = tmpdir_write("esc.txt", "a\nb\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], after_esc[1024], after_j[1024];
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
	char expected[1024];
	screen(expected, sizeof(expected), "a\r\nb", 2, 1);
	TEST_ASSERT_EQUAL_STRING(expected, after_j);
}

/** @brief An arrow key whose bytes arrive a few milliseconds apart is still an arrow. */
static void test_notvim_arrow_split_across_writes_still_works(void) {
	const char *path = tmpdir_write("split.txt", "a\nb\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], after[1024];
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
	char expected[1024];
	screen(expected, sizeof(expected), "a\r\nb", 2, 1);
	TEST_ASSERT_EQUAL_STRING(expected, after);
}

/** @brief An ordinary key does nothing yet and does not redraw. */
static void test_notvim_ordinary_key_does_not_redraw(void) {
	const char *path = tmpdir_write("plain.txt", "abc\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], after[1024];
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
	char first[1024], last[1024];
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
	char message[1024], terminal[1024];
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
	char piped[1024], message[1024];
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
	char first[1024], last[1024];
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
	resize_screen(expected, sizeof(expected), 1, 4, 30, 1); /* 5 rows: 4 text rows and the status line */
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
	resize_screen(expected, sizeof(expected), 8, 11, 40, 4); /* 4 text rows: lines 8 to 11, cursor on the last one */
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
	resize_screen(expected, sizeof(expected), 1, 7, 40, 1); /* 8 rows: 7 text rows */
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
	resize_screen(expected, sizeof(expected), 1, 3, 40, 1); /* 4 rows: 3 text rows */
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
	for (int i = 1; i <= 7; i++) { /* 8 rows: 7 text rows and the status line */
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
	resize_screen(expected, sizeof(expected), 1, 29, 10, 1); /* 30 rows: 29 text rows */
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
	resize_screen(expected, sizeof(expected), 1, 4, 40, 1); /* 5 rows: 4 text rows */
	resize_screen(down, sizeof(down), 1, 4, 40, 2);
	strcat(expected, down);
	TEST_ASSERT_EQUAL_STRING(expected, after);
}

/** @brief SIGTERM after a resize still restores the terminal and exits with 143. */
static void test_notvim_sigterm_after_a_resize_still_exits_cleanly(void) {
	int master, raw;
	char resized[2048], last[1024];
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
	char first[1024], expected[1024];
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

/** @brief Run notvim on a file of 30 lines of 80 copies of @p unit, starting with @p start_rows rows, and compare the full 24x80 screen (23 text rows and the status line). */
static void assert_full_multibyte_screen(const char *unit, unsigned short start_rows) {
	static char content[SCREEN_BYTES], text[SCREEN_BYTES], expected[SCREEN_BYTES * 2], got[SCREEN_BYTES * 2], first[SCREEN_BYTES * 2];
	content[0] = text[0] = '\0';
	for (int i = 0; i < 30; i++) {
		append_repeated(content, unit, 80);
		strcat(content, "\n");
	}
	for (int i = 0; i < 23; i++) { /* the 24th row is the status line */
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
		char one[1024];
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
	char keys[1024] = "", expected[16384] = "";
	for (size_t i = 0; i < COUNT(steps); i++) {
		char one[1024];
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
	pid_t pid = spawn_notvim(path, 5, &master); /* a text window of 4 rows and the status line */
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
		char text[1024] = "", one[1024];
		for (int r = rowoff; r < rowoff + 4; r++) {
			if (r > rowoff) strcat(text, "\r\n");
			strcat(text, lines[r]);
		}
		screen_at(one, sizeof(one), text, cy - rowoff + 1, col + 1, cy + 1, col + 1);
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
	shown_dos = 1; /* a CRLF file: the status line says [dos] */
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
	char first[1024], moves[4096], expected[4096] = "", one[1024];
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

/** @brief Without a file the last row is the status line, literally: "[No Name] NORMAL", the position at the right, in reverse video, no ESC[K, and the cursor and show-cursor last. */
static void test_notvim_status_line_without_a_file_exact_bytes(void) {
	int master;
	char out[1024];
	pid_t pid = spawn_notvim(NULL, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, out, sizeof(out));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING(ALT_ENTER "\x1b[?25l\x1b[H" "\x1b[1;1H\x1b[J" /* nothing to draw: erase the 23 text rows */
	                         "\x1b[24;1H\x1b[7m" "[No Name] NORMAL" "                                                             " "1,1" "\x1b[m" /* 16 + 61 + 3 = 80 columns */
	                         "\x1b[1;1H\x1b[?25h", out);
}

/** @brief A file name that does not exist is still the name in the status line (it is not created), and a text row comes before the erase and the status. */
static void test_notvim_status_line_shows_the_name_of_a_missing_file(void) {
	int master;
	char out[1024];
	pid_t pid = spawn_notvim("scratch-missing.txt", 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, out, sizeof(out));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING(ALT_ENTER "\x1b[?25l\x1b[H" "\x1b[1;1H\x1b[J"
	                         "\x1b[24;1H\x1b[7m" "scratch-missing.txt NORMAL" "                                                   " "1,1" "\x1b[m" /* 26 + 51 + 3 = 80 columns */
	                         "\x1b[1;1H\x1b[?25h", out);
}

/** @brief Text, erase of the unused rows, status line, cursor: in that order, so the erase of the rows below the text does not wipe the status line. */
static void test_notvim_status_line_comes_after_the_erase_of_the_unused_rows(void) {
	int master;
	char out[1024];
	pid_t pid = spawn_notvim("scratch-missing.txt", 4, &master);
	int raw = wait_until_raw(master);
	read_output(master, out, sizeof(out));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	const char *erase = strstr(out, "\x1b[1;1H\x1b[J");
	const char *bar = strstr(out, "\x1b[4;1H\x1b[7m");
	TEST_ASSERT_NOT_NULL(erase);
	TEST_ASSERT_NOT_NULL(bar);
	TEST_ASSERT_TRUE_MESSAGE(erase < bar, "the status line must be drawn after the erase, or the erase wipes it");
}

/** @brief Every move redraws the status line with the new line,column, at the last row, to the full width (no ESC[K), and the cursor is the last thing written. */
static void test_notvim_status_line_follows_the_cursor(void) {
	const char *path = tmpdir_write("pos.txt", "ab\ncd\nef\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], moves[4096], expected[4096] = "", one[1024];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "jlj", moves, sizeof(moves)); /* 2,1 then 2,2 then 3,2 (the line "ef" is as long as "cd") */
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	const int cursors[3][2] = { { 2, 1 }, { 2, 2 }, { 3, 2 } };
	for (int i = 0; i < 3; i++) {
		screen(one, sizeof(one), "ab\r\ncd\r\nef", cursors[i][0], cursors[i][1]);
		strcat(expected, one);
	}
	TEST_ASSERT_EQUAL_STRING(expected, moves);
	TEST_ASSERT_EQUAL_INT(3, count_of(moves, "\x1b[24;1H\x1b[7m"));
	TEST_ASSERT_EQUAL_INT(0, count_of(moves, "\x1b[K\x1b[m"));
	TEST_ASSERT_NOT_NULL(strstr(first, "1,1\x1b[m\x1b[1;1H\x1b[?25h"));
	size_t n = strlen(moves);
	TEST_ASSERT_EQUAL_STRING("3,2\x1b[m\x1b[3;2H\x1b[?25h", moves + n - strlen("3,2\x1b[m\x1b[3;2H\x1b[?25h"));
}

/** @brief The column in the status line is the display column: a tab takes its width, a mark two, an e-acute one. Whole redraws, in order. */
static void test_notvim_status_line_column_is_the_display_column(void) {
	const char *path = tmpdir_write("cols.txt", "\tx\n\x01y\n\xc3\xa9z\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], moves[8192], expected[8192] = "", one[1024];
	pid_t pid = spawn_notvim(path, 24, &master);
	const char *text = "        x\r\n^Ay\r\n\xc3\xa9z";
	/* l: x after the tab; j: wanted column 8, the last character of "^Ay"; h: onto the mark; j: the e-acute; l: z */
	struct { int row, col; } steps[] = { { 1, 9 }, { 2, 3 }, { 2, 1 }, { 3, 1 }, { 3, 2 } };
	for (size_t i = 0; i < COUNT(steps); i++) {
		screen(one, sizeof(one), text, steps[i].row, steps[i].col);
		strcat(expected, one);
	}
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "ljhjl", moves, sizeof(moves));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING(expected, moves);
}

/** @brief After a resize the status line is on the new last row, for the new width, and the old last row is not drawn on. */
static void test_notvim_status_line_moves_to_the_new_last_row_on_resize(void) {
	int master, raw;
	char shrunk[2048], grown[4096], expected_shrunk[2048], expected_grown[4096];
	pid_t pid = spawn_resize_notvim(24, &master, &raw);
	set_size(master, 10, 80);
	resize_screen(expected_shrunk, sizeof(expected_shrunk), 1, 9, 40, 1); /* 10 rows: 9 text rows */
	read_output(master, shrunk, sizeof(shrunk));
	set_size(master, 30, 60);
	resize_screen(expected_grown, sizeof(expected_grown), 1, 29, 40, 1);
	read_output(master, grown, sizeof(grown));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING(expected_shrunk, shrunk);
	TEST_ASSERT_NOT_NULL(strstr(shrunk, "\x1b[10;1H\x1b[7m"));
	TEST_ASSERT_NULL(strstr(shrunk, "\x1b[24;1H"));
	TEST_ASSERT_EQUAL_STRING(expected_grown, grown);
	TEST_ASSERT_NOT_NULL(strstr(grown, "\x1b[30;1H\x1b[7m"));
	TEST_ASSERT_NULL(strstr(grown, "\x1b[10;1H\x1b[7m"));
}

/** @brief Write the 100-character line of the horizontal scroll tests into @p buf, 'a' to 'z' over and over. */
static void hscroll_line(char *buf) {
	for (int i = 0; i < 100; i++) buf[i] = (char)('a' + i % 26);
	buf[100] = '\0';
}

/** @brief Write characters @p from to @p from + @p n - 1 of the horizontal scroll line into @p buf. */
static void hscroll_slice(char *buf, int from, int n) {
	char line[128];
	hscroll_line(line);
	memcpy(buf, line + from, (size_t)n);
	buf[n] = '\0';
}

/** @brief Moving right past the edge of the window scrolls one column; the status line shows the absolute column. */
static void test_notvim_scrolls_right_with_the_cursor(void) {
	char line[128], text[128], at_edge[16384], scrolled[2048], expected_scrolled[2048];
	hscroll_line(line);
	char content[130];
	snprintf(content, sizeof(content), "%s\n", line);
	const char *path = tmpdir_write("long.txt", content);
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[2048];
	pid_t pid = spawn_notvim_size(path, 24, 30, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "lllllllllllllllllllllllllllll", at_edge, sizeof(at_edge)); /* 29 moves: the last column */
	size_t n = send_and_read(master, "l", scrolled, sizeof(scrolled));
	hscroll_slice(text, 1, 30);
	screen_at(expected_scrolled, sizeof(expected_scrolled), text, 1, 30, 1, 31);
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_UINT(strlen(expected_scrolled), n);
	TEST_ASSERT_EQUAL_STRING(expected_scrolled, scrolled);
}

/** @brief Typing at the right edge in insert mode scrolls: the cursor stays on the last column, after the text. */
static void test_notvim_typing_at_the_right_edge_scrolls(void) {
	int master;
	char first[2048], before[4096], after[2048], expected[2048];
	pid_t pid = spawn_notvim_size(NULL, 24, 20, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "ixxxxxxxxxxxxxxxxxxx", before, sizeof(before)); /* 19 characters: the cursor is on the last column */
	size_t n = send_and_read(master, "x", after, sizeof(after));
	shown_modified = 1;
	shown_mode = "INSERT";
	screen_at(expected, sizeof(expected), "xxxxxxxxxxxxxxxxxxx", 1, 20, 1, 21);
	send_and_read(master, "\x1b", before, sizeof(before));
	int status = discard_and_wait(master, pid); /* ":q!" is typed text in insert mode: leave with Esc first */
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_UINT(strlen(expected), n);
	TEST_ASSERT_EQUAL_STRING(expected, after);
}

/** @brief After a resize to a narrower terminal the view follows the cursor and the status line follows the new width. */
static void test_notvim_resize_scrolls_to_keep_the_cursor(void) {
	char line[128], text[128], content[130], moved[16384], resized[2048], expected[2048];
	hscroll_line(line);
	snprintf(content, sizeof(content), "%s\n", line);
	const char *path = tmpdir_write("long.txt", content);
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[2048];
	pid_t pid = spawn_notvim_size(path, 24, 40, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "lllllllllllllllllllllllllllllllllllllll", moved, sizeof(moved)); /* 39 moves: the last column */
	set_size(master, 24, 20);
	size_t n = read_output(master, resized, sizeof(resized));
	hscroll_slice(text, 20, 20);
	screen_at(expected, sizeof(expected), text, 1, 20, 1, 40);
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_UINT(strlen(expected), n);
	TEST_ASSERT_EQUAL_STRING(expected, resized);
}

/** @brief On a file with a long line, moving back left past the edge scrolls back one column. */
static void test_notvim_scrolls_left_with_the_cursor(void) {
	char line[128], text[128], content[130], right[16384], left[16384], expected[2048];
	hscroll_line(line);
	snprintf(content, sizeof(content), "%s\n", line);
	const char *path = tmpdir_write("long.txt", content);
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[2048];
	pid_t pid = spawn_notvim_size(path, 24, 30, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "llllllllllllllllllllllllllllllllllllllll", right, sizeof(right)); /* 40 moves: the view starts at 11 */
	send_and_read(master, "hhhhhhhhhhhhhhhhhhhhhhhhhhhhh", left, sizeof(left)); /* 29: the cursor is at the first column */
	size_t n = send_and_read(master, "h", left, sizeof(left));
	hscroll_slice(text, 10, 30);
	screen_at(expected, sizeof(expected), text, 1, 1, 1, 11);
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_UINT(strlen(expected), n);
	TEST_ASSERT_EQUAL_STRING(expected, left);
}


/** @brief On a terminal of one row there is no status line: the one row is text, and the cursor moves and scrolls in it. */
static void test_notvim_one_row_terminal_has_no_status_line(void) {
	const char *path = tmpdir_write("one.txt", "ab\ncd\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], down[1024], expected[1024];
	pid_t pid = spawn_notvim(path, 1, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "j", down, sizeof(down));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING(ALT_ENTER "\x1b[?25l\x1b[H" "ab\x1b[K" "\x1b[1;1H\x1b[?25h", first);
	screen(expected, sizeof(expected), "cd", 1, 1); /* the window scrolled to the second line */
	TEST_ASSERT_EQUAL_STRING(expected, down);
	TEST_ASSERT_NULL(strstr(first, "\x1b[7m"));
	TEST_ASSERT_NULL(strstr(down, "\x1b[7m"));
}

/** @brief On a terminal two rows high the status line is the second row and one line of text is shown: the whole output, nothing else. */
static void test_notvim_two_row_terminal_has_one_text_row_and_the_status_line(void) {
	const char *path = tmpdir_write("two.txt", "ab\ncd\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char out[1024], expected[1024];
	pid_t pid = spawn_notvim(path, 2, &master);
	int raw = wait_until_raw(master);
	read_output(master, out, sizeof(out));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	char bar[128];
	snprintf(bar, sizeof(bar), "%s NORMAL", path);
	snprintf(expected, sizeof(expected), ALT_ENTER "\x1b[?25l\x1b[H" "ab\x1b[K" "\x1b[2;1H\x1b[7m%s%*s1,1\x1b[m" "\x1b[1;1H\x1b[?25h",
	         bar, (int)(80 - strlen(bar) - 3), "");
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/** @brief Assert that @p out is the whole redraw of an empty editor on @p rows rows whose status line is @p status. */
static void assert_empty_screen_with_status(const char *out, int rows, const char *status) {
	char expected[512];
	snprintf(expected, sizeof(expected), "\x1b[?25l\x1b[H" "\x1b[1;1H\x1b[J" "\x1b[%d;1H\x1b[7m%s\x1b[m" "\x1b[1;1H\x1b[?25h", rows, status);
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/** @brief A narrow terminal keeps the position and cuts the name and mode label from the right, to exactly the width, down to the position alone, cut as well at 2 columns and 1. Whole redraws. */
static void test_notvim_status_line_on_a_narrow_terminal(void) {
	int master, raw;
	char first[1024], w10[1024], w4[1024], w3[1024], w2[1024], w1[1024];
	const char *path = tmpdir_path("n.txt"); /* absent: "/tmp/notvim_XXXXXX/n.txt", it starts with "/tmp/n" */
	TEST_ASSERT_NOT_NULL(path);
	pid_t pid = spawn_notvim(path, 24, &master);
	raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	set_size(master, 5, 10);
	read_output(master, w10, sizeof(w10));
	set_size(master, 5, 4);
	read_output(master, w4, sizeof(w4));
	set_size(master, 5, 3);
	read_output(master, w3, sizeof(w3));
	set_size(master, 5, 2);
	read_output(master, w2, sizeof(w2));
	set_size(master, 5, 1);
	read_output(master, w1, sizeof(w1));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	assert_empty_screen_with_status(w10, 5, "/tmp/n 1,1"); /* 6 columns of the name, a space, the position */
	assert_empty_screen_with_status(w4, 5, " 1,1");
	assert_empty_screen_with_status(w3, 5, "1,1");
	assert_empty_screen_with_status(w2, 5, "1,");
	assert_empty_screen_with_status(w1, 5, "1");
}

/** @brief A file name with a control byte is shown with a mark, never raw, and a UTF-8 name is shown as it is. */
static void test_notvim_status_line_shows_marks_and_utf8_in_the_name(void) {
	const char *marked = tmpdir_write("a\x01" "b.txt", "x\n");
	TEST_ASSERT_NOT_NULL(marked);
	int master;
	char out[1024], utf8[1024];
	pid_t pid = spawn_notvim(marked, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, out, sizeof(out));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_NOT_NULL(strstr(out, "a^Ab.txt NORMAL"));
	TEST_ASSERT_NULL_MESSAGE(memchr(out, 0x01, strlen(out)), "the control byte of the name reached the terminal");
	const char *accented = tmpdir_write("\xc3\xa9.txt", "x\n");
	TEST_ASSERT_NOT_NULL(accented);
	pid = spawn_notvim(accented, 24, &master);
	raw = wait_until_raw(master);
	read_output(master, utf8, sizeof(utf8));
	status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_NOT_NULL(strstr(utf8, "\xc3\xa9.txt NORMAL"));
}

/** @brief A CRLF file shows [dos] after its name; an LF file does not. The whole first screen, and the cursor moves keep it. */
static void test_notvim_status_line_shows_dos_for_a_crlf_file_only(void) {
	const char *crlf = tmpdir_write("dos.txt", "ab\r\ncd\r\n");
	TEST_ASSERT_NOT_NULL(crlf);
	int master;
	char first[1024], moved[1024], expected[1024], lf_first[1024], lf_expected[1024];
	pid_t pid = spawn_notvim(crlf, 24, &master);
	shown_dos = 1;
	first_screen(expected, sizeof(expected), "ab\r\ncd", 1, 1);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "j", moved, sizeof(moved));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING(expected, first);
	TEST_ASSERT_NOT_NULL(strstr(moved, " [dos] NORMAL"));
	const char *lf = tmpdir_write("unix.txt", "ab\ncd\n");
	TEST_ASSERT_NOT_NULL(lf);
	pid = spawn_notvim(lf, 24, &master);
	first_screen(lf_expected, sizeof(lf_expected), "ab\r\ncd", 1, 1);
	raw = wait_until_raw(master);
	read_output(master, lf_first, sizeof(lf_first));
	status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING(lf_expected, lf_first);
	TEST_ASSERT_NULL(strstr(lf_first, "[dos]"));
}

/** Four-byte character used by the buffer-size tests. */
#define EMOJI "\xf0\x9f\x98\x80"

/** @brief Write the file of the buffer-size tests: a name of 60 four-byte characters (240 bytes) and 2 lines of 60 four-byte characters; return its path in @p path. */
static void write_emoji_file(char *path, size_t size) {
	const char *dir = tmpdir_path(""); /* "/tmp/notvim_XXXXXX/" */
	TEST_ASSERT_NOT_NULL(dir);
	size_t n = (size_t)snprintf(path, size, "%s", dir);
	for (int i = 0; i < 60; i++) n += (size_t)snprintf(path + n, size - n, EMOJI);
	FILE *f = fopen(path, "w");
	TEST_ASSERT_NOT_NULL(f);
	for (int line = 0; line < 2; line++) {
		for (int i = 0; i < 60; i++) fputs(EMOJI, f);
		fputc('\n', f);
	}
	TEST_ASSERT_EQUAL_INT(0, fclose(f));
}

/** @brief The whole 3x60 screen of the emoji file: 2 full-width 4-byte rows and a status line of the 4-byte name cut to 56 columns (the directory prefix, then emoji), a space and "1,1". */
static void emoji_screen(char *buf, size_t size, const char *path) {
	char text[1024] = "", status[1024];
	for (int line = 0; line < 2; line++) {
		if (line) strcat(text, "\r\n");
		for (int i = 0; i < 60; i++) strcat(text, EMOJI);
	}
	size_t dir_len = strlen(path) - 240;
	snprintf(status, sizeof(status), "%.*s", (int)dir_len, path);
	for (size_t i = dir_len; i < 56; i++) strcat(status, EMOJI);
	strcat(status, " 1,1");
	draw_expected_status(buf, size, text, 2, 60, status, 3, 1, 1);
}

/** @brief The draw buffer holds a 3x60 screen made only of 4-byte characters, status line included: a buffer sized from the text rows is too small by about 100 bytes. */
static void test_notvim_draw_buffer_holds_the_status_line_of_four_byte_text(void) {
	char path[512], first[4096], expected[4096];
	write_emoji_file(path, sizeof(path));
	int master;
	pid_t pid = spawn_notvim_size(path, 3, 60, &master);
	emoji_screen(expected + 8, sizeof(expected) - 8, path);
	memcpy(expected, ALT_ENTER, 8);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING(expected, first);
}

/** @brief The same screen reached by a resize from 24x80, where the buffer is reallocated for the new size. */
static void test_notvim_resize_draw_buffer_holds_the_status_line_of_four_byte_text(void) {
	char path[512], first[4096], after[4096], expected[4096];
	write_emoji_file(path, sizeof(path));
	int master;
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	set_size(master, 3, 60);
	emoji_screen(expected, sizeof(expected), path);
	read_output(master, after, sizeof(after));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING(expected, after);
}

/** @brief From one row to two the status line appears, and from two to one it goes: the text window stays one row, the cursor stays on the text. */
static void test_notvim_status_line_appears_and_disappears_with_the_height(void) {
	const char *path = tmpdir_write("h.txt", "ab\ncd\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], two[1024], one[1024], e_first[1024], e_two[1024], e_one[1024];
	pid_t pid = spawn_notvim(path, 1, &master);
	first_screen(e_first, sizeof(e_first), "ab", 1, 1);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	set_size(master, 2, 80);
	screen(e_two, sizeof(e_two), "ab", 1, 1);
	read_output(master, two, sizeof(two));
	set_size(master, 1, 80);
	screen(e_one, sizeof(e_one), "ab", 1, 1);
	read_output(master, one, sizeof(one));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING(e_first, first);
	TEST_ASSERT_EQUAL_STRING(e_two, two);
	TEST_ASSERT_NOT_NULL(strstr(two, "\x1b[2;1H\x1b[7m"));
	TEST_ASSERT_EQUAL_STRING(e_one, one);
	TEST_ASSERT_NULL(strstr(one, "\x1b[7m"));
}

/** @brief Growing while scrolled: the window keeps its first line, shows the lines below it, and the old status row (now text) is drawn over. */
static void test_notvim_grow_while_scrolled_keeps_the_window_and_redraws_the_old_status_row(void) {
	const char *path = tmpdir_write("grow.txt", "a\nb\nc\nd\ne\nf\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], moved[4096], after[2048], expected[2048];
	pid_t pid = spawn_notvim(path, 4, &master); /* 3 text rows */
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "jjjj", moved, sizeof(moved)); /* line 5: the window is c, d, e */
	set_size(master, 8, 80);
	screen_at(expected, sizeof(expected), "c\r\nd\r\ne\r\nf", 3, 1, 5, 1);
	read_output(master, after, sizeof(after));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_EQUAL_STRING(expected, after);
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
	for (int i = 0; i < file_lines && i < rows - 1; i++) { /* the last row is the status line */
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
/** @brief The screen with @p bottom (padded to the width, plain video) on the last row instead of the status line; the text is the two-line file "ab", "cd". */
static void screen_bottom(char *buf, size_t size, const char *bottom, int row, int col) {
	char padded[256];
	snprintf(padded, sizeof(padded), "%-*s", (int)term_cols, bottom);
	draw_expected_bottom(buf, size, "ab\r\ncd", (size_t)term_rows - 1, term_cols, padded, term_rows, row, col);
}

/** @brief ':' opens the command line on the bottom row with the cursor after the text, Enter on "z" shows E492 in plain video, and the next key brings the status line back. */
static void test_notvim_command_line_error_message_and_next_key(void) {
	const char *path = tmpdir_write("cmd.txt", "ab\ncd\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], o1[2048], o2[2048], o3[2048], o4[2048];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, ":", o1, sizeof(o1));
	send_and_read(master, "z", o2, sizeof(o2));
	send_and_read(master, "\r", o3, sizeof(o3));
	send_and_read(master, "j", o4, sizeof(o4));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	char want[2048];
	screen_bottom(want, sizeof(want), ":", 24, 2);
	TEST_ASSERT_EQUAL_STRING(want, o1);
	screen_bottom(want, sizeof(want), ":z", 24, 3);
	TEST_ASSERT_EQUAL_STRING(want, o2);
	screen_bottom(want, sizeof(want), "E492: Not an editor command: z", 1, 1);
	TEST_ASSERT_EQUAL_STRING(want, o3);
	TEST_ASSERT_NULL_MESSAGE(strstr(o3, "\x1b[7m"), "a message is not in reverse video");
	screen(want, sizeof(want), "ab\r\ncd", 2, 1);
	TEST_ASSERT_EQUAL_STRING(want, o4);
}

/** @brief Esc cancels a command line, and so does Backspace on an empty one: the status line is back and nothing is run. */
static void test_notvim_command_line_esc_and_backspace_cancel(void) {
	const char *path = tmpdir_write("cmd2.txt", "ab\ncd\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], o1[2048], o2[2048], o3[2048], o4[2048];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, ":w", o1, sizeof(o1)); /* two redraws */
	send_and_read(master, "\x1b", o2, sizeof(o2));
	send_and_read(master, ":", o3, sizeof(o3));
	send_and_read(master, "\x7f", o4, sizeof(o4));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	char want[2048], one[2048];
	screen_bottom(want, sizeof(want), ":", 24, 2);
	screen_bottom(one, sizeof(one), ":w", 24, 3);
	strcat(want, one);
	TEST_ASSERT_EQUAL_STRING(want, o1);
	screen(want, sizeof(want), "ab\r\ncd", 1, 1);
	TEST_ASSERT_EQUAL_STRING(want, o2);
	screen_bottom(want, sizeof(want), ":", 24, 2);
	TEST_ASSERT_EQUAL_STRING(want, o3);
	screen(want, sizeof(want), "ab\r\ncd", 1, 1);
	TEST_ASSERT_EQUAL_STRING(want, o4);
}

/** @brief A resize in command mode keeps the command line, drawn for the new size with the cursor at the end of the text on the new last row. */
static void test_notvim_command_line_survives_a_resize(void) {
	const char *path = tmpdir_write("cmd3.txt", "ab\ncd\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], o1[2048], o2[2048];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, ":wq", o1, sizeof(o1));
	set_size(master, 10, 30);
	read_output(master, o2, sizeof(o2));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	char want[2048];
	screen_bottom(want, sizeof(want), ":wq", 10, 4);
	TEST_ASSERT_EQUAL_STRING(want, o2);
}

/** @brief Edit a file (created with @p content) by typing "x" at the start, save with :w, and check that its bytes on disk are the @p want_len bytes of @p want and that the screen says "written". */
static void check_edit_and_save(const char *name, const char *content, const char *want, size_t want_len) {
	char path[256], buf[256];
	snprintf(path, sizeof(path), "%s", tmpdir_write(name, content));
	int master;
	char first[2048], o1[2048], o2[2048], o3[2048], o4[2048];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "ix", o1, sizeof(o1));
	send_and_read(master, "\x1b", o2, sizeof(o2));
	send_and_read(master, ":w\r", o3, sizeof(o3));
	send_and_read(master, "j", o4, sizeof(o4));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_NOT_NULL_MESSAGE(strstr(o3, " written"), "the message says written");
	TEST_ASSERT_NOT_NULL_MESSAGE(strstr(o3, path), "the message names the file");
	TEST_ASSERT_NULL_MESSAGE(strstr(o4, " written"), "the message goes with the next key");
	TEST_ASSERT_NULL_MESSAGE(strstr(o4, "[+]"), "the file is no longer modified");
	FILE *f = fopen(path, "rb");
	TEST_ASSERT_NOT_NULL(f);
	size_t n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	TEST_ASSERT_EQUAL_UINT(want_len, n);
	TEST_ASSERT_EQUAL_MEMORY(want, buf, want_len);
}

/** @brief Typing into a loaded LF file and :w saves it with LF only. */
static void test_notvim_w_saves_an_lf_file_without_cr(void) {
	check_edit_and_save("save_lf.txt", "ab\ncd\n", "xab\ncd\n", 7);
}

/** @brief Typing into a loaded CRLF file and :w keeps the CRLF line endings. */
static void test_notvim_w_keeps_a_crlf_file_crlf(void) {
	check_edit_and_save("save_crlf.txt", "ab\r\ncd\r\n", "xab\r\ncd\r\n", 9);
}

/** @brief Without a file name :w shows E32; ":w <path>" then creates the file and shows [New]. */
static void test_notvim_w_without_a_name_and_with_one(void) {
	char path[256], cmd[300], buf[64];
	snprintf(path, sizeof(path), "%s", tmpdir_path("created.txt"));
	snprintf(cmd, sizeof(cmd), ":w %s\r", path);
	int master;
	static char o4[65536]; /* every typed character of the long command redraws the screen */
	char first[2048], o1[2048], o2[2048], o3[2048];
	pid_t pid = spawn_notvim(NULL, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "ihi", o1, sizeof(o1));
	send_and_read(master, "\x1b", o2, sizeof(o2));
	send_and_read(master, ":w\r", o3, sizeof(o3));
	send_and_read(master, cmd, o4, sizeof(o4));
	int status = quit_and_wait(master, pid);
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	TEST_ASSERT_NOT_NULL(strstr(o3, "E32: No file name"));
	TEST_ASSERT_NOT_NULL(strstr(o4, "[New] 1L, 3B written"));
	FILE *f = fopen(path, "rb");
	TEST_ASSERT_NOT_NULL(f);
	size_t n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	TEST_ASSERT_EQUAL_UINT(3, n);
	TEST_ASSERT_EQUAL_MEMORY("hi\n", buf, 3);
}

/** @brief Send @p keys that must quit notvim, read what it writes in @p last and return the exit status; what it writes must END with the switch back from the alternate screen (the typed characters of a command line are redrawn before it). */
static int quit_with_keys(int master, pid_t pid, const char *keys, char *last, size_t size) {
	if (write(master, keys, strlen(keys)) < 0) kill(pid, SIGKILL);
	read_output(master, last, size);
	return wait_exit(pid);
}

/** @brief Assert that @p out ends with the switch back from the alternate screen and holds the other one nowhere else. */
static void assert_left_alt_screen(const char *out) {
	size_t n = strlen(out), m = strlen(ALT_LEAVE);
	TEST_ASSERT_TRUE_MESSAGE(n >= m && strcmp(out + n - m, ALT_LEAVE) == 0, "the alternate screen was not left last");
	TEST_ASSERT_NULL(strstr(out, "E37"));
}

/** @brief Read a whole file into @p buf (NUL-terminated); return its length. */
static size_t file_text(const char *path, char *buf, size_t size) {
	FILE *f = fopen(path, "rb");
	size_t n = f ? fread(buf, 1, size - 1, f) : 0;
	if (f) fclose(f);
	buf[n] = '\0';
	return n;
}

/** @brief ":q" quits an unmodified file with status 0; on a modified one it shows E37 and goes on, and ":q!" then quits without writing; both leave the alternate screen. */
static void test_notvim_q_is_refused_when_modified_and_q_bang_forces(void) {
	const char *path = tmpdir_write("qq.txt", "ab\n");
	TEST_ASSERT_NOT_NULL(path);
	int master;
	char first[1024], o1[2048], o2[2048], last[1024], text[64];
	pid_t pid = spawn_notvim(path, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	int status = quit_with_keys(master, pid, ":q\r", last, sizeof(last));
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_EQUAL_INT(0, status);
	assert_left_alt_screen(last);
	pid = spawn_notvim(path, 24, &master);
	raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "ix\x1b", o1, sizeof(o1));
	send_and_read(master, ":q\r", o2, sizeof(o2));
	status = quit_with_keys(master, pid, ":q!\r", last, sizeof(last));
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_NOT_NULL_MESSAGE(strstr(o2, "E37: No write since last change (add ! to override)"), "refusal not shown");
	TEST_ASSERT_NULL_MESSAGE(strstr(o2, ALT_LEAVE), "the refused quit must not leave");
	TEST_ASSERT_EQUAL_INT(0, status);
	assert_left_alt_screen(last);
	file_text(path, text, sizeof(text));
	TEST_ASSERT_EQUAL_STRING("ab\n", text);
}

/** @brief ":wq" and "ZZ" save the changes and quit with status 0; "ZQ" quits without saving. */
static void test_notvim_wq_zz_and_zq_quit(void) {
	const char *keys[] = { ":wq\r", "ZZ", "ZQ" };
	const char *want[] = { "xab\n", "xab\n", "ab\n" };
	for (int i = 0; i < 3; i++) {
		const char *path = tmpdir_write("wq.txt", "ab\n");
		TEST_ASSERT_NOT_NULL(path);
		int master;
		char first[1024], o1[2048], last[1024], text[64];
		pid_t pid = spawn_notvim(path, 24, &master);
		int raw = wait_until_raw(master);
		read_output(master, first, sizeof(first));
		send_and_read(master, "ix\x1b", o1, sizeof(o1));
		int status = quit_with_keys(master, pid, keys[i], last, sizeof(last));
		close(master);
		TEST_ASSERT_TRUE(raw);
		TEST_ASSERT_EQUAL_INT(0, status);
		assert_left_alt_screen(last);
		file_text(path, text, sizeof(text));
		TEST_ASSERT_EQUAL_STRING(want[i], text);
	}
}

/** @brief Ctrl+Q on a modified buffer shows E37 and goes on; a ":wq" whose write fails shows the error and goes on; ":q!" then quits. */
static void test_notvim_ctrl_q_and_failed_wq_do_not_quit_a_modified_buffer(void) {
	char path[256], cmd[300];
	snprintf(path, sizeof(path), "nodir/f.txt"); /* relative: a long path would be cut on the bottom row */
	snprintf(cmd, sizeof(cmd), ":w %s\r", path);
	int master;
	char first[1024], o1[2048], o2[2048], o3[2048], last[1024];
	static char o4[65536]; /* every typed character of the long command redraws the screen */
	pid_t pid = spawn_notvim(NULL, 24, &master);
	int raw = wait_until_raw(master);
	read_output(master, first, sizeof(first));
	send_and_read(master, "ihi", o1, sizeof(o1));
	send_and_read(master, "\x11", o2, sizeof(o2));
	send_and_read(master, "\x1b", o3, sizeof(o3)); /* alone in its write: a lone Esc */
	send_and_read(master, cmd, o4, sizeof(o4));
	send_and_read(master, ":wq\r", o3, sizeof(o3));
	int status = quit_with_keys(master, pid, ":q!\r", last, sizeof(last));
	close(master);
	TEST_ASSERT_TRUE(raw);
	TEST_ASSERT_NOT_NULL(strstr(o2, "E37: No write since last change (add ! to override)"));
	TEST_ASSERT_NULL(strstr(o2, ALT_LEAVE));
	TEST_ASSERT_NOT_NULL(strstr(o4, "No such file or directory"));
	TEST_ASSERT_NULL(strstr(o4, ALT_LEAVE));
	TEST_ASSERT_NOT_NULL(strstr(o3, "E32: No file name"));
	TEST_ASSERT_EQUAL_INT(0, status);
	assert_left_alt_screen(last);
}

void test_notvim_suite(void) {
	RUN_TEST(test_notvim_binary_enters_raw_and_quits_on_ctrl_q);
	RUN_TEST(test_notvim_shows_file_lines);
	RUN_TEST(test_notvim_shows_only_the_rows_that_fit);
	RUN_TEST(test_notvim_shows_a_full_screen_of_long_lines);
	RUN_TEST(test_notvim_arrow_keys_move_the_cursor_and_redraw);
	RUN_TEST(test_notvim_hjkl_move_the_cursor_and_redraw);
	RUN_TEST(test_notvim_line_and_word_motions_move_the_cursor);
	RUN_TEST(test_notvim_counts_gg_and_g_move_the_cursor);
	RUN_TEST(test_notvim_dollar_then_j_keeps_to_the_end);
	RUN_TEST(test_notvim_page_keys_scroll_and_follow_a_resize);
	RUN_TEST(test_notvim_paragraph_and_bracket_motions_move_the_cursor);
	RUN_TEST(test_notvim_character_search_moves_the_cursor);
	RUN_TEST(test_notvim_uppercase_hjkl_do_nothing);
	RUN_TEST(test_notvim_i_and_esc_switch_modes);
	RUN_TEST(test_notvim_arrows_in_insert_mode_reach_the_end_of_the_line);
	RUN_TEST(test_notvim_typing_in_insert_mode);
	RUN_TEST(test_notvim_enter_backspace_and_delete_split_and_join_lines);
	RUN_TEST(test_notvim_status_says_insert_and_ctrl_q_quits_in_insert_mode);
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
	RUN_TEST(test_notvim_status_line_without_a_file_exact_bytes);
	RUN_TEST(test_notvim_status_line_shows_the_name_of_a_missing_file);
	RUN_TEST(test_notvim_status_line_comes_after_the_erase_of_the_unused_rows);
	RUN_TEST(test_notvim_status_line_follows_the_cursor);
	RUN_TEST(test_notvim_status_line_column_is_the_display_column);
	RUN_TEST(test_notvim_status_line_moves_to_the_new_last_row_on_resize);
	RUN_TEST(test_notvim_scrolls_right_with_the_cursor);
	RUN_TEST(test_notvim_typing_at_the_right_edge_scrolls);
	RUN_TEST(test_notvim_resize_scrolls_to_keep_the_cursor);
	RUN_TEST(test_notvim_scrolls_left_with_the_cursor);
	RUN_TEST(test_notvim_one_row_terminal_has_no_status_line);
	RUN_TEST(test_notvim_two_row_terminal_has_one_text_row_and_the_status_line);
	RUN_TEST(test_notvim_status_line_on_a_narrow_terminal);
	RUN_TEST(test_notvim_status_line_shows_marks_and_utf8_in_the_name);
	RUN_TEST(test_notvim_status_line_shows_dos_for_a_crlf_file_only);
	RUN_TEST(test_notvim_draw_buffer_holds_the_status_line_of_four_byte_text);
	RUN_TEST(test_notvim_resize_draw_buffer_holds_the_status_line_of_four_byte_text);
	RUN_TEST(test_notvim_status_line_appears_and_disappears_with_the_height);
	RUN_TEST(test_notvim_grow_while_scrolled_keeps_the_window_and_redraws_the_old_status_row);
	RUN_TEST(test_notvim_command_line_error_message_and_next_key);
	RUN_TEST(test_notvim_command_line_esc_and_backspace_cancel);
	RUN_TEST(test_notvim_command_line_survives_a_resize);
	RUN_TEST(test_notvim_w_saves_an_lf_file_without_cr);
	RUN_TEST(test_notvim_w_keeps_a_crlf_file_crlf);
	RUN_TEST(test_notvim_w_without_a_name_and_with_one);
	RUN_TEST(test_notvim_q_is_refused_when_modified_and_q_bang_forces);
	RUN_TEST(test_notvim_wq_zz_and_zq_quit);
	RUN_TEST(test_notvim_ctrl_q_and_failed_wq_do_not_quit_a_modified_buffer);
}
