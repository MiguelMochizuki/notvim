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
#include "keys.h"

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

/** @brief Fill the shared editor with buffer @p b of @p bufs and put the cursor at (@p y, @p x). */
static void load_from(const buffer_t *bufs, int b, size_t y, size_t x) {
	editor_free(&e);
	editor_init(&e);
	for (size_t i = 0; i < bufs[b].n; i++) TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, bufs[b].lines[i]));
	e.cy = y;
	e.cx = x;
}

/** @brief Fill the shared editor with buffer @p b of the motions table and put the cursor at (@p y, @p x). */
static void load(int b, size_t y, size_t x) {
	load_from(buffers, b, y, x);
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

/** One row of the counts table: from where, which keys after which count (0 = none), to where, and where a following "j" goes (all from the real Vim). */
typedef struct {
	int buf;
	const char *keys;
	unsigned count;
	size_t fy, fx, ty, tx, jy, jx;
} crow_t;

/** Expected results of counted motions taken from the real Vim 9.1 (see gen-vim-data.sh). */
static const crow_t counts_table[] = {
#include "counts_vim.inc"
};

/** @brief Type @p keys (bytes) into the shared editor; return what the last key returned. */
static int type_keys(const char *keys) {
	int r = 0;
	for (; *keys; keys++) r = press((unsigned char)*keys);
	return r;
}

/** @brief Type the count @p n (if any) and the keys of row @p r in the shared editor. */
static void type_counted(unsigned n, const char *keys) {
	if (n) {
		char digits[16];
		snprintf(digits, sizeof(digits), "%u", n);
		type_keys(digits);
	}
	type_keys(keys);
}

/** @brief A count before every motion key, "gg" and "G" gives the cursor of the real Vim, and so does the "j" after it (the wanted column); the text stays unmodified. */
static void test_counts_match_vim_with_the_wanted_column(void) {
	char msg[96];
	for (size_t i = 0; i < sizeof(counts_table) / sizeof(counts_table[0]); i++) {
		const crow_t *r = &counts_table[i];
		snprintf(msg, sizeof(msg), "buffer %d, %u%s, from %zu,%zu", r->buf, r->count, r->keys, r->fy, r->fx);
		load(r->buf, r->fy, r->fx);
		e.wantcol = display_of(e.lines[r->fy], r->fx);
		type_counted(r->count, r->keys);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(r->ty, e.cy, msg);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(r->tx, e.cx, msg);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(0, e.pending.count, msg);
		TEST_ASSERT_EQUAL_INT_MESSAGE(0, e.modified, msg);
		press('j');
		TEST_ASSERT_EQUAL_UINT_MESSAGE(r->jy, e.cy, msg);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(r->jx, e.cx, msg);
	}
}

/** @brief The goto-line motion is pure: line n counted from 1 (0 or too big: the last), on its first non-blank. */
static void test_goto_line_motion(void) {
	load(0, 1, 0);
	motion_pos_t t = motion_goto_line(&e, 3);
	TEST_ASSERT_EQUAL_UINT(2, t.y);
	TEST_ASSERT_EQUAL_UINT(2, t.x);
	t = motion_goto_line(&e, 0);
	TEST_ASSERT_EQUAL_UINT(4, t.y);
	t = motion_goto_line(&e, 99);
	TEST_ASSERT_EQUAL_UINT(4, t.y);
	t = motion_goto_line(&e, 4); /* only blanks: the last character */
	TEST_ASSERT_EQUAL_UINT(3, t.y);
	TEST_ASSERT_EQUAL_UINT(1, t.x);
	TEST_ASSERT_EQUAL_UINT(1, e.cy);
	TEST_ASSERT_EQUAL_UINT(0, e.cx);
	editor_free(&e);
	editor_init(&e);
	t = motion_goto_line(&e, 5);
	TEST_ASSERT_EQUAL_UINT(0, t.y);
	TEST_ASSERT_EQUAL_UINT(0, t.x);
}

/** @brief Digits build a count without a redraw; "0" continues it ("10l" is ten) and is the motion when no count is pending; "30" waits for a motion (Vim: 30j). */
static void test_zero_is_a_count_digit_only_after_a_digit(void) {
	editor_init(&e);
	editor_append_line(&e, "abcdefghijklmnopqrstuvwxyz");
	TEST_ASSERT_EQUAL_INT(0, press('1'));
	TEST_ASSERT_EQUAL_UINT(1, e.pending.count);
	TEST_ASSERT_EQUAL_INT(0, press('0'));
	TEST_ASSERT_EQUAL_UINT(10, e.pending.count);
	TEST_ASSERT_EQUAL_INT(1, press('l'));
	TEST_ASSERT_EQUAL_UINT(10, e.cx);
	TEST_ASSERT_EQUAL_UINT(0, e.pending.count);
	TEST_ASSERT_EQUAL_INT(1, press('0')); /* no count: the motion */
	TEST_ASSERT_EQUAL_UINT(0, e.cx);
	TEST_ASSERT_EQUAL_UINT(0, e.pending.count);
	for (int i = 0; i < 40; i++) editor_append_line(&e, "x");
	type_keys("30j");
	TEST_ASSERT_EQUAL_UINT(30, e.cy);
	type_keys("0"); /* the count was used up: the motion again */
	TEST_ASSERT_EQUAL_UINT(0, e.cx);
	type_keys("2G0");
	TEST_ASSERT_EQUAL_UINT(1, e.cy);
}

/** @brief A count is used by one command and then cleared: "2l" then "l" moves three in all. */
static void test_the_count_is_cleared_after_each_command(void) {
	editor_init(&e);
	editor_append_line(&e, "abcdefgh");
	type_keys("2l");
	TEST_ASSERT_EQUAL_UINT(2, e.cx);
	type_keys("l");
	TEST_ASSERT_EQUAL_UINT(3, e.cx);
	type_keys("2w"); /* a motion that stops at the end of the text clears it too */
	type_keys("h");
	TEST_ASSERT_EQUAL_UINT(6, e.cx);
	TEST_ASSERT_EQUAL_UINT(0, e.pending.count);
}

/** @brief Huge counts are clamped and act as "as far as possible"; the count never overflows. */
static void test_huge_counts_are_clamped(void) {
	editor_init(&e);
	for (int i = 0; i < 5; i++) editor_append_line(&e, "abc def");
	type_keys("99999999999999999999999999");
	TEST_ASSERT_EQUAL_UINT(EDITOR_COUNT_MAX, e.pending.count);
	type_keys("j");
	TEST_ASSERT_EQUAL_UINT(4, e.cy);
	type_keys("18446744073709551616l");
	TEST_ASSERT_EQUAL_UINT(6, e.cx);
	type_keys("4294967296k");
	TEST_ASSERT_EQUAL_UINT(0, e.cy);
	type_keys("9999999999999999999999w"); /* stops early: no loop to a billion */
	TEST_ASSERT_EQUAL_UINT(4, e.cy);
	TEST_ASSERT_EQUAL_UINT(6, e.cx);
	type_keys("99999999999999999999G");
	TEST_ASSERT_EQUAL_UINT(4, e.cy);
}

/** @brief Esc and an unknown key cancel a pending count: the next motion is plain. */
static void test_esc_and_unknown_keys_cancel_the_count(void) {
	editor_init(&e);
	editor_append_line(&e, "abcdefgh");
	type_keys("3");
	TEST_ASSERT_EQUAL_INT(0, press(KEY_ESC));
	TEST_ASSERT_EQUAL_UINT(0, e.pending.count);
	type_keys("l");
	TEST_ASSERT_EQUAL_UINT(1, e.cx);
	type_keys("3x"); /* "x" is not a command yet */
	TEST_ASSERT_EQUAL_UINT(0, e.pending.count);
	type_keys("l");
	TEST_ASSERT_EQUAL_UINT(2, e.cx);
}

/** @brief The arrows take a count like "h", "j", "k" and "l". */
static void test_counts_apply_to_the_arrows(void) {
	editor_init(&e);
	for (int i = 0; i < 6; i++) editor_append_line(&e, "abcdefgh");
	type_keys("3");
	press(KEY_RIGHT);
	TEST_ASSERT_EQUAL_UINT(3, e.cx);
	type_keys("4");
	press(KEY_DOWN);
	TEST_ASSERT_EQUAL_UINT(4, e.cy);
	type_keys("2");
	press(KEY_LEFT);
	TEST_ASSERT_EQUAL_UINT(1, e.cx);
	type_keys("9");
	press(KEY_UP);
	TEST_ASSERT_EQUAL_UINT(0, e.cy);
}

/** @brief The digit and a first "g" ask for no redraw; "gg" and "G" ask for one only when the cursor moves (the move keys always do, as before counts). */
static void test_digits_and_g_ask_for_no_redraw_and_line_jumps_only_when_moving(void) {
	editor_init(&e);
	editor_append_line(&e, "abc");
	editor_append_line(&e, "abc");
	TEST_ASSERT_EQUAL_INT(0, type_keys("5"));
	TEST_ASSERT_EQUAL_INT(1, press('j'));
	TEST_ASSERT_EQUAL_INT(0, type_keys("g"));
	TEST_ASSERT_EQUAL_INT(1, press('g'));
	TEST_ASSERT_EQUAL_INT(0, type_keys("gg")); /* already there */
	TEST_ASSERT_EQUAL_INT(1, type_keys("G"));
	TEST_ASSERT_EQUAL_INT(0, type_keys("G"));
}

/** @brief "3$" goes two lines down to the end of that line, "0" and "^" ignore the count; "$" keeps the end for "j". */
static void test_count_before_dollar_goes_down_lines(void) {
	editor_init(&e);
	editor_append_line(&e, "abcdef");
	editor_append_line(&e, "ab");
	editor_append_line(&e, "abcd");
	editor_append_line(&e, "abcdefgh");
	type_keys("3$");
	TEST_ASSERT_EQUAL_UINT(2, e.cy);
	TEST_ASSERT_EQUAL_UINT(3, e.cx);
	TEST_ASSERT_EQUAL_UINT(EDITOR_WANTCOL_EOL, e.wantcol);
	type_keys("j");
	TEST_ASSERT_EQUAL_UINT(7, e.cx);
	type_keys("0");
	type_keys("2$"); /* already on the last line: Vim fails and the cursor stays */
	TEST_ASSERT_EQUAL_UINT(3, e.cy);
	TEST_ASSERT_EQUAL_UINT(0, e.cx);
	type_keys("gg9$"); /* too few lines: as far down as there are */
	TEST_ASSERT_EQUAL_UINT(3, e.cy);
	TEST_ASSERT_EQUAL_UINT(7, e.cx);
}

/** @brief "g" waits (no redraw), a second "g" goes to the first line, any other key or Esc drops both the "g" and the count and is not handled further. */
static void test_g_prefix(void) {
	editor_init(&e);
	for (int i = 0; i < 4; i++) editor_append_line(&e, "abcd");
	e.cy = 3;
	TEST_ASSERT_EQUAL_INT(0, press('g'));
	TEST_ASSERT_EQUAL_INT('g', e.pending.prefix);
	press('g');
	TEST_ASSERT_EQUAL_UINT(0, e.cy);
	TEST_ASSERT_EQUAL_INT(0, e.pending.prefix);
	e.cy = 3;
	type_keys("g");
	press(KEY_ESC);
	TEST_ASSERT_EQUAL_INT(0, e.pending.prefix);
	type_keys("k");
	TEST_ASSERT_EQUAL_UINT(2, e.cy);
	type_keys("gl"); /* the second key is consumed */
	TEST_ASSERT_EQUAL_UINT(0, e.cx);
	TEST_ASSERT_EQUAL_UINT(2, e.cy);
	type_keys("l");
	TEST_ASSERT_EQUAL_UINT(1, e.cx);
	type_keys("3gxj"); /* the count goes with the unknown g command, as in Vim */
	TEST_ASSERT_EQUAL_UINT(3, e.cy);
	type_keys("2gg");
	TEST_ASSERT_EQUAL_UINT(1, e.cy);
	type_keys("g3k"); /* a digit after "g" is dropped, not a count: "k" moves one line */
	TEST_ASSERT_EQUAL_UINT(0, e.cy);
	TEST_ASSERT_EQUAL_UINT(0, e.pending.count);
}

/** @brief Ctrl+Q still asks to quit with a count or "g" pending, and clears them. */
static void test_ctrl_q_ends_a_pending_count_and_g(void) {
	editor_init(&e);
	editor_append_line(&e, "ab");
	type_keys("4");
	press(0x11);
	TEST_ASSERT_EQUAL_INT(1, e.quit);
	TEST_ASSERT_EQUAL_UINT(0, e.pending.count);
	e.quit = 0;
	type_keys("g");
	press(0x11);
	TEST_ASSERT_EQUAL_INT(1, e.quit);
	TEST_ASSERT_EQUAL_INT(0, e.pending.prefix);
}

/** @brief "G" and "gg" on an editor with no lines do nothing. */
static void test_g_and_gg_on_an_empty_editor_do_nothing(void) {
	editor_init(&e);
	TEST_ASSERT_EQUAL_INT(0, type_keys("G"));
	TEST_ASSERT_EQUAL_INT(0, type_keys("gg"));
	TEST_ASSERT_EQUAL_INT(0, type_keys("5G"));
	type_keys("3j");
	TEST_ASSERT_EQUAL_UINT(0, e.cy);
	TEST_ASSERT_EQUAL_UINT(0, e.count);
}

/** @brief In insert mode digits and "g" are typed; in command mode they go to the command line; neither leaves a count behind. */
static void test_counts_and_g_are_normal_mode_only(void) {
	editor_init(&e);
	editor_append_line(&e, "ab");
	press('i');
	type_keys("3gG0");
	TEST_ASSERT_EQUAL_STRING("3gG0ab", e.lines[0]);
	TEST_ASSERT_EQUAL_UINT(0, e.pending.count);
	TEST_ASSERT_EQUAL_INT(0, e.pending.prefix);
	press(KEY_ESC);
	press(':');
	type_keys("5g");
	TEST_ASSERT_EQUAL_STRING("5g", e.cmd.text);
	TEST_ASSERT_EQUAL_UINT(0, e.pending.count);
	TEST_ASSERT_EQUAL_INT(0, e.pending.prefix);
}

/** @brief A mode change drops the pending state: "3" then "i" or ":" leaves no count, and a count left over from before is not used in insert mode. */
static void test_a_mode_change_clears_the_pending_state(void) {
	editor_init(&e);
	editor_append_line(&e, "abcdef");
	type_keys("3i");
	TEST_ASSERT_EQUAL_UINT(0, e.pending.count);
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_INSERT, e.mode);
	type_keys("x");
	TEST_ASSERT_EQUAL_STRING("xabcdef", e.lines[0]);
	press(KEY_ESC);
	type_keys("g:");
	TEST_ASSERT_EQUAL_INT(EDITOR_MODE_NORMAL, e.mode);
	type_keys("2:");
	TEST_ASSERT_EQUAL_UINT(0, e.pending.count);
	press(KEY_ESC);
	e.pending.count = 4; /* a state left behind by whatever changed the mode */
	e.pending.prefix = 'g';
	e.mode = EDITOR_MODE_INSERT;
	press('l');
	TEST_ASSERT_EQUAL_STRING("lxabcdef", e.lines[0]);
	TEST_ASSERT_EQUAL_UINT(0, e.pending.count);
	TEST_ASSERT_EQUAL_INT(0, e.pending.prefix);
}

