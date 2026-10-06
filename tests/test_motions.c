/**
 * @file test_motions.c
 * @brief Tests of the pure motions (motions.c) and of the normal-mode keys that apply them (editor.c).
 */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "test_motions.h"
#include "editor.h"
#include "motions.h"

/** Editor shared by the tests of this file. */
static editor_t e;

/** @brief Free the shared editor and the one of the table tests; called from tearDown() through test_motions_teardown(). */
void test_motions_teardown(void) {
	editor_free(&e);
}

/** One buffer of the table: its lines. */
typedef struct {
	const char *const *lines;
	size_t n;
} buffer_t;

/** @brief Define the lines of a table buffer. */
#define LINES(...) (const char *const[]){ __VA_ARGS__ }
/** @brief A table buffer from its lines. */
#define BUFFER(...) { LINES(__VA_ARGS__), sizeof(LINES(__VA_ARGS__)) / sizeof(char *) }

/** The buffers of motions_vim.inc, in its order. */
static const buffer_t buffers[] = {
	BUFFER("foo bar  baz", "", "  qux.quux(a, b);", "  ", "end"),
	BUFFER("h\xc3\xa9llo w\xc3\xb6rld \xc3\xbcn\xc3\xaf", "\xe2\x80\x94" "dash" "\xe2\x80\x94" "x", "\xe6\x97\xa5\xe6\x9c\xac \xe8\xaa\x9e" "a", "x\xc3\x97y \xc2\xa1z"),
	BUFFER("a"),
	BUFFER(""),
	BUFFER("", "", "x"),
	BUFFER("foo.bar", "baz...", "a_b-c", "(  )"),
	BUFFER("\tfoo\tbar", "x  "),
	BUFFER("foo", "   "),
	BUFFER("foo", ""),
	BUFFER("  ", "  a"),
	BUFFER("a b", "", "", "c d"),
	BUFFER("\xe3\x81\xb2\xe3\x82\x89\xe3\x82\xab\xe3\x82\xbf\xe6\xbc\xa2\xe5\xad\x97\xed\x95\x9c\xea\xb8\x80" "ab", "a\xe2\x80\xa6" "b \xe2\x9f\xa8" "c\xe2\x9f\xa9 \xe2\x82\xac" "x \xc7\x86\xc5\xad"),
	BUFFER("a\xc2\xa0" "b c", "\xc3\xa9\xe2\x80\x93" "b  ", "\x01" "a\x7f" "b"),
};

/** One row of the table: from where, which key, to where (all from the real Vim). */
typedef struct {
	int buf;
	char key;
	size_t fy, fx, ty, tx;
} row_t;

/** Expected targets taken from the real Vim 9.1. */
static const row_t table[] = {
#include "motions_vim.inc"
};

/** @brief Fill the shared editor with buffer @p b and put the cursor at (@p y, @p x). */
static void load(int b, size_t y, size_t x) {
	editor_free(&e);
	editor_init(&e);
	for (size_t i = 0; i < buffers[b].n; i++) TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, buffers[b].lines[i]));
	e.cy = y;
	e.cx = x;
}

/** @brief The motion for @p key from the cursor of the shared editor. */
static motion_pos_t run_motion(char key) {
	switch (key) {
	case '0': return motion_line_start(&e);
	case '^': return motion_first_nonblank(&e);
	case '$': return motion_line_end(&e);
	case 'w': return motion_word_next(&e, 0);
	case 'W': return motion_word_next(&e, 1);
	case 'b': return motion_word_prev(&e, 0);
	case 'B': return motion_word_prev(&e, 1);
	case 'e': return motion_word_end(&e, 0);
	default: return motion_word_end(&e, 1);
	}
}

/** @brief Failure message naming the table row @p i. */
static const char *where(size_t i, char *buf, size_t size) {
	snprintf(buf, size, "buffer %d, key %c, from %zu,%zu", table[i].buf, table[i].key, table[i].fy, table[i].fx);
	return buf;
}

/** @brief Every motion from every character of every table buffer gives the target of the real Vim, and moves nothing. */
static void test_motions_match_vim_on_every_start(void) {
	char msg[96];
	for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		load(table[i].buf, table[i].fy, table[i].fx);
		motion_pos_t t = run_motion(table[i].key);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(table[i].ty, t.y, where(i, msg, sizeof(msg)));
		TEST_ASSERT_EQUAL_UINT_MESSAGE(table[i].tx, t.x, where(i, msg, sizeof(msg)));
		TEST_ASSERT_EQUAL_UINT_MESSAGE(table[i].fy, e.cy, "a motion must not move the cursor");
		TEST_ASSERT_EQUAL_UINT_MESSAGE(table[i].fx, e.cx, "a motion must not move the cursor");
	}
}

