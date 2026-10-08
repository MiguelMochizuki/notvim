/**
 * @file test_editor.c
 * @brief Unit tests for editor.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "unity.h"
#include "test_editor.h"
#include "editor.h"
#include "keys.h"
#include "tmpdir.h"
#include "drawfmt.h"
#include "allocfail.h"

/** A row limit larger than any test editor, for tests that are not about the limit. */
#define ALL_ROWS 1000
/** A width larger than any test line, for tests that are not about clipping. */
#define ALL_COLS ((size_t)-1)

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
	TEST_ASSERT_EQUAL_UINT(0, editor_render(&e, ALL_ROWS, ALL_COLS, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("", out);
}

/** @brief Rendering one line copies it and returns its length. */
static void test_editor_render_one_line(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "abc"));
	char out[256];
	TEST_ASSERT_EQUAL_UINT(3, editor_render(&e, ALL_ROWS, ALL_COLS, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("abc", out);
}

/** @brief Lines are joined with "\r\n" and there is no trailing separator. */
static void test_editor_render_joins_lines_with_crlf(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "a"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, ""));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "b"));
	char out[256];
	TEST_ASSERT_EQUAL_UINT(6, editor_render(&e, ALL_ROWS, ALL_COLS, out, sizeof(out)));
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
	TEST_ASSERT_EQUAL_UINT(4, editor_render(&e, 2, ALL_COLS, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("a\r\nb", out);
}

/** @brief max_rows equal to the line count renders every line. */
static void test_editor_render_rows_equal_to_line_count(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "a"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "b"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "c"));
	char out[256];
	TEST_ASSERT_EQUAL_UINT(7, editor_render(&e, 3, ALL_COLS, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("a\r\nb\r\nc", out);
}

/** @brief max_rows of 0 renders an empty string. */
static void test_editor_render_zero_rows(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "a"));
	char out[256] = "garbage";
	TEST_ASSERT_EQUAL_UINT(0, editor_render(&e, 0, ALL_COLS, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("", out);
}

/** @brief A blank line inside the limit takes a row, leaving a trailing separator. */
static void test_editor_render_blank_line_counts_as_a_row(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "a"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, ""));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "b"));
	char out[256];
	TEST_ASSERT_EQUAL_UINT(3, editor_render(&e, 2, ALL_COLS, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("a\r\n", out);
}

/** @brief Rendering truncates to out_size - 1 and stays NUL-terminated. */
static void test_editor_render_truncates_on_small_buffer(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "abcdef"));
	char out[4]; /* 3 chars + '\0' */
	TEST_ASSERT_EQUAL_UINT(3, editor_render(&e, ALL_ROWS, ALL_COLS, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("abc", out);
}

/** @brief Truncation can cut in the middle of the "\r\n" separator. */
static void test_editor_render_truncates_across_lines(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "ab"));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "cd"));
	char out[5]; /* "ab\r\n" would need 4 chars + '\0'; "cd" is cut off */
	TEST_ASSERT_EQUAL_UINT(4, editor_render(&e, ALL_ROWS, ALL_COLS, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("ab\r\n", out);
}

/** @brief Rendering with out_size 0 returns 0 and leaves out untouched. */
static void test_editor_render_zero_size_returns_zero(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "abc"));
	char out[1] = { 'x' };
	TEST_ASSERT_EQUAL_UINT(0, editor_render(&e, ALL_ROWS, ALL_COLS, out, 0));
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

/** @brief When the copy of the text cannot be allocated, append fails with ENOMEM and the editor keeps its lines. */
static void test_editor_append_line_fails_cleanly_when_the_copy_cannot_be_allocated(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "old"));
	allocfail_after(0);
	TEST_ASSERT_EQUAL_INT(-1, editor_append_line(&e, "new"));
	allocfail_reset();
	TEST_ASSERT_EQUAL_INT(ENOMEM, errno);
	TEST_ASSERT_EQUAL_UINT(1, editor_line_count(&e));
	assert_line(&e, 0, "old");
}

/** @brief When the array of lines cannot grow, append fails, frees the copy it made (the sanitizer reports a leak otherwise) and adds nothing. */
static void test_editor_append_line_frees_the_copy_when_the_array_cannot_grow(void) {
	editor_init(&e);
	allocfail_after(1); /* the copy succeeds, the first growth of the array fails */
	TEST_ASSERT_EQUAL_INT(-1, editor_append_line(&e, "new"));
	allocfail_reset();
	TEST_ASSERT_EQUAL_INT(ENOMEM, errno);
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "again")); /* and the editor is still usable */
	assert_line(&e, 0, "again");
}

/** @brief When the copy of the path cannot be allocated, load fails with ENOMEM and leaves the editor empty, as for any failed load. */
static void test_editor_load_fails_with_enomem_when_the_path_cannot_be_copied(void) {
	const char *path = tmpdir_write("a.txt", "x\n");
	TEST_ASSERT_NOT_NULL(path);
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "old"));
	allocfail_after(0);
	TEST_ASSERT_EQUAL_INT(-1, editor_load_file(&e, path));
	allocfail_reset();
	TEST_ASSERT_EQUAL_INT(ENOMEM, errno);
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
	TEST_ASSERT_NULL(e.path);
}

