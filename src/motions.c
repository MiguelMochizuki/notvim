/**
 * @file motions.c
 * @brief Implementation of motions.h. Public symbols are documented there.
 *
 * The word motions are a port of Vim's fwd_word(), bck_word() and end_word(): a cursor steps over
 * the characters and, between lines, over the NUL that ends each one (a blank), exactly as Vim does.
 */
#include <string.h>
#include "motions.h"
#include "utf8.h"

/** @brief The character at @p p as a code point; an invalid byte as its own value, as Vim does. */
static unsigned long decode(const char *p) {
	const unsigned char *u = (const unsigned char *)p;
	switch (utf8_valid_len(p)) {
	case 1: return u[0];
	case 2: return (u[0] & 0x1fUL) << 6 | (u[1] & 0x3fUL);
	case 3: return (u[0] & 0x0fUL) << 12 | (u[1] & 0x3fUL) << 6 | (u[2] & 0x3fUL);
	case 4: return (u[0] & 0x07UL) << 18 | (u[1] & 0x3fUL) << 12 | (u[2] & 0x3fUL) << 6 | (u[3] & 0x3fUL);
	default: return u[0];
	}
}

/** @brief Vim's utf_class() for code point @p c, reduced: 0 blank, 1 punctuation, 2 word, other values for the scripts that are words of their own. */
static unsigned long char_class(unsigned long c) {
	if (c == 0 || c == ' ' || c == '\t' || c == 0xa0) return 0;
	if (c < 0x100) return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c >= 0xc0 ? 2 : 1;
	if ((c >= 0x2000 && c <= 0x200b) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000 || c == 0x1680) return 0;
	if ((c >= 0x200c && c <= 0x27ff && !(c >= 0x2070 && c <= 0x207f) && !(c >= 0x2080 && c <= 0x2094)) || (c >= 0x3001 && c <= 0x3020)) return 1;
	if (c >= 0x3040 && c <= 0x309f) return 0x3040; /* Hiragana */
	if (c >= 0x30a0 && c <= 0x30ff) return 0x30a0; /* Katakana */
	if ((c >= 0x3300 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff)) return 0x4e00; /* CJK ideographs */
	if (c >= 0xac00 && c <= 0xd7a3) return 0xac00; /* Hangul */
	return 2;
}

/** @brief Class of the character at @p pos (the NUL at the end of a line is a blank); with @p big every non-blank is one class. */
static unsigned long cls(const editor_t *e, motion_pos_t pos, int big) {
	unsigned long c = char_class(decode(e->lines[pos.y] + pos.x));
	return c != 0 && big ? 1 : c;
}

/** @brief Whether @p pos is on an empty line. */
static int on_empty_line(const editor_t *e, motion_pos_t pos) {
	return e->lines[pos.y][0] == '\0';
}

/**
 * @brief Vim's inc(): one character on, onto the NUL that ends the line, then to the start of the next line.
 * @return 0 if it moved inside the line, 2 if it landed on the NUL, 1 if it went to the next line, -1 at the end of the text (not moved).
 */
static int step_fwd(const editor_t *e, motion_pos_t *pos) {
	const char *line = e->lines[pos->y];
	if (line[pos->x]) {
		pos->x += utf8_cell_len(line + pos->x);
		return line[pos->x] ? 0 : 2;
	}
	if (pos->y + 1 < e->count) {
		pos->y++;
		pos->x = 0;
		return 1;
	}
	return -1;
}

/** @brief Vim's dec(): one character back, from column 0 onto the NUL that ends the previous line. @return 0 inside a line, 1 to the previous line, -1 at the start of the text (not moved). */
static int step_back(const editor_t *e, motion_pos_t *pos) {
	if (pos->x > 0) {
		pos->x = utf8_prev(e->lines[pos->y], pos->x);
		return 0;
	}
	if (pos->y > 0) {
		pos->y--;
		pos->x = strlen(e->lines[pos->y]);
		return 1;
	}
	return -1;
}

