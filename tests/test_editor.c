/**
 * Tests for editor struct implementations
 */
#include <termios.h>
#include <string.h>
#include "unity.h"
#include "test_editor.h"
#include "editor.h"

void setUp(void) {}
void tearDown(void) {}

static void test_editor_version_returns_zero(void) {
	TEST_ASSERT_EQUAL_INT(0, editor_version());
}

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

static void test_editor_should_exit_on_ctrl_q(void) {
	TEST_ASSERT_EQUAL_INT(1, editor_should_exit(0x11));
}

static void test_editor_should_not_exit_on_other_keys(void) {
	TEST_ASSERT_EQUAL_INT(0, editor_should_exit('a'));
	TEST_ASSERT_EQUAL_INT(0, editor_should_exit(0x03));  /* Ctrl+C */
	TEST_ASSERT_EQUAL_INT(0, editor_should_exit('q'));
}

void test_editor_suite(void) {
	RUN_TEST(test_editor_version_returns_zero);
	RUN_TEST(test_editor_sets_raw_flags);
	RUN_TEST(test_editor_preserves_unrelated_flags);
	RUN_TEST(test_editor_should_exit_on_ctrl_q);
	RUN_TEST(test_editor_should_not_exit_on_other_keys);
}
