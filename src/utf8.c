/**
 * @file utf8.c
 * @brief Implementation of utf8.h. Public symbols are documented there.
 */
#include "utf8.h"

/** A range of code points, both ends included. */
typedef struct {
	unsigned lo; /**< First code point. */
	unsigned hi; /**< Last code point. */
} range_t;

#include "width_table.inc" /* wide_ranges and combining_ranges, generated from Vim */

/** @brief Whether byte @p c is a continuation byte, 0x80 to 0xBF. A NUL is not one, so no read goes past the end. */
static int is_continuation(unsigned char c) {
	return c >= 0x80 && c <= 0xbf;
}

size_t utf8_valid_len(const char *s) {
	const unsigned char *p = (const unsigned char *)s;
	if (p[0] < 0x80) return p[0] ? 1 : 0;
	if (p[0] >= 0xc2 && p[0] <= 0xdf) return is_continuation(p[1]) ? 2 : 0;
	if (p[0] >= 0xe0 && p[0] <= 0xef) {
		/* E0 needs A0..BF (no overlong), ED needs 80..9F (no surrogate) */
		unsigned char lo = p[0] == 0xe0 ? 0xa0 : 0x80, hi = p[0] == 0xed ? 0x9f : 0xbf;
		return p[1] >= lo && p[1] <= hi && is_continuation(p[2]) ? 3 : 0;
	}
	if (p[0] >= 0xf0 && p[0] <= 0xf4) {
		/* F0 needs 90..BF (no overlong), F4 needs 80..8F (nothing above U+10FFFF) */
		unsigned char lo = p[0] == 0xf0 ? 0x90 : 0x80, hi = p[0] == 0xf4 ? 0x8f : 0xbf;
		return p[1] >= lo && p[1] <= hi && is_continuation(p[2]) && is_continuation(p[3]) ? 4 : 0;
	}
	return 0; /* stray continuation byte, C0, C1, F5 to FF */
}

/** @brief Code point of the valid sequence of @p n bytes at @p s. */
static unsigned decode(const char *s, size_t n) {
	const unsigned char *p = (const unsigned char *)s;
	if (n == 1) return p[0];
	unsigned cp = p[0] & (0xff >> (n + 1));
	for (size_t i = 1; i < n; i++) cp = cp << 6 | (p[i] & 0x3f);
	return cp;
}

/** @brief Whether @p cp is inside one of the @p count sorted @p ranges (binary search). */
static int in_ranges(const range_t *ranges, size_t count, unsigned cp) {
	size_t lo = 0, hi = count;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (cp < ranges[mid].lo) hi = mid;
		else if (cp > ranges[mid].hi) lo = mid + 1;
		else return 1;
	}
	return 0;
}

/** @brief Whether the character at @p s is a combining mark: one that adds no column after a base. */
static int is_mark(const char *s) {
	if ((unsigned char)s[0] < 0xcc) return 0; /* U+0300 is the first, and this keeps ASCII off the table */
	size_t n = utf8_valid_len(s);
	return n && in_ranges(combining_ranges, sizeof(combining_ranges) / sizeof(*combining_ranges), decode(s, n));
}

/** @brief Whether marks that follow the character at @p s attach to it: it is valid and drawn as itself, not a control (^A or ?) and not a mark. */
static int takes_marks(const char *s) {
	unsigned char c = (unsigned char)s[0];
	return utf8_valid_len(s) && c >= 0x20 && c != 0x7f && !utf8_is_c1(s) && !is_mark(s);
}

size_t utf8_cell_len(const char *s) {
	size_t n = utf8_valid_len(s);
	if (!n) return *s ? 1 : 0;
	if (!takes_marks(s)) return n;
	while (is_mark(s + n)) n += utf8_valid_len(s + n);
	return n;
}

size_t utf8_cell_cols(const char *s) {
	size_t n = utf8_valid_len(s);
	if (!n) return *s ? 1 : 0;
	return in_ranges(wide_ranges, sizeof(wide_ranges) / sizeof(*wide_ranges), decode(s, n)) ? 2 : 1;
}

int utf8_is_c1(const char *s) {
	const unsigned char *p = (const unsigned char *)s;
	return p[0] == 0xc2 && p[1] >= 0x80 && p[1] <= 0x9f;
}

/** @brief Start of the character (a valid sequence or an invalid byte) that contains byte @p i - 1 of @p line; @p i must be above 0. */
static size_t char_start(const char *line, size_t i) {
	/* A byte that is not a continuation byte always starts a character, and a character has at most 4 bytes: look back at most 3 bytes for the lead */
	size_t p = i - 1;
	while (p > 0 && i - 1 - p < 3 && is_continuation((unsigned char)line[p])) p--;
	if (is_continuation((unsigned char)line[p])) return i - 1; /* ran out of bytes: a stray continuation byte is its own character */
	return p + (utf8_valid_len(line + p) ? utf8_valid_len(line + p) : 1) > i - 1 ? p : i - 1; /* byte i - 1 is inside the character at p, or stray after it */
}

size_t utf8_prev(const char *line, size_t i) {
	if (i == 0) return 0;
	size_t p = char_start(line, i);
	if (!is_mark(line + p)) return p;
	size_t q = p; /* back over the marks to what they follow: a base takes them, anything else leaves each mark a cell of its own */
	while (q > 0 && is_mark(line + q)) q = char_start(line, q);
	return takes_marks(line + q) ? q : p;
}
