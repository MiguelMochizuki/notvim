/**
 * @file test_utf8.c
 * @brief Unit tests for utf8.c.
 *
 * Inputs are copied into a heap block of exactly their length plus the NUL,
 * so that AddressSanitizer fails a function that reads past the NUL.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "test_utf8.h"
#include "utf8.h"

/** One table row: @p len bytes and what utf8_valid_len() must answer for them. */
typedef struct {
	const char *bytes; /**< The input; may contain bytes that are not UTF-8. */
	size_t len;        /**< Number of bytes in @ref bytes. */
	size_t expected;   /**< Expected result. */
} row_t;

/** @brief Copy @p len bytes of @p bytes into an exact-size NUL-terminated heap block; the caller frees it. */
static char *exact_copy(const char *bytes, size_t len) {
	char *copy = malloc(len + 1);
	TEST_ASSERT_NOT_NULL(copy);
	memcpy(copy, bytes, len);
	copy[len] = '\0';
	return copy;
}

/** @brief Assert utf8_valid_len() on every row; the failure message names the row. */
static void assert_valid_len_rows(const row_t *rows, size_t n) {
	for (size_t i = 0; i < n; i++) {
		char *s = exact_copy(rows[i].bytes, rows[i].len);
		size_t got = utf8_valid_len(s);
		free(s);
		char msg[32];
		snprintf(msg, sizeof(msg), "row %zu", i);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(rows[i].expected, got, msg);
	}
}

/** @brief Table rows of an array literal: the string, its length without the NUL, and the expected result. */
#define ROW(str, expected) { str, sizeof(str) - 1, expected }
/** @brief Number of elements of an array. */
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/** @brief ASCII, including control bytes, is one byte; an empty string has no character. */
static void test_utf8_valid_len_ascii(void) {
	const row_t rows[] = { ROW("a", 1), ROW("~", 1), ROW("\x01", 1), ROW("\x7f", 1), ROW("\t", 1), ROW("", 0) };
	assert_valid_len_rows(rows, COUNT(rows));
}

/** @brief Valid 2, 3 and 4-byte sequences, at the edges of each range, give their length. */
static void test_utf8_valid_len_multibyte(void) {
	const row_t rows[] = {
		ROW("\xc3\xa9", 2),             /* U+00E9 */
		ROW("\xc2\x80", 2),             /* U+0080, the smallest 2-byte */
		ROW("\xdf\xbf", 2),             /* U+07FF, the largest 2-byte */
		ROW("\xe2\x82\xac", 3),         /* U+20AC */
		ROW("\xe0\xa0\x80", 3),         /* U+0800, the smallest 3-byte */
		ROW("\xed\x9f\xbf", 3),         /* U+D7FF, just below the surrogates */
		ROW("\xee\x80\x80", 3),         /* U+E000, just above the surrogates */
		ROW("\xef\xbf\xbf", 3),         /* U+FFFF */
		ROW("\xf0\x9f\x98\x80", 4),     /* U+1F600 */
		ROW("\xf0\x90\x80\x80", 4),     /* U+10000, the smallest 4-byte */
		ROW("\xf4\x8f\xbf\xbf", 4),     /* U+10FFFF, the largest code point */
	};
	assert_valid_len_rows(rows, COUNT(rows));
}

/** @brief Only the first character counts: bytes after a valid sequence do not change its length. */
static void test_utf8_valid_len_stops_after_the_first_character(void) {
	const row_t rows[] = { ROW("\xc3\xa9zz", 2), ROW("\xe2\x82\xac\xc3\xa9", 3), ROW("a\xff", 1), ROW("\xf0\x9f\x98\x80\x80", 4) };
	assert_valid_len_rows(rows, COUNT(rows));
}

