/**
 * @file utf8.c
 * @brief Implementation of utf8.h. Public symbols are documented there.
 */
#include "utf8.h"

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

size_t utf8_cell_len(const char *s) {
	size_t n = utf8_valid_len(s);
	return n ? n : (*s ? 1 : 0);
}

int utf8_is_c1(const char *s) {
	const unsigned char *p = (const unsigned char *)s;
	return p[0] == 0xc2 && p[1] >= 0x80 && p[1] <= 0x9f;
}

size_t utf8_prev(const char *line, size_t i) {
	size_t start = 0, pos = 0;
	while (line[pos] && pos < i) { /* cells are defined by decoding forward, so walk forward */
		start = pos;
		pos += utf8_cell_len(line + pos);
	}
	return start;
}
