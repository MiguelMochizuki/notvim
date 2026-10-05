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

/** A row limit larger than any test editor, for tests that are not about the limit. */
#define ALL_ROWS 1000

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

/** Editor shared by the tests of this file; freed in test_editor_teardown(), even after a failed assertion. */
static editor_t e;

/** @brief Free the shared editor; called from tearDown(). Safe on an untouched editor. */
void test_editor_teardown(void) {
	editor_free(&e);
}

/** @brief Assert that line @p i of @p e equals @p expected. */
static void assert_line(const editor_t *e, size_t i, const char *expected) {
	TEST_ASSERT_NOT_NULL(editor_line(e, i));
	TEST_ASSERT_EQUAL_STRING(expected, editor_line(e, i));
}

/** @brief A fresh editor has no lines. */
static void test_editor_init_is_empty(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
	TEST_ASSERT_NULL(editor_line(&e, 0));
}

/** @brief Appending one line gives a count of 1 and the same text. */
static void test_editor_append_one_line(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "hello"));
	TEST_ASSERT_EQUAL_UINT(1, editor_line_count(&e));
	assert_line(&e, 0, "hello");
}

/** @brief Several appends keep their order. */
static void test_editor_append_keeps_order(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "one"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "two"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "three"));
	TEST_ASSERT_EQUAL_UINT(3, editor_line_count(&e));
	assert_line(&e, 0, "one");
	assert_line(&e, 1, "two");
	assert_line(&e, 2, "three");
}

/** @brief Append copies the text: changing the source later does not affect the editor. */
static void test_editor_append_copies_text(void) {
	editor_init(&e);
	char src[] = "abc";
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, src));
	src[0] = 'X';
	assert_line(&e, 0, "abc");
}

/** @brief Appending "" adds a blank line. */
static void test_editor_append_empty_string_is_a_line(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "a"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, ""));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "b"));
	TEST_ASSERT_EQUAL_UINT(3, editor_line_count(&e));
	assert_line(&e, 1, "");
}

/** @brief 100 appends (past any initial capacity) all stay intact. */
static void test_editor_append_many_lines_grows(void) {
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
}

/** @brief A line far longer than the render buffer is stored whole. */
static void test_editor_append_long_line_is_not_truncated(void) {
	editor_init(&e);
	char text[5001]; /* on the stack: a failed assert must not leak it */
	memset(text, 'a', 5000);
	text[5000] = '\0';
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, text));
	TEST_ASSERT_EQUAL_UINT(5000, strlen(editor_line(&e, 0)));
	assert_line(&e, 0, text);
}

/** @brief editor_line() returns NULL for an index past the last line. */
static void test_editor_line_out_of_range_is_null(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "a"));
	TEST_ASSERT_NULL(editor_line(&e, 1));
	TEST_ASSERT_NULL(editor_line(&e, 1000));
}

/** @brief After editor_free() the editor is empty and can be used again. */
static void test_editor_free_resets_and_allows_reuse(void) {
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
	editor_init(&e);
	editor_free(&e);
	editor_free(&e);
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
}