/** @brief Move @p pos over the characters of class @p c, forward or back; return 1 if it hit the end (or start) of the text. */
static int skip_class(const editor_t *e, motion_pos_t *pos, unsigned long c, int forward, int big) {
	while (cls(e, *pos, big) == c)
		if ((forward ? step_fwd(e, pos) : step_back(e, pos)) == -1) return 1;
	return 0;
}

/** @brief Pull @p pos off the NUL that ends a line onto its last character: a cursor in normal mode is never after the text. */
static motion_pos_t on_char(const editor_t *e, motion_pos_t pos) {
	const char *line = e->lines[pos.y];
	if (line[pos.x] == '\0') pos.x = utf8_prev(line, pos.x);
	return pos;
}

/** @brief The cursor, or (0, 0) when there are no lines. */
static motion_pos_t cursor(const editor_t *e) {
	motion_pos_t pos = { 0, 0 };
	if (e->count) {
		pos.y = e->cy;
		pos.x = e->cx;
	}
	return pos;
}

motion_pos_t motion_line_start(const editor_t *e) {
	motion_pos_t pos = cursor(e);
	pos.x = 0;
	return pos;
}

motion_pos_t motion_first_nonblank(const editor_t *e) {
	motion_pos_t pos = cursor(e);
	if (!e->count) return pos;
	const char *line = e->lines[pos.y];
	pos.x = strspn(line, " \t");
	return on_char(e, pos);
}

motion_pos_t motion_goto_line(const editor_t *e, size_t n) {
	motion_pos_t pos = cursor(e);
	if (!e->count) return pos;
	pos.y = n == 0 || n > e->count ? e->count - 1 : n - 1;
	pos.x = strspn(e->lines[pos.y], " \t");
	return on_char(e, pos);
}

motion_pos_t motion_line_end(const editor_t *e) {
	motion_pos_t pos = cursor(e);
	if (!e->count) return pos;
	pos.x = strlen(e->lines[pos.y]);
	return on_char(e, pos);
}

motion_pos_t motion_word_next(const editor_t *e, int big) {
	motion_pos_t pos = cursor(e);
	if (!e->count) return pos;
	unsigned long start = cls(e, pos, big);
	int last_line = pos.y + 1 == e->count;
	int i = step_fwd(e, &pos);
	if (i == -1 || (i >= 1 && last_line)) return on_char(e, pos);
	if (start != 0 && skip_class(e, &pos, start, 1, big)) return on_char(e, pos);
	while (cls(e, pos, big) == 0) { /* to the next non-blank, or an empty line */
		if (pos.x == 0 && on_empty_line(e, pos)) break;
		i = step_fwd(e, &pos);
		if (i == -1 || (i >= 1 && last_line)) break;
	}
	return on_char(e, pos);
}

motion_pos_t motion_word_prev(const editor_t *e, int big) {
	motion_pos_t pos = cursor(e);
	if (!e->count || step_back(e, &pos) == -1) return pos;
	while (cls(e, pos, big) == 0) { /* back over blanks, stopping on an empty line */
		if (pos.x == 0 && on_empty_line(e, pos)) return pos;
		if (step_back(e, &pos) == -1) return pos;
	}
	if (skip_class(e, &pos, cls(e, pos, big), 0, big)) return pos;
	step_fwd(e, &pos); /* overshot by one */
	return pos;
}

motion_pos_t motion_word_end(const editor_t *e, int big) {
	motion_pos_t pos = cursor(e);
	if (!e->count) return pos;
	unsigned long start = cls(e, pos, big);
	if (step_fwd(e, &pos) == -1) return on_char(e, pos);
	if (cls(e, pos, big) == start && start != 0) {
		if (skip_class(e, &pos, start, 1, big)) return on_char(e, pos);
	} else {
		while (cls(e, pos, big) == 0)
			if (step_fwd(e, &pos) == -1) return on_char(e, pos);
		if (skip_class(e, &pos, cls(e, pos, big), 1, big)) return on_char(e, pos);
	}
	step_back(e, &pos); /* overshot by one */
	return pos;
}