/** @brief A count then "Z": the count goes, "ZZ" works as before. */
static void test_a_count_before_z_is_dropped(void) {
	editor_init(&e);
	editor_append_line(&e, "ab");
	type_keys("3Z");
	TEST_ASSERT_EQUAL_UINT(0, e.pending.count);
	TEST_ASSERT_EQUAL_INT('Z', e.pending.prefix);
	type_keys("l"); /* cancels the Z and moves once */
	TEST_ASSERT_EQUAL_UINT(1, e.cx);
	TEST_ASSERT_EQUAL_INT(0, e.pending.prefix);
}

/** @brief "5j", "G" and "gg" scroll the window with the cursor, and "$"-style sideways scrolling is untouched. */
static void test_counted_and_line_jumps_scroll_the_window(void) {
	editor_init(&e);
	for (int i = 0; i < 20; i++) editor_append_line(&e, "x");
	type_keys("5j");
	editor_scroll(&e, 3);
	TEST_ASSERT_EQUAL_UINT(5, e.cy);
	TEST_ASSERT_EQUAL_UINT(3, e.rowoff);
	type_keys("G");
	editor_scroll(&e, 3);
	TEST_ASSERT_EQUAL_UINT(19, e.cy);
	TEST_ASSERT_EQUAL_UINT(17, e.rowoff);
	type_keys("gg");
	editor_scroll(&e, 3);
	TEST_ASSERT_EQUAL_UINT(0, e.cy);
	TEST_ASSERT_EQUAL_UINT(0, e.rowoff);
}