/** @brief When a line cannot be stored halfway through a file, load frees the lines already read and the path, and fails with ENOMEM. */
static void test_editor_load_frees_everything_when_a_line_cannot_be_stored(void) {
	const char *path = tmpdir_write("a.txt", "one\ntwo\nthree\n");
	TEST_ASSERT_NOT_NULL(path);
	editor_init(&e);
	allocfail_after(3); /* path copy, first line, array of lines succeed; the second line fails */
	TEST_ASSERT_EQUAL_INT(-1, editor_load_file(&e, path));
	allocfail_reset();
	TEST_ASSERT_EQUAL_INT(ENOMEM, errno);
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
	TEST_ASSERT_NULL(e.path);
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
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

/** @brief An empty line has column 0; a move to the right there does not move, so the old column is kept (as in Vim). */
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
	assert_cursor(2, 4);
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

/** @brief Assert that editor_draw_text() of the shared editor draws the rows @p text with the cursor at @p row, @p col (1-based), in a roomy buffer. */
static void assert_draw(size_t max_rows, const char *text, int row, int col) {
	char out[1024], expected[1024];
	draw_expected(expected, sizeof(expected), text, max_rows, ALL_COLS, row, col);
	TEST_ASSERT_EQUAL_UINT(strlen(expected), editor_draw_text(&e, max_rows, ALL_COLS, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/** @brief Draw the shared editor into a roomy buffer with @p max_rows rows and @p max_cols columns and compare with the literal @p expected. */
static void assert_draw_literal(size_t max_rows, size_t max_cols, const char *expected) {
	char out[512];
	TEST_ASSERT_EQUAL_UINT(strlen(expected), editor_draw_text(&e, max_rows, max_cols, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/** @brief The exact bytes of a draw, spelled out (the other draw tests build theirs with draw_expected()): short screen. */
static void test_editor_draw_exact_bytes(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	/* two rows on a 24-row screen: erase to the end of each row, then move to the first free row and erase the rest */
	assert_draw_literal(24, ALL_COLS, "\x1b[?25l\x1b[H" "ab\x1b[K\r\n" "cd\x1b[K" "\x1b[3;1H\x1b[J" "\x1b[2;2H" "\x1b[?25h");
}

/** @brief An empty editor draws no rows: hide, home, move to row 1 and erase the screen, cursor 1;1, show. */
static void test_editor_draw_empty_editor_exact_bytes(void) {
	editor_init(&e);
	assert_draw_literal(24, ALL_COLS, "\x1b[?25l\x1b[H\x1b[1;1H\x1b[J\x1b[1;1H\x1b[?25h");
}

/** @brief A row of exactly max_cols columns gets no ESC[K: after its last character the cursor is still on it (pending wrap) and the erase would remove it. */
static void test_editor_draw_row_of_exactly_max_cols_has_no_erase(void) {
	editor_init(&e);
	append("abc");
	assert_draw_literal(24, 3, "\x1b[?25l\x1b[H" "abc" "\x1b[2;1H\x1b[J" "\x1b[1;1H" "\x1b[?25h");
}

/** @brief A row of max_cols - 1 columns does get its ESC[K. */
static void test_editor_draw_row_one_column_short_has_the_erase(void) {
	editor_init(&e);
	append("ab");
	assert_draw_literal(24, 3, "\x1b[?25l\x1b[H" "ab\x1b[K" "\x1b[2;1H\x1b[J" "\x1b[1;1H" "\x1b[?25h");
}

/** @brief A clipped row is exactly max_cols columns wide, so it has no ESC[K either. */
static void test_editor_draw_clipped_row_has_no_erase(void) {
	editor_init(&e);
	append("abcdef");
	assert_draw_literal(24, 3, "\x1b[?25l\x1b[H" "abc" "\x1b[2;1H\x1b[J" "\x1b[1;1H" "\x1b[?25h");
}

/** @brief A mark that does not fit leaves the row one column short: it still gets its ESC[K. */
static void test_editor_draw_row_short_by_a_mark_has_the_erase(void) {
	editor_init(&e);
	append("ab\x01"); /* ^A needs two columns, one is left */
	assert_draw_literal(24, 3, "\x1b[?25l\x1b[H" "ab\x1b[K" "\x1b[2;1H\x1b[J" "\x1b[1;1H" "\x1b[?25h");
}

/** @brief A screen that is full (n == max_rows) has no move to a free row and no ESC[J. */
static void test_editor_draw_full_screen_has_no_erase_of_the_rest(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	append("ef");
	assert_draw_literal(2, ALL_COLS, "\x1b[?25l\x1b[H" "ab\x1b[K\r\n" "cd\x1b[K" "\x1b[1;1H" "\x1b[?25h");
}

/** @brief A blank row is just its ESC[K. */
static void test_editor_draw_blank_row_exact_bytes(void) {
	editor_init(&e);
	append("");
	assert_draw_literal(24, ALL_COLS, "\x1b[?25l\x1b[H" "\x1b[K" "\x1b[2;1H\x1b[J" "\x1b[1;1H" "\x1b[?25h");
}

/** @brief A blank line is a row of its own: nothing but the erase-to-end-of-line. */
static void test_editor_draw_blank_lines_are_rows(void) {
	editor_init(&e);
	append("a");
	append("");
	append("b");
	assert_draw(24, "a\r\n\r\nb", 1, 1);
	editor_free(&e);
	editor_init(&e);
	append("");
	assert_draw(24, "", 1, 1);
}

/** @brief With max_rows 0 no row is drawn and nothing is erased below: only home, the cursor and show. */
static void test_editor_draw_zero_rows_draws_no_rows(void) {
	editor_init(&e);
	append("ab");
	assert_draw(0, NULL, 1, 1);
}

/** @brief The screen is never cleared: no ESC[2J whatever the text holds, and the text of a file is shown, not sent. */
static void test_editor_draw_never_clears_the_screen(void) {
	editor_init(&e);
	append("a\x1b[2Jb");
	append("\xc2\x9b[2Jc");
	char out[256];
	editor_draw_text(&e, 24, ALL_COLS, out, sizeof(out));
	TEST_ASSERT_NULL(strstr(out, "\x1b[2J"));
	TEST_ASSERT_NULL(strstr(out, "\xc2\x9b"));
	TEST_ASSERT_NOT_NULL(strstr(out, "a^[[2Jb\x1b[K\r\n?[2Jc\x1b[K\x1b[3;1H\x1b[J"));
}

/** @brief Drawing an empty editor draws no row and puts the cursor at 1;1. */
static void test_editor_draw_empty_editor(void) {
	editor_init(&e);
	assert_draw(24, NULL, 1, 1);
}

/** @brief Drawing puts the rows after the home and the cursor at its row and column, 1-based. */
static void test_editor_draw_lines_and_cursor(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_draw(24, "ab\r\ncd", 2, 2);
}

/** @brief The row limit applies to the drawn text. */
static void test_editor_draw_limits_rows(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	append("ef");
	assert_draw(1, "ab", 1, 1);
}

/** @brief A cursor below the visible rows is still reported at its real row. */
static void test_editor_draw_cursor_below_visible_rows(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	append("ef");
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_draw(1, "ab", 3, 1);
}

/** @brief A buffer smaller than the overhead gets nothing, not half an escape sequence. */
static void test_editor_draw_buffer_too_small_writes_nothing(void) {
	editor_init(&e);
	append("ab");
	char out[EDITOR_DRAW_OVERHEAD] = "garbage";
	TEST_ASSERT_EQUAL_UINT(0, editor_draw_text(&e, 24, ALL_COLS, out, EDITOR_DRAW_OVERHEAD - 1));
	TEST_ASSERT_EQUAL_STRING("", out);
	char untouched[1] = { 'x' };
	TEST_ASSERT_EQUAL_UINT(0, editor_draw_text(&e, 24, ALL_COLS, untouched, 0));
	TEST_ASSERT_EQUAL_INT('x', untouched[0]);
}

/** @brief With little spare room a row that does not fit is left out whole: no half row, no half sequence, and the tail is complete. */
static void test_editor_draw_drops_a_row_that_does_not_fit_but_keeps_the_tail(void) {
	editor_init(&e);
	append("abcdef"); /* the row needs 6 + 3 bytes */
	char out[EDITOR_DRAW_OVERHEAD + 8], expected[256];
	size_t n = editor_draw_text(&e, 24, ALL_COLS, out, sizeof(out));
	draw_expected(expected, sizeof(expected), NULL, 24, ALL_COLS, 1, 1);
	TEST_ASSERT_EQUAL_UINT(strlen(expected), n);
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/**
 * @brief The rows that fit are exactly those whose bytes (text, 3 for ESC[K, 2 for each CR LF) are at most out_size - EDITOR_DRAW_OVERHEAD:
 *        at every buffer size the output is exactly the first k whole rows with the full tail, for the k that rule gives.
 */
static void test_editor_draw_keeps_exactly_the_rows_that_fit_at_every_buffer_size(void) {
	const char *rows[] = { "abc", "de", "", "fghi" };
	editor_init(&e);
	for (size_t i = 0; i < 4; i++) append(rows[i]);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT); /* row 2, column 2 */
	char wanted[5][256];
	size_t bytes[5] = { 0 }; /* bytes the first k rows take */
	for (size_t k = 0; k <= 4; k++) {
		char text[64] = "";
		for (size_t i = 0; i < k; i++) {
			if (i > 0) strcat(text, "\r\n");
			strcat(text, rows[i]);
		}
		draw_expected(wanted[k], sizeof(wanted[k]), k ? text : NULL, 24, ALL_COLS, 2, 2);
		if (k > 0) bytes[k] = bytes[k - 1] + strlen(rows[k - 1]) + 3 + (k > 1 ? 2 : 0);
	}
	for (size_t size = EDITOR_DRAW_OVERHEAD; size <= EDITOR_DRAW_OVERHEAD + bytes[4] + 8; size++) {
		size_t k = 0;
		while (k < 4 && bytes[k + 1] <= size - EDITOR_DRAW_OVERHEAD) k++;
		char out[512], msg[96];
		memset(out, 'x', sizeof(out));
		size_t n = editor_draw_text(&e, 24, ALL_COLS, out, size);
		snprintf(msg, sizeof(msg), "out_size %zu: expected exactly the first %zu rows", size, k);
		TEST_ASSERT_TRUE_MESSAGE(n < size, "wrote past the buffer");
		TEST_ASSERT_EQUAL_STRING_MESSAGE(wanted[k], out, msg);
		TEST_ASSERT_EQUAL_UINT(strlen(out), n);
	}
}

/** @brief The worst case tail (a cursor row of 20 digits, the first free row, the erase) is complete in a buffer of exactly EDITOR_DRAW_OVERHEAD. */
static void test_editor_draw_worst_case_cursor_fits_in_the_overhead(void) {
	editor_init(&e);
	append("a");
	e.cy = (size_t)-1 - 2; /* far below the text: the row is SIZE_MAX - 1 */
	char out[EDITOR_DRAW_OVERHEAD];
	memset(out, 'x', sizeof(out));
	size_t n = editor_draw_text(&e, (size_t)-1, ALL_COLS, out, sizeof(out));
	char expected[256];
	snprintf(expected, sizeof(expected), "\x1b[?25l\x1b[H" "\x1b[1;1H\x1b[J" "\x1b[%zu;1H" "\x1b[?25h", (size_t)-1 - 1);
	TEST_ASSERT_EQUAL_UINT(strlen(expected), n);
	TEST_ASSERT_EQUAL_STRING(expected, out);
	/* and the move to the first free row, with max_rows at its largest too, when a row is drawn */
	char big[EDITOR_DRAW_OVERHEAD + 8];
	e.cy = 0;
	n = editor_draw_text(&e, (size_t)-1, ALL_COLS, big, sizeof(big));
	TEST_ASSERT_TRUE(n < sizeof(big));
	TEST_ASSERT_EQUAL_STRING("\x1b[?25h", big + n - 6);
}

/** @brief EDITOR_DRAW_OVERHEAD covers the longest tail: hide, home, a move to row 20 digits, erase, a cursor position of two 20-digit numbers, show, and the NUL. */
static void test_editor_draw_overhead_covers_the_worst_case_tail(void) {
	size_t worst = strlen("\x1b[?25l") + strlen("\x1b[H") + strlen("\x1b[18446744073709551615;1H") + strlen("\x1b[J")
	             + strlen("\x1b[18446744073709551615;18446744073709551615H") + strlen("\x1b[?25h") + 1;
	TEST_ASSERT_TRUE_MESSAGE(EDITOR_DRAW_OVERHEAD >= worst, "EDITOR_DRAW_OVERHEAD is smaller than the longest possible tail");
}

/** @brief Append the lines "a", "b", ... (@p n of them) to the shared editor. */
static void append_letters(int n) {
	for (int i = 0; i < n; i++) {
		char text[2] = { (char)('a' + i), '\0' };
		append(text);
	}
}

/** @brief A cursor inside the window does not scroll. */
static void test_editor_scroll_keeps_offset_while_cursor_is_visible(void) {
	editor_init(&e);
	append_letters(5);
	e.cy = 2;
	editor_scroll(&e, 3);
	TEST_ASSERT_EQUAL_UINT(0, e.rowoff);
}

/** @brief A cursor just below the window scrolls down by one line. */
static void test_editor_scroll_down_by_one(void) {
	editor_init(&e);
	append_letters(5);
	e.cy = 3;
	editor_scroll(&e, 3);
	TEST_ASSERT_EQUAL_UINT(1, e.rowoff);
}

/** @brief A cursor far below the window puts it on the last window row. */
static void test_editor_scroll_down_far(void) {
	editor_init(&e);
	append_letters(10);
	e.cy = 9;
	editor_scroll(&e, 3);
	TEST_ASSERT_EQUAL_UINT(7, e.rowoff);
}

/** @brief A cursor above the window scrolls up so that it is on the first row. */
static void test_editor_scroll_up(void) {
	editor_init(&e);
	append_letters(10);
	e.rowoff = 5;
	e.cy = 2;
	editor_scroll(&e, 3);
	TEST_ASSERT_EQUAL_UINT(2, e.rowoff);
}

/** @brief A cursor on the last window row, and on the first one, does not scroll. */
static void test_editor_scroll_window_edges_are_visible(void) {
	editor_init(&e);
	append_letters(10);
	e.rowoff = 4;
	e.cy = 6;
	editor_scroll(&e, 3);
	TEST_ASSERT_EQUAL_UINT(4, e.rowoff);
	e.cy = 4;
	editor_scroll(&e, 3);
	TEST_ASSERT_EQUAL_UINT(4, e.rowoff);
}

/** @brief A window of 0 rows, and an editor without lines, never scroll. */
static void test_editor_scroll_does_nothing_without_rows_or_lines(void) {
	editor_init(&e);
	editor_scroll(&e, 3);
	TEST_ASSERT_EQUAL_UINT(0, e.rowoff);
	append_letters(5);
	e.cy = 4;
	editor_scroll(&e, 0);
	TEST_ASSERT_EQUAL_UINT(0, e.rowoff);
}

/** @brief Loading a file puts the scroll offset back at 0. */
static void test_editor_load_resets_the_scroll_offset(void) {
	editor_init(&e);
	append_letters(10);
	e.cy = 9;
	editor_scroll(&e, 3);
	TEST_ASSERT_EQUAL_UINT(7, e.rowoff);
	const char *path = tmpdir_write("scroll.txt", "x\n");
	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_UINT(0, e.rowoff);
}

/** @brief Rendering starts at the first visible line. */
static void test_editor_render_starts_at_the_scroll_offset(void) {
	editor_init(&e);
	append_letters(5);
	e.rowoff = 2;
	char out[256];
	TEST_ASSERT_EQUAL_UINT(4, editor_render(&e, 2, ALL_COLS, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("c\r\nd", out);
}

/** @brief A scroll offset past the last line renders nothing. */
static void test_editor_render_offset_past_the_end_is_empty(void) {
	editor_init(&e);
	append_letters(3);
	e.rowoff = 10;
	char out[256] = "garbage";
	TEST_ASSERT_EQUAL_UINT(0, editor_render(&e, 5, ALL_COLS, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("", out);
}

/** @brief The drawn cursor row is relative to the first visible line. */
static void test_editor_draw_cursor_row_is_relative_to_the_offset(void) {
	editor_init(&e);
	append_letters(5);
	e.rowoff = 2;
	e.cy = 3;
	assert_draw(2, "c\r\nd", 2, 1);
}

/** @brief A cursor above the window is drawn on row 1 instead of wrapping around. */
static void test_editor_draw_cursor_above_the_window_is_row_one(void) {
	editor_init(&e);
	append_letters(5);
	e.rowoff = 3;
	e.cy = 1;
	assert_draw(2, "d\r\ne", 1, 1);
}

/** @brief Lines longer than max_cols are clipped on the right; shorter and exact ones are kept. */
static void test_editor_render_clips_lines_to_max_cols(void) {
	editor_init(&e);
	append("abcdef");
	append("gh");
	append("ijk");
	char out[256];
	TEST_ASSERT_EQUAL_UINT(12, editor_render(&e, ALL_ROWS, 3, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("abc\r\ngh\r\nijk", out);
}

/** @brief With 0 columns every line is empty but the lines are still there. */
static void test_editor_render_zero_cols_keeps_the_rows(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	char out[256];
	TEST_ASSERT_EQUAL_UINT(2, editor_render(&e, ALL_ROWS, 0, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING("\r\n", out);
}

/** @brief Clipping applies to the lines drawn, and the cursor is shown on the last column at most. */
static void test_editor_draw_clips_lines_and_clamps_the_cursor_column(void) {
	editor_init(&e);
	append("abcdef");
	e.cx = 5;
	char out[512], expected[512];
	draw_expected(expected, sizeof(expected), "abc", 24, 3, 1, 3);
	TEST_ASSERT_EQUAL_UINT(strlen(expected), editor_draw_text(&e, 24, 3, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/** @brief Assert that loading @p len bytes of @p data is refused as a binary file and leaves the editor empty. */
static void assert_load_refused(const char *data, size_t len) {
	editor_init(&e);
	append("old");
	const char *path = tmpdir_write_bytes("binary.bin", data, len);
	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL_INT(-1, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_INT(EILSEQ, errno);
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
}

/** @brief A NUL byte in the middle of a line makes the load fail with EILSEQ instead of cutting the line. */
static void test_editor_load_refuses_a_nul_byte_in_a_line(void) {
	assert_load_refused("a\0b\n", 4);
}

/** @brief A NUL in a later line empties the editor, including the lines already loaded. */
static void test_editor_load_refuses_a_nul_after_good_lines(void) {
	assert_load_refused("one\ntwo\nth\0ree\n", 14);
}

/** @brief A NUL in a last line without a trailing newline is found too. */
static void test_editor_load_refuses_a_nul_in_the_last_unterminated_line(void) {
	assert_load_refused("a\nb\0", 5);
}

/** @brief A file that is only a NUL byte is refused. */
static void test_editor_load_refuses_a_file_that_is_only_nul(void) {
	assert_load_refused("\0", 1);
}

/** @brief A NUL at the very start of a line (an empty-looking line) is found. */
static void test_editor_load_refuses_a_nul_at_the_start_of_a_line(void) {
	assert_load_refused("a\n\0b\n", 5);
}

/** @brief Write @p content to a file and load it into the shared (already initialised) editor; the load must succeed. */
static void load_text(const char *content) {
	const char *path = tmpdir_write("crlf.txt", content);
	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
}

/** @brief A CRLF file is stored without the CRs, and crlf is set. */
static void test_editor_load_crlf_file_drops_the_cr_and_sets_crlf(void) {
	editor_init(&e);
	load_text("one\r\ntwo\r\nthree\r\n");
	TEST_ASSERT_EQUAL_UINT(3, editor_line_count(&e));
	assert_line(&e, 0, "one");
	assert_line(&e, 1, "two");
	assert_line(&e, 2, "three");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
}

/** @brief An LF file keeps its bytes and crlf is 0. */
static void test_editor_load_lf_file_is_not_crlf(void) {
	editor_init(&e);
	load_text("one\ntwo\n");
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	assert_line(&e, 0, "one");
	assert_line(&e, 1, "two");
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
}

/** @brief An empty file has crlf 0, and so does a missing one. */
static void test_editor_load_empty_and_missing_files_are_not_crlf(void) {
	editor_init(&e);
	load_text("");
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, tmpdir_path("nope.txt")));
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
}

/** @brief A file that is only CR LF is one empty line. */
static void test_editor_load_only_crlf_is_one_empty_line(void) {
	editor_init(&e);
	load_text("\r\n");
	TEST_ASSERT_EQUAL_UINT(1, editor_line_count(&e));
	assert_line(&e, 0, "");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
}

/** @brief Blank lines of a CRLF file are empty lines. */
static void test_editor_load_crlf_blank_lines(void) {
	editor_init(&e);
	load_text("a\r\n\r\nb\r\n");
	TEST_ASSERT_EQUAL_UINT(3, editor_line_count(&e));
	assert_line(&e, 0, "a");
	assert_line(&e, 1, "");
	assert_line(&e, 2, "b");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
}

/** @brief Only the CR right before the LF goes: a CR inside the line stays, and "CR CR LF" leaves one CR. */
static void test_editor_load_crlf_removes_exactly_one_cr_per_line(void) {
	editor_init(&e);
	load_text("a\rb\r\n\r\r\n");
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	assert_line(&e, 0, "a\rb");
	assert_line(&e, 1, "\r");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
}

/** @brief A mixed file (CRLF first, then LF) keeps every CR and crlf is 0: "every" line, not "any" or "the first". */
static void test_editor_load_mixed_crlf_then_lf_keeps_the_cr(void) {
	editor_init(&e);
	load_text("a\r\nb\n");
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	assert_line(&e, 0, "a\r");
	assert_line(&e, 1, "b");
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
}

/** @brief A mixed file (LF first, then CRLF) keeps every CR and crlf is 0: not "the last line". */
static void test_editor_load_mixed_lf_then_crlf_keeps_the_cr(void) {
	editor_init(&e);
	load_text("a\nb\r\n");
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	assert_line(&e, 0, "a");
	assert_line(&e, 1, "b\r");
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
}

/** @brief One LF line in the middle of CRLF lines makes the file mixed. */
static void test_editor_load_one_lf_line_among_crlf_lines_is_mixed(void) {
	editor_init(&e);
	load_text("a\r\nb\nc\r\n");
	TEST_ASSERT_EQUAL_UINT(3, editor_line_count(&e));
	assert_line(&e, 0, "a\r");
	assert_line(&e, 1, "b");
	assert_line(&e, 2, "c\r");
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
}

/** Room for the big files below: 10000 lines of 6 bytes, a short last line and a long line. */
#define BIG_BYTES 100000

/** @brief Fill the static buffer @p buf with @p n lines "line" ended by CR LF, and return its length. */
static size_t fill_crlf_lines(char *buf, int n) {
	size_t len = 0;
	for (int i = 0; i < n; i++) {
		memcpy(buf + len, "line\r\n", 6);
		len += 6;
	}
	buf[len] = '\0';
	return len;
}

/** @brief Load @p len bytes of @p data (no NUL) into the initialised shared editor; the load must succeed. */
static void load_bytes(const char *data, size_t len) {
	const char *path = tmpdir_write_bytes("big.txt", data, len);
	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
}

/** @brief A deciding LF line 60 KB into the file, far past any read buffer, makes the file mixed: every CR stays. */
static void test_editor_load_a_late_lf_line_makes_a_big_crlf_file_mixed(void) {
	static char content[BIG_BYTES];
	size_t len = fill_crlf_lines(content, 10000);
	memcpy(content + len, "last\n", 5);
	editor_init(&e);
	load_bytes(content, len + 5);
	TEST_ASSERT_EQUAL_UINT(10001, editor_line_count(&e));
	assert_line(&e, 0, "line\r");
	assert_line(&e, 5000, "line\r");
	assert_line(&e, 9999, "line\r");
	assert_line(&e, 10000, "last");
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
}

/** @brief A big CRLF file (60 KB) loses every CR, from the first line to the last. */
static void test_editor_load_a_big_crlf_file_loses_every_cr(void) {
	static char content[BIG_BYTES];
	size_t len = fill_crlf_lines(content, 10000);
	editor_init(&e);
	load_bytes(content, len);
	TEST_ASSERT_EQUAL_UINT(10000, editor_line_count(&e));
	assert_line(&e, 0, "line");
	assert_line(&e, 5000, "line");
	assert_line(&e, 9999, "line");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
}

/** @brief Load a CRLF file whose first line is @p n times 'x', then "b": the first line is exactly the x's, without CR. */
static void assert_long_first_line_crlf(size_t n) {
	static char content[BIG_BYTES], expected[BIG_BYTES];
	memset(content, 'x', n);
	memcpy(content + n, "\r\nb\r\n", 5);
	memset(expected, 'x', n);
	expected[n] = '\0';
	editor_init(&e);
	load_bytes(content, n + 5);
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	assert_line(&e, 0, expected);
	assert_line(&e, 1, "b");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
}

/** @brief A line of 10000 bytes (far longer than a read block) ends in CR LF: found and removed. */
static void test_editor_load_crlf_after_a_very_long_line(void) {
	assert_long_first_line_crlf(10000);
}

/** @brief The CR is byte 4094 and the LF byte 4095 of the file: both in the first 4096-byte block. */
static void test_editor_load_crlf_with_cr_and_lf_ending_the_first_block(void) {
	assert_long_first_line_crlf(4094);
}

/** @brief The CR is the last byte of the first 4096-byte block and the LF the first of the next. */
static void test_editor_load_crlf_with_cr_and_lf_split_across_blocks(void) {
	assert_long_first_line_crlf(4095);
}

/** @brief The same split with a bigger block size (8192). */
static void test_editor_load_crlf_with_cr_and_lf_split_across_8k_blocks(void) {
	assert_long_first_line_crlf(8191);
}

/** @brief A long line with CR LF and a later LF-only line is mixed: the long line keeps its CR. */
static void test_editor_load_mixed_after_a_very_long_line_keeps_the_cr(void) {
	static char content[BIG_BYTES], expected[BIG_BYTES];
	memset(content, 'x', 10000);
	memcpy(content + 10000, "\r\nb\n", 4);
	memset(expected, 'x', 10000);
	expected[10000] = '\r';
	expected[10001] = '\0';
	editor_init(&e);
	load_bytes(content, 10004);
	assert_line(&e, 0, expected);
	assert_line(&e, 1, "b");
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
}

/** @brief A blank LF line among CRLF lines makes the file mixed: it counts against CRLF. */
static void test_editor_load_a_blank_lf_line_among_crlf_lines_is_mixed(void) {
	editor_init(&e);
	load_text("a\r\n\nb\r\n");
	TEST_ASSERT_EQUAL_UINT(3, editor_line_count(&e));
	assert_line(&e, 0, "a\r");
	assert_line(&e, 1, "");
	assert_line(&e, 2, "b\r");
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
}

/** @brief A file that starts with an empty LF line is mixed too (and the check must not read before the line). */
static void test_editor_load_a_file_starting_with_a_blank_lf_line_is_mixed(void) {
	editor_init(&e);
	load_text("\nab\r\n");
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	assert_line(&e, 0, "");
	assert_line(&e, 1, "ab\r");
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
}

/** @brief A file that is only "\n" is one empty line and not CRLF. */
static void test_editor_load_only_lf_is_one_empty_line_and_not_crlf(void) {
	editor_init(&e);
	load_text("\n");
	TEST_ASSERT_EQUAL_UINT(1, editor_line_count(&e));
	assert_line(&e, 0, "");
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
}

/** @brief The CR is gone from the stored line: right twice on "ab" stops on the b, not on a hidden CR. */
static void test_editor_load_crlf_cursor_stops_on_the_last_character(void) {
	editor_init(&e);
	load_text("ab\r\ncd\r\n");
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_cursor(0, 1);
}

/** @brief An unterminated last line without CR does not stop the file from being CRLF. */
static void test_editor_load_crlf_with_an_unterminated_last_line(void) {
	editor_init(&e);
	load_text("a\r\nb");
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	assert_line(&e, 0, "a");
	assert_line(&e, 1, "b");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
}

/** @brief An unterminated last line that ends in CR keeps that CR: only terminated lines lose theirs. */
static void test_editor_load_crlf_keeps_a_cr_on_the_unterminated_last_line(void) {
	editor_init(&e);
	load_text("a\r\nb\r");
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	assert_line(&e, 0, "a");
	assert_line(&e, 1, "b\r");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
}

/** @brief With no terminated line there is nothing to detect: the file is not CRLF and keeps its CR. */
static void test_editor_load_unterminated_line_alone_is_not_crlf(void) {
	editor_init(&e);
	load_text("abc\r");
	TEST_ASSERT_EQUAL_UINT(1, editor_line_count(&e));
	assert_line(&e, 0, "abc\r");
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
}

/** @brief Characters other than ASCII are untouched by the CR removal. */
static void test_editor_load_crlf_keeps_utf8_text(void) {
	editor_init(&e);
	load_text("caf\xc3\xa9\r\n\xe2\x82\xac\r\n\tx\r\n");
	TEST_ASSERT_EQUAL_UINT(3, editor_line_count(&e));
	assert_line(&e, 0, "caf\xc3\xa9");
	assert_line(&e, 1, "\xe2\x82\xac");
	assert_line(&e, 2, "\tx");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
}

/** @brief Loading an LF file after a CRLF file clears the flag, and the reverse sets it: the flag follows the file. */
static void test_editor_load_crlf_flag_follows_the_file(void) {
	editor_init(&e);
	load_text("a\r\n");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
	load_text("b\n");
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
	assert_line(&e, 0, "b");
	load_text("c\r\n");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
	assert_line(&e, 0, "c");
}

/** @brief Loading a mixed or empty file or a missing one after a CRLF file clears the flag. */
static void test_editor_load_crlf_flag_is_cleared_by_other_loads(void) {
	editor_init(&e);
	load_text("a\r\n");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
	load_text("a\r\nb\n");
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
	load_text("a\r\n");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
	load_text("");
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
	load_text("a\r\n");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, tmpdir_path("nope.txt")));
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
}

/** @brief A failed load (a directory) after a CRLF file clears the flag. */
static void test_editor_load_crlf_flag_is_cleared_by_a_failed_load(void) {
	editor_init(&e);
	load_text("a\r\n");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
	TEST_ASSERT_EQUAL_INT(-1, editor_load_file(&e, tmpdir_path(".")));
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
}

/** @brief A NUL still refuses a file that is otherwise CRLF, with the old contents gone and crlf 0. */
static void test_editor_load_refuses_a_nul_in_a_crlf_file(void) {
	editor_init(&e);
	load_text("old\r\n");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
	const char data[] = "a\r\nb\0c\r\nd\r\n";
	const char *path = tmpdir_write_bytes("binary.bin", data, sizeof(data) - 1);
	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL_INT(-1, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_INT(EILSEQ, errno);
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
}

/** @brief A NUL in the unterminated last line of a CRLF file is still refused. */
static void test_editor_load_refuses_a_nul_in_the_last_line_of_a_crlf_file(void) {
	const char data[] = "a\r\nb\0";
	editor_init(&e);
	const char *path = tmpdir_write_bytes("binary.bin", data, sizeof(data) - 1);
	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL_INT(-1, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_INT(EILSEQ, errno);
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
}

/** @brief editor_init and editor_free reset crlf, even on a garbage struct and after a CRLF load. */
static void test_editor_init_and_free_reset_crlf(void) {
	memset(&e, 0xff, sizeof(e));
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
	load_text("a\r\n");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
	editor_free(&e);
	TEST_ASSERT_EQUAL_INT(0, e.crlf);
}

/** @brief Render the shared editor with @p max_cols columns and compare with @p expected. */
static void assert_render_cols(size_t max_cols, const char *expected) {
	char out[256];
	TEST_ASSERT_EQUAL_UINT(strlen(expected), editor_render(&e, ALL_ROWS, max_cols, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/** @brief An escape character is drawn as the mark ^[ so that it cannot reach the terminal. */
static void test_editor_render_shows_escape_as_a_mark(void) {
	editor_init(&e);
	append("a\x1b[2Jb");
	assert_render_cols(ALL_COLS, "a^[[2Jb");
}

/** @brief Other control bytes become ^ plus a letter, and 0x7f becomes ^?. */
static void test_editor_render_shows_other_control_bytes_as_marks(void) {
	editor_init(&e);
	append("\x01x\x1f\x7f\r");
	assert_render_cols(ALL_COLS, "^Ax^_^?^M");
}

/** @brief Bytes of 0x80 and above, such as UTF-8 text, are not control bytes here. */
static void test_editor_render_leaves_high_bytes_alone(void) {
	editor_init(&e);
	append("caf\xc3\xa9");
	assert_render_cols(ALL_COLS, "caf\xc3\xa9");
}

/** @brief Clipping counts the two columns of a mark and never shows half of one. */
static void test_editor_render_clips_on_whole_marks(void) {
	editor_init(&e);
	append("ab\x01" "cd");
	assert_render_cols(4, "ab^A");
	assert_render_cols(3, "ab");   /* ^A needs two columns and only one is left */
	assert_render_cols(2, "ab");
	assert_render_cols(5, "ab^Ac");
}

/** @brief The drawn cursor column counts the width of the marks before it. */
static void test_editor_draw_cursor_column_counts_marks(void) {
	editor_init(&e);
	append("\x01" "bc");
	e.cx = 1; /* on the b, after the two-column mark ^A */
	assert_draw(24, "^Abc", 1, 3);
	e.cx = 0; /* on the mark itself: its first column */
	assert_draw(24, "^Abc", 1, 1);
	e.cx = 2;
	assert_draw(24, "^Abc", 1, 4);
}

/** @brief A tab is drawn as spaces up to the next multiple of 8 columns. */
static void test_editor_render_expands_tabs_to_the_next_tab_stop(void) {
	editor_init(&e);
	append("\tx");
	append("ab\tc");
	append("abcdefg\tx");
	append("abcdefgh\tx");
	char out[256];
	editor_render(&e, ALL_ROWS, ALL_COLS, out, sizeof(out));
	TEST_ASSERT_EQUAL_STRING("        x\r\nab      c\r\nabcdefg x\r\nabcdefgh        x", out);
}

/** @brief A tab after a mark counts the two columns of the mark. */
static void test_editor_render_tab_after_a_mark(void) {
	editor_init(&e);
	append("\x01\tx");
	assert_render_cols(ALL_COLS, "^A      x");
}

/** @brief A tab that does not fit is cut to the columns that are left, and nothing follows it. */
static void test_editor_render_clips_a_tab(void) {
	editor_init(&e);
	append("\tx");
	assert_render_cols(5, "     ");
	assert_render_cols(8, "        ");
	assert_render_cols(9, "        x");
}

/** @brief The cursor is on the first column of a tab, and after it at the next tab stop. */
static void test_editor_draw_cursor_column_with_tabs(void) {
	editor_init(&e);
	append("\tx");
	e.cx = 0;
	assert_draw(24, "        x", 1, 1);
	e.cx = 1;
	assert_draw(24, "        x", 1, 9);
}

/** @brief After a tab that starts past column 0 the cursor is at the next tab stop, not 8 columns further. */
static void test_editor_draw_cursor_column_after_a_tab_in_the_middle(void) {
	editor_init(&e);
	append("ab\tc");
	e.cx = 3; /* the c: "ab" is 2 columns, the tab goes to column 8 */
	assert_draw(24, "ab      c", 1, 9);
	e.cx = 2; /* the tab itself starts at column 2 */
	assert_draw(24, "ab      c", 1, 3);
}

/** @brief Press @p dir @p n times on the shared editor. */
static void move_n(editor_move_t dir, int n) {
	for (int i = 0; i < n; i++) editor_move_cursor(&e, dir);
}

/** @brief Assert that the cursor column of editor_draw_text() with @p max_cols columns is @p col (1-based) on row 1. */
static void assert_draw_cursor_col(size_t max_cols, size_t col) {
	char out[512], expected[32];
	editor_draw_text(&e, 24, max_cols, out, sizeof(out));
	snprintf(expected, sizeof(expected), "\x1b[1;%zuH", col);
	const size_t show = strlen("\x1b[?25h"); /* the cursor position is followed by the show-cursor sequence */
	TEST_ASSERT_TRUE(strlen(out) >= strlen(expected) + show);
	char *tail = out + strlen(out) - show - strlen(expected);
	tail[strlen(expected)] = '\0';
	TEST_ASSERT_EQUAL_STRING(expected, tail);
}

/** @brief A lead byte followed by a control byte does not swallow it: the ESC is still drawn as a mark. */
static void test_editor_render_does_not_swallow_a_control_byte_after_a_lead_byte(void) {
	editor_init(&e);
	append("\xe2\x1b[2J");
	assert_render_cols(ALL_COLS, "?^[[2J");
	editor_free(&e);
	editor_init(&e);
	append("\xc3\tx");
	assert_render_cols(ALL_COLS, "?       x"); /* the tab goes from column 1 to 8: seven spaces */
}

/** @brief Clipping to N columns keeps N whole characters of 1, 2, 3 and 4 bytes and never cuts one. */
static void test_editor_render_clips_utf8_by_characters(void) {
	editor_init(&e);
	append("a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80z"); /* a, e-acute, euro, emoji, z: 5 columns, 11 bytes */
	assert_render_cols(0, "");
	assert_render_cols(1, "a");
	assert_render_cols(2, "a\xc3\xa9");
	assert_render_cols(3, "a\xc3\xa9\xe2\x82\xac");
	assert_render_cols(4, "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80");
	assert_render_cols(5, "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80z");
	assert_render_cols(80, "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80z");
}

/** @brief A line of 201 bytes, 'a' and 100 e-acutes, clipped to 80 columns is 80 characters of valid UTF-8. */
static void test_editor_render_clips_a_long_utf8_line_to_80_columns(void) {
	char line[256] = "a", expected[256] = "a";
	for (int i = 0; i < 100; i++) strcat(line, "\xc3\xa9");
	for (int i = 0; i < 79; i++) strcat(expected, "\xc3\xa9");
	TEST_ASSERT_EQUAL_UINT(201, strlen(line));
	editor_init(&e);
	append(line);
	assert_render_cols(80, expected);
}

/** @brief An invalid byte is drawn as one '?', whatever it is. */
static void test_editor_render_shows_invalid_bytes_as_question_marks(void) {
	editor_init(&e);
	append("a\xff" "b\x80" "c\xfe" "d");
	assert_render_cols(ALL_COLS, "a?b?c?d");
}

/** @brief Overlong forms, surrogates and code points above U+10FFFF give one '?' per byte. */
static void test_editor_render_shows_each_byte_of_an_invalid_sequence_as_a_question_mark(void) {
	editor_init(&e);
	append("\xc0\x80" "|\xed\xa0\x80" "|\xf4\x90\x80\x80" "|\xe0\x80\x80");
	assert_render_cols(ALL_COLS, "??|???|????|???");
}

/** @brief A truncated sequence is '?' for its lead byte, and the byte after it is drawn normally. */
static void test_editor_render_shows_a_truncated_sequence_as_question_marks(void) {
	editor_init(&e);
	append("\xe2\x82" "z");
	assert_render_cols(ALL_COLS, "??z");
	editor_free(&e);
	editor_init(&e);
	append("x\xf0\x9f\x98"); /* cut by the end of the line */
	assert_render_cols(ALL_COLS, "x???");
}

/** @brief C1 controls U+0080 to U+009F are drawn as one '?' each, so that U+009B (CSI) cannot reach the terminal. */
static void test_editor_render_shows_c1_controls_as_question_marks(void) {
	editor_init(&e);
	append("a\xc2\x80" "b\xc2\x9b" "[2J\xc2\x9f" "c");
	assert_render_cols(ALL_COLS, "a?b?[2J?c");
}

/** @brief The neighbours of the C1 range are valid characters and are copied: U+00A0 and U+00C0. */
static void test_editor_render_copies_characters_next_to_the_c1_range(void) {
	editor_init(&e);
	append("\xc2\xa0\xc3\x80");
	assert_render_cols(ALL_COLS, "\xc2\xa0\xc3\x80");
}

/** @brief '?' for an invalid byte or a C1 control takes one column when clipping. */
static void test_editor_render_clips_question_marks_by_column(void) {
	editor_init(&e);
	append("\xff\xc2\x85\xff" "x");
	assert_render_cols(2, "??");
	assert_render_cols(3, "???");
	assert_render_cols(4, "???x");
}

/** @brief UTF-8 text mixes with marks and tabs: the widths add up and clipping stays on whole characters. */
static void test_editor_render_mixes_utf8_with_marks_and_tabs(void) {
	editor_init(&e);
	append("\xc3\xa9\x01\t" "x"); /* e-acute (1), ^A (2), tab to column 8 (5 spaces) */
	assert_render_cols(2, "\xc3\xa9"); /* ^A needs two columns and one is left */
	assert_render_cols(3, "\xc3\xa9^A");
	assert_render_cols(5, "\xc3\xa9^A  ");
	assert_render_cols(9, "\xc3\xa9^A     x");
}

/** @brief An invalid byte and a C1 control each advance the column by one, so the tab after them stops at column 8. */
static void test_editor_render_tab_after_question_mark_cells(void) {
	editor_init(&e);
	append("\xff\xc2\x85\tx"); /* two '?' cells (2 columns), then 6 spaces */
	assert_render_cols(ALL_COLS, "??      x");
	assert_render_cols(4, "??  ");
	assert_render_cols(8, "??      ");
}

/** @brief The drawn cursor column counts characters, not bytes: after e-acute, euro sign and emoji. */
static void test_editor_draw_cursor_column_counts_characters(void) {
	editor_init(&e);
	append("\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80z");
	const size_t starts[] = { 0, 2, 5, 9 }; /* byte index of each character */
	for (size_t i = 0; i < 4; i++) {
		e.cx = starts[i];
		assert_draw_cursor_col(ALL_COLS, i + 1);
	}
}

/** @brief Each invalid byte and a whole C1 control is one column before the cursor. */
static void test_editor_draw_cursor_column_counts_invalid_bytes_and_c1(void) {
	editor_init(&e);
	append("\xff\xc2\x85" "b");
	e.cx = 1;
	assert_draw_cursor_col(ALL_COLS, 2); /* on the C1 control, after the invalid byte */
	e.cx = 3;
	assert_draw_cursor_col(ALL_COLS, 3); /* on the b */
}

/** @brief Multi-byte characters, marks and tabs before the cursor all add their columns. */
static void test_editor_draw_cursor_column_mixes_widths(void) {
	editor_init(&e);
	append("\xc3\xa9\x01\t" "x");
	e.cx = 3; /* on the tab: after e-acute (1) and ^A (2) */
	assert_draw_cursor_col(ALL_COLS, 4);
	e.cx = 4; /* on the x: the tab went to column 8 */
	assert_draw_cursor_col(ALL_COLS, 9);
}

/** @brief The column after '?' cells and a tab: the cursor on the tab and on the x that follows it. */
static void test_editor_draw_cursor_column_after_question_mark_cells_and_a_tab(void) {
	editor_init(&e);
	append("\xff\xc2\x85\tx");
	e.cx = 3; /* on the tab, after two cells */
	assert_draw_cursor_col(ALL_COLS, 3);
	e.cx = 4; /* on the x: the tab went from column 2 to 8 */
	assert_draw_cursor_col(ALL_COLS, 9);
}

/** @brief A cursor column past the right edge is drawn on the last column, counted in characters. */
static void test_editor_draw_cursor_column_is_clamped_to_the_width_in_characters(void) {
	editor_init(&e);
	append("\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9");
	e.cx = 8; /* the fifth character */
	assert_draw_cursor_col(3, 3);
	assert_draw_cursor_col(5, 5);
	assert_draw_cursor_col(6, 5);
}

/**
 * @brief editor_draw_text() keeps a whole screen of multi-byte text when the buffer has 4 bytes per column.
 * @note It is the clipping that fails today, not the buffer: the end-to-end tests with 3 and 4-byte
 *       characters in test_notvim.c pin the size of the buffer that main() allocates.
 */
static void test_editor_draw_full_screen_of_multibyte_text_is_complete(void) {
	enum { ROWS = 5, COLS = 20 };
	char line[COLS * 2 + 1] = "", text[ROWS * (COLS * 2 + 2) + 1] = "", expected[ROWS * (COLS * 2 + 5) + 64];
	for (int i = 0; i < COLS; i++) strcat(line, "\xc3\xa9");
	editor_init(&e);
	for (int i = 0; i < ROWS; i++) {
		append(line);
		if (i > 0) strcat(text, "\r\n");
		strcat(text, line);
	}
	draw_expected(expected, sizeof(expected), text, ROWS, COLS, 1, 1);
	char out[ROWS * (COLS * 4 + 3 + 2) + EDITOR_DRAW_OVERHEAD]; /* the documented room: 4 bytes per column, ESC[K and CR LF per row */
	size_t n = editor_draw_text(&e, ROWS, COLS, out, sizeof(out));
	TEST_ASSERT_EQUAL_UINT(strlen(expected), n);
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/** @brief Right moves one character at a time over 1, 2, 3 and 4-byte characters and stops on the last one. */
static void test_editor_cursor_right_moves_by_characters(void) {
	editor_init(&e);
	append("a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80"); /* characters start at 0, 1, 3, 6 */
	const size_t starts[] = { 1, 3, 6, 6, 6 };
	for (size_t i = 0; i < 5; i++) {
		editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
		assert_cursor(0, starts[i]);
	}
}

/** @brief Left moves back one character at a time and stops at column 0. */
static void test_editor_cursor_left_moves_by_characters(void) {
	editor_init(&e);
	append("a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80");
	e.cx = 6;
	const size_t starts[] = { 3, 1, 0, 0 };
	for (size_t i = 0; i < 4; i++) {
		editor_move_cursor(&e, EDITOR_MOVE_LEFT);
		assert_cursor(0, starts[i]);
	}
}

/** @brief Right and left on an empty line, and left at the start of a multi-byte line, stay put. */
static void test_editor_cursor_stays_put_on_an_empty_line_and_at_the_start(void) {
	editor_init(&e);
	append("");
	append("\xc3\xa9\xc3\xa9");
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	editor_move_cursor(&e, EDITOR_MOVE_LEFT);
	assert_cursor(0, 0);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_move_cursor(&e, EDITOR_MOVE_LEFT);
	assert_cursor(1, 0);
}

/** @brief Two presses of right on three e-acutes put the cursor on the third, never inside the second. */
static void test_editor_cursor_right_never_lands_inside_a_character(void) {
	editor_init(&e);
	append("\xc3\xa9\xc3\xa9\xc3\xa9");
	move_n(EDITOR_MOVE_RIGHT, 2);
	assert_cursor(0, 4);
}

/** @brief Each invalid byte is a stop for right and left. */
static void test_editor_cursor_visits_each_invalid_byte(void) {
	editor_init(&e);
	append("a\xff\xfe" "b\xe2\x82" "z");
	const size_t right[] = { 1, 2, 3, 4, 5, 6, 6 };
	for (size_t i = 0; i < 7; i++) {
		editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
		assert_cursor(0, right[i]);
	}
	const size_t left[] = { 5, 4, 3, 2, 1, 0 };
	for (size_t i = 0; i < 6; i++) {
		editor_move_cursor(&e, EDITOR_MOVE_LEFT);
		assert_cursor(0, left[i]);
	}
}

/** @brief A C1 control is one stop of two bytes. */
static void test_editor_cursor_moves_over_a_c1_control_as_one_character(void) {
	editor_init(&e);
	append("a\xc2\x85" "b");
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_cursor(0, 1);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_cursor(0, 3);
	editor_move_cursor(&e, EDITOR_MOVE_LEFT);
	assert_cursor(0, 1);
}

/** @brief Down to a shorter line puts the cursor on the start of its last character, not on its last byte. */
static void test_editor_cursor_down_clamps_to_the_start_of_the_last_character(void) {
	editor_init(&e);
	append("abcdef");
	append("\xc3\xa9\xe2\x82\xac"); /* last character starts at 2 and ends at 4 */
	move_n(EDITOR_MOVE_RIGHT, 5);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 2);
	editor_free(&e);
	editor_init(&e);
	append("abcdef");
	append("a\xf0\x9f\x98\x80"); /* 4-byte last character at 1 */
	move_n(EDITOR_MOVE_RIGHT, 5);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 1);
}

/** @brief Up clamps the same way. */
static void test_editor_cursor_up_clamps_to_the_start_of_the_last_character(void) {
	editor_init(&e);
	append("a\xf0\x9f\x98\x80"); /* 4-byte last character at 1 */
	append("abcdef");
	e.cy = 1;
	e.cx = 5;
	e.wantcol = 5; /* set by hand: the wanted column follows moves, not assignments */
	editor_move_cursor(&e, EDITOR_MOVE_UP);
	assert_cursor(0, 1);
}

/** @brief A last invalid byte or C1 control is a character start: the cursor lands on it, not after it. */
static void test_editor_cursor_vertical_clamp_uses_invalid_bytes_and_c1_as_characters(void) {
	editor_init(&e);
	append("abcdef");
	append("ab\xff");
	append("a\xc2\x85");
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	move_n(EDITOR_MOVE_RIGHT, 4);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 2);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN); /* byte 2 is inside the C1 control, which starts at 1 */
	assert_cursor(2, 1);
}

/**
 * @brief A vertical move lands on a character start, chosen by display column (H6.10): it never lands inside a character.
 * @note Before H6.10 these moves snapped the byte index back (cx 1 -> 0, cx 3 -> 2); now the wanted display column decides.
 */
static void test_editor_cursor_vertical_move_never_lands_inside_a_character(void) {
	editor_init(&e);
	append("abcdef");
	append("\xc3\xa9\xe2\x82\xac" "z"); /* characters start at bytes 0, 2, 5 and at display columns 0, 1, 2 */
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT); /* wanted column 1 */
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 2); /* the euro sign: column 1, byte 2 */
	editor_free(&e);
	editor_init(&e);
	append("abcdef");
	append("\xc3\xa9\xe2\x82\xac" "z");
	move_n(EDITOR_MOVE_RIGHT, 3); /* wanted column 3: past the last character */
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 5); /* the z, the last character */
}

/** @brief The cursor on the last 4-byte character, exactly at the width, is drawn on that last column. */
static void test_editor_draw_cursor_on_the_last_four_byte_character_at_the_width(void) {
	editor_init(&e);
	append("\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80");
	e.cx = 8;
	assert_draw_cursor_col(3, 3);
	assert_draw_cursor_col(4, 3);
	e.cx = 4;
	assert_draw_cursor_col(3, 2);
}

/** @brief A CRLF file is drawn without ^M, a mixed file with it (render of the stored lines). */
static void test_editor_render_crlf_file_has_no_marks_and_mixed_file_has(void) {
	editor_init(&e);
	load_text("a\r\nb\r\n");
	assert_render_cols(ALL_COLS, "a\r\nb");
	load_text("a\r\nb\n");
	assert_render_cols(ALL_COLS, "a^M\r\nb");
}

/** @brief Start the shared editor over with the two lines @p first and @p second. */
static void two_lines(const char *first, const char *second) {
	editor_free(&e);
	editor_init(&e);
	append(first);
	append(second);
}

/** @brief Right moves by @p n, then down once: the cursor of the shared editor ends on the second line. */
static void right_then_down(int n) {
	move_n(EDITOR_MOVE_RIGHT, n);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
}

/** @brief Long, short, long: the cursor returns to its column, going down and going up. */
static void test_editor_wanted_column_comes_back_after_a_short_line(void) {
	two_lines("abcdefgh", "ab");
	append("abcdefgh");
	move_n(EDITOR_MOVE_RIGHT, 6);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 1);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(2, 6);
	editor_move_cursor(&e, EDITOR_MOVE_UP);
	assert_cursor(1, 1);
	editor_move_cursor(&e, EDITOR_MOVE_UP);
	assert_cursor(0, 6);
}

/** @brief An empty line in the middle is passed through without losing the column. */
static void test_editor_wanted_column_survives_an_empty_line(void) {
	two_lines("abcdef", "");
	append("abcdef");
	move_n(EDITOR_MOVE_RIGHT, 4);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 0);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(2, 4);
	editor_move_cursor(&e, EDITOR_MOVE_UP);
	editor_move_cursor(&e, EDITOR_MOVE_UP);
	assert_cursor(0, 4);
}

/** @brief The wanted column is far beyond every shorter line: it keeps landing on the last character, and the long line gets it back. */
static void test_editor_wanted_column_far_beyond_short_lines_lands_on_the_last_character(void) {
	two_lines("abcdefghijklmnopqrst", "abc");
	append("abcdef");
	append("abcdefghijklmnopqrst");
	move_n(EDITOR_MOVE_RIGHT, 19);
	assert_cursor(0, 19);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 2);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(2, 5);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(3, 19);
	move_n(EDITOR_MOVE_UP, 3);
	assert_cursor(0, 19);
}

/** @brief A wanted column equal to the line length is past the last character; one less is on it. */
static void test_editor_wanted_column_at_the_edge_of_a_short_line(void) {
	two_lines("abcd", "ab");
	right_then_down(2); /* wanted 2: the line has columns 0 and 1 */
	assert_cursor(1, 1);
	two_lines("abcd", "ab");
	right_then_down(1); /* wanted 1: the b */
	assert_cursor(1, 1);
	two_lines("abcd", "abc");
	right_then_down(3); /* wanted 3 on a line of 3 columns: the last character */
	assert_cursor(1, 2);
}

/** @brief A left move that really moves sets the wanted column to the display column of the new cx (Vim: `5l j h j` gives 0). */
static void test_editor_left_sets_the_wanted_column(void) {
	two_lines("abcdef", "ab");
	append("abcdef");
	right_then_down(5);
	assert_cursor(1, 1);
	editor_move_cursor(&e, EDITOR_MOVE_LEFT); /* a left move that moves */
	assert_cursor(1, 0);
	TEST_ASSERT_EQUAL_UINT(0, e.wantcol);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(2, 0);
}

/** @brief A right move that does not move (last character) keeps the wanted column, as in Vim: `5l j l j` ends on column 5. */
static void test_editor_right_that_does_not_move_keeps_the_wanted_column(void) {
	two_lines("abcdef", "ab");
	append("abcdef");
	right_then_down(5);
	assert_cursor(1, 1);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT); /* does not move: it is the last character */
	assert_cursor(1, 1);
	TEST_ASSERT_EQUAL_UINT(5, e.wantcol);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(2, 5);
}

/** @brief Left at column 0 and right on an empty line do not move, and keep the wanted column (Vim: `5l j h j` on an empty middle line gives 5). */
static void test_editor_left_that_does_not_move_keeps_the_wanted_column(void) {
	two_lines("abcdef", "");
	append("abcdef");
	right_then_down(5);
	assert_cursor(1, 0);
	editor_move_cursor(&e, EDITOR_MOVE_LEFT);
	assert_cursor(1, 0);
	TEST_ASSERT_EQUAL_UINT(5, e.wantcol);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	TEST_ASSERT_EQUAL_UINT(5, e.wantcol);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(2, 5);
}

/** @brief After a vertical move, left and right start from the real cx and not from the wanted column. */
static void test_editor_left_and_right_start_from_the_real_cursor_after_a_vertical_move(void) {
	two_lines("abcdefgh", "abc");
	right_then_down(6); /* wanted 6, the cursor is on the c, cx 2 */
	assert_cursor(1, 2);
	editor_move_cursor(&e, EDITOR_MOVE_LEFT);
	assert_cursor(1, 1); /* not column 5 */
	editor_move_cursor(&e, EDITOR_MOVE_UP);
	assert_cursor(0, 1);
	two_lines("abcdefgh", "abc");
	right_then_down(6);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_cursor(1, 2); /* stays on the last character, not column 7 */
}

/** @brief Up on the first line does nothing, and the wanted column is kept. */
static void test_editor_up_on_the_first_line_keeps_the_wanted_column(void) {
	two_lines("ab", "abcdefgh");
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	move_n(EDITOR_MOVE_RIGHT, 6);
	editor_move_cursor(&e, EDITOR_MOVE_UP);
	assert_cursor(0, 1);
	editor_move_cursor(&e, EDITOR_MOVE_UP); /* first line: nothing happens */
	assert_cursor(0, 1);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 6);
}

/** @brief Down on the last line does nothing, and the wanted column is kept. */
static void test_editor_down_on_the_last_line_keeps_the_wanted_column(void) {
	two_lines("abcdefgh", "ab");
	right_then_down(6);
	assert_cursor(1, 1);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN); /* last line: nothing happens */
	assert_cursor(1, 1);
	editor_move_cursor(&e, EDITOR_MOVE_UP);
	assert_cursor(0, 6);
}

/** @brief Left and right set @c wantcol in display columns: a tab, a multi-byte character and the end of the line. */
static void test_editor_horizontal_moves_set_the_wanted_column_in_display_columns(void) {
	editor_init(&e);
	append("\xc3\xa9\t\xe2\x82\xac" "x"); /* e-acute at column 0 (byte 0), tab 1 to 7 (byte 2), euro 8 (byte 3), x 9 (byte 6) */
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_cursor(0, 2);
	TEST_ASSERT_EQUAL_UINT(1, e.wantcol);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_cursor(0, 3);
	TEST_ASSERT_EQUAL_UINT(8, e.wantcol);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	TEST_ASSERT_EQUAL_UINT(9, e.wantcol);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT); /* at the end: does not move, the wanted column stays 9 */
	TEST_ASSERT_EQUAL_UINT(9, e.wantcol);
	editor_move_cursor(&e, EDITOR_MOVE_LEFT);
	assert_cursor(0, 3);
	TEST_ASSERT_EQUAL_UINT(8, e.wantcol);
}

/** @brief Vertical moves do not change @c wantcol, even when the cursor is clamped, or when there is no line to go to. */
static void test_editor_vertical_moves_keep_the_wanted_column(void) {
	two_lines("abcdefgh", "ab");
	move_n(EDITOR_MOVE_RIGHT, 6);
	TEST_ASSERT_EQUAL_UINT(6, e.wantcol);
	editor_move_cursor(&e, EDITOR_MOVE_UP);
	TEST_ASSERT_EQUAL_UINT(6, e.wantcol);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	TEST_ASSERT_EQUAL_UINT(6, e.wantcol);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	TEST_ASSERT_EQUAL_UINT(6, e.wantcol);
}

/** @brief A wanted column inside a tab picks the tab (it starts at or before the column); the next character is picked from its own column. */
static void test_editor_wanted_column_inside_a_tab_picks_the_tab(void) {
	const int cols[] = { 0, 3, 7 }; /* the tab at the start of the line spans columns 0 to 7 */
	for (size_t i = 0; i < 3; i++) {
		two_lines("abcdefghij", "\txy");
		right_then_down(cols[i]);
		assert_cursor(1, 0);
	}
	two_lines("abcdefghij", "\txy");
	right_then_down(8);
	assert_cursor(1, 1); /* the x starts at column 8 */
}

/** @brief A tab in the middle of a line, after a '?' cell: its columns depend on what comes before it. */
static void test_editor_wanted_column_inside_a_tab_in_the_middle_of_a_line(void) {
	editor_init(&e);
	append("abcdefghij");
	append("ab\tc"); /* ab 0-1, tab 2-7 (byte 2), c at column 8 (byte 3) */
	right_then_down(6);
	assert_cursor(1, 2);
	two_lines("abcdefghij", "ab\tc");
	right_then_down(8);
	assert_cursor(1, 3);
	editor_free(&e);
	editor_init(&e);
	append("abcdefghij");
	append("\xff\tx"); /* '?' at column 0, tab 1 to 7 (byte 1), x at column 8 (byte 2) */
	right_then_down(5);
	assert_cursor(1, 1);
}

/** @brief The cursor on a tab and moving down uses the tab's first column, not the column it was drawn up to. */
static void test_editor_wanted_column_from_a_tab_is_its_first_column(void) {
	editor_init(&e);
	append("ab\tc"); /* the tab starts at column 2 (byte 2) */
	append("abcdefghij");
	move_n(EDITOR_MOVE_RIGHT, 2);
	assert_cursor(0, 2);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 2);
	editor_move_cursor(&e, EDITOR_MOVE_UP);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT); /* the c, at column 8 */
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 8);
}

/** @brief A wanted column on either column of a ^X mark picks the mark; the next character is picked from its own column. */
static void test_editor_wanted_column_on_a_mark_picks_the_mark(void) {
	const int cols[] = { 1, 2 }; /* a at column 0, ^A at columns 1 and 2 (byte 1), b at column 3 (byte 2) */
	for (size_t i = 0; i < 2; i++) {
		two_lines("abcdef", "a\x01" "b");
		right_then_down(cols[i]);
		assert_cursor(1, 1);
	}
	two_lines("abcdef", "a\x01" "b");
	right_then_down(3);
	assert_cursor(1, 2);
	editor_move_cursor(&e, EDITOR_MOVE_UP); /* from the b (column 3) back up */
	assert_cursor(0, 3);
}

/** @brief Going up from a mark line: the wanted column is the first column of the mark. */
static void test_editor_wanted_column_from_a_mark_is_its_first_column(void) {
	two_lines("abcdef", "a\x01" "b");
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	move_n(EDITOR_MOVE_RIGHT, 1); /* on the mark, byte 1 */
	assert_cursor(1, 1);
	editor_move_cursor(&e, EDITOR_MOVE_UP);
	assert_cursor(0, 1);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT); /* the b: column 3 */
	editor_move_cursor(&e, EDITOR_MOVE_UP);
	assert_cursor(0, 3);
}

/** @brief Multi-byte characters count one column each: the byte index is not the wanted column, in either direction. */
static void test_editor_wanted_column_with_multibyte_characters(void) {
	editor_init(&e);
	append("abcdef");
	append("\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80z"); /* columns 0 1 2 3 at bytes 0 2 5 9 */
	right_then_down(2);
	assert_cursor(1, 5);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_cursor(1, 9);
	editor_move_cursor(&e, EDITOR_MOVE_UP);
	assert_cursor(0, 3);
	two_lines("\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80z", "abcdef");
	move_n(EDITOR_MOVE_RIGHT, 2); /* byte 5, column 2 */
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 2); /* the c: not byte 5 */
}

/** @brief Invalid bytes and C1 controls are one column each, in the wanted column too. */
static void test_editor_wanted_column_after_question_mark_cells(void) {
	editor_init(&e);
	append("abcdef");
	append("\xff\xc2\x85\xfe" "b"); /* cells at bytes 0, 1 (two bytes), 3, 4: columns 0 1 2 3 */
	right_then_down(2);
	assert_cursor(1, 3);
	two_lines("\xff\xc2\x85\xfe" "b", "abcdef");
	move_n(EDITOR_MOVE_RIGHT, 2); /* byte 3, column 2 */
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 2);
}

/** @brief Put the cursor of the shared editor on line 0 with the wanted column @p want, set by hand, and go down. */
static void down_with_wanted_column(size_t want) {
	e.wantcol = want;
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
}

/** @brief A huge wanted column lands on the last character without overflow, even for a line whose first character is not the last. */
static void test_editor_a_huge_wanted_column_lands_on_the_last_character(void) {
	const size_t huge[] = { (size_t)-1, (size_t)-1 - 1, (size_t)-1 / 2, (size_t)-1 / 2 + 1 };
	for (size_t i = 0; i < 4; i++) {
		two_lines("abcdef", "ab");
		down_with_wanted_column(huge[i]);
		assert_cursor(1, 1);
		editor_move_cursor(&e, EDITOR_MOVE_UP);
		assert_cursor(0, 5);
	}
	two_lines("abcdef", "\xc3\xa9\t\xe2\x82\xac");
	down_with_wanted_column((size_t)-1);
	assert_cursor(1, 3); /* the euro sign, the last character */
}

/** @brief When the last character is a tab, a wanted column anywhere from its start to far beyond picks the tab (Vim: `9l j` onto "ab<tab>" gives byte 2). */
static void test_editor_wanted_column_beyond_a_line_that_ends_in_a_tab(void) {
	const size_t wanted[] = { 2, 7, 8, 9, 100, (size_t)-1 }; /* "ab" then a tab: columns 2 to 7, width 8 */
	for (size_t i = 0; i < 6; i++) {
		two_lines("abcdefghij", "ab\t");
		down_with_wanted_column(wanted[i]);
		assert_cursor(1, 2);
	}
	two_lines("abcdefghij", "ab\t");
	down_with_wanted_column(1); /* before the tab: the b */
	assert_cursor(1, 1);
}

/** @brief When the last character is a ^X mark, a wanted column from its first column to far beyond picks it (Vim: `9l j` onto "a^A" gives byte 1). */
static void test_editor_wanted_column_beyond_a_line_that_ends_in_a_mark(void) {
	const size_t wanted[] = { 1, 2, 3, 4, 100, (size_t)-1 }; /* "a" then a mark: columns 1 and 2, width 3 */
	for (size_t i = 0; i < 6; i++) {
		two_lines("abcdefghij", "a\x01");
		down_with_wanted_column(wanted[i]);
		assert_cursor(1, 1);
	}
}

/** @brief When the last character has 4 bytes, a wanted column at or beyond its column picks it, not byte len - 1 and not the character before. */
static void test_editor_wanted_column_beyond_a_line_that_ends_in_a_four_byte_character(void) {
	const size_t wanted[] = { 1, 2, 3, 100, (size_t)-1 }; /* "a" then the emoji: one column each, bytes 0 and 1 */
	for (size_t i = 0; i < 5; i++) {
		two_lines("abcdefghij", "a\xf0\x9f\x98\x80");
		down_with_wanted_column(wanted[i]);
		assert_cursor(1, 1);
	}
}

/** @brief Wanted column 0 goes to the start of a line that begins with a tab or a multi-byte character. */
static void test_editor_wanted_column_zero_goes_to_the_start_of_the_line(void) {
	const char *lines[] = { "\txy", "\xc3\xa9z", "\x01z", "\xff" "z" };
	for (size_t i = 0; i < 4; i++) {
		two_lines("abcdef", lines[i]);
		down_with_wanted_column(0);
		assert_cursor(1, 0);
	}
}

/** @brief Loading a file, init and free reset the wanted column: after them, down keeps cx 0. */
static void test_editor_wanted_column_is_reset_by_init_free_and_load(void) {
	memset(&e, 0xff, sizeof(e));
	editor_init(&e);
	TEST_ASSERT_EQUAL_UINT(0, e.wantcol);
	e.wantcol = 7;
	editor_free(&e);
	TEST_ASSERT_EQUAL_UINT(0, e.wantcol);
	e.wantcol = 7;
	const char *path = tmpdir_write("want.txt", "abcdefgh\nabcdefgh\n");
	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_UINT(0, e.wantcol);
	e.wantcol = 7;
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, tmpdir_path("nope.txt")));
	TEST_ASSERT_EQUAL_UINT(0, e.wantcol);
	e.wantcol = 7;
	TEST_ASSERT_EQUAL_INT(-1, editor_load_file(&e, tmpdir_path(".")));
	TEST_ASSERT_EQUAL_UINT(0, e.wantcol);
}