/** @brief Overlong encodings are invalid, up to the largest overlong of each length. */
static void test_utf8_valid_len_rejects_overlong_forms(void) {
	const row_t rows[] = {
		ROW("\xc0\x80", 0), ROW("\xc0\xaf", 0), ROW("\xc1\xbf", 0),
		ROW("\xe0\x80\x80", 0), ROW("\xe0\x9f\xbf", 0),
		ROW("\xf0\x80\x80\x80", 0), ROW("\xf0\x8f\xbf\xbf", 0),
	};
	assert_valid_len_rows(rows, COUNT(rows));
}

/** @brief Surrogates U+D800 to U+DFFF are invalid. */
static void test_utf8_valid_len_rejects_surrogates(void) {
	const row_t rows[] = { ROW("\xed\xa0\x80", 0), ROW("\xed\xaf\xbf", 0), ROW("\xed\xb0\x80", 0), ROW("\xed\xbf\xbf", 0) };
	assert_valid_len_rows(rows, COUNT(rows));
}

/** @brief Code points above U+10FFFF, and the lead bytes 0xF5 to 0xFF, are invalid. */
static void test_utf8_valid_len_rejects_code_points_above_10ffff(void) {
	const row_t rows[] = {
		ROW("\xf4\x90\x80\x80", 0), ROW("\xf4\xbf\xbf\xbf", 0),
		ROW("\xf5\x80\x80\x80", 0), ROW("\xf7\xbf\xbf\xbf", 0),
		ROW("\xf8\x88\x80\x80\x80", 0), ROW("\xf6\x80\x80\x80", 0), ROW("\xfd\x80\x80\x80\x80\x80", 0),
		ROW("\xfe", 0), ROW("\xff", 0),
	};
	assert_valid_len_rows(rows, COUNT(rows));
}

/** @brief A sequence cut short by the end of the string or by another byte is invalid. */
static void test_utf8_valid_len_rejects_truncated_sequences(void) {
	const row_t rows[] = {
		ROW("\xc3", 0), ROW("\xe2", 0), ROW("\xe2\x82", 0), ROW("\xf0", 0), ROW("\xf0\x9f", 0), ROW("\xf0\x9f\x98", 0),
		ROW("\xc3z", 0), ROW("\xe2\x82z", 0), ROW("\xf0\x9f\x98z", 0), ROW("\xe2z\xac", 0),
		ROW("\xc3\xc3\xa9", 0), /* a lead byte in place of the continuation byte */
		ROW("\xc2\x7f", 0), ROW("\xc3\x1b", 0), ROW("\xe2\x1b[2J", 0), /* a control byte is not a continuation byte */
		ROW("\xe1\x80\x7f", 0), ROW("\xf1\x80\x80\x7f", 0), ROW("\xf1\x80\x7f\x80", 0),
	};
	assert_valid_len_rows(rows, COUNT(rows));
}

/** @brief A continuation byte with no lead byte is invalid. */
static void test_utf8_valid_len_rejects_stray_continuation_bytes(void) {
	const row_t rows[] = { ROW("\x80", 0), ROW("\xa9", 0), ROW("\xbf", 0), ROW("\x80\x80", 0) };
	assert_valid_len_rows(rows, COUNT(rows));
}

/** @brief A cell is the valid sequence, or exactly one byte when the sequence is invalid; 0 at the end. */
static void test_utf8_cell_len(void) {
	const row_t rows[] = {
		ROW("a", 1), ROW("\xc3\xa9", 2), ROW("\xe2\x82\xac", 3), ROW("\xf0\x9f\x98\x80", 4),
		ROW("\xc2\x85", 2),                      /* a C1 control is a valid sequence: one 2-byte cell */
		ROW("\xff", 1), ROW("\x80", 1), ROW("\xc0\x80", 1),
		ROW("\xe2\x82", 1), ROW("\xe2\x82z", 1), ROW("\xed\xa0\x80", 1),
		ROW("", 0),
	};
	for (size_t i = 0; i < COUNT(rows); i++) {
		char *s = exact_copy(rows[i].bytes, rows[i].len);
		size_t got = utf8_cell_len(s);
		free(s);
		char msg[32];
		snprintf(msg, sizeof(msg), "row %zu", i);
		TEST_ASSERT_EQUAL_UINT_MESSAGE(rows[i].expected, got, msg);
	}
}