/** @brief An editor with no lines: every motion gives (0, 0). */
static void test_motions_on_an_editor_with_no_lines(void) {
	editor_free(&e);
	editor_init(&e);
	for (const char *k = "0^$wWbBeE"; *k; k++) {
		motion_pos_t t = run_motion(*k);
		TEST_ASSERT_EQUAL_UINT(0, t.y);
		TEST_ASSERT_EQUAL_UINT(0, t.x);
	}
}

/** @brief Apply @p key in the shared editor. @return What editor_handle_key() returned. */
static int press(int key) {
	return editor_handle_key(&e, key);
}

/** @brief The display column (tab stops of 8, a control byte 2 columns, a UTF-8 character 1) of byte @p x of @p line. */
static size_t display_of(const char *line, size_t x) {
	size_t col = 0;
	for (size_t i = 0; i < x; i++) {
		unsigned char c = (unsigned char)line[i];
		if ((c & 0xc0) == 0x80) continue;
		col += c == '\t' ? 8 - col % 8 : (c < 0x20 || c == 0x7f) ? 2 : 1;
	}
	return col;
}

/** @brief Every motion key in normal mode puts the cursor on the Vim target, sets the wanted column from it ("$": the end of the line), and never sets modified. */
static void test_motion_keys_match_vim_and_set_wantcol(void) {
	char msg[96];
	for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		load(table[i].buf, table[i].fy, table[i].fx);
		e.wantcol = 99;
		int redraw = press(table[i].key);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(table[i].ty, e.cy, where(i, msg, sizeof(msg)));
		TEST_ASSERT_EQUAL_UINT_MESSAGE(table[i].tx, e.cx, where(i, msg, sizeof(msg)));
		TEST_ASSERT_EQUAL_INT_MESSAGE(table[i].ty != table[i].fy || table[i].tx != table[i].fx, redraw, where(i, msg, sizeof(msg)));
		TEST_ASSERT_EQUAL_INT_MESSAGE(0, e.modified, where(i, msg, sizeof(msg)));
		const char *line = e.lines[e.cy];
		if (table[i].key == '$') {
			TEST_ASSERT_EQUAL_UINT_MESSAGE(EDITOR_WANTCOL_EOL, e.wantcol, where(i, msg, sizeof(msg)));
		} else if (line[e.cx] != '\t') { /* a tab: the editor wants its first column, Vim its last */
			TEST_ASSERT_EQUAL_UINT_MESSAGE(display_of(line, e.cx), e.wantcol, where(i, msg, sizeof(msg)));
		}
	}
}

/** @brief "$" makes j and k keep to the end of each line, however long; h (a move that moves) ends it. */
static void test_dollar_then_j_and_k_keep_to_the_end_of_the_line(void) {
	editor_init(&e);
	editor_append_line(&e, "abcdef");
	editor_append_line(&e, "ab");
	editor_append_line(&e, "abcdefgh");
	editor_append_line(&e, "");
	editor_append_line(&e, "xyz");
	press('$');
	TEST_ASSERT_EQUAL_UINT(5, e.cx);
	press('j');
	TEST_ASSERT_EQUAL_UINT(1, e.cx);
	press('j');
	TEST_ASSERT_EQUAL_UINT(7, e.cx);
	press('j');
	TEST_ASSERT_EQUAL_UINT(0, e.cx);
	press('j');
	TEST_ASSERT_EQUAL_UINT(2, e.cx);
	press('k');
	press('k');
	TEST_ASSERT_EQUAL_UINT(7, e.cx);
	press('h');
	press('k');
	TEST_ASSERT_EQUAL_UINT(1, e.cx); /* wanted column 6 now: the end of "ab" */
	press('j');
	TEST_ASSERT_EQUAL_UINT(6, e.cx);
}

/** @brief "$" on an empty line also makes j keep to the end; "$" when already at the end only sets the wanted column. */
static void test_dollar_sets_the_wanted_column_even_without_moving(void) {
	editor_init(&e);
	editor_append_line(&e, "abcdef");
	editor_append_line(&e, "abcdef");
	e.cx = 5;
	e.wantcol = 2;
	TEST_ASSERT_EQUAL_INT(0, press('$')); /* nothing to redraw */
	TEST_ASSERT_EQUAL_UINT(EDITOR_WANTCOL_EOL, e.wantcol);
	press('j');
	TEST_ASSERT_EQUAL_UINT(5, e.cx);
}

/** @brief A motion that cannot move (Vim beeps) still takes the wanted column from the cursor, as Vim does. */
static void test_a_motion_that_does_not_move_still_sets_the_wanted_column(void) {
	editor_init(&e);
	editor_append_line(&e, "abcdef");
	editor_append_line(&e, "ab");
	e.wantcol = 5;
	e.cy = 1;
	e.cx = 1;
	TEST_ASSERT_EQUAL_INT(0, press('w')); /* the end of the text */
	TEST_ASSERT_EQUAL_UINT(1, e.cx);
	TEST_ASSERT_EQUAL_UINT(1, e.wantcol);
	press('k');
	TEST_ASSERT_EQUAL_UINT(1, e.cx);
	e.cy = 0;
	e.cx = 0;
	e.wantcol = 4;
	TEST_ASSERT_EQUAL_INT(0, press('b')); /* the start of the text */
	TEST_ASSERT_EQUAL_UINT(0, e.wantcol);
}