/** @brief A column wanted before loading a new file does not leak into it: the cursor starts at column 0. */
static void test_editor_a_loaded_file_starts_with_no_wanted_column(void) {
	two_lines("abcdef", "abcdef");
	move_n(EDITOR_MOVE_RIGHT, 5);
	const char *path = tmpdir_write("want.txt", "abcdefgh\nabcdefgh\n");
	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 0);
}

/* ---- the path the editor was loaded from (H5.1) ---- */

/** @brief A fresh editor has no path. */
static void test_editor_init_has_no_path(void) {
	e.path = (char *)"garbage";
	editor_init(&e);
	TEST_ASSERT_NULL(e.path);
}

/** @brief Loading a file remembers the path it was given, as a copy of its own. */
static void test_editor_load_remembers_the_path_as_a_copy(void) {
	editor_init(&e);
	const char *given = tmpdir_write("named.txt", "x\n");
	TEST_ASSERT_NOT_NULL(given);
	char path[128];
	snprintf(path, sizeof(path), "%s", given);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	TEST_ASSERT_NOT_NULL(e.path);
	TEST_ASSERT_EQUAL_STRING(given, e.path);
	TEST_ASSERT_TRUE_MESSAGE(e.path != path, "the path must be copied, not kept");
	path[0] = '?'; /* the caller's buffer changes: the copy does not */
	TEST_ASSERT_EQUAL_STRING(given, e.path);
}