/** @brief "G" puts the cursor on the first non-blank of the last line and the wanted column follows it: "k" then comes back to that column. */
static void test_g_sets_the_wanted_column_from_the_first_non_blank(void) {
	editor_init(&e);
	editor_append_line(&e, "abcdefgh");
	editor_append_line(&e, "abcdefgh");
	editor_append_line(&e, "    xy");
	type_keys("$G");
	TEST_ASSERT_EQUAL_UINT(4, e.cx);
	TEST_ASSERT_EQUAL_UINT(4, e.wantcol);
	type_keys("k");
	TEST_ASSERT_EQUAL_UINT(4, e.cx);
}


/** The buffers of blocks_vim.inc, in its order. */
static const buffer_t block_buffers[] = {
	BUFFER("a", "b", "", "c", "d", "", "", "e", "  ", "f"),
	BUFFER("", "", "a"),
	BUFFER("a", ""),
	BUFFER("a"),
	BUFFER(""),
	BUFFER("  x", "", "  y  z", " "),
	BUFFER("f(a[1], {b})", "  if (x) {", "    y(z);", "  }", "end)"),
	BUFFER("(a", "b", "c)"),
	BUFFER("((a)", "b)", ")"),
	BUFFER("x(h\xc3\xa9llo)y", "[\xc3\xa9]"),
	BUFFER("no brackets", ""),
	BUFFER("  ( )", "}{"),
};