/** @brief Rendering an editor with no lines yields an empty string. */
static void test_editor_render_no_lines(void) {
	editor_init(&e);
	char out[256] = "garbage";
	TEST_ASSERT_EQUAL_UINT(0, editor_render(&e, ALL_ROWS, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("", out);
}

/** @brief Rendering one line copies it and returns its length. */
static void test_editor_render_one_line(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "abc"));
	char out[256];
	TEST_ASSERT_EQUAL_UINT(3, editor_render(&e, ALL_ROWS, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("abc", out);
}

/** @brief Lines are joined with "\r\n" and there is no trailing separator. */
static void test_editor_render_joins_lines_with_crlf(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "a"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, ""));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "b"));
	char out[256];
	TEST_ASSERT_EQUAL_UINT(6, editor_render(&e, ALL_ROWS, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("a\r\n\r\nb", out);
}

/** @brief Only the first max_rows lines are rendered, with no trailing separator. */
static void test_editor_render_limits_rows(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "a"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "b"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "c"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "d"));
	char out[256];
	TEST_ASSERT_EQUAL_UINT(4, editor_render(&e, 2, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("a\r\nb", out);
}

/** @brief max_rows equal to the line count renders every line. */
static void test_editor_render_rows_equal_to_line_count(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "a"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "b"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "c"));
	char out[256];
	TEST_ASSERT_EQUAL_UINT(7, editor_render(&e, 3, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("a\r\nb\r\nc", out);
}

/** @brief max_rows of 0 renders an empty string. */
static void test_editor_render_zero_rows(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "a"));
	char out[256] = "garbage";
	TEST_ASSERT_EQUAL_UINT(0, editor_render(&e, 0, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("", out);
}

/** @brief A blank line inside the limit takes a row, leaving a trailing separator. */
static void test_editor_render_blank_line_counts_as_a_row(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "a"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, ""));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "b"));
	char out[256];
	TEST_ASSERT_EQUAL_UINT(3, editor_render(&e, 2, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("a\r\n", out);
}

/** @brief Rendering truncates to out_size - 1 and stays NUL-terminated. */
static void test_editor_render_truncates_on_small_buffer(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "abcdef"));
	char out[4]; /* 3 chars + '\0' */
	TEST_ASSERT_EQUAL_UINT(3, editor_render(&e, ALL_ROWS, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("abc", out);
}

/** @brief Truncation can cut in the middle of the "\r\n" separator. */
static void test_editor_render_truncates_across_lines(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "ab"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "cd"));
	char out[5]; /* "ab\r\n" would need 4 chars + '\0'; "cd" is cut off */
	TEST_ASSERT_EQUAL_UINT(4, editor_render(&e, ALL_ROWS, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("ab\r\n", out);
}

/** @brief Rendering with out_size 0 returns 0 and leaves out untouched. */
static void test_editor_render_zero_size_returns_zero(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "abc"));
	char out[1] = { 'x' };
	TEST_ASSERT_EQUAL_UINT(0, editor_render(&e, ALL_ROWS, out, 0));
	TEST_ASSERT_EQUAL_INT('x', out[0]); /* out must not be touched */
}

/** @brief File with three lines is correctly loaded into editor */
static void test_editor_load_three_lines(void) {
	const char *path = tmpdir_write("testfile.txt", "line1\nline2\nline3\n");
	TEST_ASSERT_NOT_NULL(path);
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_UINT(3, editor_line_count(&e));
	assert_line(&e, 0, "line1");
	assert_line(&e, 1, "line2");
	assert_line(&e, 2, "line3");
}

/** @brief A last line without a trailing newline is loaded whole. */
static void test_editor_load_no_trailing_newline(void) {
	const char *path = tmpdir_write("testfile.txt", "ab\ncd");
	TEST_ASSERT_NOT_NULL(path);
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	assert_line(&e, 0, "ab");
	assert_line(&e, 1, "cd");
}

/** @brief An empty file loads with no lines. */
static void test_editor_load_empty_file(void) {
	const char *path = tmpdir_write("empty.txt", "");
	TEST_ASSERT_NOT_NULL(path);
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
}

/** @brief A blank line in the middle is kept as an empty line. */
static void test_editor_load_blank_line_in_the_middle(void) {
	const char *path = tmpdir_write("blank.txt", "a\n\nb\n");
	TEST_ASSERT_NOT_NULL(path);
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_UINT(3, editor_line_count(&e));
	assert_line(&e, 0, "a");
	assert_line(&e, 1, "");
	assert_line(&e, 2, "b");
}

/** @brief Loading again replaces the previous contents. */
static void test_editor_load_twice_replaces_contents(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, tmpdir_write("one.txt", "a\nb\n")));
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, tmpdir_write("two.txt", "c\n")));
	TEST_ASSERT_EQUAL_UINT(1, editor_line_count(&e));
	assert_line(&e, 0, "c");
}

/** @brief A missing file succeeds with an empty editor and is not created. */
static void test_editor_load_missing_file_is_empty_and_not_created(void) {
	const char *path = tmpdir_path("nope.txt");
	TEST_ASSERT_NOT_NULL(path);
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
	struct stat st;
	TEST_ASSERT_EQUAL_INT(-1, stat(path, &st));
	TEST_ASSERT_EQUAL_INT(ENOENT, errno);
}

/** @brief Loading a missing file into a non-empty editor empties it. */
static void test_editor_load_missing_file_discards_old_contents(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "old"));
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, tmpdir_path("nope.txt")));
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
}

/** @brief A directory as the path fails with EISDIR and leaves the editor empty. */
static void test_editor_load_directory_fails_with_eisdir(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "old"));
	TEST_ASSERT_EQUAL_INT(-1, editor_load_file(&e, tmpdir_path(".")));
	TEST_ASSERT_EQUAL_INT(EISDIR, errno);
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
}

/** @brief An unreadable file fails with EACCES and leaves the editor empty. */
static void test_editor_load_unreadable_file_fails_with_eacces(void) {
	const char *path = tmpdir_write("secret.txt", "x\n");
	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL_INT(0, chmod(path, 0));
	if (geteuid() == 0) TEST_IGNORE_MESSAGE("root can read any file");
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "old"));
	TEST_ASSERT_EQUAL_INT(-1, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_INT(EACCES, errno);
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
}