/** @brief A file that does not exist still gives the editor its name: the saver will create it. */
static void test_editor_load_of_a_missing_file_keeps_the_path(void) {
	editor_init(&e);
	const char *path = tmpdir_path("missing.txt");
	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
	TEST_ASSERT_NOT_NULL(e.path);
	TEST_ASSERT_EQUAL_STRING(path, e.path);
}

/** @brief Loading again replaces the path, also by the one of a missing file. */
static void test_editor_load_again_replaces_the_path(void) {
	editor_init(&e);
	char first[128];
	snprintf(first, sizeof(first), "%s", tmpdir_write("first.txt", "1\n"));
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, first));
	char second[128];
	snprintf(second, sizeof(second), "%s", tmpdir_write("second.txt", "2\n"));
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, second));
	TEST_ASSERT_EQUAL_STRING(second, e.path);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, "no-such-file-here.txt"));
	TEST_ASSERT_EQUAL_STRING("no-such-file-here.txt", e.path);
}

/** @brief A load that fails leaves no path, not even the one from before (the contents are dropped too). */
static void test_editor_failed_load_leaves_no_path(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, tmpdir_write("ok.txt", "x\n")));
	TEST_ASSERT_NOT_NULL(e.path);
	const char data[] = { 'a', '\0', 'b' };
	const char *binary = tmpdir_write_bytes("bin.dat", data, sizeof(data));
	TEST_ASSERT_NOT_NULL(binary);
	TEST_ASSERT_EQUAL_INT(-1, editor_load_file(&e, binary));
	TEST_ASSERT_NULL(e.path);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, tmpdir_write("ok2.txt", "x\n")));
	TEST_ASSERT_NOT_NULL(e.path);
	TEST_ASSERT_EQUAL_INT(-1, editor_load_file(&e, tmpdir_path("."))); /* a directory: reading fails */
	TEST_ASSERT_NULL(e.path);
}

/** @brief editor_free() drops the path and is safe twice. */
static void test_editor_free_drops_the_path(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, tmpdir_write("gone.txt", "x\n")));
	TEST_ASSERT_NOT_NULL(e.path);
	editor_free(&e);
	TEST_ASSERT_NULL(e.path);
	editor_free(&e);
	TEST_ASSERT_NULL(e.path);
}

/* ---- the status line (H5.1) ---- */

/** @brief The text window is one row shorter than the terminal, except on a terminal of 0 or 1 rows. */
static void test_editor_text_rows_reserves_the_status_row(void) {
	TEST_ASSERT_EQUAL_UINT(23, editor_text_rows(24));
	TEST_ASSERT_EQUAL_UINT(1, editor_text_rows(2));
	TEST_ASSERT_EQUAL_UINT(1, editor_text_rows(1));
	TEST_ASSERT_EQUAL_UINT(0, editor_text_rows(0));
}

/** @brief Assert that the status line of the shared editor at @p cols columns is exactly @p expected (and NUL-terminated, with the length returned). */
static void assert_status(size_t cols, const char *expected) {
	char out[1024];
	memset(out, 'x', sizeof(out));
	size_t n = editor_status(&e, cols, out, sizeof(out));
	TEST_ASSERT_EQUAL_UINT(strlen(expected), n);
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/** @brief Load @p name (a path of a file that does not exist) into the shared editor, so that it has that name. */
static void name_it(const char *name) {
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, name));
	TEST_ASSERT_NOT_NULL(e.path);
}

/** @brief Without a name the status line says [No Name], then the mode, and the position is at the right: 1,1 for an empty editor. */
static void test_editor_status_without_a_name(void) {
	editor_init(&e);
	assert_status(30, "[No Name] NORMAL" "           " "1,1");
}

/** @brief The name is the path as given. */
static void test_editor_status_shows_the_path(void) {
	editor_init(&e);
	name_it("no-such-dir/main.c");
	assert_status(40, "no-such-dir/main.c NORMAL" "            " "1,1");
}

/** @brief The position is the line and the column of the cursor, both 1-based. */
static void test_editor_status_shows_line_and_column(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	append("ef");
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_status(20, "[No Name] NORMAL" " " "3,2");
}

/** @brief A line number of two digits widens the position, which keeps ending in the last column. */
static void test_editor_status_with_a_two_digit_line(void) {
	editor_init(&e);
	for (int i = 0; i < 12; i++) append("x");
	for (int i = 0; i < 11; i++) editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_status(24, "[No Name] NORMAL" "    " "12,1");
}

/** @brief The column is not clipped to the width: 100 characters in is column 100, and the position keeps its place over the label. */
static void test_editor_status_column_is_not_clipped(void) {
	editor_init(&e);
	char wide[101];
	memset(wide, 'a', 100);
	wide[100] = '\0';
	append(wide);
	for (int i = 0; i < 99; i++) editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_status(20, "[No Name] NORM" " " "1,100");
}

/** @brief The position always ends in the last column, at any width that has room for it and the gap. */
static void test_editor_status_position_ends_in_the_last_column(void) {
	editor_init(&e);
	size_t widths[] = { 4, 5, 10, 19, 20, 21, 33, 79, 80, 200 };
	for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
		char out[1024], msg[64];
		size_t cols = widths[i];
		snprintf(msg, sizeof(msg), "width %zu", cols);
		size_t n = editor_status(&e, cols, out, sizeof(out));
		TEST_ASSERT_EQUAL_UINT_MESSAGE(cols, n, msg);
		TEST_ASSERT_EQUAL_STRING_MESSAGE("1,1", out + cols - 3, msg);
		TEST_ASSERT_EQUAL_INT_MESSAGE(' ', out[cols - 4], msg);
	}
}

/** @brief Assert that the position of the shared editor, in a status line 30 columns wide, is @p position (the last characters). */
static void assert_position(const char *position) {
	char out[1024];
	size_t n = editor_status(&e, 30, out, sizeof(out));
	size_t len = strlen(position);
	TEST_ASSERT_EQUAL_UINT(30, n);
	TEST_ASSERT_EQUAL_STRING(position, out + n - len);
}

/** @brief The column is the display column: after a tab it is 9, since the tab takes the columns 1 to 8. */
static void test_editor_status_column_after_a_tab(void) {
	editor_init(&e);
	append("\tx");
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_position("1,9");
}

/** @brief On a tab the column is the one where the tab starts, as the cursor is drawn there. */
static void test_editor_status_column_on_a_tab(void) {
	editor_init(&e);
	append("ab\tx");
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_position("1,3");
}

/** @brief On a mark the column is the one where the mark starts. */
static void test_editor_status_column_on_a_mark(void) {
	editor_init(&e);
	append("a\x01" "b");
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_position("1,2");
}

/** @brief A mark such as ^A takes two columns: the character after it is at column 4. */
static void test_editor_status_column_after_a_mark(void) {
	editor_init(&e);
	append("a\x01" "b");
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_position("1,4");
}

/** @brief A multi-byte character is one column, not its bytes. */
static void test_editor_status_column_counts_characters(void) {
	editor_init(&e);
	append("\xc3\xa9\xe2\x82\xac" "z");
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_position("1,3");
}

/** @brief An invalid byte and a C1 control are one column each, drawn as '?'. */
static void test_editor_status_column_counts_invalid_bytes_and_c1(void) {
	editor_init(&e);
	append("a\xff" "b\xc2\x9b" "c");
	for (int i = 0; i < 4; i++) editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_position("1,5");
}

/** @brief The mode label follows the name after one space. */
static void test_editor_status_has_the_mode_label_after_the_name(void) {
	editor_init(&e);
	name_it("a.txt");
	assert_status(30, "a.txt NORMAL" "               " "1,1");
}

/** @brief A control byte in the name is a mark, so a name can never send a command to the terminal: no byte below 0x20 comes out. */
static void test_editor_status_shows_marks_in_the_name(void) {
	editor_init(&e);
	name_it("a\x1b[2Jb");
	assert_status(30, "a^[[2Jb NORMAL" "             " "1,1");
	name_it("\x01\x7f\x1f.c");
	char out[1024];
	editor_status(&e, 40, out, sizeof(out));
	for (const char *p = out; *p; p++) TEST_ASSERT_TRUE_MESSAGE((unsigned char)*p >= 0x20 && *p != 0x7f, "a control byte reached the status text");
	TEST_ASSERT_EQUAL_STRING("^A^?^_.c NORMAL" "                      " "1,1", out);
}

/** @brief A tab in the name is spaces to the next multiple of 8 columns from the start of the line. */
static void test_editor_status_shows_a_tab_in_the_name_as_spaces(void) {
	editor_init(&e);
	name_it("a\tb");
	assert_status(30, "a       b NORMAL" "           " "1,1");
}

/** @brief A UTF-8 name is copied: 6 columns of name, 13 of text, and 3 more bytes than columns. */
static void test_editor_status_shows_a_utf8_name(void) {
	editor_init(&e);
	name_it("\xc3\xa9\xe2\x82\xac.txt");
	assert_status(40, "\xc3\xa9\xe2\x82\xac.txt NORMAL" "                        " "1,1");
}

/** @brief An invalid byte and a C1 control in the name are '?', as in the text. */
static void test_editor_status_shows_invalid_bytes_and_c1_in_the_name_as_question_marks(void) {
	editor_init(&e);
	name_it("a\xff" "b\xc2\x9b" "c");
	assert_status(30, "a?b?c NORMAL" "               " "1,1");
}

/** @brief A mark that does not fit is not shown half: at a width that leaves one column for it, it is left out and the line is padded. */
static void test_editor_status_never_cuts_a_mark(void) {
	editor_init(&e);
	name_it("a\x01");
	assert_status(5, "a" " " "1,1");        /* room for the name: 1 column, "a" */
	assert_status(6, "a" "  " "1,1");        /* 2 columns: "a" and the mark would need 3 */
	assert_status(7, "a^A" " " "1,1");      /* 3 columns: the whole mark */
	assert_status(8, "a^A " " " "1,1");     /* 4 columns: the space after it */
}

/** @brief One cell of a name as the status shows it, for the oracle of the truncation test. */
typedef struct {
	const char *cells[24]; /**< The cells of "<name> NORMAL" as drawn, NULL ends the list. */
} drawn_t;

/** @brief Assert that the status line of the shared editor at every width from 0 to 30 is the first cells of @p d that fit in the width less the position, padded, with the position cut on the right when it does not fit. */
static void assert_status_at_every_width(const drawn_t *d) {
	const char *right = "1,1";
	for (size_t cols = 0; cols <= 30; cols++) {
		char expected[256] = "", out[1024], msg[64];
		if (cols <= 3) {
			strncat(expected, right, cols);
		} else {
			size_t avail = cols - 4, used = 0;
			for (size_t i = 0; d->cells[i]; i++) {
				size_t w = d->cells[i][0] == '^' ? 2 : 1; /* a mark is two columns, any other cell is one */
				if (used + w > avail) break;
				strcat(expected, d->cells[i]);
				used += w;
			}
			for (size_t pad = cols - 3 - used; pad > 0; pad--) strcat(expected, " ");
			strcat(expected, right);
		}
		snprintf(msg, sizeof(msg), "width %zu", cols);
		memset(out, 'x', sizeof(out));
		size_t n = editor_status(&e, cols, out, sizeof(out));
		TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, out, msg);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(strlen(expected), n, msg);
	}
}

/** @brief Truncation at every width from 0 to wider than needed, for a plain name, a name with a mark and a name with multi-byte characters. */
static void test_editor_status_truncates_at_every_width(void) {
	editor_init(&e);
	name_it("a.txt");
	drawn_t plain = { { "a", ".", "t", "x", "t", " ", "N", "O", "R", "M", "A", "L", NULL } };
	assert_status_at_every_width(&plain);
	name_it("a\x01" "b");
	drawn_t mark = { { "a", "^A", "b", " ", "N", "O", "R", "M", "A", "L", NULL } };
	assert_status_at_every_width(&mark);
	name_it("\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80x");
	drawn_t utf8 = { { "\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80", "x", " ", "N", "O", "R", "M", "A", "L", NULL } };
	assert_status_at_every_width(&utf8);
	name_it("a\tb"); /* cut inside the 7 spaces of the tab */
	drawn_t tab = { { "a", " ", " ", " ", " ", " ", " ", " ", "b", " ", "N", "O", "R", "M", "A", "L", NULL } };
	assert_status_at_every_width(&tab);
	name_it("\xe2\x82"); /* a truncated sequence: two invalid bytes, '?' each */
	drawn_t bad = { { "?", "?", " ", "N", "O", "R", "M", "A", "L", NULL } };
	assert_status_at_every_width(&bad);
	name_it("a.txt");
	e.crlf = 1;
	drawn_t dos = { { "a", ".", "t", "x", "t", " ", "[", "d", "o", "s", "]", " ", "N", "O", "R", "M", "A", "L", NULL } };
	assert_status_at_every_width(&dos);
}

/** @brief Literal cases of a narrow terminal: the position wins, then the name is cut from the right, the mode label first. */
static void test_editor_status_on_a_narrow_terminal(void) {
	editor_init(&e);
	name_it("n.txt");
	assert_status(0, "");
	assert_status(1, "1");
	assert_status(2, "1,");
	assert_status(3, "1,1");
	assert_status(4, " 1,1");
	assert_status(5, "n" " " "1,1");
	assert_status(10, "n.txt " " " "1,1");
	assert_status(12, "n.txt NO 1,1");
	assert_status(15, "n.txt NORMA 1,1");
	assert_status(16, "n.txt NORMAL 1,1");
	assert_status(17, "n.txt NORMAL" "  " "1,1");
}

/** @brief The status is exactly @p cols columns wide: for ASCII, @p cols bytes, whatever the name and the width. */
static void test_editor_status_is_exactly_the_width(void) {
	editor_init(&e);
	name_it("some/dir/with a long name.c");
	for (size_t cols = 0; cols < 100; cols++) {
		char out[1024], msg[64];
		snprintf(msg, sizeof(msg), "width %zu", cols);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(cols, editor_status(&e, cols, out, sizeof(out)), msg);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(cols, strlen(out), msg);
	}
}

/** @brief A buffer that is too small gets the line cut at the buffer, still NUL-terminated; a size of 0 is untouched. */
static void test_editor_status_into_a_small_buffer(void) {
	editor_init(&e);
	char out[8];
	memset(out, 'x', sizeof(out));
	TEST_ASSERT_EQUAL_UINT(0, editor_status(&e, 30, out, 0));
	TEST_ASSERT_EQUAL_INT('x', out[0]);
	TEST_ASSERT_EQUAL_UINT(4, editor_status(&e, 30, out, 5));
	TEST_ASSERT_EQUAL_STRING("[No ", out);
	TEST_ASSERT_EQUAL_INT('x', out[5]);
	/* a buffer that is too small is cut at a byte, as documented (editor_draw_screen() reserves the room, so it never meets this) */
	name_it("\xc3\xa9\xc3\xa9");
	TEST_ASSERT_EQUAL_UINT(2, editor_status(&e, 30, out, 3));
	TEST_ASSERT_EQUAL_STRING("\xc3\xa9", out);
	TEST_ASSERT_EQUAL_UINT(1, editor_status(&e, 30, out, 2));
	TEST_ASSERT_EQUAL_STRING("\xc3", out);
	char all[64];
	TEST_ASSERT_EQUAL_UINT(1, editor_status(&e, 1, all, sizeof(all)));
	TEST_ASSERT_EQUAL_STRING("1", all);
}

/** @brief Loading the editor's own path (a reload, as ":e" will do) works: the path is copied before the old one is freed. */
static void test_editor_load_of_its_own_path_works(void) {
	editor_init(&e);
	const char *path = tmpdir_write("self.txt", "one\n");
	TEST_ASSERT_NOT_NULL(path);
	char expected[128];
	snprintf(expected, sizeof(expected), "%s", path);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	TEST_ASSERT_NOT_NULL(tmpdir_write("self.txt", "two\nthree\n"));
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, e.path));
	TEST_ASSERT_EQUAL_STRING(expected, e.path);
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	assert_line(&e, 0, "two");
	/* and when that load fails: the file is now a binary one */
	const char data[] = { 'a', '\0' };
	TEST_ASSERT_NOT_NULL(tmpdir_write_bytes("self.txt", data, sizeof(data)));
	TEST_ASSERT_EQUAL_INT(-1, editor_load_file(&e, e.path));
	TEST_ASSERT_NULL(e.path);
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
}

/** @brief An empty path is no name: the status shows [No Name]. */
static void test_editor_status_of_an_empty_path_is_no_name(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, ""));
	assert_status(30, "[No Name] NORMAL" "           " "1,1");
}

/** @brief The mode label is NORMAL: the one place Task 2 changes. */
static void test_editor_mode_label_is_normal(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_STRING("NORMAL", editor_mode_label(&e));
}

/** @brief Send each byte of @p keys to editor_handle_key() (a lone 'E' is KEY_ESC, 'U', 'D', 'L', 'R' are the arrows). */
static void press(const char *keys) {
	for (; *keys; keys++) {
		int key = *keys;
		switch (*keys) {
		case 'E': key = KEY_ESC; break;
		case 'U': key = KEY_UP; break;
		case 'D': key = KEY_DOWN; break;
		case 'L': key = KEY_LEFT; break;
		case 'R': key = KEY_RIGHT; break;
		}
		editor_handle_key(&e, key);
	}
}

/** @brief A fresh editor is in normal mode; i enters insert mode where the cursor is and asks for a redraw. */
static void test_editor_i_enters_insert_mode_at_the_cursor(void) {
	editor_init(&e);
	append("abc");
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_NORMAL, e.mode);
	press("l");
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, 'i'));
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_INSERT, e.mode);
	assert_cursor(0, 1);
	TEST_ASSERT_EQUAL_STRING("INSERT", editor_mode_label(&e));
}

/** @brief i works in an editor with no lines. */
static void test_editor_i_on_an_empty_editor(void) {
	editor_init(&e);
	press("i");
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_INSERT, e.mode);
	press("E");
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_NORMAL, e.mode);
	assert_cursor(0, 0);
}

/** @brief Esc in normal mode does nothing and needs no redraw. */
static void test_editor_esc_in_normal_mode_does_nothing(void) {
	editor_init(&e);
	append("abc");
	press("l");
	TEST_ASSERT_EQUAL_INT(0, editor_handle_key(&e, KEY_ESC));
	assert_cursor(0, 1);
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_NORMAL, e.mode);
}

/** @brief Esc at column 0 leaves insert mode and the cursor stays (Vim: i, Esc, i, Y gives Yabc). */
static void test_editor_esc_at_column_0_stays(void) {
	editor_init(&e);
	append("abc");
	press("i");
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, KEY_ESC));
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_NORMAL, e.mode);
	assert_cursor(0, 0);
	TEST_ASSERT_EQUAL_STRING("NORMAL", editor_mode_label(&e));
}

/** @brief Esc in the middle of a line moves one character left and sets the wanted column (Vim: 3l, i, Esc is column 2). */
static void test_editor_esc_in_the_middle_moves_left(void) {
	editor_init(&e);
	append("abcdef");
	press("llli");
	e.wantcol = 99;
	press("E");
	assert_cursor(0, 2);
	TEST_ASSERT_EQUAL_UINT(2, e.wantcol);
}

/** @brief Esc after the last character lands on the last character (Vim: A, Esc, i, Y gives abYc). */
static void test_editor_esc_after_the_last_character_lands_on_it(void) {
	editor_init(&e);
	append("abc");
	press("lllR"); /* normal: stops on c */
	assert_cursor(0, 2);
	press("iRE");
	assert_cursor(0, 2);
	press("iRRE");
	assert_cursor(0, 2);
}

/** @brief Esc on an empty line stays at column 0. */
static void test_editor_esc_on_an_empty_line(void) {
	editor_init(&e);
	append("");
	press("iE");
	assert_cursor(0, 0);
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_NORMAL, e.mode);
}

/** @brief Esc steps over a whole multi-byte character, from its end and from the end of the line. */
static void test_editor_esc_steps_over_a_multibyte_character(void) {
	editor_init(&e);
	append("a\xc3\xa9" "b"); /* a, e-acute (bytes 1-2), b (byte 3) */
	press("lli"); /* on b */
	assert_cursor(0, 3);
	press("E");
	assert_cursor(0, 1);
	press("iRRE"); /* end of line (byte 4), then Esc on b */
	assert_cursor(0, 3);
}

/** @brief Esc at column 0 still sets the wanted column to 0 (Vim: 3l, j onto an empty line, i, Esc, k goes to column 0). */
static void test_editor_esc_at_column_0_sets_the_wanted_column(void) {
	editor_init(&e);
	append("abcdef");
	append("");
	press("lllj");
	TEST_ASSERT_EQUAL_UINT(3, e.wantcol);
	press("iEk");
	assert_cursor(0, 0);
}