/** @brief Exactly U+0080 to U+009F are C1 controls. */
static void test_utf8_is_c1_covers_exactly_the_c1_range(void) {
	char s[3] = { '\xc2', '\0', '\0' };
	for (int c = 0x80; c <= 0xbf; c++) {
		s[1] = (char)c;
		char msg[32];
		snprintf(msg, sizeof(msg), "0xC2 0x%02X", c);
		TEST_ASSERT_EQUAL_INT_MESSAGE(c <= 0x9f ? 1 : 0, utf8_is_c1(s), msg);
	}
}

/** @brief Other characters and invalid input are not C1 controls. */
static void test_utf8_is_c1_is_false_for_everything_else(void) {
	const char *not_c1[] = { "a", "", "\xc3\x85", "\xc2", "\xc2z", "\xc2\x7f", "\xe2\x82\xac", "\x85", "\xc0\x85", "\xc2\xa0" };
	for (size_t i = 0; i < COUNT(not_c1); i++) {
		char *s = exact_copy(not_c1[i], strlen(not_c1[i]));
		int got = utf8_is_c1(s);
		free(s);
		char msg[32];
		snprintf(msg, sizeof(msg), "case %zu", i);
		TEST_ASSERT_EQUAL_INT_MESSAGE(0, got, msg);
	}
}

/** @brief Walking back from the end of "a", e-acute, euro sign and the emoji lands on each start. */
static void test_utf8_prev_over_valid_characters(void) {
	const char line[] = "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80"; /* starts at 0, 1, 3, 6; length 10 */
	char *s = exact_copy(line, sizeof(line) - 1);
	size_t at10 = utf8_prev(s, 10), at6 = utf8_prev(s, 6), at3 = utf8_prev(s, 3), at1 = utf8_prev(s, 1), at0 = utf8_prev(s, 0);
	free(s);
	TEST_ASSERT_EQUAL_UINT(6, at10);
	TEST_ASSERT_EQUAL_UINT(3, at6);
	TEST_ASSERT_EQUAL_UINT(1, at3);
	TEST_ASSERT_EQUAL_UINT(0, at1);
	TEST_ASSERT_EQUAL_UINT(0, at0);
}

/** @brief Each invalid byte, and a whole C1 control, is one cell when walking back. */
static void test_utf8_prev_over_invalid_bytes_and_c1(void) {
	const char line[] = "\xe2\x82z\xc2\x85y\xff"; /* cells start at 0 (e2), 1 (82), 2 (z), 3 (C1, 2 bytes), 5 (y), 6 (ff); length 7 */
	char *s = exact_copy(line, sizeof(line) - 1);
	size_t at7 = utf8_prev(s, 7), at6 = utf8_prev(s, 6), at5 = utf8_prev(s, 5), at3 = utf8_prev(s, 3);
	size_t at2 = utf8_prev(s, 2), at1 = utf8_prev(s, 1);
	free(s);
	TEST_ASSERT_EQUAL_UINT(6, at7);
	TEST_ASSERT_EQUAL_UINT(5, at6);
	TEST_ASSERT_EQUAL_UINT(3, at5); /* back over the two bytes of the C1 control at once */
	TEST_ASSERT_EQUAL_UINT(2, at3);
	TEST_ASSERT_EQUAL_UINT(1, at2);
	TEST_ASSERT_EQUAL_UINT(0, at1);
}