/** Expected results of "{", "}" and "%" taken from the real Vim 9.1 (see gen-vim-data.sh). */
static const crow_t blocks_table[] = {
#include "blocks_vim.inc"
};

/** @brief The one-step pure motion for the block key of @p key ('{', '}' or '%') from the cursor of the shared editor. */
static motion_pos_t run_block(char key) {
	return key == '{' ? motion_paragraph_prev(&e) : key == '}' ? motion_paragraph_next(&e) : motion_bracket_match(&e);
}

/** @brief The pure "{", "}" and "%" give the target of the real Vim from every character of every buffer (one step, no count) and move nothing. */
static void test_paragraph_and_bracket_motions_match_vim(void) {
	char msg[96];
	for (size_t i = 0; i < sizeof(blocks_table) / sizeof(blocks_table[0]); i++) {
		const crow_t *r = &blocks_table[i];
		if (r->count) continue;
		snprintf(msg, sizeof(msg), "buffer %d, %s, from %zu,%zu", r->buf, r->keys, r->fy, r->fx);
		load_from(block_buffers, r->buf, r->fy, r->fx);
		motion_pos_t t = run_block(r->keys[0]);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(r->ty, t.y, msg);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(r->tx, t.x, msg);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(r->fy, e.cy, "a motion must not move the cursor");
		TEST_ASSERT_EQUAL_UINT_MESSAGE(r->fx, e.cx, "a motion must not move the cursor");
	}
}