/** @brief Send every byte of @p text to editor_handle_key() as a key (no letter is a command here: insert mode types). */
static void type(const char *text) {
	for (; *text; text++) editor_handle_key(&e, (unsigned char)*text);
}

/** @brief Start typing: one line @p line, insert mode, cursor at byte @p cx. */
static void insert_at(const char *line, size_t cx) {
	editor_free(&e); /* a test calls this more than once: the editor is freed in tearDown */
	append(line);
	press("i");
	for (size_t i = 0; i < cx; i++) press("R");
}

/** @brief In insert mode h, j, k, l and every printable key are typed, not commands. */
static void test_editor_hjkl_are_typed_in_insert_mode(void) {
	insert_at("abc", 1);
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, 'h'));
	type("jkl");
	assert_line(&e, 0, "ahjklbc");
	assert_cursor(0, 5);
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_INSERT, e.mode);
}

/** @brief Typing at the start, in the middle and at the end of a line; the cursor and wantcol follow (Vim: ixy then Esc leaves the cursor on y). */
static void test_editor_typing_inserts_at_the_cursor(void) {
	insert_at("abc", 0);
	type("xy");
	assert_line(&e, 0, "xyabc");
	assert_cursor(0, 2);
	TEST_ASSERT_EQUAL_UINT(2, e.wantcol);
	insert_at("abc", 2);
	type("Z");
	assert_line(&e, 0, "abZc");
	assert_cursor(0, 3);
	insert_at("abc", 3);
	type("XY");
	assert_line(&e, 0, "abcXY");
	assert_cursor(0, 5);
	TEST_ASSERT_EQUAL_UINT(5, e.wantcol);
	press("E");
	assert_cursor(0, 4);
}

/** @brief Typing on an empty line, and on a line other than the first. */
static void test_editor_typing_on_an_empty_line(void) {
	editor_init(&e);
	append("one");
	append("");
	press("ji");
	type("hi");
	assert_line(&e, 0, "one");
	assert_line(&e, 1, "hi");
	assert_cursor(1, 2);
}

/** @brief The first insertion into an editor with no lines creates the first line. */
static void test_editor_typing_in_an_empty_editor(void) {
	editor_init(&e);
	press("i");
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, 'a'));
	TEST_ASSERT_EQUAL_UINT(1, editor_line_count(&e));
	type("b");
	assert_line(&e, 0, "ab");
	assert_cursor(0, 2);
	TEST_ASSERT_TRUE(e.modified);
}

/** @brief A complete multibyte character is inserted whole and the cursor moves past all its bytes; the keys before the last one ask for nothing. */
static void test_editor_typing_a_multibyte_character(void) {
	insert_at("ab", 1);
	TEST_ASSERT_EQUAL_INT(0, editor_handle_key(&e, 0xc3));
	assert_line(&e, 0, "ab");
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, 0xa9));
	assert_line(&e, 0, "a\xc3\xa9" "b");
	assert_cursor(0, 3);
	TEST_ASSERT_EQUAL_UINT(2, e.wantcol);
	type("\xe2\x82\xac\xf0\x9f\x98\x80"); /* 3 and 4 bytes */
	assert_line(&e, 0, "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80" "b");
	assert_cursor(0, 10);
	TEST_ASSERT_EQUAL_UINT(4, e.wantcol);
}

/** @brief Stray continuation bytes, invalid leads and a character cut short are dropped, never inserted. */
static void test_editor_typing_drops_invalid_bytes(void) {
	insert_at("ab", 1);
	type("\xa9\xff\xc0\xf5"); /* continuation, 0xff, overlong lead, too-high lead */
	assert_line(&e, 0, "ab");
	type("\xc3" "x"); /* lead cut short by an ASCII byte: only the x is typed */
	assert_line(&e, 0, "axb");
	type("\xe2\x82\xc3\xa9"); /* 3-byte lead cut short by a new lead: the e-acute is typed */
	assert_line(&e, 0, "ax\xc3\xa9" "b");
	type("\xe0\x80\x80"); /* overlong: complete but invalid */
	assert_line(&e, 0, "ax\xc3\xa9" "b");
	type("\xc3"); /* half a character, then Esc and a new insert: nothing leaks into the new one */
	press("Ei");
	type("\xa9");
	assert_line(&e, 0, "ax\xc3\xa9" "b");
	TEST_ASSERT_EQUAL_UINT(0, e.pend_len);
}

/** @brief An arrow key between the two halves of a character drops the first half. */
static void test_editor_an_arrow_drops_half_a_character(void) {
	insert_at("ab", 0);
	editor_handle_key(&e, 0xc3);
	press("R");
	editor_handle_key(&e, 0xa9);
	assert_line(&e, 0, "ab");
}

/** @brief Tab inserts a tab character; the cursor column is the next tab stop. */
static void test_editor_tab_inserts_a_tab(void) {
	insert_at("ab", 1);
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, '\t'));
	assert_line(&e, 0, "a\tb");
	assert_cursor(0, 2);
	TEST_ASSERT_EQUAL_UINT(8, e.wantcol);
}

/** @brief Control keys other than Enter and Backspace are ignored: no change, no redraw, not modified. */
static void test_editor_control_keys_are_ignored_when_typing(void) {
	insert_at("abc", 1);
	const int ignored[] = { 0x01, 0x07, 0x1f, 0x00 };
	for (size_t i = 0; i < sizeof(ignored) / sizeof(*ignored); i++) TEST_ASSERT_EQUAL_INT(0, editor_handle_key(&e, ignored[i]));
	assert_line(&e, 0, "abc");
	assert_cursor(0, 1);
	TEST_ASSERT_FALSE(e.modified);
}

/** @brief A very long line grows by realloc and keeps its terminator. */
static void test_editor_typing_a_very_long_line(void) {
	insert_at("", 0);
	for (size_t i = 0; i < 100000; i++) editor_handle_key(&e, 'a' + (int)(i % 26));
	TEST_ASSERT_EQUAL_UINT(100000, strlen(editor_line(&e, 0)));
	TEST_ASSERT_EQUAL_UINT(100000, e.cx);
	TEST_ASSERT_EQUAL_CHAR('a' + (99999 % 26), editor_line(&e, 0)[99999]);
	insert_at("end", 0);
	for (size_t i = 0; i < 5000; i++) type("\xc3\xa9");
	TEST_ASSERT_EQUAL_UINT(10003, strlen(editor_line(&e, 0)));
	TEST_ASSERT_EQUAL_STRING("end", editor_line(&e, 0) + 10000);
}

/** @brief modified is 0 after init, set by typing, not by moving or by normal mode keys, and cleared by a load. */
static void test_editor_modified_is_set_by_typing_and_cleared_by_load(void) {
	editor_init(&e);
	TEST_ASSERT_FALSE(e.modified);
	append("abc");
	press("iRLE");
	press("lh");
	TEST_ASSERT_FALSE(e.modified);
	press("ix");
	TEST_ASSERT_TRUE(e.modified);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, "no-such-dir/none.txt"));
	TEST_ASSERT_FALSE(e.modified);
	editor_free(&e);
	TEST_ASSERT_FALSE(e.modified);
}

/** @brief Typing leaves crlf alone. */
static void test_editor_typing_keeps_the_crlf_flag(void) {
	insert_at("abc", 0);
	e.crlf = 1;
	type("x");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
	assert_line(&e, 0, "xabc");
}

/** @brief The status line shows [+] after the name once modified, and after [dos]; the position keeps the last column. */
static void test_editor_status_shows_the_modified_mark(void) {
	editor_init(&e);
	append("abc");
	e.modified = 1;
	assert_status(30, "[No Name] [+] NORMAL" "       " "1,1");
	e.crlf = 1;
	assert_status(30, "[No Name] [dos] [+] NORMAL" " " "1,1");
	e.crlf = 0;
	assert_status(11, "[No Nam" " " "1,1"); /* the left part is cut first */
	assert_status(3, "1,1");
}

/** @brief After typing, editor_scroll() keeps the cursor line in the window and the cursor stays after the new character. */
static void test_editor_typing_at_the_bottom_of_a_window_scrolls(void) {
	editor_init(&e);
	for (int i = 0; i < 10; i++) append("line");
	press("iDDDDDDDDD");
	editor_scroll(&e, 4);
	TEST_ASSERT_EQUAL_UINT(6, e.rowoff);
	type("X");
	editor_scroll(&e, 4);
	TEST_ASSERT_EQUAL_UINT(6, e.rowoff);
	assert_line(&e, 9, "Xline");
	assert_cursor(9, 1);
}

/** @brief Send KEY_DELETE to the shared editor; return what editor_handle_key() returned. */
static int del(void) {
	return editor_handle_key(&e, KEY_DELETE);
}

/** @brief Assert the wanted column of the shared editor. */
static void assert_wantcol(size_t col) {
	TEST_ASSERT_EQUAL_UINT(col, e.wantcol);
}

/** @brief Enter splits at the cursor: middle, end and start of a line; the cursor goes to the start of the new line (Vim: same). */
static void test_editor_enter_splits_the_line(void) {
	insert_at("abcd", 2);
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, '\r'));
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	assert_line(&e, 0, "ab");
	assert_line(&e, 1, "cd");
	assert_cursor(1, 0);
	assert_wantcol(0);
	TEST_ASSERT_TRUE(e.modified);
	insert_at("ab", 2);
	press("\r");
	assert_line(&e, 0, "ab");
	assert_line(&e, 1, "");
	assert_cursor(1, 0);
	insert_at("ab", 0);
	press("\r");
	assert_line(&e, 0, "");
	assert_line(&e, 1, "ab");
	assert_cursor(1, 0);
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
}

/** @brief The byte 0x0a splits like 0x0d, and typing goes on in the new line. */
static void test_editor_line_feed_splits_like_carriage_return(void) {
	insert_at("abcd", 2);
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, '\n'));
	type("X");
	assert_line(&e, 0, "ab");
	assert_line(&e, 1, "Xcd");
	assert_cursor(1, 1);
}

/** @brief Enter in the middle of a text keeps the lines before and after in order. */
static void test_editor_enter_in_the_middle_of_a_text(void) {
	editor_init(&e);
	append("one");
	append("two");
	append("three");
	press("jiR\r");
	TEST_ASSERT_EQUAL_UINT(4, editor_line_count(&e));
	assert_line(&e, 0, "one");
	assert_line(&e, 1, "t");
	assert_line(&e, 2, "wo");
	assert_line(&e, 3, "three");
	assert_cursor(2, 0);
	press("\r\r\r\r\r\r\r\r\r\r");
	TEST_ASSERT_EQUAL_UINT(14, editor_line_count(&e));
	assert_line(&e, 12, "wo");
	assert_line(&e, 13, "three");
}

/** @brief Enter in an editor with no lines gives two empty lines and the cursor on the second, as Vim does. */
static void test_editor_enter_in_an_empty_editor(void) {
	editor_init(&e);
	press("i");
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, '\r'));
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	assert_line(&e, 0, "");
	assert_line(&e, 1, "");
	assert_cursor(1, 0);
	TEST_ASSERT_TRUE(e.modified);
}

/** @brief Enter never cuts a multibyte character, and the wanted column is the new cursor's. */
static void test_editor_enter_with_multibyte_text_and_wantcol(void) {
	insert_at("a\xc3\xa9\xe2\x82\xac" "b", 2);
	assert_wantcol(2);
	press("\r");
	assert_line(&e, 0, "a\xc3\xa9");
	assert_line(&e, 1, "\xe2\x82\xac" "b");
	assert_cursor(1, 0);
	assert_wantcol(0);
	press("U");
	assert_cursor(0, 0);
}

/** @brief Enter at the bottom of the window moves the window by one line. */
static void test_editor_enter_at_the_bottom_of_a_window_scrolls(void) {
	editor_init(&e);
	for (int i = 0; i < 10; i++) append("line");
	press("iDDDDDDDDD");
	editor_scroll(&e, 4);
	TEST_ASSERT_EQUAL_UINT(6, e.rowoff);
	press("\r");
	editor_scroll(&e, 4);
	TEST_ASSERT_EQUAL_UINT(11, editor_line_count(&e));
	TEST_ASSERT_EQUAL_UINT(7, e.rowoff);
	assert_cursor(10, 0);
}

/** @brief Backspace deletes the character before the cursor and moves the cursor and wantcol back (0x7f and 0x08). */
static void test_editor_backspace_deletes_the_previous_character(void) {
	insert_at("abc", 2);
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, 0x7f));
	assert_line(&e, 0, "ac");
	assert_cursor(0, 1);
	assert_wantcol(1);
	TEST_ASSERT_TRUE(e.modified);
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, 0x08));
	assert_line(&e, 0, "c");
	assert_cursor(0, 0);
	insert_at("abc", 3);
	press("\x7f");
	assert_line(&e, 0, "ab");
	assert_cursor(0, 2);
	assert_wantcol(2);
}

/** @brief Backspace removes a whole multibyte character (2, 3 and 4 bytes), never one byte of it (Vim: same). */
static void test_editor_backspace_deletes_a_whole_multibyte_character(void) {
	insert_at("a\xc3\xa9" "b", 2);
	press("\x7f");
	assert_line(&e, 0, "ab");
	assert_cursor(0, 1);
	insert_at("\xe2\x82\xac" "x", 1);
	press("\x7f");
	assert_line(&e, 0, "x");
	assert_cursor(0, 0);
	insert_at("a\xf0\x9f\x98\x80", 2);
	press("\x7f");
	assert_line(&e, 0, "a");
	assert_cursor(0, 1);
	assert_wantcol(1);
}

/** @brief Backspace after a tab brings wantcol back to the column before it. */
static void test_editor_backspace_after_a_tab(void) {
	insert_at("a\tb", 3);
	assert_wantcol(9);
	press("\x7f");
	assert_line(&e, 0, "a\t");
	assert_wantcol(8);
	press("\x7f");
	assert_line(&e, 0, "a");
	assert_wantcol(1);
}

/** @brief Backspace at the start of a line joins it to the end of the previous one; the cursor sits at the join (Vim: same). */
static void test_editor_backspace_at_the_start_of_a_line_joins(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	append("ef");
	press("ji");
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, 0x7f));
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	assert_line(&e, 0, "abcd");
	assert_line(&e, 1, "ef");
	assert_cursor(0, 2);
	assert_wantcol(2);
	TEST_ASSERT_TRUE(e.modified);
	type("X");
	assert_line(&e, 0, "abXcd");
}

/** @brief Joining takes the display column of the join (a tab counts), and keeps the lines around it. */
static void test_editor_join_sets_wantcol_from_the_display_column(void) {
	editor_init(&e);
	append("x");
	append("a\t");
	append("b");
	append("y");
	press("jji");
	press("\x7f");
	assert_line(&e, 1, "a\tb");
	assert_line(&e, 2, "y");
	assert_cursor(1, 2);
	assert_wantcol(8);
	TEST_ASSERT_EQUAL_UINT(3, editor_line_count(&e));
}

/** @brief Joining with an empty previous line, an empty current line, and the last line; the text of the others is intact. */
static void test_editor_join_with_empty_lines_and_at_the_end(void) {
	editor_init(&e);
	append("");
	append("cd");
	press("ji\x7f");
	TEST_ASSERT_EQUAL_UINT(1, editor_line_count(&e));
	assert_line(&e, 0, "cd");
	assert_cursor(0, 0);
	editor_free(&e);
	append("ab");
	append("");
	press("ji\x7f");
	TEST_ASSERT_EQUAL_UINT(1, editor_line_count(&e));
	assert_line(&e, 0, "ab");
	assert_cursor(0, 2);
	editor_free(&e);
	append("");
	append("");
	append("");
	press("jji\x7f");
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	press("\x7f\x7f");
	TEST_ASSERT_EQUAL_UINT(1, editor_line_count(&e));
	assert_line(&e, 0, "");
	assert_cursor(0, 0);
}

/** @brief Joining keeps valid UTF-8 whole, including the invalid bytes, and the joined line is terminated. */
static void test_editor_join_keeps_utf8_and_terminates_the_line(void) {
	editor_init(&e);
	append("\xc3\xa9\xff");
	append("\xe2\x82\xac\xf0\x9f\x98\x80");
	press("ji\x7f");
	TEST_ASSERT_EQUAL_UINT(1, editor_line_count(&e));
	assert_line(&e, 0, "\xc3\xa9\xff\xe2\x82\xac\xf0\x9f\x98\x80");
	TEST_ASSERT_EQUAL_UINT(10, strlen(editor_line(&e, 0)));
	assert_cursor(0, 3);
	static char big_a[5001], big_b[5001];
	memset(big_a, 'a', 5000);
	memset(big_b, 'b', 5000);
	big_a[5000] = big_b[5000] = '\0';
	editor_free(&e);
	append(big_a);
	append(big_b);
	press("ji\x7f");
	size_t len = strlen(editor_line(&e, 0));
	TEST_ASSERT_EQUAL_UINT(10000, len);
	TEST_ASSERT_EQUAL_UINT(5000, e.cx);
	TEST_ASSERT_EQUAL_CHAR('b', editor_line(&e, 0)[5000]);
}

/** @brief Backspace on the first character of the first line does nothing: no change, no redraw, not modified. */
static void test_editor_backspace_at_the_start_of_the_buffer_does_nothing(void) {
	insert_at("ab", 0);
	TEST_ASSERT_EQUAL_INT(0, editor_handle_key(&e, 0x7f));
	assert_line(&e, 0, "ab");
	assert_cursor(0, 0);
	TEST_ASSERT_FALSE(e.modified);
	editor_free(&e);
	append("");
	press("i");
	TEST_ASSERT_EQUAL_INT(0, editor_handle_key(&e, 0x7f));
	TEST_ASSERT_EQUAL_UINT(1, editor_line_count(&e));
	editor_free(&e);
	press("i");
	TEST_ASSERT_EQUAL_INT(0, editor_handle_key(&e, 0x7f));
	TEST_ASSERT_EQUAL_INT(0, del());
	TEST_ASSERT_EQUAL_UINT(0, editor_line_count(&e));
	TEST_ASSERT_FALSE(e.modified);
}

/** @brief Delete removes the character under the cursor, a whole multibyte one, and the cursor stays (Vim: same). */
static void test_editor_delete_removes_the_character_under_the_cursor(void) {
	insert_at("abc", 1);
	TEST_ASSERT_NOT_EQUAL(0, del());
	assert_line(&e, 0, "ac");
	assert_cursor(0, 1);
	assert_wantcol(1);
	TEST_ASSERT_TRUE(e.modified);
	insert_at("\xc3\xa9" "b", 0);
	del();
	assert_line(&e, 0, "b");
	assert_cursor(0, 0);
	insert_at("\xe2\x82\xac\xf0\x9f\x98\x80" "z", 1);
	del();
	assert_line(&e, 0, "\xe2\x82\xac" "z");
	assert_cursor(0, 3);
	del();
	assert_line(&e, 0, "\xe2\x82\xac");
	assert_cursor(0, 3);
}

/** @brief Delete at the end of a line joins the next one; the cursor stays at the join (Vim: same). */
static void test_editor_delete_at_the_end_of_a_line_joins(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	append("ef");
	press("iRR");
	TEST_ASSERT_NOT_EQUAL(0, del());
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	assert_line(&e, 0, "abcd");
	assert_line(&e, 1, "ef");
	assert_cursor(0, 2);
	assert_wantcol(2);
	TEST_ASSERT_TRUE(e.modified);
	type("X");
	assert_line(&e, 0, "abXcd");
}

/** @brief Delete at the end of an empty line, joining an empty next line, and the last line (nothing). */
static void test_editor_delete_with_empty_lines_and_at_the_end(void) {
	editor_init(&e);
	append("");
	append("cd");
	append("");
	press("iR"); /* empty line: the cursor is at its end */
	TEST_ASSERT_NOT_EQUAL(0, del());
	assert_line(&e, 0, "cd");
	assert_line(&e, 1, "");
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	press("RR");
	del();
	TEST_ASSERT_EQUAL_UINT(1, editor_line_count(&e));
	assert_line(&e, 0, "cd");
	assert_cursor(0, 2);
	e.modified = 0;
	TEST_ASSERT_EQUAL_INT(0, del());
	assert_line(&e, 0, "cd");
	assert_cursor(0, 2);
	TEST_ASSERT_FALSE(e.modified);
}

/** @brief The three keys do nothing in normal mode. */
static void test_editor_editing_keys_do_nothing_in_normal_mode(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	press("j");
	TEST_ASSERT_EQUAL_INT(0, editor_handle_key(&e, 0x7f));
	TEST_ASSERT_EQUAL_INT(0, editor_handle_key(&e, 0x08));
	TEST_ASSERT_EQUAL_INT(0, editor_handle_key(&e, '\r'));
	TEST_ASSERT_EQUAL_INT(0, editor_handle_key(&e, '\n'));
	TEST_ASSERT_EQUAL_INT(0, del());
	TEST_ASSERT_EQUAL_UINT(2, editor_line_count(&e));
	assert_line(&e, 0, "ab");
	assert_line(&e, 1, "cd");
	assert_cursor(1, 0);
	TEST_ASSERT_FALSE(e.modified);
}

/** @brief A half typed character is dropped by Backspace, which still deletes what is before the cursor. */
static void test_editor_backspace_drops_a_half_typed_character(void) {
	insert_at("ab", 2);
	editor_handle_key(&e, 0xc3);
	TEST_ASSERT_EQUAL_UINT(1, e.pend_len);
	press("\x7f");
	TEST_ASSERT_EQUAL_UINT(0, e.pend_len);
	assert_line(&e, 0, "a");
	editor_handle_key(&e, 0xc3);
	press("\r");
	TEST_ASSERT_EQUAL_UINT(0, e.pend_len);
	editor_handle_key(&e, 0xa9);
	assert_line(&e, 1, "");
}

/** @brief A CRLF file is edited and stays CRLF: lines carry no CR, and splitting and joining leave the flag alone. */
static void test_editor_line_keys_keep_the_crlf_flag(void) {
	editor_init(&e);
	load_text("ab\r\ncd\r\n");
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
	press("iR\r");
	assert_line(&e, 0, "a");
	assert_line(&e, 1, "b");
	assert_line(&e, 2, "cd");
	press("\x7f");
	assert_line(&e, 0, "ab");
	press("RR");
	del();
	assert_line(&e, 0, "abcd");
	TEST_ASSERT_EQUAL_UINT(1, editor_line_count(&e));
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
	TEST_ASSERT_TRUE(e.modified);
}

/** @brief Many splits and joins in a row (the line array grows and shrinks) end with the original text. */
static void test_editor_many_splits_and_joins_round_trip(void) {
	insert_at("abc", 0);
	for (int i = 0; i < 40; i++) press("\r");
	TEST_ASSERT_EQUAL_UINT(41, editor_line_count(&e));
	for (int i = 0; i < 40; i++) press("\x7f");
	TEST_ASSERT_EQUAL_UINT(1, editor_line_count(&e));
	assert_line(&e, 0, "abc");
	assert_cursor(0, 0);
	for (int i = 0; i < 40; i++) press("\r");
	press("U");
	for (int i = 0; i < 40; i++) del(); /* one join, "abc" erased, the rest do nothing on the last line */
	TEST_ASSERT_EQUAL_UINT(40, editor_line_count(&e));
	assert_line(&e, 39, "");
}