/** @brief A stray continuation byte after a valid character is a cell of its own: a backward scan for a lead byte would get this wrong. */
static void test_utf8_prev_with_a_stray_continuation_byte_after_a_character(void) {
	char *a = exact_copy("\xc3\xa9\x80", 3);                          /* cells at 0 and 2 */
	size_t a3 = utf8_prev(a, 3), a2 = utf8_prev(a, 2);
	free(a);
	char *b = exact_copy("\x80\x80\xe2\x82\xac", 5);                  /* cells at 0, 1, 2 */
	size_t b5 = utf8_prev(b, 5), b2 = utf8_prev(b, 2), b1 = utf8_prev(b, 1);
	free(b);
	char *c = exact_copy("\xf0\x9f\x98\x80\x80", 5);                  /* cells at 0 and 4 */
	size_t c5 = utf8_prev(c, 5), c4 = utf8_prev(c, 4);
	free(c);
	TEST_ASSERT_EQUAL_UINT(2, a3);
	TEST_ASSERT_EQUAL_UINT(0, a2);
	TEST_ASSERT_EQUAL_UINT(2, b5);
	TEST_ASSERT_EQUAL_UINT(1, b2);
	TEST_ASSERT_EQUAL_UINT(0, b1);
	TEST_ASSERT_EQUAL_UINT(4, c5);
	TEST_ASSERT_EQUAL_UINT(0, c4);
}

/** @brief An index inside a character gives the start of that character; the editor uses this to snap the cursor back. */
static void test_utf8_prev_inside_a_character_gives_its_start(void) {
	char *s = exact_copy("a\xe2\x82\xac" "b", 5); /* cells at 0, 1 (3 bytes), 4 */
	size_t i2 = utf8_prev(s, 2), i3 = utf8_prev(s, 3), i4 = utf8_prev(s, 4);
	free(s);
	TEST_ASSERT_EQUAL_UINT(1, i2);
	TEST_ASSERT_EQUAL_UINT(1, i3);
	TEST_ASSERT_EQUAL_UINT(1, i4);
}

/** @brief Assert utf8_cell_cols() of @p bytes (@p len bytes, copied to an exact-size block) is @p expected. */
static void assert_cell_cols(const char *bytes, size_t len, size_t expected) {
	char *s = exact_copy(bytes, len);
	size_t got = utf8_cell_cols(s);
	free(s);
	char msg[32];
	snprintf(msg, sizeof(msg), "first byte 0x%02x", (unsigned char)bytes[0]);
	TEST_ASSERT_EQUAL_UINT_MESSAGE(expected, got, msg);
}

/** @brief Assert utf8_cell_len() of @p bytes is @p expected, on an exact-size block. */
static void assert_cell_len(const char *bytes, size_t len, size_t expected) {
	char *s = exact_copy(bytes, len);
	size_t got = utf8_cell_len(s);
	free(s);
	char msg[32];
	snprintf(msg, sizeof(msg), "first byte 0x%02x", (unsigned char)bytes[0]);
	TEST_ASSERT_EQUAL_UINT_MESSAGE(expected, got, msg);
}

/** @brief East Asian wide, fullwidth and emoji characters take two columns (the widths of Vim 9.1); everything else takes one. */
static void test_utf8_cell_cols_wide_characters_take_two_columns(void) {
	assert_cell_cols("\xe6\x97\xa5", 3, 2);         /* U+65E5 */
	assert_cell_cols("\xe3\x80\x80", 3, 2);         /* U+3000 ideographic space */
	assert_cell_cols("\xef\xbc\xa1", 3, 2);         /* U+FF21 fullwidth A */
	assert_cell_cols("\xe1\x84\x80", 3, 2);         /* U+1100 Hangul Jamo */
	assert_cell_cols("\xf0\x9f\x98\x80", 4, 2);     /* U+1F600 */
	assert_cell_cols("\xf0\x9f\x87\xa6", 4, 2);     /* U+1F1E6 regional indicator */
	assert_cell_cols("\xf0\xa0\x80\x80", 4, 2);     /* U+20000 */
}