/** @brief 0, ^, $, w, b, e (and the capitals) on an editor with no lines change nothing and need no redraw. */
static void test_motion_keys_on_an_empty_editor_do_nothing(void) {
	editor_init(&e);
	for (const char *k = "0^$wWbBeE"; *k; k++) {
		TEST_ASSERT_EQUAL_INT(0, press(*k));
		TEST_ASSERT_EQUAL_UINT(0, e.cy);
		TEST_ASSERT_EQUAL_UINT(0, e.cx);
		TEST_ASSERT_EQUAL_UINT(0, e.count);
		TEST_ASSERT_EQUAL_INT(0, e.modified);
	}
}

/** @brief In insert mode the motion keys type their letters; nothing moves by word. */
static void test_motion_keys_in_insert_mode_type_text(void) {
	editor_init(&e);
	editor_append_line(&e, "ab cd");
	press('i');
	for (const char *k = "w0^$bBeEW"; *k; k++) press(*k);
	TEST_ASSERT_EQUAL_STRING("w0^$bBeEWab cd", e.lines[0]);
	TEST_ASSERT_EQUAL_UINT(9, e.cx);
	TEST_ASSERT_EQUAL_INT(1, e.modified);
}

/** @brief In command mode the motion keys are typed into the command line. */
static void test_motion_keys_in_command_mode_go_to_the_command_line(void) {
	editor_init(&e);
	editor_append_line(&e, "ab cd");
	press(':');
	press('w');
	press('$');
	TEST_ASSERT_EQUAL_STRING("w$", e.cmd.text);
	TEST_ASSERT_EQUAL_UINT(0, e.cx);
}

/** @brief After a word motion over many lines, editor_scroll() brings the cursor line into the window. */
static void test_word_motion_scrolls_down_with_the_cursor(void) {
	editor_init(&e);
	for (int i = 0; i < 10; i++) editor_append_line(&e, "x");
	for (int i = 0; i < 5; i++) {
		press('w');
		editor_scroll(&e, 3);
	}
	TEST_ASSERT_EQUAL_UINT(5, e.cy);
	TEST_ASSERT_EQUAL_UINT(3, e.rowoff);
	for (int i = 0; i < 5; i++) {
		press('b');
		editor_scroll(&e, 3);
	}
	TEST_ASSERT_EQUAL_UINT(0, e.cy);
	TEST_ASSERT_EQUAL_UINT(0, e.rowoff);
}

/** @brief "$" on a long line scrolls it sideways to show the end; "0" scrolls back. */
static void test_dollar_and_zero_scroll_sideways(void) {
	char line[101];
	memset(line, 'a', 100);
	line[100] = '\0';
	editor_init(&e);
	editor_append_line(&e, line);
	press('$');
	editor_scroll_cols(&e, 20);
	TEST_ASSERT_EQUAL_UINT(99, e.cx);
	TEST_ASSERT_EQUAL_UINT(80, e.coloff);
	press('0');
	editor_scroll_cols(&e, 20);
	TEST_ASSERT_EQUAL_UINT(0, e.coloff);
}

/** @brief A motion draws the cursor at its new place and does not touch the text. */
static void test_motion_moves_the_drawn_cursor(void) {
	char out[256];
	editor_init(&e);
	editor_append_line(&e, "foo bar");
	press('w');
	editor_draw_text(&e, 5, 20, out, sizeof(out));
	TEST_ASSERT_NOT_NULL(strstr(out, "foo bar"));
	TEST_ASSERT_NOT_NULL(strstr(out, "\x1b[1;5H"));
}

/** @brief Run the motion tests. */
void test_motions_suite(void) {
	RUN_TEST(test_motions_match_vim_on_every_start);
	RUN_TEST(test_motions_on_an_editor_with_no_lines);
	RUN_TEST(test_motion_keys_match_vim_and_set_wantcol);
	RUN_TEST(test_dollar_then_j_and_k_keep_to_the_end_of_the_line);
	RUN_TEST(test_dollar_sets_the_wanted_column_even_without_moving);
	RUN_TEST(test_a_motion_that_does_not_move_still_sets_the_wanted_column);
	RUN_TEST(test_motion_keys_on_an_empty_editor_do_nothing);
	RUN_TEST(test_motion_keys_in_insert_mode_type_text);
	RUN_TEST(test_motion_keys_in_command_mode_go_to_the_command_line);
	RUN_TEST(test_word_motion_scrolls_down_with_the_cursor);
	RUN_TEST(test_dollar_and_zero_scroll_sideways);
	RUN_TEST(test_motion_moves_the_drawn_cursor);
}