/** @brief In normal mode h, j, k, l and the arrows move and ask for a redraw. */
static void test_editor_hjkl_and_arrows_work_in_normal_mode(void) {
	editor_init(&e);
	append("abc");
	append("def");
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, 'l'));
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, 'j'));
	assert_cursor(1, 1);
	press("hk");
	assert_cursor(0, 0);
	press("RD");
	assert_cursor(1, 1);
	press("LU");
	assert_cursor(0, 0);
	TEST_ASSERT_EQUAL_INT(0, editor_handle_key(&e, 'x'));
}

/** @brief The arrows move in insert mode and ask for a redraw. */
static void test_editor_arrows_work_in_insert_mode(void) {
	editor_init(&e);
	append("abc");
	append("def");
	press("i");
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, KEY_RIGHT));
	press("D");
	assert_cursor(1, 1);
	press("LU");
	assert_cursor(0, 0);
}

/** @brief In insert mode right goes to the end of the line (Vim: A-like, Right at the end stays) and left comes back from it. */
static void test_editor_insert_right_reaches_the_end_of_the_line(void) {
	editor_init(&e);
	append("abc");
	press("iRRR");
	assert_cursor(0, 3);
	TEST_ASSERT_EQUAL_UINT(3, e.wantcol);
	press("R");
	assert_cursor(0, 3);
	TEST_ASSERT_EQUAL_UINT(3, e.wantcol);
	press("L");
	assert_cursor(0, 2);
	TEST_ASSERT_EQUAL_UINT(2, e.wantcol);
}

/** @brief Left at column 0 and right on an empty line do not move in insert mode. */
static void test_editor_insert_left_at_0_and_right_on_an_empty_line(void) {
	editor_init(&e);
	append("");
	press("iLRL");
	assert_cursor(0, 0);
	TEST_ASSERT_EQUAL_UINT(0, e.wantcol);
}

/** @brief A horizontal move in insert mode sets the wanted column, so a vertical one follows it. */
static void test_editor_insert_horizontal_move_sets_the_wanted_column(void) {
	editor_init(&e);
	append("abcdef");
	append("abcdefgh");
	append("abcdefgh");
	press("iRRRRRRDRD"); /* end of line 0 (column 6), down to column 6, right, down: Vim puts the cursor at column 7 */
	assert_cursor(2, 7);
	TEST_ASSERT_EQUAL_UINT(7, e.wantcol);
}

/** @brief Down onto a shorter line goes to its end in insert mode, and up comes back to the wanted column (Vim: A on abcdef, Down gives abX). */
static void test_editor_insert_down_onto_a_shorter_line_goes_to_its_end(void) {
	editor_init(&e);
	append("abcdef");
	append("ab");
	append("abcdef");
	press("iRRRRRRD");
	assert_cursor(1, 2);
	TEST_ASSERT_EQUAL_UINT(6, e.wantcol); /* a vertical move keeps it */
	press("D");
	assert_cursor(2, 6); /* the end of the long line */
	press("UU");
	assert_cursor(0, 6);
}

/** @brief Up and down through an empty line in insert mode keep the wanted column. */
static void test_editor_insert_vertical_through_an_empty_line(void) {
	editor_init(&e);
	append("abcdef");
	append("");
	append("abcdef");
	press("iRRRD");
	assert_cursor(1, 0);
	press("D");
	assert_cursor(2, 3);
	press("UU");
	assert_cursor(0, 3);
}

/** @brief A wanted column past a shorter line puts the cursor on its end, but a column inside the last character picks it. */
static void test_editor_insert_vertical_onto_a_line_ending_in_a_tab(void) {
	editor_init(&e);
	append("abcdefghijkl");
	append("a\t"); /* tab: columns 1 to 7, the end of the line is column 8 */
	press("iRRRRRRRD"); /* column 7 */
	assert_cursor(1, 1);
	press("U");
	assert_cursor(0, 7);
	press("R");
	press("D"); /* column 8 */
	assert_cursor(1, 2);
	press("U");
	assert_cursor(0, 8);
}

/** @brief The end of a line with a multi-byte character is at its display column, not its byte length (Vim: aé, A, Down onto aéb gives aéXb). */
static void test_editor_insert_vertical_with_multibyte_text(void) {
	editor_init(&e);
	append("a\xc3\xa9");
	append("a\xc3\xa9" "b");
	press("iRR"); /* end of line 0: byte 3, column 2 */
	assert_cursor(0, 3);
	TEST_ASSERT_EQUAL_UINT(2, e.wantcol);
	press("D");
	assert_cursor(1, 3); /* before b */
	press("L");
	assert_cursor(1, 1);
	press("U");
	assert_cursor(0, 1);
}

/** @brief Esc after insert-mode moves keeps the wanted column (Vim: 4l, i, Left, Esc, j goes to column 2). */
static void test_editor_esc_after_insert_moves_sets_the_wanted_column(void) {
	editor_init(&e);
	append("abcdef");
	append("abcdef");
	press("llll"); /* column 4 */
	press("iLE");
	assert_cursor(0, 2);
	press("D");
	assert_cursor(1, 2);
}

/** @brief Esc from the end of a long line then down past a short one comes back to column 5 (Vim: A, Esc, j, j). */
static void test_editor_esc_from_the_end_then_vertical(void) {
	editor_init(&e);
	append("abcdef");
	append("ab");
	append("abcdef");
	press("iRRRRRRE");
	assert_cursor(0, 5);
	press("jj");
	assert_cursor(2, 5);
}

/** @brief The status line shows INSERT in insert mode and NORMAL again after Esc. */
static void test_editor_status_shows_insert(void) {
	editor_init(&e);
	append("abc");
	char out[128];
	press("i");
	editor_status(&e, 40, out, sizeof(out));
	TEST_ASSERT_EQUAL_STRING("[No Name] INSERT" "                     " "1,1", out);
	press("E");
	editor_status(&e, 40, out, sizeof(out));
	TEST_ASSERT_EQUAL_STRING("[No Name] NORMAL" "                     " "1,1", out);
}

/** @brief The status column and the drawn cursor sit after the last character in insert mode. */
static void test_editor_insert_end_of_line_is_drawn_after_the_text(void) {
	editor_init(&e);
	append("abc");
	press("iRRR");
	char out[256];
	editor_status(&e, 40, out, sizeof(out));
	TEST_ASSERT_EQUAL_STRING("[No Name] INSERT" "                     " "1,4", out);
	char buf[512];
	editor_draw_text(&e, 5, 40, buf, sizeof(buf));
	TEST_ASSERT_NOT_NULL(strstr(buf, "\x1b[1;4H\x1b[?25h"));
}

/** @brief Loading a file or freeing the editor goes back to normal mode. */
static void test_editor_free_and_load_return_to_normal_mode(void) {
	editor_init(&e);
	press("i");
	editor_free(&e);
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_NORMAL, e.mode);
	press("i");
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, "no-such-dir/none.txt"));
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_NORMAL, e.mode);
}

/** @brief A CRLF file shows [dos] after its name, before the mode label. */
static void test_editor_status_shows_dos_for_a_crlf_file(void) {
	editor_init(&e);
	const char *path = tmpdir_write("dos.txt", "a\r\nb\r\n");
	TEST_ASSERT_NOT_NULL(path);
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_INT(1, e.crlf);
	char out[1024], want[1024];
	snprintf(want, sizeof(want), "%s [dos] NORMAL", path);
	size_t n = editor_status(&e, 100, out, sizeof(out));
	TEST_ASSERT_EQUAL_UINT(100, n);
	TEST_ASSERT_EQUAL_INT(0, strncmp(want, out, strlen(want)));
	TEST_ASSERT_EQUAL_STRING("1,1", out + 97);
}

/** @brief An LF file, a mixed file and an editor without a file show no [dos]. */
static void test_editor_status_shows_no_dos_for_other_files(void) {
	editor_init(&e);
	char out[1024];
	editor_status(&e, 40, out, sizeof(out));
	TEST_ASSERT_NULL(strstr(out, "[dos]"));
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, tmpdir_write("lf.txt", "a\nb\n")));
	editor_status(&e, 200, out, sizeof(out));
	TEST_ASSERT_NULL(strstr(out, "[dos]"));
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, tmpdir_write("mixed.txt", "a\r\nb\n")));
	editor_status(&e, 200, out, sizeof(out));
	TEST_ASSERT_NULL(strstr(out, "[dos]"));
}

/** @brief [dos] with a UTF-8 name and a mark in it, then cut with the rest of the left part: never inside a character or a mark. */
static void test_editor_status_dos_with_a_utf8_name_and_truncation(void) {
	editor_init(&e);
	name_it("\xc3\xa9\x01.txt");
	e.crlf = 1; /* as after loading a CRLF file (loading resets it) */
	/* the left part: e-acute (1 column), ^A (2), ".txt" (4), " [dos]" (6), " NORMAL" (7) = 20 columns */
	assert_status(30, "\xc3\xa9^A.txt [dos] NORMAL" "       " "1,1");
	assert_status(16, "\xc3\xa9^A.txt [dos" " " "1,1");  /* cut inside [dos]: 12 columns */
	assert_status(11, "\xc3\xa9^A.txt" " " "1,1");
	assert_status(10, "\xc3\xa9^A.tx" " " "1,1");
	assert_status(7, "\xc3\xa9^A" " " "1,1");
	assert_status(6, "\xc3\xa9" "  " "1,1");              /* the mark would need 2 columns and 1 is left */
}

/* ---- drawing with the status line (H5.1) ---- */

/** @brief Draw the shared editor on a terminal of @p rows by @p cols into a roomy buffer and compare with the literal @p expected. */
static void assert_screen_literal(size_t rows, size_t cols, const char *expected) {
	char out[2048];
	TEST_ASSERT_EQUAL_UINT(strlen(expected), editor_draw_screen(&e, rows, cols, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/** @brief The exact bytes: text rows, erase of the unused text rows, then the status line in reverse video, then the cursor. */
static void test_editor_draw_screen_exact_bytes(void) {
	editor_init(&e);
	name_it("f.txt");
	append("ab");
	append("cd");
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_screen_literal(4, 20, "\x1b[?25l\x1b[H" "ab\x1b[K\r\n" "cd\x1b[K" "\x1b[3;1H\x1b[J"
	                      "\x1b[4;1H\x1b[7m" "f.txt NORMAL" "     " "2,2" "\x1b[m" "\x1b[2;2H\x1b[?25h");
}

/** @brief The erase of the unused rows comes before the status line: ESC[J from the first free row would wipe it otherwise. */
static void test_editor_draw_screen_erases_before_it_draws_the_status(void) {
	editor_init(&e);
	append("ab");
	char out[2048];
	editor_draw_screen(&e, 5, 20, out, sizeof(out));
	const char *erase = strstr(out, "\x1b[2;1H\x1b[J");
	const char *bar = strstr(out, "\x1b[5;1H\x1b[7m");
	TEST_ASSERT_NOT_NULL(erase);
	TEST_ASSERT_NOT_NULL(bar);
	TEST_ASSERT_TRUE(erase < bar);
	TEST_ASSERT_NULL_MESSAGE(strstr(bar, "\x1b[J"), "an erase after the status line would wipe it");
}

/** @brief A full text window has no erase of the free rows, and the status line still has its move. */
static void test_editor_draw_screen_with_a_full_text_window(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	append("ef");
	assert_screen_literal(3, 20, "\x1b[?25l\x1b[H" "ab\x1b[K\r\n" "cd\x1b[K"
	                      "\x1b[3;1H\x1b[7m" "[No Name] NORMAL 1,1" "\x1b[m" "\x1b[1;1H\x1b[?25h");
}

/** @brief An empty editor: erase from the first row, status on the last, cursor 1;1. */
static void test_editor_draw_screen_of_an_empty_editor(void) {
	editor_init(&e);
	assert_screen_literal(3, 20, "\x1b[?25l\x1b[H" "\x1b[1;1H\x1b[J"
	                      "\x1b[3;1H\x1b[7m" "[No Name] NORMAL 1,1" "\x1b[m" "\x1b[1;1H\x1b[?25h");
}

/** @brief Two rows: one text row and the status line. */
static void test_editor_draw_screen_of_two_rows(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	assert_screen_literal(2, 20, "\x1b[?25l\x1b[H" "ab\x1b[K"
	                      "\x1b[2;1H\x1b[7m" "[No Name] NORMAL 1,1" "\x1b[m" "\x1b[1;1H\x1b[?25h");
}

/** @brief One row or none: no status line, the output of editor_draw_text(). */
static void test_editor_draw_screen_has_no_status_below_two_rows(void) {
	editor_init(&e);
	append("ab");
	assert_screen_literal(1, 20, "\x1b[?25l\x1b[H" "ab\x1b[K" "\x1b[1;1H\x1b[?25h");
	assert_screen_literal(0, 20, "\x1b[?25l\x1b[H" "\x1b[1;1H\x1b[?25h"); /* as editor_draw_text() with no rows: nothing drawn, nothing erased */
}

/** @brief The status takes the whole width and has no ESC[K, at every width (the escape would erase its last character on a terminal that wraps late). */
static void test_editor_draw_screen_status_is_the_whole_width_without_erase(void) {
	editor_init(&e);
	name_it("a\x01\xc3\xa9\xe2\x82\xac.txt");
	append("x");
	for (size_t cols = 1; cols <= 40; cols++) {
		char out[2048], want[1024], msg[64];
		snprintf(msg, sizeof(msg), "width %zu", cols);
		editor_status(&e, cols, want, sizeof(want));
		editor_draw_screen(&e, 3, cols, out, sizeof(out));
		const char *start = strstr(out, "\x1b[3;1H\x1b[7m");
		TEST_ASSERT_NOT_NULL_MESSAGE(start, msg);
		start += strlen("\x1b[3;1H\x1b[7m");
		const char *end = strstr(start, "\x1b[m");
		TEST_ASSERT_NOT_NULL_MESSAGE(end, msg);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(strlen(want), (size_t)(end - start), msg);
		TEST_ASSERT_EQUAL_INT_MESSAGE(0, strncmp(want, start, strlen(want)), msg);
		TEST_ASSERT_NULL_MESSAGE(memchr(start, '\x1b', (size_t)(end - start)), msg);
	}
}

/** @brief The cursor is never put on the status line: below the text window of an editor that was not scrolled (a caller bug) it stays on the last text row. */
static void test_editor_draw_screen_clamps_the_cursor_row_when_not_scrolled(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	append("ef");
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_screen_literal(3, 20, "\x1b[?25l\x1b[H" "ab\x1b[K\r\n" "cd\x1b[K"
	                      "\x1b[3;1H\x1b[7m" "[No Name] NORMAL 3,1" "\x1b[m" "\x1b[2;1H\x1b[?25h");
}

/** @brief A scrolled window: the rows from rowoff, the cursor row relative to it, the status shows the real line. */
static void test_editor_draw_screen_of_a_scrolled_window(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	append("ef");
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_scroll(&e, editor_text_rows(3));
	assert_screen_literal(3, 20, "\x1b[?25l\x1b[H" "cd\x1b[K\r\n" "ef\x1b[K"
	                      "\x1b[3;1H\x1b[7m" "[No Name] NORMAL 3,1" "\x1b[m" "\x1b[2;1H\x1b[?25h");
}

/** @brief A full-width text row keeps having no ESC[K, and the status still comes after it. */
static void test_editor_draw_screen_with_a_full_width_row(void) {
	editor_init(&e);
	append("abcd");
	assert_screen_literal(3, 4, "\x1b[?25l\x1b[H" "abcd" "\x1b[2;1H\x1b[J"
	                      "\x1b[3;1H\x1b[7m" " 1,1" "\x1b[m" "\x1b[1;1H\x1b[?25h");
}

/** @brief The draw is never a clear-screen, and a name or a text with escape sequences is shown, not sent. */
static void test_editor_draw_screen_never_clears_or_sends_escapes_from_the_name(void) {
	editor_init(&e);
	name_it("a\x1b[2Jb");
	append("\xc2\x9b[2Jc");
	char out[2048];
	editor_draw_screen(&e, 5, 40, out, sizeof(out));
	TEST_ASSERT_NULL(strstr(out, "\x1b[2J"));
	TEST_ASSERT_NULL(strstr(out, "\xc2\x9b"));
	TEST_ASSERT_NOT_NULL(strstr(out, "\x1b[7m" "a^[[2Jb NORMAL"));
}

/** @brief The status line is reserved first: at every buffer size it is drawn whole or not at all, and the text rows keep the room that remains. */
static void test_editor_draw_screen_keeps_what_fits_at_every_buffer_size(void) {
	const char *rows[] = { "abc", "de", "", "fghi" };
	editor_init(&e);
	for (size_t i = 0; i < 4; i++) append(rows[i]);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT); /* line 2, column 2 */
	char status[64];
	editor_status(&e, 10, status, sizeof(status));
	TEST_ASSERT_EQUAL_UINT(10, strlen(status));
	size_t status_bytes = strlen("\x1b[24;1H\x1b[7m") + strlen(status) + strlen("\x1b[m");
	size_t bytes[5] = { 0 };
	char text[5][64];
	for (size_t k = 0; k <= 4; k++) {
		text[k][0] = '\0';
		for (size_t i = 0; i < k; i++) {
			if (i > 0) strcat(text[k], "\r\n");
			strcat(text[k], rows[i]);
		}
		if (k > 0) bytes[k] = bytes[k - 1] + strlen(rows[k - 1]) + 3 + (k > 1 ? 2 : 0);
	}
	for (size_t size = EDITOR_DRAW_OVERHEAD; size <= EDITOR_DRAW_OVERHEAD + status_bytes + bytes[4] + 8; size++) {
		int fits = size - EDITOR_DRAW_OVERHEAD >= status_bytes;
		size_t room = size - EDITOR_DRAW_OVERHEAD - (fits ? status_bytes : 0);
		size_t k = 0;
		while (k < 4 && bytes[k + 1] <= room) k++;
		char wanted[512], out[512], msg[96];
		draw_expected_status(wanted, sizeof(wanted), k ? text[k] : NULL, 23, 10, fits ? status : NULL, 24, 2, 2);
		memset(out, 'x', sizeof(out));
		size_t n = editor_draw_screen(&e, 24, 10, out, size);
		snprintf(msg, sizeof(msg), "out_size %zu: expected the first %zu rows, status %s", size, k, fits ? "drawn" : "left out");
		TEST_ASSERT_TRUE_MESSAGE(n < size, "wrote past the buffer");
		TEST_ASSERT_EQUAL_STRING_MESSAGE(wanted, out, msg);
		TEST_ASSERT_EQUAL_UINT(strlen(out), n);
	}
}

/** @brief A buffer smaller than EDITOR_DRAW_OVERHEAD gets nothing, with a status line too. */
static void test_editor_draw_screen_buffer_too_small_writes_nothing(void) {
	editor_init(&e);
	append("ab");
	char out[EDITOR_DRAW_OVERHEAD] = "garbage";
	TEST_ASSERT_EQUAL_UINT(0, editor_draw_screen(&e, 24, 20, out, EDITOR_DRAW_OVERHEAD - 1));
	TEST_ASSERT_EQUAL_STRING("", out);
}

/** @brief EDITOR_STATUS_OVERHEAD covers the move to a 20-digit row, ESC[7m and ESC[m. */
static void test_editor_status_overhead_covers_the_status_escapes(void) {
	size_t worst = strlen("\x1b[18446744073709551615;1H") + strlen("\x1b[7m") + strlen("\x1b[m");
	TEST_ASSERT_TRUE_MESSAGE(EDITOR_STATUS_OVERHEAD >= worst, "EDITOR_STATUS_OVERHEAD is smaller than the escapes of the status line");
}

/** @brief A buffer of exactly rows * (cols * 4 + 5) + the two overheads holds a whole screen of full-width 4-byte rows and a status line of 4-byte characters. */
static void test_editor_draw_screen_worst_case_fits_the_documented_size(void) {
	enum { ROWS = 3, COLS = 12 };
	editor_init(&e);
	name_it("\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80"
	        "\xf0\x9f\x98\x80\xf0\x9f\x98\x80.txt");
	char line[COLS * 4 + 1] = "", text[2 * (COLS * 4 + 2) + 1] = "", status[COLS * 4 + 1];
	for (int i = 0; i < COLS; i++) strcat(line, "\xf0\x9f\x98\x80");
	append(line);
	append(line);
	snprintf(text, sizeof(text), "%s\r\n%s", line, line);
	editor_status(&e, COLS, status, sizeof(status));
	char out[ROWS * (COLS * 4 + 5) + EDITOR_DRAW_OVERHEAD + EDITOR_STATUS_OVERHEAD], wanted[1024];
	draw_expected_status(wanted, sizeof(wanted), text, ROWS - 1, COLS, status, ROWS, 1, 1);
	size_t n = editor_draw_screen(&e, ROWS, COLS, out, sizeof(out));
	TEST_ASSERT_TRUE(n < sizeof(out));
	TEST_ASSERT_EQUAL_STRING(wanted, out);
}

/** @brief The worst tail: a terminal of SIZE_MAX rows, so a 20-digit status row and a 20-digit cursor row, fits the buffer of the two overheads and the status text. */
static void test_editor_draw_screen_worst_case_tail_is_complete(void) {
	editor_init(&e);
	append("a");
	e.cy = (size_t)-1 - 2;
	char status[64];
	editor_status(&e, 10, status, sizeof(status));
	char out[EDITOR_DRAW_OVERHEAD + EDITOR_STATUS_OVERHEAD + 10];
	memset(out, 'x', sizeof(out));
	size_t n = editor_draw_screen(&e, (size_t)-1, 10, out, sizeof(out));
	char wanted[256];
	snprintf(wanted, sizeof(wanted), "\x1b[?25l\x1b[H" "\x1b[1;1H\x1b[J" "\x1b[%zu;1H\x1b[7m%s\x1b[m" "\x1b[%zu;1H" "\x1b[?25h",
	         (size_t)-1, status, (size_t)-1 - 1);
	TEST_ASSERT_TRUE(n < sizeof(out));
	TEST_ASSERT_EQUAL_STRING(wanted, out);
}

/** @brief Register every test in this file with Unity. */
/* ---- the command line and the message (H4.1, first half) ---- */

/** @brief Send each byte of @p keys as a key, unchanged (no letter stands for a special key here; bytes above 127 are kept). */
static void type_cmd(const char *keys) {
	for (; *keys; keys++) editor_handle_key(&e, (unsigned char)*keys);
}

/** @brief Assert that the bottom row of the shared editor for @p cols columns is @p expected (padded to the full width). */
static void assert_bottom(size_t cols, const char *expected) {
	char out[1024];
	size_t n = editor_bottom_line(&e, cols, out, sizeof(out));
	TEST_ASSERT_EQUAL_STRING(expected, out);
	TEST_ASSERT_EQUAL_UINT(strlen(expected), n);
}

/** @brief ':' in normal mode opens the command line and asks for a redraw; the label says COMMAND; Esc cancels and the cursor stays. */
static void test_editor_colon_opens_the_command_line_and_esc_cancels(void) {
	editor_init(&e);
	append("abc");
	press("l");
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, ':'));
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_COMMAND, e.mode);
	TEST_ASSERT_EQUAL_STRING("COMMAND", editor_mode_label(&e));
	type_cmd("wq");
	TEST_ASSERT_EQUAL_STRING("wq", e.cmd.text);
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, KEY_ESC));
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_NORMAL, e.mode);
	TEST_ASSERT_EQUAL_STRING("", e.cmd.text);
	TEST_ASSERT_NULL(e.msg);
	assert_cursor(0, 1);
	editor_handle_key(&e, ':'); /* a new command line starts empty */
	TEST_ASSERT_EQUAL_STRING("", e.cmd.text);
}