/** @brief Narrow characters, invalid bytes and the end of the string. */
static void test_utf8_cell_cols_other_cells_take_one_column(void) {
	assert_cell_cols("a", 1, 1);
	assert_cell_cols("\xc3\xa9", 2, 1);             /* U+00E9 */
	assert_cell_cols("\xc2\xa0", 2, 1);             /* U+00A0 */
	assert_cell_cols("\xef\xbd\xa1", 3, 1);         /* U+FF61 halfwidth ideographic full stop */
	assert_cell_cols("\xff", 1, 1);                 /* invalid byte */
	assert_cell_cols("\xe6\x97", 2, 1);             /* truncated sequence: its first byte is a cell */
	assert_cell_cols("", 0, 0);
}

/** @brief A cell takes the width of its base: marks after it add nothing, and a mark with no base takes one column. */
static void test_utf8_cell_cols_ignores_the_marks_after_the_base(void) {
	assert_cell_cols("e\xcc\x81", 3, 1);             /* e + U+0301 */
	assert_cell_cols("\xe6\x97\xa5\xcc\x81", 5, 2); /* wide base + U+0301 */
	assert_cell_cols("\xcc\x81", 2, 1);             /* U+0301 alone */
}

/** @brief A cell is a base character plus the combining marks that follow it. */
static void test_utf8_cell_len_takes_the_combining_marks_after_a_base(void) {
	assert_cell_len("e\xcc\x81x", 4, 3);                 /* e + U+0301, then x */
	assert_cell_len("e\xcc\x81\xcc\x82", 5, 5);          /* two marks */
	assert_cell_len("\xe6\x97\xa5\xcc\x81", 5, 5);       /* wide base */
	assert_cell_len("\xcc\x81x", 3, 2);                  /* a mark with no base is a cell of its own */
	assert_cell_len("\xcc\x81\xcc\x82", 4, 2);           /* and a second mark after it is the next cell */
	assert_cell_len("a\xff\xcc\x81", 4, 1);              /* an invalid byte is no base */
	assert_cell_len("e\xff\xcc\x81", 4, 1);              /* and does not let a mark through */
}

/** @brief A tab, an ASCII control (drawn as ^A) and a C1 control (drawn as ?) take no marks: the mark is a cell of its own. */
static void test_utf8_cell_len_gives_no_marks_to_controls(void) {
	assert_cell_len("\t\xcc\x81", 3, 1);
	assert_cell_len("\x01\xcc\x81", 3, 1);
	assert_cell_len("\x7f\xcc\x81", 3, 1);
	assert_cell_len("\xc2\x85\xcc\x81", 4, 2);
}

/** @brief Stepping back lands on the base of a cell, from after it or from inside its marks. */
static void test_utf8_prev_steps_back_over_the_marks_of_a_cell(void) {
	char *s = exact_copy("xe\xcc\x81\xcc\x82y", 8);     /* x, e + two marks (bytes 1 to 5), y at 6; 8 bytes */
	size_t at6 = utf8_prev(s, 6), at5 = utf8_prev(s, 5), at3 = utf8_prev(s, 3), at2 = utf8_prev(s, 2), at1 = utf8_prev(s, 1);
	free(s);
	TEST_ASSERT_EQUAL_UINT(1, at6); /* after the group: its base */
	TEST_ASSERT_EQUAL_UINT(1, at5); /* inside the second mark */
	TEST_ASSERT_EQUAL_UINT(1, at3); /* after the first mark */
	TEST_ASSERT_EQUAL_UINT(1, at2); /* inside the first mark */
	TEST_ASSERT_EQUAL_UINT(0, at1); /* after x */
}

/** @brief The old utf8_prev(): walk forward from the start of the line, the definition of a cell. */
static size_t reference_prev(const char *line, size_t i) {
	size_t start = 0, pos = 0;
	while (line[pos] && pos < i) {
		start = pos;
		pos += utf8_cell_len(line + pos);
	}
	return start;
}

