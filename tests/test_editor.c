/**
 * @file test_editor.c
 * @brief Unit tests for editor.c.
 */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "unity.h"
#include "test_editor.h"
#include "editor.h"
#include "tmpdir.h"

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

/** @brief Assert that line @p i of @p e equals @p expected. */
static void assert_line(const editor_t *e, size_t i, const char *expected) {
	TEST_ASSERT_NOT_NULL(editor_line(e, i));
	TEST_ASSERT_EQUAL_STRING(expected, editor_line(e, i));
}

/** @brief A fresh editor has no lines. */
static void test_editor_init_is_empty(void) {
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
	TEST_ASSERT_NULL(editor_line(&e, 0));
	editor_free(&e);
}

/** @brief Appending one line gives a count of 1 and the same text. */
static void test_editor_append_one_line(void) {
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "hello"));
	TEST_ASSERT_EQUAL_UINT(1, editor_line_count(&e));
	assert_line(&e, 0, "hello");
	editor_free(&e);
}

/** @brief Several appends keep their order. */
static void test_editor_append_keeps_order(void) {
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "one"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "two"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "three"));
	TEST_ASSERT_EQUAL_UINT(3, editor_line_count(&e));
	assert_line(&e, 0, "one");
	assert_line(&e, 1, "two");
	assert_line(&e, 2, "three");
	editor_free(&e);
}

/** @brief Append copies the text: changing the source later does not affect the editor. */
static void test_editor_append_copies_text(void) {
	editor_t e;
	editor_init(&e);
	char src[] = "abc";
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, src));
	src[0] = 'X';
	assert_line(&e, 0, "abc");
	editor_free(&e);
}

/** @brief Appending "" adds a blank line. */
static void test_editor_append_empty_string_is_a_line(void) {
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "a"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, ""));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "b"));
	TEST_ASSERT_EQUAL_UINT(3, editor_line_count(&e));
	assert_line(&e, 1, "");
	editor_free(&e);
}

/** @brief 100 appends (past any initial capacity) all stay intact. */
static void test_editor_append_many_lines_grows(void) {
	editor_t e;
	editor_init(&e);
	char text[16];
	for (int i = 0; i < 100; i++) {
		snprintf(text, sizeof(text), "line %d", i);
		TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, text));
	}
	TEST_ASSERT_EQUAL_UINT(100, editor_line_count(&e));
	for (int i = 0; i < 100; i++) {
		snprintf(text, sizeof(text), "line %d", i);
		assert_line(&e, (size_t)i, text);
	}
	editor_free(&e);
}

/** @brief A line far longer than the render buffer is stored whole. */
static void test_editor_append_long_line_is_not_truncated(void) {
	editor_t e;
	editor_init(&e);
	char text[5001]; /* on the stack: a failed assert must not leak it */
	memset(text, 'a', 5000);
	text[5000] = '\0';
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, text));
	TEST_ASSERT_EQUAL_UINT(5000, strlen(editor_line(&e, 0)));
	assert_line(&e, 0, text);
	editor_free(&e);
}

/** @brief editor_line() returns NULL for an index past the last line. */
static void test_editor_line_out_of_range_is_null(void) {
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "a"));
	TEST_ASSERT_NULL(editor_line(&e, 1));
	TEST_ASSERT_NULL(editor_line(&e, 1000));
	editor_free(&e);
}

/** @brief After editor_free() the editor is empty and can be used again. */
static void test_editor_free_resets_and_allows_reuse(void) {
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "a"));
	editor_free(&e);
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "b"));
	assert_line(&e, 0, "b");
	editor_free(&e);
}

/** @brief editor_free() is safe on a fresh editor and when called twice. */
static void test_editor_free_is_safe_when_empty_or_repeated(void) {
	editor_t e;
	editor_init(&e);
	editor_free(&e);
	editor_free(&e);
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
}

/** @brief Rendering an editor with no lines yields an empty string. */
static void test_editor_render_no_lines(void) {
	editor_t e;
	editor_init(&e);
	char out[256] = "garbage";
	TEST_ASSERT_EQUAL_UINT(0, editor_render(&e, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("", out);
	editor_free(&e);
}

/** @brief Rendering one line copies it and returns its length. */
static void test_editor_render_one_line(void) {
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "abc"));
	char out[256];
	TEST_ASSERT_EQUAL_UINT(3, editor_render(&e, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("abc", out);
	editor_free(&e);
}

/** @brief Lines are joined with "\r\n" and there is no trailing separator. */
static void test_editor_render_joins_lines_with_crlf(void) {
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "a"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, ""));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "b"));
	char out[256];
	TEST_ASSERT_EQUAL_UINT(6, editor_render(&e, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("a\r\n\r\nb", out);
	editor_free(&e);
}

/** @brief Rendering truncates to out_size - 1 and stays NUL-terminated. */
static void test_editor_render_truncates_on_small_buffer(void) {
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "abcdef"));
	char out[4]; /* 3 chars + '\0' */
	TEST_ASSERT_EQUAL_UINT(3, editor_render(&e, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("abc", out);
	editor_free(&e);
}

/** @brief Truncation can cut in the middle of the "\r\n" separator. */
static void test_editor_render_truncates_across_lines(void) {
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "ab"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "cd"));
	char out[5]; /* "ab\r\n" would need 4 chars + '\0'; "cd" is cut off */
	TEST_ASSERT_EQUAL_UINT(4, editor_render(&e, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("ab\r\n", out);
	editor_free(&e);
}

/** @brief Rendering with out_size 0 returns 0 and leaves out untouched. */
static void test_editor_render_zero_size_returns_zero(void) {
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "abc"));
	char out[1] = { 'x' };
	TEST_ASSERT_EQUAL_UINT(0, editor_render(&e, out, 0));
	TEST_ASSERT_EQUAL_INT('x', out[0]); /* out must not be touched */
	editor_free(&e);
}

