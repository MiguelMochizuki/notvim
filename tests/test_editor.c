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
	assert_draw(24, CLEAR_HOME "^Abc" "\x1b[1;3H");
	e.cx = 0; /* on the mark itself: its first column */
	assert_draw(24, CLEAR_HOME "^Abc" "\x1b[1;1H");
	e.cx = 2;
	assert_draw(24, CLEAR_HOME "^Abc" "\x1b[1;4H");
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
	assert_draw(24, CLEAR_HOME "        x" "\x1b[1;1H");
	e.cx = 1;
	assert_draw(24, CLEAR_HOME "        x" "\x1b[1;9H");
}

/** @brief After a tab that starts past column 0 the cursor is at the next tab stop, not 8 columns further. */
static void test_editor_draw_cursor_column_after_a_tab_in_the_middle(void) {
	editor_init(&e);
	append("ab\tc");
	e.cx = 3; /* the c: "ab" is 2 columns, the tab goes to column 8 */
	assert_draw(24, CLEAR_HOME "ab      c" "\x1b[1;9H");
	e.cx = 2; /* the tab itself starts at column 2 */
	assert_draw(24, CLEAR_HOME "ab      c" "\x1b[1;3H");
}

/** @brief Press @p dir @p n times on the shared editor. */
static void move_n(editor_move_t dir, int n) {
	for (int i = 0; i < n; i++) editor_move_cursor(&e, dir);
}

/** @brief Assert that the cursor column of editor_draw() with @p max_cols columns is @p col (1-based) on row 1. */
static void assert_draw_cursor_col(size_t max_cols, size_t col) {
	char out[512], expected[32];
	editor_draw(&e, 24, max_cols, out, sizeof(out));
	snprintf(expected, sizeof(expected), "\x1b[1;%zuH", col);
	const char *tail = out + strlen(out) - strlen(expected);
	TEST_ASSERT_TRUE(tail >= out);
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
 * @brief editor_draw() keeps a whole screen of multi-byte text when the buffer has 4 bytes per column.
 * @note It is the clipping that fails today, not the buffer: the end-to-end tests with 3 and 4-byte
 *       characters in test_notvim.c pin the size of the buffer that main() allocates.
 */
static void test_editor_draw_full_screen_of_multibyte_text_is_complete(void) {
	enum { ROWS = 5, COLS = 20 };
	char line[COLS * 2 + 1] = "", expected[ROWS * (COLS * 2 + 2) + 64] = CLEAR_HOME;
	for (int i = 0; i < COLS; i++) strcat(line, "\xc3\xa9");
	editor_init(&e);
	for (int i = 0; i < ROWS; i++) {
		append(line);
		if (i > 0) strcat(expected, "\r\n");
		strcat(expected, line);
	}
	strcat(expected, "\x1b[1;1H");
	char out[ROWS * (COLS * 4 + 2) + EDITOR_DRAW_OVERHEAD];
	size_t n = editor_draw(&e, ROWS, COLS, out, sizeof(out));
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

/** @brief A vertical move that lands inside a character moves the cursor back to the start of that character. */
static void test_editor_cursor_vertical_move_never_lands_inside_a_character(void) {
	editor_init(&e);
	append("abcdef");
	append("\xc3\xa9\xe2\x82\xac" "z"); /* characters start at 0, 2, 5 */
	editor_move_cursor(&e, EDITOR_MOVE_RIGHT); /* cx 1: inside the e-acute on the next line */
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 0);
	editor_free(&e);
	editor_init(&e);
	append("abcdef");
	append("\xc3\xa9\xe2\x82\xac" "z");
	move_n(EDITOR_MOVE_RIGHT, 3); /* cx 3: inside the euro sign */
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 2);
}

/** @brief Down onto a line where the column is already a character start keeps the byte index. */
static void test_editor_cursor_vertical_move_keeps_a_valid_byte_index(void) {
	editor_init(&e);
	append("\xc3\xa9\xe2\x82\xac" "z");
	append("abcdefgh");
	e.cx = 5; /* the z */
	editor_move_cursor(&e, EDITOR_MOVE_DOWN);
	assert_cursor(1, 5);
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
	RUN_TEST(test_editor_cursor_vertical_move_keeps_a_valid_byte_index);
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
}