/** @brief Assert utf8_prev() agrees with the forward walk for every index of every string of 1 to @p maxlen bytes over the @p n bytes of @p set. */
static void assert_prev_agrees_over(const unsigned char *set, size_t n, size_t maxlen) {
	for (size_t len = 1; len <= maxlen; len++) {
		size_t total = 1;
		for (size_t k = 0; k < len; k++) total *= n;
		for (size_t code = 0; code < total; code++) {
			char *s = malloc(len + 1);
			TEST_ASSERT_NOT_NULL(s);
			size_t c = code;
			for (size_t k = 0; k < len; k++, c /= n) s[k] = (char)set[c % n];
			s[len] = '\0';
			for (size_t i = 0; i <= len; i++)
				if (utf8_prev(s, i) != reference_prev(s, i)) TEST_FAIL_MESSAGE("utf8_prev differs from the forward walk");
			free(s);
		}
	}
}

/** @brief utf8_prev() agrees with the forward walk for every index of every string up to 5 bytes over a byte set that covers each UTF-8 boundary. */
static void test_utf8_prev_agrees_with_the_forward_walk(void) {
	static const unsigned char set[] = {'a', 0x7f, 0x80, 0xbf, 0xc0, 0xc2, 0xdf, 0xe0, 0xed, 0xef, 0xf0, 0xf4, 0xf5, 0xff, 0x90, 0xa0};
	assert_prev_agrees_over(set, sizeof set, 5);
}

/** @brief The same with combining marks (U+0301, U+0302), a tab, a C1 control, a wide character, an invalid byte and a stray continuation byte: every way a mark can follow or not follow a base. */
static void test_utf8_prev_agrees_with_the_forward_walk_with_marks(void) {
	static const unsigned char set[] = {'a', '\t', 0xc2, 0x85, 0xcc, 0x81, 0x82, 0xe6, 0x97, 0xa5, 0xff};
	assert_prev_agrees_over(set, sizeof set, 5);
}

/** @brief Register every test in this file with Unity. */
void test_utf8_suite(void) {
	RUN_TEST(test_utf8_valid_len_ascii);
	RUN_TEST(test_utf8_valid_len_multibyte);
	RUN_TEST(test_utf8_valid_len_stops_after_the_first_character);
	RUN_TEST(test_utf8_valid_len_rejects_overlong_forms);
	RUN_TEST(test_utf8_valid_len_rejects_surrogates);
	RUN_TEST(test_utf8_valid_len_rejects_code_points_above_10ffff);
	RUN_TEST(test_utf8_valid_len_rejects_truncated_sequences);
	RUN_TEST(test_utf8_valid_len_rejects_stray_continuation_bytes);
	RUN_TEST(test_utf8_cell_len);
	RUN_TEST(test_utf8_is_c1_covers_exactly_the_c1_range);
	RUN_TEST(test_utf8_is_c1_is_false_for_everything_else);
	RUN_TEST(test_utf8_prev_over_valid_characters);
	RUN_TEST(test_utf8_prev_over_invalid_bytes_and_c1);
	RUN_TEST(test_utf8_prev_with_a_stray_continuation_byte_after_a_character);
	RUN_TEST(test_utf8_prev_inside_a_character_gives_its_start);
	RUN_TEST(test_utf8_prev_agrees_with_the_forward_walk);
	RUN_TEST(test_utf8_prev_agrees_with_the_forward_walk_with_marks);
	RUN_TEST(test_utf8_cell_cols_wide_characters_take_two_columns);
	RUN_TEST(test_utf8_cell_cols_other_cells_take_one_column);
	RUN_TEST(test_utf8_cell_cols_ignores_the_marks_after_the_base);
	RUN_TEST(test_utf8_cell_len_takes_the_combining_marks_after_a_base);
	RUN_TEST(test_utf8_cell_len_gives_no_marks_to_controls);
	RUN_TEST(test_utf8_prev_steps_back_over_the_marks_of_a_cell);
}
