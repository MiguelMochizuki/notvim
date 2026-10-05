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
/** A width larger than any test line, for tests that are not about clipping. */
#define ALL_COLS ((size_t)-1)

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

/** Clear screen and move home: the start of every editor_draw() output. */
#define CLEAR_HOME "\x1b[H\x1b[2J"

/** @brief Assert that editor_draw() of the shared editor gives @p expected in a roomy buffer. */
static void assert_draw(size_t max_rows, const char *expected) {
	char out[512];
	TEST_ASSERT_EQUAL_UINT(strlen(expected), editor_draw(&e, max_rows, ALL_COLS, out, sizeof(out)));
	TEST_ASSERT_EQUAL_STRING(expected, out);
}

/** @brief Drawing an empty editor clears the screen and puts the cursor at 1;1. */
static void test_editor_draw_empty_editor(void) {
	editor_init(&e);
	assert_draw(24, CLEAR_HOME "\x1b[1;1H");
}

/** @brief Drawing puts the text after the clear and the cursor at its row and column, 1-based. */
static void test_editor_draw_lines_and_cursor(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT);
	assert_draw(24, CLEAR_HOME "ab\r\ncd" "\x1b[2;2H");
}

/** @brief The row limit applies to the drawn text. */
static void test_editor_draw_limits_rows(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	append("ef");
	assert_draw(1, CLEAR_HOME "ab" "\x1b[1;1H");
}

/** @brief A cursor below the visible rows is still reported at its real row. */
static void test_editor_draw_cursor_below_visible_rows(void) {
	editor_init(&e);
	append("ab");
	append("cd");
	append("ef");
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_draw(1, CLEAR_HOME "ab" "\x1b[3;1H");
}

/** @brief A buffer smaller than the overhead gets nothing, not half an escape sequence. */
static void test_editor_draw_buffer_too_small_writes_nothing(void) {
	editor_init(&e);
	append("ab");
	char out[EDITOR_DRAW_OVERHEAD] = "garbage";
	TEST_ASSERT_EQUAL_UINT(0, editor_draw(&e, 24, ALL_COLS, out, EDITOR_DRAW_OVERHEAD - 1));
	TEST_ASSERT_EQUAL_STRING("", out);
	char untouched[1] = { 'x' };
	TEST_ASSERT_EQUAL_UINT(0, editor_draw(&e, 24, ALL_COLS, untouched, 0));
	TEST_ASSERT_EQUAL_INT('x', untouched[0]);
}

/** @brief With little spare room the text is cut but the cursor sequence stays whole. */
static void test_editor_draw_cuts_text_but_keeps_cursor_sequence(void) {
	editor_init(&e);
	append("abcdef");
	char out[EDITOR_DRAW_OVERHEAD + 3];
	size_t n = editor_draw(&e, 24, ALL_COLS, out, sizeof(out));
	const char *expected = CLEAR_HOME "abc" "\x1b[1;1H";
	TEST_ASSERT_EQUAL_UINT(strlen(expected), n);
	TEST_ASSERT_EQUAL_STRING(expected, out);
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
	assert_draw(2, CLEAR_HOME "c\r\nd" "\x1b[2;1H");
}

/** @brief A cursor above the window is drawn on row 1 instead of wrapping around. */
static void test_editor_draw_cursor_above_the_window_is_row_one(void) {
	editor_init(&e);
	append_letters(5);
	e.rowoff = 3;
	e.cy = 1;
	assert_draw(2, CLEAR_HOME "d\r\ne" "\x1b[1;1H");
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
	char out[512];
	const char *expected = CLEAR_HOME "abc" "\x1b[1;3H";
	TEST_ASSERT_EQUAL_UINT(strlen(expected), editor_draw(&e, 24, 3, out, sizeof(out)));
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
	assert_draw(24, CLEAR_HOME "^Abc" "\x1b[1;3H");
	e.cx = 0; /* on the mark itself: its first column */
	assert_draw(24, CLEAR_HOME "^Abc" "\x1b[1;1H");
	e.cx = 2;
	assert_draw(24, CLEAR_HOME "^Abc" "\x1b[1;4H");
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
	RUN_TEST(test_editor_render_clips_lines_to_max_cols);
	RUN_TEST(test_editor_render_zero_cols_keeps_the_rows);
	RUN_TEST(test_editor_draw_clips_lines_and_clamps_the_cursor_column);
	RUN_TEST(test_editor_render_shows_escape_as_a_mark);
	RUN_TEST(test_editor_render_shows_other_control_bytes_as_marks);
	RUN_TEST(test_editor_render_leaves_high_bytes_alone);
	RUN_TEST(test_editor_render_clips_on_whole_marks);
	RUN_TEST(test_editor_draw_cursor_column_counts_marks);
	RUN_TEST(test_editor_load_three_lines);
	RUN_TEST(test_editor_load_no_trailing_newline);
	RUN_TEST(test_editor_load_empty_file);
	RUN_TEST(test_editor_load_blank_line_in_the_middle);
	RUN_TEST(test_editor_load_twice_replaces_contents);
	RUN_TEST(test_editor_load_missing_file_is_empty_and_not_created);
	RUN_TEST(test_editor_load_missing_file_discards_old_contents);
	RUN_TEST(test_editor_load_directory_fails_with_eisdir);
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
	RUN_TEST(test_editor_draw_cuts_text_but_keeps_cursor_sequence);
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
}