/** @brief File with three lines is correctly loaded into editor */
static void test_editor_load_three_lines(void) {
	const char *path = tmpdir_write("testfile.txt", "line1\nline2\nline3\n");
	TEST_ASSERT_NOT_NULL(path);
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_UINT(3, editor_line_count(&e));
	assert_line(&e, 0, "line1");
	assert_line(&e, 1, "line2");
	assert_line(&e, 2, "line3");
	editor_free(&e);
}

/** @brief A last line without a trailing newline is loaded whole. */
static void test_editor_load_no_trailing_newline(void) {
	const char *path = tmpdir_write("testfile.txt", "ab\ncd");
	TEST_ASSERT_NOT_NULL(path);
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	assert_line(&e, 0, "ab");
	assert_line(&e, 1, "cd");
	editor_free(&e);
}

/** @brief An empty file loads with no lines. */
static void test_editor_load_empty_file(void) {
	const char *path = tmpdir_write("empty.txt", "");
	TEST_ASSERT_NOT_NULL(path);
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
	editor_free(&e);
}

/** @brief A blank line in the middle is kept as an empty line. */
static void test_editor_load_blank_line_in_the_middle(void) {
	const char *path = tmpdir_write("blank.txt", "a\n\nb\n");
	TEST_ASSERT_NOT_NULL(path);
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_UINT(3, editor_line_count(&e));
	assert_line(&e, 0, "a");
	assert_line(&e, 1, "");
	assert_line(&e, 2, "b");
	editor_free(&e);
}

/** @brief Loading again replaces the previous contents. */
static void test_editor_load_twice_replaces_contents(void) {
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, tmpdir_write("one.txt", "a\nb\n")));
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, tmpdir_write("two.txt", "c\n")));
	TEST_ASSERT_EQUAL_UINT(1, editor_line_count(&e));
	assert_line(&e, 0, "c");
	editor_free(&e);
}

/** @brief A missing file succeeds with an empty editor and is not created. */
static void test_editor_load_missing_file_is_empty_and_not_created(void) {
	const char *path = tmpdir_path("nope.txt");
	TEST_ASSERT_NOT_NULL(path);
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
	struct stat st;
	TEST_ASSERT_EQUAL_INT(-1, stat(path, &st));
	TEST_ASSERT_EQUAL_INT(ENOENT, errno);
	editor_free(&e);
}

/** @brief Loading a missing file into a non-empty editor empties it. */
static void test_editor_load_missing_file_discards_old_contents(void) {
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "old"));
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, tmpdir_path("nope.txt")));
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
	editor_free(&e);
}

/** @brief A directory as the path fails with EISDIR and leaves the editor empty. */
static void test_editor_load_directory_fails_with_eisdir(void) {
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "old"));
	TEST_ASSERT_EQUAL_INT(-1, editor_load_file(&e, tmpdir_path(".")));
	TEST_ASSERT_EQUAL_INT(EISDIR, errno);
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
	editor_free(&e);
}

/** @brief An unreadable file fails with EACCES and leaves the editor empty. */
static void test_editor_load_unreadable_file_fails_with_eacces(void) {
	const char *path = tmpdir_write("secret.txt", "x\n");
	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL_INT(0, chmod(path, 0));
	if (geteuid() == 0) TEST_IGNORE_MESSAGE("root can read any file");
	editor_t e;
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "old"));
	TEST_ASSERT_EQUAL_INT(-1, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_INT(EACCES, errno);
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
	editor_free(&e);
}

/** @brief Register every test in this file with Unity. */
void test_editor_suite(void) {
	RUN_TEST(test_editor_should_exit_on_ctrl_q);
	RUN_TEST(test_editor_should_not_exit_on_other_keys);
	RUN_TEST(test_editor_init_is_empty);
	RUN_TEST(test_editor_append_one_line);
	RUN_TEST(test_editor_append_keeps_order);
	RUN_TEST(test_editor_append_copies_text);
	RUN_TEST(test_editor_append_empty_string_is_a_line);
	RUN_TEST(test_editor_append_many_lines_grows);
	RUN_TEST(test_editor_append_long_line_is_not_truncated);
	RUN_TEST(test_editor_line_out_of_range_is_null);
	RUN_TEST(test_editor_free_resets_and_allows_reuse);
	RUN_TEST(test_editor_free_is_safe_when_empty_or_repeated);
	RUN_TEST(test_editor_render_no_lines);
	RUN_TEST(test_editor_render_one_line);
	RUN_TEST(test_editor_render_joins_lines_with_crlf);
	RUN_TEST(test_editor_render_truncates_on_small_buffer);
	RUN_TEST(test_editor_render_truncates_across_lines);
	RUN_TEST(test_editor_render_zero_size_returns_zero);
	RUN_TEST(test_editor_load_three_lines);
	RUN_TEST(test_editor_load_no_trailing_newline);
	RUN_TEST(test_editor_load_empty_file);
	RUN_TEST(test_editor_load_blank_line_in_the_middle);
	RUN_TEST(test_editor_load_twice_replaces_contents);
	RUN_TEST(test_editor_load_missing_file_is_empty_and_not_created);
	RUN_TEST(test_editor_load_missing_file_discards_old_contents);
	RUN_TEST(test_editor_load_directory_fails_with_eisdir);
	RUN_TEST(test_editor_load_unreadable_file_fails_with_eacces);
}
