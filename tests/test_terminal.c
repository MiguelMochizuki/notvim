/**
 * @file test_terminal.c
 * @brief Unit and pty-based tests for terminal.c.
 */
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

/** @brief Register every test in this file with Unity. */
void test_terminal_suite(void) {
	RUN_TEST(test_terminal_sets_raw_flags);
	RUN_TEST(test_terminal_preserves_unrelated_flags);
	RUN_TEST(test_enter_and_leave_raw_restores_terminal);
	RUN_TEST(test_enter_raw_twice_still_restores_original);
	RUN_TEST(test_leave_raw_without_enter_is_noop);
	RUN_TEST(test_leave_raw_twice_is_noop);
}