/** @brief Backspace deletes the last character, a whole UTF-8 one; on an empty command line it cancels, as Vim does. */
static void test_editor_command_backspace_deletes_and_cancels_when_empty(void) {
	editor_init(&e);
	type_cmd(":a\xc3\xa9");
	TEST_ASSERT_EQUAL_STRING("a\xc3\xa9", e.cmd.text);
	type_cmd("\x7f");
	TEST_ASSERT_EQUAL_STRING("a", e.cmd.text);
	type_cmd("\x08"); /* Ctrl+H is Backspace too */
	TEST_ASSERT_EQUAL_STRING("", e.cmd.text);
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_COMMAND, e.mode);
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, 0x7f));
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_NORMAL, e.mode);
	TEST_ASSERT_NULL(e.msg);
}

/** @brief A UTF-8 character is typed once all its bytes are in; a half one followed by something else is dropped. */
static void test_editor_command_utf8_characters_are_whole(void) {
	editor_init(&e);
	editor_handle_key(&e, ':');
	TEST_ASSERT_EQUAL_INT(0, editor_handle_key(&e, 0xe2));
	TEST_ASSERT_EQUAL_INT(0, editor_handle_key(&e, 0x82));
	TEST_ASSERT_EQUAL_STRING("", e.cmd.text);
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, 0xac));
	TEST_ASSERT_EQUAL_STRING("\xe2\x82\xac", e.cmd.text);
	type_cmd("\xc3"); /* half, then a letter: dropped */
	type_cmd("b");
	TEST_ASSERT_EQUAL_STRING("\xe2\x82\xac" "b", e.cmd.text);
	type_cmd("\x80"); /* a stray continuation byte */
	TEST_ASSERT_EQUAL_STRING("\xe2\x82\xac" "b", e.cmd.text);
}

/** @brief Arrows, Delete, Tab and other control keys do nothing in command mode, and the cursor of the text does not move. */
static void test_editor_command_ignores_other_keys(void) {
	editor_init(&e);
	append("abc");
	append("def");
	editor_handle_key(&e, ':');
	type_cmd("a");
	press("UDLR");
	editor_handle_key(&e, KEY_DELETE);
	type_cmd("\t\x01");
	TEST_ASSERT_EQUAL_STRING("a", e.cmd.text);
	TEST_ASSERT_EQUAL_INT(0, editor_handle_key(&e, KEY_UP));
	assert_cursor(0, 0);
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_COMMAND, e.mode);
}

/** @brief The command line holds CMDLINE_MAX bytes; more keys are dropped and the mode stays. */
static void test_editor_command_line_is_bounded(void) {
	editor_init(&e);
	editor_handle_key(&e, ':');
	for (int i = 0; i < CMDLINE_MAX + 20; i++) editor_handle_key(&e, 'a');
	TEST_ASSERT_EQUAL_UINT(CMDLINE_MAX, e.cmd.len);
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_COMMAND, e.mode);
}

/** @brief Enter on an empty command line (or only spaces and colons) returns to normal mode and shows nothing. */
static void test_editor_enter_on_an_empty_command_does_nothing(void) {
	editor_init(&e);
	type_cmd(":\r");
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_NORMAL, e.mode);
	TEST_ASSERT_NULL(e.msg);
	type_cmd(": :  \n");
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_NORMAL, e.mode);
	TEST_ASSERT_NULL(e.msg);
	TEST_ASSERT_EQUAL_STRING("", e.cmd.text);
}

/** @brief Every non-empty command that is not known shows E492 with the text as typed, and the mode goes back to normal. */
static void test_editor_every_other_command_shows_e492_for_now(void) {
	const char *cmds[] = { "zz", "writ", "qq", "wqa", "xx", "foo!" };
	for (size_t i = 0; i < sizeof(cmds) / sizeof(*cmds); i++) {
		editor_init(&e);
		append("t");
		editor_handle_key(&e, ':');
		type_cmd(cmds[i]);
		TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, '\r'));
		char want[CMDLINE_MAX + 64];
		snprintf(want, sizeof(want), "E492: Not an editor command: %s", cmds[i]);
		TEST_ASSERT_EQUAL_INT(EDITOR_MODE_NORMAL, e.mode);
		TEST_ASSERT_NOT_NULL(e.msg);
		TEST_ASSERT_EQUAL_STRING(want, e.msg);
		TEST_ASSERT_EQUAL_STRING("", e.cmd.text);
		TEST_ASSERT_EQUAL_INT(0, e.modified);
		editor_free(&e);
	}
}

/** @brief The next key, whatever it is, clears the message and asks for a redraw, even a key that does nothing. */
static void test_editor_the_next_key_clears_the_message(void) {
	editor_init(&e);
	type_cmd(":zz\r");
	TEST_ASSERT_NOT_NULL(e.msg);
	TEST_ASSERT_NOT_EQUAL(0, editor_handle_key(&e, 'z')); /* unknown in normal mode: ignored, but the message goes */
	TEST_ASSERT_NULL(e.msg);
	TEST_ASSERT_EQUAL_INT(0, editor_handle_key(&e, 'z')); /* no message now: nothing to redraw */
	type_cmd(":zz\r");
	editor_handle_key(&e, ':'); /* the key that clears it can start a new command line */
	TEST_ASSERT_NULL(e.msg);
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_COMMAND, e.mode);
	editor_handle_key(&e, KEY_ESC);
	TEST_ASSERT_EQUAL_INT(0, editor_set_message(&e, "m"));
	editor_handle_key(&e, KEY_LEFT);
	TEST_ASSERT_NULL(e.msg);
}

/** @brief A new message replaces the old one; free drops message and command line. */
static void test_editor_set_message_replaces_and_free_drops(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_set_message(&e, "one"));
	TEST_ASSERT_EQUAL_INT(0, editor_set_message(&e, "two"));
	TEST_ASSERT_EQUAL_STRING("two", e.msg);
	type_cmd(":ab");
	editor_free(&e);
	TEST_ASSERT_NULL(e.msg);
	TEST_ASSERT_EQUAL_STRING("", e.cmd.text);
}

/** @brief ':' in insert mode types a colon: there is no command line there. */
static void test_editor_colon_in_insert_mode_is_a_colon(void) {
	editor_init(&e);
	append("ab");
	press("i");
	type_cmd(":");
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_INSERT, e.mode);
	assert_line(&e, 0, ":ab");
	TEST_ASSERT_EQUAL_INT(1, e.modified);
}

/** @brief The bottom row in command mode is ':' and the text, padded to the width; cut at the width. */
static void test_editor_bottom_line_in_command_mode(void) {
	editor_init(&e);
	type_cmd(":wq");
	assert_bottom(10, ":wq       ");
	assert_bottom(3, ":wq");
	assert_bottom(2, ":w");
	assert_bottom(0, "");
	type_cmd("\xc3\xa9");
	assert_bottom(6, ":wq\xc3\xa9  ");
}

/** @brief The bottom row with a message: the message, cut and padded, marks as marks, in place of the status line. */
static void test_editor_bottom_line_with_a_message(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_set_message(&e, "a\x01" "b\xff" "c"));
	assert_bottom(8, "a^Ab?c  ");
	assert_bottom(5, "a^Ab?");
	assert_bottom(4, "a^Ab");
	assert_bottom(3, "a^A");
	assert_bottom(2, "a "); /* the mark needs 2 columns and 1 is left */
	assert_bottom(1, "a");
}

/** @brief Without command line or message the bottom row is the status line. */
static void test_editor_bottom_line_is_the_status_by_default(void) {
	editor_init(&e);
	char want[64], got[64];
	editor_status(&e, 20, want, sizeof(want));
	editor_bottom_line(&e, 20, got, sizeof(got));
	TEST_ASSERT_EQUAL_STRING(want, got);
	TEST_ASSERT_EQUAL_UINT(0, editor_bottom_line(&e, 20, got, 0));
}

/** @brief Command mode draws ':' and the text in plain video on the last row, and the cursor sits at the end of the text there. */
static void test_editor_draw_screen_in_command_mode(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	type_cmd(":wq");
	assert_screen_literal(4, 20, "\x1b[?25l\x1b[H" "ab\x1b[K\r\n" "cd\x1b[K" "\x1b[3;1H\x1b[J"
	                      "\x1b[4;1H" ":wq" "                 " "\x1b[4;4H\x1b[?25h");
}

/** @brief The status line is back after Esc; a text cursor that was moved is where it was. */
static void test_editor_draw_screen_after_cancel_has_the_status_again(void) {
	editor_init(&e);
	append("ab");
	type_cmd(":wq");
	editor_handle_key(&e, KEY_ESC);
	assert_screen_literal(3, 20, "\x1b[?25l\x1b[H" "ab\x1b[K" "\x1b[2;1H\x1b[J"
	                      "\x1b[3;1H\x1b[7m" "[No Name] NORMAL" " " "1,1" "\x1b[m" "\x1b[1;1H\x1b[?25h");
}

/** @brief A command line wider than the terminal is cut at the width and the cursor stays on the last column. */
static void test_editor_draw_screen_command_wider_than_the_terminal(void) {
	editor_init(&e);
	type_cmd(":abcdefg");
	assert_screen_literal(2, 5, "\x1b[?25l\x1b[H" "\x1b[1;1H\x1b[J" "\x1b[2;1H" ":abcd" "\x1b[2;5H\x1b[?25h");
	editor_init(&e);
	type_cmd(":abcd"); /* exactly the width: no erase, the cursor on the last column */
	assert_screen_literal(2, 5, "\x1b[?25l\x1b[H" "\x1b[1;1H\x1b[J" "\x1b[2;1H" ":abcd" "\x1b[2;5H\x1b[?25h");
}

/** @brief The row of a message is plain video, full width, drawn instead of the status line, and the cursor stays in the text. */
static void test_editor_draw_screen_with_a_message(void) {
	editor_init(&e);
	append("ab");
	TEST_ASSERT_EQUAL_INT(0, editor_set_message(&e, "E492: Not an editor command: x"));
	assert_screen_literal(3, 40, "\x1b[?25l\x1b[H" "ab\x1b[K" "\x1b[2;1H\x1b[J"
	                      "\x1b[3;1H" "E492: Not an editor command: x" "          " "\x1b[1;1H\x1b[?25h");
	assert_screen_literal(3, 20, "\x1b[?25l\x1b[H" "ab\x1b[K" "\x1b[2;1H\x1b[J"
	                      "\x1b[3;1H" "E492: Not an editor " "\x1b[1;1H\x1b[?25h");
}

/** @brief A message goes through the render rules: marks are marks, never cut, UTF-8 counts by columns, and no raw byte of it reaches the terminal. */
static void test_editor_draw_screen_message_marks_and_utf8(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, editor_set_message(&e, "\x1b[2J\xc3\xa9\xc3\xa9"));
	assert_screen_literal(2, 6, "\x1b[?25l\x1b[H" "\x1b[1;1H\x1b[J"
	                      "\x1b[2;1H" "^[[2J\xc3\xa9" "\x1b[1;1H\x1b[?25h");
	assert_screen_literal(2, 7, "\x1b[?25l\x1b[H" "\x1b[1;1H\x1b[J"
	                      "\x1b[2;1H" "^[[2J\xc3\xa9\xc3\xa9" "\x1b[1;1H\x1b[?25h");
	assert_screen_literal(2, 3, "\x1b[?25l\x1b[H" "\x1b[1;1H\x1b[J"
	                      "\x1b[2;1H" "^[[" "\x1b[1;1H\x1b[?25h");
	assert_screen_literal(2, 1, "\x1b[?25l\x1b[H" "\x1b[1;1H\x1b[J"
	                      "\x1b[2;1H" " " "\x1b[1;1H\x1b[?25h"); /* the mark needs 2 columns */
}

/** @brief A terminal of one row has no bottom row: command mode and a message draw as the text only, with the cursor in the text. */
static void test_editor_draw_screen_one_row_has_no_bottom_row(void) {
	editor_init(&e);
	append("ab");
	type_cmd(":x");
	char out[512], want[512];
	editor_draw_screen(&e, 1, 20, out, sizeof(out));
	editor_draw_text(&e, 1, 20, want, sizeof(want));
	TEST_ASSERT_EQUAL_STRING(want, out);
}

/** @brief A resize in command mode: the same state drawn for another size keeps the command line and its cursor. */
static void test_editor_resize_in_command_mode_keeps_the_command_line(void) {
	editor_init(&e);
	append("ab");
	type_cmd(":wq");
	assert_screen_literal(3, 10, "\x1b[?25l\x1b[H" "ab\x1b[K" "\x1b[2;1H\x1b[J" "\x1b[3;1H" ":wq       " "\x1b[3;4H\x1b[?25h");
	assert_screen_literal(2, 3, "\x1b[?25l\x1b[H" "ab\x1b[K" "\x1b[2;1H" ":wq" "\x1b[2;3H\x1b[?25h");
}

/** @brief Bytes of the command line are bounded by the draw buffer rule: a screen of the documented size holds a full-width command row. */
static void test_editor_draw_screen_command_row_fits_the_documented_buffer(void) {
	editor_init(&e);
	type_cmd(":\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac");
	size_t rows = 3, cols = 4;
	size_t size = rows * (cols * 4 + 5) + EDITOR_DRAW_OVERHEAD + EDITOR_STATUS_OVERHEAD;
	char out[1024];
	TEST_ASSERT_TRUE(size <= sizeof(out));
	size_t n = editor_draw_screen(&e, rows, cols, out, size);
	TEST_ASSERT_NOT_NULL(strstr(out, "\x1b[3;1H:\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\x1b[3;4H\x1b[?25h"));
	TEST_ASSERT_EQUAL_UINT(strlen(out), n);
}

/** @brief Send @p key @p n times, scrolling to a window of @p cols columns after each, as main does. */
static void key_scroll(int key, int n, size_t cols) {
	for (int i = 0; i < n; i++) {
		editor_handle_key(&e, key);
		editor_scroll_cols(&e, cols);
	}
}