/** @brief The keys "{", "}" and "%", with and without a count, put the cursor where Vim does, the "j" after them uses Vim's wanted column, and the text stays unmodified. */
static void test_paragraph_and_bracket_keys_match_vim(void) {
	char msg[96];
	for (size_t i = 0; i < sizeof(blocks_table) / sizeof(blocks_table[0]); i++) {
		const crow_t *r = &blocks_table[i];
		snprintf(msg, sizeof(msg), "buffer %d, %u%s, from %zu,%zu", r->buf, r->count, r->keys, r->fy, r->fx);
		load_from(block_buffers, r->buf, r->fy, r->fx);
		e.wantcol = display_of(e.lines[r->fy], r->fx);
		type_counted(r->count, r->keys);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(r->ty, e.cy, msg);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(r->tx, e.cx, msg);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(0, e.pending.count, msg);
		TEST_ASSERT_EQUAL_INT_MESSAGE(0, e.modified, msg);
		press('j');
		TEST_ASSERT_EQUAL_UINT_MESSAGE(r->jy, e.cy, msg);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(r->jx, e.cx, msg);
	}
}

/** @brief On an editor with no lines "{", "}" and "%" give (0, 0) and the keys do nothing; they return non-zero only when the cursor moved. */
static void test_blocks_on_an_empty_editor_and_return_values(void) {
	editor_init(&e);
	for (const char *k = "{}%"; *k; k++) {
		motion_pos_t t = run_block(*k);
		TEST_ASSERT_EQUAL_UINT(0, t.y);
		TEST_ASSERT_EQUAL_UINT(0, t.x);
		TEST_ASSERT_EQUAL_INT(0, press(*k));
	}
	load_from(block_buffers, 0, 0, 0);
	TEST_ASSERT_EQUAL_INT(1, press('}'));
	TEST_ASSERT_EQUAL_INT(1, press('{'));
	TEST_ASSERT_EQUAL_INT(0, press('{')); /* at the start of the text */
	TEST_ASSERT_EQUAL_INT(0, press('%')); /* no bracket */
}

/** @brief "{", "}" and "%" are normal mode only: in insert mode they are typed, in command mode they go to the command line. */
static void test_blocks_are_normal_mode_only(void) {
	load_from(block_buffers, 6, 0, 0);
	press('i');
	type_keys("%{}");
	TEST_ASSERT_EQUAL_STRING("%{}f(a[1], {b})", e.lines[0]);
	TEST_ASSERT_EQUAL_UINT(0, e.cy);
	press(KEY_ESC);
	press(':');
	type_keys("}%");
	TEST_ASSERT_EQUAL_STRING("}%", e.cmd.text);
}