/** @brief Append @p text to the shared editor, failing the test if that does not work. */
static void append(const char *text) {
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, text));
}

/** @brief Assert that the cursor of the shared editor is at row @p y, column @p x. */
static void assert_cursor(size_t y, size_t x) {
	TEST_ASSERT_EQUAL_UINT(y, e.cy);
	TEST_ASSERT_EQUAL_UINT(x, e.cx);
}

/** @brief A fresh editor has the cursor at 0,0. */
static void test_editor_cursor_starts_at_origin(void) {
	editor_init(&e);
	assert_cursor(0, 0);
	append("abc");
	assert_cursor(0, 0);
}

/** @brief Moving in an editor with no lines keeps the cursor at 0,0. */
static void test_editor_cursor_does_not_move_without_lines(void) {
	editor_init(&e);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	editor_move_cursor(&e, EDITOR_MOVE_UP);
	editor_move_cursor(&e, EDITOR_MOVE_LEFT);
	assert_cursor(0, 0);
}

/** @brief Right moves along the line and stops on the last character. */
static void test_editor_cursor_right_stops_on_last_character(void) {
	editor_init(&e);
	append("abc");
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_cursor(0, 1);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_cursor(0, 2);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_cursor(0, 2);
}

/** @brief Left moves back and stops at column 0, without wrapping to the previous line. */
static void test_editor_cursor_left_stops_at_column_zero(void) {
	editor_init(&e);
	append("abc");
	append("def");
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	editor_move_cursor(&e, EDITOR_MOVE_LEFT);
	assert_cursor(1, 0);
	editor_move_cursor(&e, EDITOR_MOVE_LEFT);
	assert_cursor(1, 0);
}

/** @brief Up and down move between lines and stop at the first and last one. */
static void test_editor_cursor_vertical_moves_stop_at_the_ends(void) {
	editor_init(&e);
	append("a");
	append("b");
	editor_move_cursor(&e, EDITOR_MOVE_UP);
	assert_cursor(0, 0);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 0);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 0);
	editor_move_cursor(&e, EDITOR_MOVE_UP);
	assert_cursor(0, 0);
}

/** @brief Moving down onto a shorter line clamps the column to its last character. */
static void test_editor_cursor_down_clamps_column(void) {
	editor_init(&e);
	append("abcdef");
	append("ab");
	for (int i = 0; i < 5; i++) editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_cursor(0, 5);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 1);
}

/** @brief Moving up onto a shorter line clamps the column too. */
static void test_editor_cursor_up_clamps_column(void) {
	editor_init(&e);
	append("ab");
	append("abcdef");
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	for (int i = 0; i < 5; i++) editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_cursor(1, 5);
	editor_move_cursor(&e, EDITOR_MOVE_UP);
	assert_cursor(0, 1);
}

/** @brief An empty line has column 0, and the old column is not remembered afterwards. */
static void test_editor_cursor_on_empty_line_and_no_remembered_column(void) {
	editor_init(&e);
	append("abcdef");
	append("");
	append("abcdef");
	for (int i = 0; i < 4; i++) editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 0);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_cursor(1, 0);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(2, 0);
}

/** @brief Loading a file puts the cursor back at 0,0. */
static void test_editor_load_resets_the_cursor(void) {
	editor_init(&e);
	append("abc");
	append("def");
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_cursor(1, 1);
	const char *path = tmpdir_write("cursor.txt", "x\ny\n");
	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	assert_cursor(0, 0);
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
	RUN_TEST(test_editor_render_limits_rows);
	RUN_TEST(test_editor_render_rows_equal_to_line_count);
	RUN_TEST(test_editor_render_zero_rows);
	RUN_TEST(test_editor_render_blank_line_counts_as_a_row);
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
	RUN_TEST(test_editor_cursor_starts_at_origin);
	RUN_TEST(test_editor_cursor_does_not_move_without_lines);
	RUN_TEST(test_editor_cursor_right_stops_on_last_character);
	RUN_TEST(test_editor_cursor_left_stops_at_column_zero);
	RUN_TEST(test_editor_cursor_vertical_moves_stop_at_the_ends);
	RUN_TEST(test_editor_cursor_down_clamps_column);
	RUN_TEST(test_editor_cursor_up_clamps_column);
	RUN_TEST(test_editor_cursor_on_empty_line_and_no_remembered_column);
	RUN_TEST(test_editor_load_resets_the_cursor);
}