/** @brief Assert that editor_render() of one row of @p cols columns gives @p expected. */
static void assert_render1(size_t cols, const char *expected) {
	char out[256];
	editor_render(&e, 1, cols, out, sizeof(out));
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/** @brief Moving right over a long line scrolls one column at a time, and moving left scrolls back only when the cursor leaves the window. */
static void test_editor_hscroll_right_and_left_over_a_long_line(void) {
	editor_init(&e);
	append("0123456789abcdefghij");
	key_scroll('l', 4, 5);
	TEST_ASSERT_EQUAL_UINT(0, e.coloff);
	key_scroll('l', 1, 5);
	TEST_ASSERT_EQUAL_UINT(1, e.coloff);
	assert_render1(5, "12345");
	key_scroll('l', 14, 5);
	TEST_ASSERT_EQUAL_UINT(15, e.coloff);
	assert_render1(5, "fghij");
	key_scroll('h', 4, 5);
	TEST_ASSERT_EQUAL_UINT(15, e.coloff);
	key_scroll('h', 1, 5);
	TEST_ASSERT_EQUAL_UINT(14, e.coloff);
	key_scroll('h', 14, 5);
	TEST_ASSERT_EQUAL_UINT(0, e.coloff);
}

/** @brief Going up or down onto a shorter line brings the view back to the cursor. */
static void test_editor_hscroll_vertical_move_to_a_short_line(void) {
	editor_init(&e);
	append("0123456789abcdefghij");
	append("ab");
	key_scroll('l', 19, 5);
	TEST_ASSERT_EQUAL_UINT(15, e.coloff);
	key_scroll('j', 1, 5);
	TEST_ASSERT_EQUAL_UINT(1, e.cx);
	TEST_ASSERT_EQUAL_UINT(1, e.coloff);
	e.rowoff = 1;
	assert_render1(5, "b");
}

/** @brief A tab or a mark that straddles the left edge never shows cut: the tab shows the spaces it has left, a mark one space. */
static void test_editor_hscroll_tab_and_mark_at_the_left_edge(void) {
	editor_init(&e);
	append("\tab");
	e.coloff = 5;
	assert_render1(10, "   ab");
	e.coloff = 8;
	assert_render1(10, "ab");
	editor_free(&e);
	append("a\x01" "bc");
	e.coloff = 1;
	assert_render1(10, "^Abc");
	e.coloff = 2;
	assert_render1(10, " bc");
	e.coloff = 3;
	assert_render1(10, "bc");
}

/** @brief A tab straddling the left edge is cut on the right too, and a mark at the right edge is dropped, never half shown. */
static void test_editor_hscroll_cuts_at_the_right_edge(void) {
	editor_init(&e);
	append("\tab");
	e.coloff = 5;
	assert_render1(2, "  ");
	editor_free(&e);
	append("ab\x01" "cd");
	assert_render1(3, "ab");
	e.coloff = 1;
	assert_render1(3, "b^A");
	assert_render1(2, "b");
}

/** @brief UTF-8 characters are never cut at the left edge, and a multibyte cursor character scrolls into view whole. */
static void test_editor_hscroll_utf8(void) {
	editor_init(&e);
	append("\xc3\xa9\xc3\xa0\xc3\xbc\xe2\x82\xac" "z"); /* e acute, a grave, u diaeresis, euro, z */
	key_scroll('l', 3, 2);
	TEST_ASSERT_EQUAL_UINT(2, e.coloff);
	assert_render1(2, "\xc3\xbc\xe2\x82\xac");
	e.coloff = 1;
	assert_render1(3, "\xc3\xa0\xc3\xbc\xe2\x82\xac");
}

/** @brief A mark under the cursor must fit whole: the view moves until it does; a tab needs only its first column. */
static void test_editor_hscroll_mark_under_the_cursor_fits_whole(void) {
	editor_init(&e);
	append("ab\x01" "cd");
	key_scroll('l', 2, 3);
	TEST_ASSERT_EQUAL_UINT(1, e.coloff);
	assert_render1(3, "b^A");
	editor_free(&e);
	append("abc\t" "d");
	key_scroll('l', 3, 4);
	TEST_ASSERT_EQUAL_UINT(0, e.coloff);
	assert_render1(4, "abc ");
}

/** @brief In insert mode the cursor after the last character needs a column of its own. */
static void test_editor_hscroll_cursor_after_the_end_of_the_line(void) {
	editor_init(&e);
	append("abcd");
	press("i");
	key_scroll(KEY_RIGHT, 4, 4);
	TEST_ASSERT_EQUAL_UINT(4, e.cx);
	TEST_ASSERT_EQUAL_UINT(1, e.coloff);
	assert_render1(4, "bcd");
	char out[256];
	editor_draw_text(&e, 3, 4, out, sizeof(out));
	TEST_ASSERT_NOT_NULL(strstr(out, "bcd\x1b[K"));
	TEST_ASSERT_NOT_NULL(strstr(out, "\x1b[1;4H"));
}

/** @brief Typing at the right edge scrolls when the window is scrolled after each key. */
static void test_editor_hscroll_typing_at_the_right_edge(void) {
	editor_init(&e);
	press("i");
	for (const char *c = "abcdef"; *c; c++) key_scroll(*c, 1, 4);
	assert_line(&e, 0, "abcdef");
	TEST_ASSERT_EQUAL_UINT(3, e.coloff);
	assert_render1(4, "def");
	editor_free(&e);
	press("i");
	for (const char *c = "\xc3\xa9\xc3\xa9\xc3\xa9"; *c; c++) key_scroll((unsigned char)*c, 1, 2);
	TEST_ASSERT_EQUAL_UINT(2, e.coloff);
	assert_render1(2, "\xc3\xa9");
}

/** @brief After a resize the view follows: a narrower window moves it, a wider one leaves it, and 0 columns do nothing. */
static void test_editor_hscroll_resize(void) {
	editor_init(&e);
	append("0123456789");
	key_scroll('l', 7, 10);
	TEST_ASSERT_EQUAL_UINT(0, e.coloff);
	editor_scroll_cols(&e, 4);
	TEST_ASSERT_EQUAL_UINT(4, e.coloff);
	editor_scroll_cols(&e, 20);
	TEST_ASSERT_EQUAL_UINT(4, e.coloff);
	editor_scroll_cols(&e, 0);
	TEST_ASSERT_EQUAL_UINT(4, e.coloff);
	editor_free(&e);
	editor_scroll_cols(&e, 5); /* no lines: nothing to scroll to */
	TEST_ASSERT_EQUAL_UINT(0, e.coloff);
}

/** @brief The exact bytes of a draw from a scrolled view: the cursor column is relative to coloff, a full row has no erase. */
static void test_editor_hscroll_draw_exact_bytes(void) {
	editor_init(&e);
	append("0123456789");
	key_scroll('l', 7, 5);
	assert_draw_literal(3, 5, "\x1b[?25l\x1b[H" "34567" "\x1b[2;1H\x1b[J" "\x1b[1;5H" "\x1b[?25h");
	e.coloff = 3; /* the cursor inside the window: its column is relative to coloff, not clamped */
	e.cx = 5;
	assert_draw_literal(3, 10, "\x1b[?25l\x1b[H" "3456789\x1b[K" "\x1b[2;1H\x1b[J" "\x1b[1;3H" "\x1b[?25h");
	editor_free(&e);
	append("\tab");
	e.coloff = 5; /* a straddling tab: its spaces, then the erase, as the row is narrower than the window */
	assert_draw_literal(3, 10, "\x1b[?25l\x1b[H" "   ab\x1b[K" "\x1b[2;1H\x1b[J" "\x1b[1;1H" "\x1b[?25h");
}

/** @brief The status line keeps the absolute column of the cursor while the view is scrolled. */
static void test_editor_hscroll_status_shows_the_absolute_position(void) {
	editor_init(&e);
	append("0123456789");
	key_scroll('l', 7, 5);
	TEST_ASSERT_EQUAL_UINT(3, e.coloff);
	char out[128];
	editor_status(&e, 20, out, sizeof(out));
	TEST_ASSERT_EQUAL_STRING("[No Name] NORMAL 1,8", out);
}

/** @brief A screen drawn from the middle of a line of 4-byte characters fits the documented buffer size, whole. */
static void test_editor_hscroll_draw_screen_fits_the_documented_buffer(void) {
	editor_init(&e);
	append("\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80");
	e.coloff = 2;
	size_t rows = 2, cols = 5;
	size_t size = rows * (cols * 4 + 5) + EDITOR_DRAW_OVERHEAD + EDITOR_STATUS_OVERHEAD;
	char *out = malloc(size);
	TEST_ASSERT_NOT_NULL(out);
	size_t n = editor_draw_screen(&e, rows, cols, out, size);
	int ok = strstr(out, "\x1b[H" "\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80\xf0\x9f\x98\x80" "\x1b[2;1H") != NULL;
	free(out);
	TEST_ASSERT_TRUE(ok);
	TEST_ASSERT_TRUE(n > 0);
}

/** @brief Free and load put the view back at column 0. */
static void test_editor_hscroll_is_reset_by_init_free_and_load(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_UINT(0, e.coloff);
	e.coloff = 5;
	editor_free(&e);
	TEST_ASSERT_EQUAL_UINT(0, e.coloff);
	const char *path = tmpdir_write("wide.txt", "0123456789\n");
	TEST_ASSERT_NOT_NULL(path);
	append("0123456789");
	e.coloff = 5;
	TEST_ASSERT_EQUAL_INT(0, editor_load_file(&e, path));
	TEST_ASSERT_EQUAL_UINT(0, e.coloff);
}

void test_editor_suite(void) {
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
	RUN_TEST(test_editor_render_clips_lines_to_max_cols);
	RUN_TEST(test_editor_render_zero_cols_keeps_the_rows);
	RUN_TEST(test_editor_draw_clips_lines_and_clamps_the_cursor_column);
	RUN_TEST(test_editor_render_shows_escape_as_a_mark);
	RUN_TEST(test_editor_render_shows_other_control_bytes_as_marks);
	RUN_TEST(test_editor_render_leaves_high_bytes_alone);
	RUN_TEST(test_editor_render_clips_on_whole_marks);
	RUN_TEST(test_editor_draw_cursor_column_counts_marks);
	RUN_TEST(test_editor_render_expands_tabs_to_the_next_tab_stop);
	RUN_TEST(test_editor_render_tab_after_a_mark);
	RUN_TEST(test_editor_render_clips_a_tab);
	RUN_TEST(test_editor_draw_cursor_column_with_tabs);
	RUN_TEST(test_editor_draw_cursor_column_after_a_tab_in_the_middle);
	RUN_TEST(test_editor_load_three_lines);
	RUN_TEST(test_editor_load_no_trailing_newline);
	RUN_TEST(test_editor_load_empty_file);
	RUN_TEST(test_editor_load_blank_line_in_the_middle);
	RUN_TEST(test_editor_load_twice_replaces_contents);
	RUN_TEST(test_editor_load_missing_file_is_empty_and_not_created);
	RUN_TEST(test_editor_load_missing_file_discards_old_contents);
	RUN_TEST(test_editor_load_directory_fails_with_eisdir);
	RUN_TEST(test_editor_append_line_fails_cleanly_when_the_copy_cannot_be_allocated);
	RUN_TEST(test_editor_append_line_frees_the_copy_when_the_array_cannot_grow);
	RUN_TEST(test_editor_load_fails_with_enomem_when_the_path_cannot_be_copied);
	RUN_TEST(test_editor_load_frees_everything_when_a_line_cannot_be_stored);
	RUN_TEST(test_editor_load_unreadable_file_fails_with_eacces);
	RUN_TEST(test_editor_load_refuses_a_nul_byte_in_a_line);
	RUN_TEST(test_editor_load_refuses_a_nul_after_good_lines);
	RUN_TEST(test_editor_load_refuses_a_nul_in_the_last_unterminated_line);
	RUN_TEST(test_editor_load_refuses_a_file_that_is_only_nul);
	RUN_TEST(test_editor_load_refuses_a_nul_at_the_start_of_a_line);
	RUN_TEST(test_editor_cursor_starts_at_origin);
	RUN_TEST(test_editor_cursor_does_not_move_without_lines);
	RUN_TEST(test_editor_cursor_right_stops_on_last_character);
	RUN_TEST(test_editor_cursor_left_stops_at_column_zero);
	RUN_TEST(test_editor_cursor_vertical_moves_stop_at_the_ends);
	RUN_TEST(test_editor_cursor_down_clamps_column);
	RUN_TEST(test_editor_cursor_up_clamps_column);
	RUN_TEST(test_editor_cursor_on_empty_line_and_no_remembered_column);
	RUN_TEST(test_editor_load_resets_the_cursor);
	RUN_TEST(test_editor_draw_empty_editor);
	RUN_TEST(test_editor_draw_lines_and_cursor);
	RUN_TEST(test_editor_draw_limits_rows);
	RUN_TEST(test_editor_draw_cursor_below_visible_rows);
	RUN_TEST(test_editor_draw_buffer_too_small_writes_nothing);
	RUN_TEST(test_editor_draw_drops_a_row_that_does_not_fit_but_keeps_the_tail);
	RUN_TEST(test_editor_draw_keeps_exactly_the_rows_that_fit_at_every_buffer_size);
	RUN_TEST(test_editor_draw_worst_case_cursor_fits_in_the_overhead);
	RUN_TEST(test_editor_draw_overhead_covers_the_worst_case_tail);
	RUN_TEST(test_editor_draw_exact_bytes);
	RUN_TEST(test_editor_draw_empty_editor_exact_bytes);
	RUN_TEST(test_editor_draw_row_of_exactly_max_cols_has_no_erase);
	RUN_TEST(test_editor_draw_row_one_column_short_has_the_erase);
	RUN_TEST(test_editor_draw_clipped_row_has_no_erase);
	RUN_TEST(test_editor_draw_row_short_by_a_mark_has_the_erase);
	RUN_TEST(test_editor_draw_full_screen_has_no_erase_of_the_rest);
	RUN_TEST(test_editor_draw_blank_row_exact_bytes);
	RUN_TEST(test_editor_draw_blank_lines_are_rows);
	RUN_TEST(test_editor_draw_zero_rows_draws_no_rows);
	RUN_TEST(test_editor_draw_never_clears_the_screen);
	RUN_TEST(test_editor_scroll_keeps_offset_while_cursor_is_visible);
	RUN_TEST(test_editor_scroll_down_by_one);
	RUN_TEST(test_editor_scroll_down_far);
	RUN_TEST(test_editor_scroll_up);
	RUN_TEST(test_editor_scroll_window_edges_are_visible);
	RUN_TEST(test_editor_scroll_does_nothing_without_rows_or_lines);
	RUN_TEST(test_editor_load_resets_the_scroll_offset);
	RUN_TEST(test_editor_render_starts_at_the_scroll_offset);
	RUN_TEST(test_editor_render_offset_past_the_end_is_empty);
	RUN_TEST(test_editor_draw_cursor_row_is_relative_to_the_offset);
	RUN_TEST(test_editor_draw_cursor_above_the_window_is_row_one);
	RUN_TEST(test_editor_render_clips_utf8_by_characters);
	RUN_TEST(test_editor_render_clips_a_long_utf8_line_to_80_columns);
	RUN_TEST(test_editor_render_shows_invalid_bytes_as_question_marks);
	RUN_TEST(test_editor_render_shows_each_byte_of_an_invalid_sequence_as_a_question_mark);
	RUN_TEST(test_editor_render_shows_a_truncated_sequence_as_question_marks);
	RUN_TEST(test_editor_render_shows_c1_controls_as_question_marks);
	RUN_TEST(test_editor_render_copies_characters_next_to_the_c1_range);
	RUN_TEST(test_editor_render_clips_question_marks_by_column);
	RUN_TEST(test_editor_render_mixes_utf8_with_marks_and_tabs);
	RUN_TEST(test_editor_draw_cursor_column_counts_characters);
	RUN_TEST(test_editor_draw_cursor_column_counts_invalid_bytes_and_c1);
	RUN_TEST(test_editor_draw_cursor_column_mixes_widths);
	RUN_TEST(test_editor_draw_cursor_column_is_clamped_to_the_width_in_characters);
	RUN_TEST(test_editor_draw_full_screen_of_multibyte_text_is_complete);
	RUN_TEST(test_editor_cursor_right_moves_by_characters);
	RUN_TEST(test_editor_cursor_left_moves_by_characters);
	RUN_TEST(test_editor_cursor_right_never_lands_inside_a_character);
	RUN_TEST(test_editor_cursor_visits_each_invalid_byte);
	RUN_TEST(test_editor_cursor_moves_over_a_c1_control_as_one_character);
	RUN_TEST(test_editor_cursor_down_clamps_to_the_start_of_the_last_character);
	RUN_TEST(test_editor_cursor_up_clamps_to_the_start_of_the_last_character);
	RUN_TEST(test_editor_cursor_vertical_clamp_uses_invalid_bytes_and_c1_as_characters);
	RUN_TEST(test_editor_cursor_vertical_move_never_lands_inside_a_character);
	RUN_TEST(test_editor_load_crlf_file_drops_the_cr_and_sets_crlf);
	RUN_TEST(test_editor_load_lf_file_is_not_crlf);
	RUN_TEST(test_editor_load_empty_and_missing_files_are_not_crlf);
	RUN_TEST(test_editor_load_only_crlf_is_one_empty_line);
	RUN_TEST(test_editor_load_crlf_blank_lines);
	RUN_TEST(test_editor_load_crlf_removes_exactly_one_cr_per_line);
	RUN_TEST(test_editor_load_mixed_crlf_then_lf_keeps_the_cr);
	RUN_TEST(test_editor_load_mixed_lf_then_crlf_keeps_the_cr);
	RUN_TEST(test_editor_load_one_lf_line_among_crlf_lines_is_mixed);
	RUN_TEST(test_editor_load_crlf_with_an_unterminated_last_line);
	RUN_TEST(test_editor_load_crlf_keeps_a_cr_on_the_unterminated_last_line);
	RUN_TEST(test_editor_load_unterminated_line_alone_is_not_crlf);
	RUN_TEST(test_editor_load_crlf_keeps_utf8_text);
	RUN_TEST(test_editor_load_a_late_lf_line_makes_a_big_crlf_file_mixed);
	RUN_TEST(test_editor_load_a_big_crlf_file_loses_every_cr);
	RUN_TEST(test_editor_load_crlf_after_a_very_long_line);
	RUN_TEST(test_editor_load_crlf_with_cr_and_lf_ending_the_first_block);
	RUN_TEST(test_editor_load_crlf_with_cr_and_lf_split_across_blocks);
	RUN_TEST(test_editor_load_crlf_with_cr_and_lf_split_across_8k_blocks);
	RUN_TEST(test_editor_load_mixed_after_a_very_long_line_keeps_the_cr);
	RUN_TEST(test_editor_load_a_blank_lf_line_among_crlf_lines_is_mixed);
	RUN_TEST(test_editor_load_a_file_starting_with_a_blank_lf_line_is_mixed);
	RUN_TEST(test_editor_load_only_lf_is_one_empty_line_and_not_crlf);
	RUN_TEST(test_editor_load_crlf_cursor_stops_on_the_last_character);
	RUN_TEST(test_editor_load_crlf_flag_follows_the_file);
	RUN_TEST(test_editor_load_crlf_flag_is_cleared_by_other_loads);
	RUN_TEST(test_editor_load_crlf_flag_is_cleared_by_a_failed_load);
	RUN_TEST(test_editor_load_refuses_a_nul_in_a_crlf_file);
	RUN_TEST(test_editor_load_refuses_a_nul_in_the_last_line_of_a_crlf_file);
	RUN_TEST(test_editor_init_and_free_reset_crlf);
	RUN_TEST(test_editor_render_crlf_file_has_no_marks_and_mixed_file_has);
	RUN_TEST(test_editor_render_does_not_swallow_a_control_byte_after_a_lead_byte);
	RUN_TEST(test_editor_render_tab_after_question_mark_cells);
	RUN_TEST(test_editor_draw_cursor_column_after_question_mark_cells_and_a_tab);
	RUN_TEST(test_editor_cursor_stays_put_on_an_empty_line_and_at_the_start);
	RUN_TEST(test_editor_draw_cursor_on_the_last_four_byte_character_at_the_width);
	RUN_TEST(test_editor_wanted_column_comes_back_after_a_short_line);
	RUN_TEST(test_editor_wanted_column_survives_an_empty_line);
	RUN_TEST(test_editor_wanted_column_far_beyond_short_lines_lands_on_the_last_character);
	RUN_TEST(test_editor_wanted_column_at_the_edge_of_a_short_line);
	RUN_TEST(test_editor_left_sets_the_wanted_column);
	RUN_TEST(test_editor_right_that_does_not_move_keeps_the_wanted_column);
	RUN_TEST(test_editor_left_that_does_not_move_keeps_the_wanted_column);
	RUN_TEST(test_editor_left_and_right_start_from_the_real_cursor_after_a_vertical_move);
	RUN_TEST(test_editor_up_on_the_first_line_keeps_the_wanted_column);
	RUN_TEST(test_editor_down_on_the_last_line_keeps_the_wanted_column);
	RUN_TEST(test_editor_horizontal_moves_set_the_wanted_column_in_display_columns);
	RUN_TEST(test_editor_vertical_moves_keep_the_wanted_column);
	RUN_TEST(test_editor_wanted_column_inside_a_tab_picks_the_tab);
	RUN_TEST(test_editor_wanted_column_inside_a_tab_in_the_middle_of_a_line);
	RUN_TEST(test_editor_wanted_column_from_a_tab_is_its_first_column);
	RUN_TEST(test_editor_wanted_column_on_a_mark_picks_the_mark);
	RUN_TEST(test_editor_wanted_column_from_a_mark_is_its_first_column);
	RUN_TEST(test_editor_wanted_column_with_multibyte_characters);
	RUN_TEST(test_editor_wanted_column_after_question_mark_cells);
	RUN_TEST(test_editor_wanted_column_is_reset_by_init_free_and_load);
	RUN_TEST(test_editor_a_loaded_file_starts_with_no_wanted_column);
	RUN_TEST(test_editor_a_huge_wanted_column_lands_on_the_last_character);
	RUN_TEST(test_editor_wanted_column_beyond_a_line_that_ends_in_a_tab);
	RUN_TEST(test_editor_wanted_column_beyond_a_line_that_ends_in_a_mark);
	RUN_TEST(test_editor_wanted_column_beyond_a_line_that_ends_in_a_four_byte_character);
	RUN_TEST(test_editor_wanted_column_zero_goes_to_the_start_of_the_line);
	RUN_TEST(test_editor_init_has_no_path);
	RUN_TEST(test_editor_load_remembers_the_path_as_a_copy);
	RUN_TEST(test_editor_load_of_a_missing_file_keeps_the_path);
	RUN_TEST(test_editor_load_again_replaces_the_path);
	RUN_TEST(test_editor_failed_load_leaves_no_path);
	RUN_TEST(test_editor_free_drops_the_path);
	RUN_TEST(test_editor_text_rows_reserves_the_status_row);
	RUN_TEST(test_editor_status_without_a_name);
	RUN_TEST(test_editor_status_shows_the_path);
	RUN_TEST(test_editor_status_shows_line_and_column);
	RUN_TEST(test_editor_status_with_a_two_digit_line);
	RUN_TEST(test_editor_status_column_is_not_clipped);
	RUN_TEST(test_editor_status_position_ends_in_the_last_column);
	RUN_TEST(test_editor_status_column_after_a_tab);
	RUN_TEST(test_editor_status_column_on_a_tab);
	RUN_TEST(test_editor_status_column_after_a_mark);
	RUN_TEST(test_editor_status_column_counts_characters);
	RUN_TEST(test_editor_status_column_counts_invalid_bytes_and_c1);
	RUN_TEST(test_editor_status_has_the_mode_label_after_the_name);
	RUN_TEST(test_editor_status_shows_marks_in_the_name);
	RUN_TEST(test_editor_status_shows_a_tab_in_the_name_as_spaces);
	RUN_TEST(test_editor_status_shows_a_utf8_name);
	RUN_TEST(test_editor_status_shows_invalid_bytes_and_c1_in_the_name_as_question_marks);
	RUN_TEST(test_editor_status_never_cuts_a_mark);
	RUN_TEST(test_editor_status_truncates_at_every_width);
	RUN_TEST(test_editor_status_on_a_narrow_terminal);
	RUN_TEST(test_editor_status_is_exactly_the_width);
	RUN_TEST(test_editor_status_into_a_small_buffer);
	RUN_TEST(test_editor_draw_screen_exact_bytes);
	RUN_TEST(test_editor_draw_screen_erases_before_it_draws_the_status);
	RUN_TEST(test_editor_draw_screen_with_a_full_text_window);
	RUN_TEST(test_editor_draw_screen_of_an_empty_editor);
	RUN_TEST(test_editor_draw_screen_of_two_rows);
	RUN_TEST(test_editor_draw_screen_has_no_status_below_two_rows);
	RUN_TEST(test_editor_draw_screen_status_is_the_whole_width_without_erase);
	RUN_TEST(test_editor_draw_screen_clamps_the_cursor_row_when_not_scrolled);
	RUN_TEST(test_editor_draw_screen_of_a_scrolled_window);
	RUN_TEST(test_editor_draw_screen_with_a_full_width_row);
	RUN_TEST(test_editor_draw_screen_never_clears_or_sends_escapes_from_the_name);
	RUN_TEST(test_editor_draw_screen_keeps_what_fits_at_every_buffer_size);
	RUN_TEST(test_editor_draw_screen_buffer_too_small_writes_nothing);
	RUN_TEST(test_editor_status_overhead_covers_the_status_escapes);
	RUN_TEST(test_editor_draw_screen_worst_case_fits_the_documented_size);
	RUN_TEST(test_editor_draw_screen_worst_case_tail_is_complete);
	RUN_TEST(test_editor_load_of_its_own_path_works);
	RUN_TEST(test_editor_status_of_an_empty_path_is_no_name);
	RUN_TEST(test_editor_mode_label_is_normal);
	RUN_TEST(test_editor_i_enters_insert_mode_at_the_cursor);
	RUN_TEST(test_editor_i_on_an_empty_editor);
	RUN_TEST(test_editor_esc_in_normal_mode_does_nothing);
	RUN_TEST(test_editor_esc_at_column_0_stays);
	RUN_TEST(test_editor_esc_in_the_middle_moves_left);
	RUN_TEST(test_editor_esc_after_the_last_character_lands_on_it);
	RUN_TEST(test_editor_esc_on_an_empty_line);
	RUN_TEST(test_editor_esc_steps_over_a_multibyte_character);
	RUN_TEST(test_editor_esc_at_column_0_sets_the_wanted_column);
	RUN_TEST(test_editor_hjkl_are_typed_in_insert_mode);
	RUN_TEST(test_editor_typing_inserts_at_the_cursor);
	RUN_TEST(test_editor_typing_on_an_empty_line);
	RUN_TEST(test_editor_typing_in_an_empty_editor);
	RUN_TEST(test_editor_typing_a_multibyte_character);
	RUN_TEST(test_editor_typing_drops_invalid_bytes);
	RUN_TEST(test_editor_an_arrow_drops_half_a_character);
	RUN_TEST(test_editor_tab_inserts_a_tab);
	RUN_TEST(test_editor_control_keys_are_ignored_when_typing);
	RUN_TEST(test_editor_typing_a_very_long_line);
	RUN_TEST(test_editor_enter_splits_the_line);
	RUN_TEST(test_editor_line_feed_splits_like_carriage_return);
	RUN_TEST(test_editor_enter_in_the_middle_of_a_text);
	RUN_TEST(test_editor_enter_in_an_empty_editor);
	RUN_TEST(test_editor_enter_with_multibyte_text_and_wantcol);
	RUN_TEST(test_editor_enter_at_the_bottom_of_a_window_scrolls);
	RUN_TEST(test_editor_backspace_deletes_the_previous_character);
	RUN_TEST(test_editor_backspace_deletes_a_whole_multibyte_character);
	RUN_TEST(test_editor_backspace_after_a_tab);
	RUN_TEST(test_editor_backspace_at_the_start_of_a_line_joins);
	RUN_TEST(test_editor_join_sets_wantcol_from_the_display_column);
	RUN_TEST(test_editor_join_with_empty_lines_and_at_the_end);
	RUN_TEST(test_editor_join_keeps_utf8_and_terminates_the_line);
	RUN_TEST(test_editor_backspace_at_the_start_of_the_buffer_does_nothing);
	RUN_TEST(test_editor_delete_removes_the_character_under_the_cursor);
	RUN_TEST(test_editor_delete_at_the_end_of_a_line_joins);
	RUN_TEST(test_editor_delete_with_empty_lines_and_at_the_end);
	RUN_TEST(test_editor_editing_keys_do_nothing_in_normal_mode);
	RUN_TEST(test_editor_backspace_drops_a_half_typed_character);
	RUN_TEST(test_editor_line_keys_keep_the_crlf_flag);
	RUN_TEST(test_editor_many_splits_and_joins_round_trip);
	RUN_TEST(test_editor_modified_is_set_by_typing_and_cleared_by_load);
	RUN_TEST(test_editor_typing_keeps_the_crlf_flag);
	RUN_TEST(test_editor_status_shows_the_modified_mark);
	RUN_TEST(test_editor_typing_at_the_bottom_of_a_window_scrolls);
	RUN_TEST(test_editor_hjkl_and_arrows_work_in_normal_mode);
	RUN_TEST(test_editor_arrows_work_in_insert_mode);
	RUN_TEST(test_editor_insert_right_reaches_the_end_of_the_line);
	RUN_TEST(test_editor_insert_left_at_0_and_right_on_an_empty_line);
	RUN_TEST(test_editor_insert_horizontal_move_sets_the_wanted_column);
	RUN_TEST(test_editor_insert_down_onto_a_shorter_line_goes_to_its_end);
	RUN_TEST(test_editor_insert_vertical_through_an_empty_line);
	RUN_TEST(test_editor_insert_vertical_onto_a_line_ending_in_a_tab);
	RUN_TEST(test_editor_insert_vertical_with_multibyte_text);
	RUN_TEST(test_editor_esc_after_insert_moves_sets_the_wanted_column);
	RUN_TEST(test_editor_esc_from_the_end_then_vertical);
	RUN_TEST(test_editor_status_shows_insert);
	RUN_TEST(test_editor_insert_end_of_line_is_drawn_after_the_text);
	RUN_TEST(test_editor_free_and_load_return_to_normal_mode);
	RUN_TEST(test_editor_status_shows_dos_for_a_crlf_file);
	RUN_TEST(test_editor_status_shows_no_dos_for_other_files);
	RUN_TEST(test_editor_status_dos_with_a_utf8_name_and_truncation);
	RUN_TEST(test_editor_status_column_on_a_mark);
	RUN_TEST(test_editor_colon_opens_the_command_line_and_esc_cancels);
	RUN_TEST(test_editor_command_backspace_deletes_and_cancels_when_empty);
	RUN_TEST(test_editor_command_utf8_characters_are_whole);
	RUN_TEST(test_editor_command_ignores_other_keys);
	RUN_TEST(test_editor_command_line_is_bounded);
	RUN_TEST(test_editor_enter_on_an_empty_command_does_nothing);
	RUN_TEST(test_editor_every_other_command_shows_e492_for_now);
	RUN_TEST(test_editor_the_next_key_clears_the_message);
	RUN_TEST(test_editor_set_message_replaces_and_free_drops);
	RUN_TEST(test_editor_colon_in_insert_mode_is_a_colon);
	RUN_TEST(test_editor_bottom_line_in_command_mode);
	RUN_TEST(test_editor_bottom_line_with_a_message);
	RUN_TEST(test_editor_bottom_line_is_the_status_by_default);
	RUN_TEST(test_editor_draw_screen_in_command_mode);
	RUN_TEST(test_editor_draw_screen_after_cancel_has_the_status_again);
	RUN_TEST(test_editor_draw_screen_command_wider_than_the_terminal);
	RUN_TEST(test_editor_draw_screen_with_a_message);
	RUN_TEST(test_editor_draw_screen_message_marks_and_utf8);
	RUN_TEST(test_editor_draw_screen_one_row_has_no_bottom_row);
	RUN_TEST(test_editor_resize_in_command_mode_keeps_the_command_line);
	RUN_TEST(test_editor_draw_screen_command_row_fits_the_documented_buffer);
	RUN_TEST(test_editor_hscroll_right_and_left_over_a_long_line);
	RUN_TEST(test_editor_hscroll_vertical_move_to_a_short_line);
	RUN_TEST(test_editor_hscroll_tab_and_mark_at_the_left_edge);
	RUN_TEST(test_editor_hscroll_cuts_at_the_right_edge);
	RUN_TEST(test_editor_hscroll_utf8);
	RUN_TEST(test_editor_hscroll_mark_under_the_cursor_fits_whole);
	RUN_TEST(test_editor_hscroll_cursor_after_the_end_of_the_line);
	RUN_TEST(test_editor_hscroll_typing_at_the_right_edge);
	RUN_TEST(test_editor_hscroll_resize);
	RUN_TEST(test_editor_hscroll_draw_exact_bytes);
	RUN_TEST(test_editor_hscroll_status_shows_the_absolute_position);
	RUN_TEST(test_editor_hscroll_draw_screen_fits_the_documented_buffer);
	RUN_TEST(test_editor_hscroll_is_reset_by_init_free_and_load);
}