/** @brief Fill the shared editor with @p n lines "x", a window of @p h lines and the cursor on line @p cy with the window at @p rowoff. */
static void page(size_t n, size_t h, size_t rowoff, size_t cy) {
	editor_free(&e);
	editor_init(&e);
	for (size_t i = 0; i < n; i++) TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, "x"));
	editor_set_window_height(&e, h);
	e.rowoff = rowoff;
	e.cy = cy;
}

/** @brief Assert the window top and the cursor line after a page key. */
#define ASSERT_PAGE(want_top, want_cy) do { TEST_ASSERT_EQUAL_UINT(want_top, e.rowoff); TEST_ASSERT_EQUAL_UINT(want_cy, e.cy); TEST_ASSERT_EQUAL_INT(0, e.modified); } while (0)

/** @brief Ctrl+f scrolls forward h - 2 lines (two lines of overlap) and puts the cursor on the first line of the window, for several window heights. */
static void test_ctrl_f_scrolls_a_page_minus_two_lines(void) {
	page(100, 10, 0, 0);
	TEST_ASSERT_EQUAL_INT(1, press(0x06));
	ASSERT_PAGE(8, 8);
	press(0x06);
	ASSERT_PAGE(16, 16);
	page(100, 23, 0, 5);
	press(0x06);
	ASSERT_PAGE(21, 21);
	page(100, 5, 0, 0);
	press(0x06);
	ASSERT_PAGE(3, 3);
	page(100, 10, 0, 5); /* a cursor already below the new top stays */
	press(0x06);
	ASSERT_PAGE(8, 8);
	page(100, 10, 0, 9);
	press(0x06);
	ASSERT_PAGE(8, 9);
	page(100, 2, 0, 0); /* a window of 2 or 1 lines still moves one line */
	press(0x06);
	ASSERT_PAGE(1, 1);
	page(100, 1, 0, 0);
	press(0x06);
	ASSERT_PAGE(1, 1);
}

/** @brief Ctrl+b scrolls back h - 2 lines and puts the cursor on the last line of the new window, as Vim does (checked in a real Vim: top 22, cursor 44 after ^F^F^B). */
static void test_ctrl_b_scrolls_a_page_back(void) {
	page(100, 10, 50, 50);
	TEST_ASSERT_EQUAL_INT(1, press(0x02));
	ASSERT_PAGE(42, 51);
	page(100, 10, 50, 59);
	press(0x02);
	ASSERT_PAGE(42, 51);
	page(100, 10, 5, 7); /* a cursor inside the new window goes to its last line too */
	press(0x02);
	ASSERT_PAGE(0, 9);
	page(100, 23, 21, 21); /* the 23-line window of a 24-row terminal: ^F then ^B */
	press(0x02);
	ASSERT_PAGE(0, 22);
	page(100, 23, 42, 43); /* ^F^F^B: Vim top 22, cursor 44 (1-based) */
	press(0x02);
	ASSERT_PAGE(21, 43);
	page(100, 5, 50, 54);
	press(0x02);
	ASSERT_PAGE(47, 51);
}

/** @brief Ctrl+f and Ctrl+b stop at the ends: the window top never passes the last line nor goes below 0, and a key that changes nothing returns 0. */
static void test_ctrl_f_and_ctrl_b_stop_at_the_buffer_ends(void) {
	page(100, 10, 93, 95);
	TEST_ASSERT_EQUAL_INT(1, press(0x06));
	ASSERT_PAGE(99, 99);
	TEST_ASSERT_EQUAL_INT(0, press(0x06));
	ASSERT_PAGE(99, 99);
	page(100, 10, 3, 5);
	TEST_ASSERT_EQUAL_INT(1, press(0x02));
	ASSERT_PAGE(0, 9);
	TEST_ASSERT_EQUAL_INT(0, press(0x02)); /* already at the top: nothing moves, not even the cursor */
	ASSERT_PAGE(0, 9);
	page(100, 10, 0, 4);
	TEST_ASSERT_EQUAL_INT(0, press(0x02));
	ASSERT_PAGE(0, 4);
	page(1, 10, 0, 0);
	TEST_ASSERT_EQUAL_INT(0, press(0x06));
	TEST_ASSERT_EQUAL_INT(0, press(0x02));
	TEST_ASSERT_EQUAL_INT(0, press(0x04));
	TEST_ASSERT_EQUAL_INT(0, press(0x15));
}

