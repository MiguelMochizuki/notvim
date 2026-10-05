/**
 * @file test_editor.c
 * @brief Unit tests for editor.c.
 */
#include <string.h>
#include "unity.h"
#include "test_editor.h"
#include "editor.h"

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
	RUN_TEST(test_editor_should_exit_on_ctrl_q);
	RUN_TEST(test_editor_should_not_exit_on_other_keys);
	RUN_TEST(test_editor_init_zeroes_buffer);
	RUN_TEST(test_editor_render_empty_buffer);
	RUN_TEST(test_editor_render_abc);
	RUN_TEST(test_editor_render_truncates_on_small_buffer);
	RUN_TEST(test_editor_render_zero_size_returns_zero);
}
