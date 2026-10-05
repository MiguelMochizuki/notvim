/**
 * @file test_terminal.c
 * @brief Unit and pty-based tests for terminal.c.
 */
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
}