/** @brief A buffer shorter than the window does not scroll: Ctrl+f goes to the last line, Ctrl+b leaves the cursor (as Vim), Ctrl+d and Ctrl+u move the cursor only. */
static void test_page_keys_on_a_buffer_shorter_than_the_window(void) {
	page(5, 10, 0, 1);
	press(0x06);
	ASSERT_PAGE(0, 4);
	TEST_ASSERT_EQUAL_INT(0, press(0x02));
	ASSERT_PAGE(0, 4);
	page(5, 10, 0, 2);
	TEST_ASSERT_EQUAL_INT(0, press(0x02));
	ASSERT_PAGE(0, 2);
	page(5, 10, 0, 0);
	press(0x04);
	ASSERT_PAGE(0, 4);
	press(0x15);
	ASSERT_PAGE(0, 0);
	page(10, 10, 0, 0); /* exactly as long as the window */
	press(0x06);
	ASSERT_PAGE(0, 9);
}

/** @brief Ctrl+d scrolls half a window and moves the cursor as far; at the end the window stops with the last line at its bottom and only the cursor moves. Ctrl+u mirrors it. */
static void test_ctrl_d_and_ctrl_u_scroll_half_a_window(void) {
	page(100, 10, 0, 0);
	TEST_ASSERT_EQUAL_INT(1, press(0x04));
	ASSERT_PAGE(5, 5);
	press(0x04);
	ASSERT_PAGE(10, 10);
	press(0x15);
	ASSERT_PAGE(5, 5);
	page(100, 10, 3, 7);
	press(0x15);
	ASSERT_PAGE(0, 2);
	page(100, 11, 0, 0); /* an odd height: half rounds down */
	press(0x04);
	ASSERT_PAGE(5, 5);
	page(100, 10, 85, 90);
	press(0x04);
	ASSERT_PAGE(90, 95);
	press(0x04);
	ASSERT_PAGE(90, 99);
	TEST_ASSERT_EQUAL_INT(0, press(0x04));
	page(100, 10, 0, 3);
	press(0x15);
	ASSERT_PAGE(0, 0);
	TEST_ASSERT_EQUAL_INT(0, press(0x15));
}

/** @brief A count is pages for Ctrl+f and Ctrl+b, and the lines of Ctrl+d and Ctrl+u, which then stay as the new scroll amount; the count is used up. */
static void test_page_keys_with_counts(void) {
	page(100, 10, 0, 0);
	type_keys("2\x06");
	ASSERT_PAGE(16, 16);
	TEST_ASSERT_EQUAL_UINT(0, e.pending.count);
	page(100, 10, 49, 49);
	type_keys("2\x02");
	ASSERT_PAGE(33, 42);
	page(100, 10, 0, 0);
	type_keys("3\x04");
	ASSERT_PAGE(3, 3);
	press(0x04);
	ASSERT_PAGE(6, 6);
	type_keys("2\x15");
	ASSERT_PAGE(4, 4);
	press(0x15);
	ASSERT_PAGE(2, 2);
	page(100, 10, 0, 0);
	type_keys("100\x06"); /* more pages than lines: the end */
	ASSERT_PAGE(99, 99);
}

/** @brief After a page key the cursor is on the first non-blank of its line and the wanted column follows ("j" stays there). */
static void test_page_keys_put_the_cursor_on_the_first_non_blank(void) {
	editor_free(&e);
	editor_init(&e);
	for (size_t i = 0; i < 100; i++) TEST_ASSERT_EQUAL_INT(0, editor_append_line(&e, i % 2 ? "  \tab" : "abcdef"));
	editor_set_window_height(&e, 10);
	e.cx = 4;
	press(0x04); /* cursor on line 5, "  \tab" */
	TEST_ASSERT_EQUAL_UINT(3, e.cx);
	TEST_ASSERT_EQUAL_UINT(8, e.wantcol); /* the tab takes the columns up to 8 */
	press(0x04);
	TEST_ASSERT_EQUAL_UINT(10, e.cy);
	TEST_ASSERT_EQUAL_UINT(0, e.cx);
	TEST_ASSERT_EQUAL_UINT(0, e.wantcol);
}

/** @brief Page keys do nothing without a known window height or without lines. */
static void test_page_keys_need_a_window_height_and_lines(void) {
	page(100, 0, 0, 0);
	for (const char *k = "\x06\x02\x04\x15"; *k; k++) TEST_ASSERT_EQUAL_INT(0, press(*k));
	ASSERT_PAGE(0, 0);
	editor_free(&e);
	editor_init(&e);
	editor_set_window_height(&e, 10);
	for (const char *k = "\x06\x02\x04\x15"; *k; k++) TEST_ASSERT_EQUAL_INT(0, press(*k));
}

