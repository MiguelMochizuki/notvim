/**
 * @file test_editor.c
 * @brief Unit and pty-based tests for editor.c.
 */
#include <termios.h>
#include <string.h>
#include <pty.h>
#include <unistd.h>
#include "unity.h"
#include "test_editor.h"
#include "editor.h"

/** @brief Unity hook run before each test; nothing to prepare. */
void setUp(void) {}
/** @brief Unity hook run after each test; nothing to clean up. */
void tearDown(void) {}

/** @brief editor_version() returns 0. */
static void test_editor_version_returns_zero(void) {
	TEST_ASSERT_EQUAL_INT(0, editor_version());
}

/** @brief editor_set_raw_flags() clears the raw-mode flags and sets VMIN/VTIME. */
static void test_editor_sets_raw_flags(void) {
	struct termios t;
	memset(&t, 0, sizeof(struct termios));
	editor_set_raw_flags(&t);

	TEST_ASSERT_EQUAL_INT(0, t.c_lflag & (ICANON | ECHO | ISIG));
	TEST_ASSERT_EQUAL_INT(0, t.c_iflag & (IXON | ICRNL));
	TEST_ASSERT_EQUAL_INT(0, t.c_oflag & OPOST);
	TEST_ASSERT_EQUAL_INT(1, t.c_cc[VMIN]);
	TEST_ASSERT_EQUAL_INT(0, t.c_cc[VTIME]);
}

/** @brief editor_set_raw_flags() leaves unrelated flags untouched. */
static void test_editor_preserves_unrelated_flags(void) {
	struct termios t;
	memset(&t, 0, sizeof(struct termios));
	t.c_cflag = CS8 | B9600;
	t.c_lflag = ECHOE | ECHOK;
	t.c_iflag = IGNBRK;
	t.c_oflag = ONLCR;

	editor_set_raw_flags(&t);

	TEST_ASSERT_EQUAL_INT(CS8 | B9600, t.c_cflag);
	TEST_ASSERT_EQUAL_INT(ECHOE | ECHOK, t.c_lflag);
	TEST_ASSERT_EQUAL_INT(IGNBRK, t.c_iflag);
	TEST_ASSERT_EQUAL_INT(ONLCR, t.c_oflag);
}

/** @brief Ctrl+Q makes editor_should_exit() return 1. */
static void test_editor_should_exit_on_ctrl_q(void) {
	TEST_ASSERT_EQUAL_INT(1, editor_should_exit(0x11));
}

/** @brief Other keys, including Ctrl+C, do not exit. */
static void test_editor_should_not_exit_on_other_keys(void) {
	TEST_ASSERT_EQUAL_INT(0, editor_should_exit('a'));
	TEST_ASSERT_EQUAL_INT(0, editor_should_exit(0x03));  /* Ctrl+C */
	TEST_ASSERT_EQUAL_INT(0, editor_should_exit('q'));
}

/** @brief enter/leave on a pty switches to raw mode and restores the original attributes. */
static void test_enter_and_leave_raw_restores_terminal(void) {
	int master, slave;
	TEST_ASSERT_EQUAL_INT(0, openpty(&master, &slave, NULL, NULL, NULL));

	struct termios original;
	TEST_ASSERT_EQUAL_INT(0, tcgetattr(slave, &original));

	/* Enter raw mode */
	editor_enter_raw(slave);
	struct termios raw;
	TEST_ASSERT_EQUAL_INT(0, tcgetattr(slave, &raw));
	TEST_ASSERT_EQUAL_INT(0, raw.c_lflag & (ICANON | ECHO | ISIG));
	TEST_ASSERT_EQUAL_INT(0, raw.c_iflag & (IXON | ICRNL));
	TEST_ASSERT_EQUAL_INT(0, raw.c_oflag & OPOST);
	TEST_ASSERT_EQUAL_INT(1, raw.c_cc[VMIN]);
	TEST_ASSERT_EQUAL_INT(0, raw.c_cc[VTIME]);

	/* Leave raw mode */
	editor_leave_raw(slave);
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

/** @brief editor_init() zeroes the buffer and length. */
static void test_editor_init_zeroes_buffer(void) {
	editor_t e;
	memset(&e, 0xFF, sizeof(e));
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, e.len);
	TEST_ASSERT_EQUAL_INT(0, e.buffer[0]);
	TEST_ASSERT_EQUAL_INT(0, e.buffer[EDITOR_BUFFER_SIZE - 1]);
}

/** @brief Rendering an empty buffer yields an empty string. */
static void test_editor_render_empty_buffer(void) {
	editor_t e;
	editor_init(&e);
	char out[256];
	size_t n = editor_render(&e, out, sizeof(out));
	TEST_ASSERT_EQUAL_INT(0, n);
	TEST_ASSERT_EQUAL_INT(0, out[0]);
}

/** @brief Rendering "abc" copies it and returns 3. */
static void test_editor_render_abc(void) {
	editor_t e;
	editor_init(&e);
	memcpy(e.buffer, "abc", 3);
	e.len = 3;
	char out[256];
	size_t n = editor_render(&e, out, sizeof(out));
	TEST_ASSERT_EQUAL_INT(3, n);
	TEST_ASSERT_EQUAL_STRING("abc", out);
}

/** @brief Rendering truncates to out_size - 1 and stays NUL-terminated. */
static void test_editor_render_truncates_on_small_buffer(void) {
	editor_t e;
	editor_init(&e);
	memcpy(e.buffer, "abcdef", 6);
	e.len = 6;
	char out[4];  /* 3 chars + '\0' */
	size_t n = editor_render(&e, out, sizeof(out));
	TEST_ASSERT_EQUAL_INT(3, n);
	TEST_ASSERT_EQUAL_STRING("abc", out);
}

/** @brief Rendering with out_size 0 returns 0 and leaves out untouched. */
static void test_editor_render_zero_size_returns_zero(void) {
	editor_t e;
	editor_init(&e);
	memcpy(e.buffer, "abc", 3);
	e.len = 3;
	char out[1] = { 'x' };
	size_t n = editor_render(&e, out, 0);
	TEST_ASSERT_EQUAL_INT(0, n);
	/* out must not be touched */
	TEST_ASSERT_EQUAL_INT('x', out[0]);
}

/** @brief Register every test in this file with Unity. */
void test_editor_suite(void) {
	RUN_TEST(test_editor_version_returns_zero);
	RUN_TEST(test_editor_sets_raw_flags);
	RUN_TEST(test_editor_preserves_unrelated_flags);
	RUN_TEST(test_editor_should_exit_on_ctrl_q);
	RUN_TEST(test_editor_should_not_exit_on_other_keys);
	RUN_TEST(test_enter_and_leave_raw_restores_terminal);
	RUN_TEST(test_editor_init_zeroes_buffer);
	RUN_TEST(test_editor_render_empty_buffer);
	RUN_TEST(test_editor_render_abc);
	RUN_TEST(test_editor_render_truncates_on_small_buffer);
	RUN_TEST(test_editor_render_zero_size_returns_zero);
}
