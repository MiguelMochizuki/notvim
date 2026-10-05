/**
 * @file test_cmdline.c
 * @brief Unit tests for cmdline.c: the command line text and the command parser.
 */
#include <string.h>
#include "unity.h"
#include "test_cmdline.h"
#include "cmdline.h"

/** @brief A cleared command line is empty. */
static void test_cmdline_clear_empties(void) {
	cmdline_t c;
	cmdline_clear(&c);
	TEST_ASSERT_EQUAL_UINT(0, c.len);
	TEST_ASSERT_EQUAL_STRING("", c.text);
	TEST_ASSERT_EQUAL_INT(0, cmdline_append(&c, "wq", 2));
	cmdline_clear(&c);
	TEST_ASSERT_EQUAL_UINT(0, c.len);
	TEST_ASSERT_EQUAL_STRING("", c.text);
}

/** @brief Append adds bytes at the end and keeps the NUL. */
static void test_cmdline_append_adds_at_the_end(void) {
	cmdline_t c;
	cmdline_clear(&c);
	TEST_ASSERT_EQUAL_INT(0, cmdline_append(&c, "w", 1));
	TEST_ASSERT_EQUAL_INT(0, cmdline_append(&c, "q", 1));
	TEST_ASSERT_EQUAL_STRING("wq", c.text);
	TEST_ASSERT_EQUAL_UINT(2, c.len);
}

/** @brief The line holds CMDLINE_MAX bytes; one more is refused and nothing of it is kept, also for a multibyte character. */
static void test_cmdline_append_is_bounded(void) {
	cmdline_t c;
	cmdline_clear(&c);
	for (int i = 0; i < CMDLINE_MAX - 1; i++) TEST_ASSERT_EQUAL_INT(0, cmdline_append(&c, "a", 1));
	TEST_ASSERT_EQUAL_INT(-1, cmdline_append(&c, "\xc3\xa9", 2)); /* 2 bytes, 1 left: all or nothing */
	TEST_ASSERT_EQUAL_UINT(CMDLINE_MAX - 1, c.len);
	TEST_ASSERT_EQUAL_INT(0, cmdline_append(&c, "b", 1));
	TEST_ASSERT_EQUAL_UINT(CMDLINE_MAX, c.len);
	TEST_ASSERT_EQUAL_INT(-1, cmdline_append(&c, "c", 1));
	TEST_ASSERT_EQUAL_UINT(CMDLINE_MAX, c.len);
	TEST_ASSERT_EQUAL_UINT(CMDLINE_MAX, strlen(c.text));
	TEST_ASSERT_EQUAL_CHAR('b', c.text[CMDLINE_MAX - 1]);
}

/** @brief Backspace removes the last character, all the bytes of a UTF-8 one, and is harmless on an empty line. */
static void test_cmdline_backspace_removes_a_whole_character(void) {
	cmdline_t c;
	cmdline_clear(&c);
	cmdline_backspace(&c);
	TEST_ASSERT_EQUAL_UINT(0, c.len);
	cmdline_append(&c, "a", 1);
	cmdline_append(&c, "\xe2\x82\xac", 3);
	cmdline_backspace(&c);
	TEST_ASSERT_EQUAL_STRING("a", c.text);
	TEST_ASSERT_EQUAL_UINT(1, c.len);
	cmdline_backspace(&c);
	TEST_ASSERT_EQUAL_STRING("", c.text);
}

/** @brief Assert that @p text parses to @p name, @p bang and @p arg. */
static void assert_parse(const char *text, const char *name, int bang, const char *arg) {
	cmd_t cmd;
	cmd_parse(text, &cmd);
	TEST_ASSERT_EQUAL_STRING(name, cmd.name);
	TEST_ASSERT_EQUAL_INT(bang, cmd.bang);
	TEST_ASSERT_EQUAL_STRING(arg, cmd.arg);
}

/** @brief Every form of the commands that are coming: w, w!, w name, q, q!, wq, x. */
static void test_cmd_parse_forms(void) {
	assert_parse("w", "w", 0, "");
	assert_parse("w!", "w", 1, "");
	assert_parse("w name", "w", 0, "name");
	assert_parse("w! name", "w", 1, "name");
	assert_parse("q", "q", 0, "");
	assert_parse("q!", "q", 1, "");
	assert_parse("wq", "wq", 0, "");
	assert_parse("wq!", "wq", 1, "");
	assert_parse("x", "x", 0, "");
}

/** @brief Spaces around the argument go; spaces inside it stay; the bang may be followed by the argument without a space. */
static void test_cmd_parse_trims_spaces(void) {
	assert_parse("  w   a b.txt   ", "w", 0, "a b.txt");
	assert_parse("w!name", "w", 1, "name");
	assert_parse(" q! ", "q", 1, "");
	assert_parse(":w x", "w", 0, "x");
	assert_parse("::  wq", "wq", 0, "");
}

/** @brief An empty text gives an empty name; an unknown name parses like any other; a name stops at the first non-letter. */
static void test_cmd_parse_empty_and_unknown(void) {
	assert_parse("", "", 0, "");
	assert_parse("   ", "", 0, "");
	assert_parse(":", "", 0, "");
	assert_parse("foo", "foo", 0, "");
	assert_parse("foo bar", "foo", 0, "bar");
	assert_parse("w1", "w", 0, "1");
	assert_parse("123", "", 0, "123");
	assert_parse("!ls", "", 1, "ls");
}

/** @brief A command of CMDLINE_MAX bytes parses without overflowing name or argument. */
static void test_cmd_parse_longest_text(void) {
	char text[CMDLINE_MAX + 1];
	memset(text, 'a', CMDLINE_MAX);
	text[CMDLINE_MAX] = '\0';
	cmd_t cmd;
	cmd_parse(text, &cmd);
	TEST_ASSERT_EQUAL_UINT(CMDLINE_MAX, strlen(cmd.name));
	memset(text, 'a', CMDLINE_MAX);
	text[0] = 'w';
	text[1] = ' ';
	cmd_parse(text, &cmd);
	TEST_ASSERT_EQUAL_UINT(CMDLINE_MAX - 2, strlen(cmd.arg));
}

/** @brief Run the command line tests. */
void test_cmdline_suite(void) {
	RUN_TEST(test_cmdline_clear_empties);
	RUN_TEST(test_cmdline_append_adds_at_the_end);
	RUN_TEST(test_cmdline_append_is_bounded);
	RUN_TEST(test_cmdline_backspace_removes_a_whole_character);
	RUN_TEST(test_cmd_parse_forms);
	RUN_TEST(test_cmd_parse_trims_spaces);
	RUN_TEST(test_cmd_parse_empty_and_unknown);
	RUN_TEST(test_cmd_parse_longest_text);
}