/** @brief The window height used is the last one given, and it survives loading text into the editor (a resize between keys changes the page). */
static void test_page_keys_use_the_latest_window_height(void) {
	page(100, 10, 0, 0);
	press(0x06);
	ASSERT_PAGE(8, 8);
	editor_set_window_height(&e, 5);
	press(0x06);
	ASSERT_PAGE(11, 11);
	editor_free(&e);
	TEST_ASSERT_EQUAL_UINT(5, e.winrows);
}

/** @brief Page keys are normal mode only: in insert mode they are dropped as other control keys, in command mode ignored; Ctrl+Q still quits. */
static void test_page_keys_are_normal_mode_only(void) {
	page(100, 10, 0, 0);
	press('i');
	for (const char *k = "\x06\x02\x04\x15"; *k; k++) press(*k);
	ASSERT_PAGE(0, 0);
	TEST_ASSERT_EQUAL_STRING("x", e.lines[0]);
	press(KEY_ESC);
	press(':');
	for (const char *k = "\x06\x02\x04\x15"; *k; k++) press(*k);
	TEST_ASSERT_EQUAL_STRING("", e.cmd.text);
	TEST_ASSERT_EQUAL_UINT(0, e.cy);
	press(KEY_ESC);
	press(0x11);
	TEST_ASSERT_EQUAL_INT(1, e.quit);
}

/** @brief A page key after "g" is dropped with it (as any key that is not "g"); one after a count uses it up; the scroll a page key makes needs no correction by editor_scroll(). */
static void test_page_keys_end_a_pending_command_and_leave_the_window_alone(void) {
	page(100, 10, 0, 0);
	press('g');
	TEST_ASSERT_EQUAL_INT(0, press(0x06));
	TEST_ASSERT_EQUAL_INT(0, e.pending.prefix);
	ASSERT_PAGE(0, 0);
	press(0x06);
	ASSERT_PAGE(8, 8);
	editor_scroll(&e, 10);
	ASSERT_PAGE(8, 8);
	press(0x04);
	editor_scroll(&e, 10);
	ASSERT_PAGE(13, 13);
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
	RUN_TEST(test_counts_match_vim_with_the_wanted_column);
	RUN_TEST(test_goto_line_motion);
	RUN_TEST(test_zero_is_a_count_digit_only_after_a_digit);
	RUN_TEST(test_the_count_is_cleared_after_each_command);
	RUN_TEST(test_huge_counts_are_clamped);
	RUN_TEST(test_esc_and_unknown_keys_cancel_the_count);
	RUN_TEST(test_counts_apply_to_the_arrows);
	RUN_TEST(test_digits_and_g_ask_for_no_redraw_and_line_jumps_only_when_moving);
	RUN_TEST(test_count_before_dollar_goes_down_lines);
	RUN_TEST(test_g_prefix);
	RUN_TEST(test_ctrl_q_ends_a_pending_count_and_g);
	RUN_TEST(test_g_and_gg_on_an_empty_editor_do_nothing);
	RUN_TEST(test_counts_and_g_are_normal_mode_only);
	RUN_TEST(test_a_mode_change_clears_the_pending_state);
	RUN_TEST(test_a_count_before_z_is_dropped);
	RUN_TEST(test_counted_and_line_jumps_scroll_the_window);
	RUN_TEST(test_g_sets_the_wanted_column_from_the_first_non_blank);
	RUN_TEST(test_paragraph_and_bracket_motions_match_vim);
	RUN_TEST(test_paragraph_and_bracket_keys_match_vim);
	RUN_TEST(test_blocks_on_an_empty_editor_and_return_values);
	RUN_TEST(test_blocks_are_normal_mode_only);
	RUN_TEST(test_ctrl_f_scrolls_a_page_minus_two_lines);
	RUN_TEST(test_ctrl_b_scrolls_a_page_back);
	RUN_TEST(test_ctrl_f_and_ctrl_b_stop_at_the_buffer_ends);
	RUN_TEST(test_page_keys_on_a_buffer_shorter_than_the_window);
	RUN_TEST(test_ctrl_d_and_ctrl_u_scroll_half_a_window);
	RUN_TEST(test_page_keys_with_counts);
	RUN_TEST(test_page_keys_put_the_cursor_on_the_first_non_blank);
	RUN_TEST(test_page_keys_need_a_window_height_and_lines);
	RUN_TEST(test_page_keys_use_the_latest_window_height);
	RUN_TEST(test_page_keys_are_normal_mode_only);
	RUN_TEST(test_page_keys_end_a_pending_command_and_leave_the_window_alone);
}
