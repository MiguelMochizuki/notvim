/**
 * @file editor.c
 * @brief Implementation of editor.h. Public symbols are documented there.
 */
#define _POSIX_C_SOURCE 200809L /* getline under -std=c11 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "editor.h"
#include "utf8.h"

int editor_should_exit(char c) {
	return c == 0x11; /* Ctrl+Q = DC1 */
}

void editor_init(editor_t *e) {
	e->lines = NULL;
	e->count = 0;
	e->cap = 0;
	e->crlf = 0;
	e->cy = 0;
	e->cx = 0;
	e->rowoff = 0;
}

void editor_free(editor_t *e) {
	for (size_t i = 0; i < e->count; i++) free(e->lines[i]);
	free(e->lines);
	editor_init(e);
}

size_t editor_line_count(const editor_t *e) {
	return e->count;
}

const char *editor_line(const editor_t *e, size_t i) {
	return i < e->count ? e->lines[i] : NULL;
}

int editor_append_line(editor_t *e, const char *text) {
	size_t len = strlen(text);
	char *copy = malloc(len + 1);
	if (!copy) return -1;
	memcpy(copy, text, len + 1);

	if (e->count == e->cap) {
		size_t cap = e->cap ? e->cap * 2 : 8;
		char **lines = realloc(e->lines, cap * sizeof(*lines));
		if (!lines) {
			free(copy);
			return -1;
		}
		e->lines = lines;
		e->cap = cap;
	}
	e->lines[e->count++] = copy;
	return 0;
}

/** @brief Copy up to @p len bytes of @p src to @p out at *pos without passing @p max. */
static void put(char *out, size_t *pos, size_t max, const char *src, size_t len) {
	size_t room = max - *pos;
	size_t n = len < room ? len : room;
	memcpy(out + *pos, src, n);
	*pos += n;
}

void editor_scroll(editor_t *e, size_t rows) {
	if (rows == 0) return;
	if (e->cy < e->rowoff) {
		e->rowoff = e->cy;
	} else if (e->cy >= e->rowoff + rows) {
		e->rowoff = e->cy - rows + 1;
	}
}

/** @brief Start of the last cell on line @p y (a character or an invalid byte), or 0 for an empty line. */
static size_t last_col(const editor_t *e, size_t y) {
	return utf8_prev(e->lines[y], strlen(e->lines[y]));
}

void editor_move_cursor(editor_t *e, editor_move_t dir) {
	if (e->count == 0) return;
	const char *line;
	switch (dir) {
	case EDITOR_MOVE_UP:
		if (e->cy > 0) e->cy--;
		break;
	case EDITOR_MOVE_DOWN:
		if (e->cy + 1 < e->count) e->cy++;
		break;
	case EDITOR_MOVE_LEFT:
		line = e->lines[e->cy];
		e->cx = utf8_prev(line, e->cx);
		break;
	case EDITOR_MOVE_RIGHT:
		line = e->lines[e->cy];
		e->cx += utf8_cell_len(line + e->cx); /* clamped below */
		break;
	}
	size_t last = last_col(e, e->cy);
	if (e->cx > last) e->cx = last;
	else e->cx = utf8_prev(e->lines[e->cy], e->cx + 1); /* a vertical move can land inside a character: back to its start */
}

/** Tabs stop at every multiple of this many columns. */
#define TAB_STOP 8

/** @brief Whether byte @p c is a control byte, which is drawn as a mark. A tab is not: it is drawn as spaces. */
static int is_control(unsigned char c) {
	return (c < 0x20 && c != '\t') || c == 0x7f;
}

/**
 * @brief Width in columns of the cell at @p p drawn at column @p col: a tab goes to the next tab stop,
 *        a mark such as ^A is two, anything else (a character, '?' for an invalid byte or C1) is one.
 */
static size_t cell_width(const char *p, size_t col) {
	unsigned char c = (unsigned char)*p;
	if (c == '\t') return TAB_STOP - col % TAB_STOP;
	return is_control(c) ? 2 : 1;
}

/** @brief Display column of byte index @p cx in @p line: the width of the cells before it. */
static size_t display_col(const char *line, size_t cx) {
	size_t col = 0;
	for (size_t i = 0; i < cx && line[i]; i += utf8_cell_len(line + i)) col += cell_width(line + i, col);
	return col;
}

/** @brief Append @p line to @p out at *pos as drawn, clipped to @p max_cols columns without cutting a character or a mark. */
static void put_line(const char *line, size_t max_cols, char *out, size_t *pos, size_t max) {
	static const char spaces[TAB_STOP + 1] = "        ";
	size_t col = 0;
	for (const char *p = line; *p; p += utf8_cell_len(p)) {
		size_t w = cell_width(p, col);
		size_t room = max_cols - col;
		if (*p == '\t') {
			size_t n = w < room ? w : room; /* spaces can be cut anywhere */
			put(out, pos, max, spaces, n);
			col += n; /* if the tab was cut, col == max_cols and the loop ends below */
		} else if (w > room) {
			break; /* never show half of a mark, and room is 0 at the right edge */
		} else if (is_control((unsigned char)*p)) {
			char mark[2] = { '^', *p == 0x7f ? '?' : (char)(*p ^ 0x40) };
			put(out, pos, max, mark, 2);
			col += w;
		} else if (utf8_valid_len(p) == 0 || utf8_is_c1(p)) {
			put(out, pos, max, "?", 1); /* cannot act on the terminal */
			col += w;
		} else {
			put(out, pos, max, p, utf8_cell_len(p));
			col += w;
		}
	}
}

size_t editor_render(const editor_t *e, size_t max_rows, size_t max_cols, char *out, size_t out_size) {
	if (out_size == 0) return 0;
	size_t max = out_size - 1; /* room for the NUL */
	size_t pos = 0;
	size_t avail = e->rowoff < e->count ? e->count - e->rowoff : 0;
	if (avail > max_rows) avail = max_rows;
	for (size_t i = 0; i < avail; i++) {
		if (i > 0) put(out, &pos, max, "\r\n", 2);
		put_line(e->lines[e->rowoff + i], max_cols, out, &pos, max);
	}
	out[pos] = '\0';
	return pos;
}

int editor_load_file(editor_t *e, const char *path) {
	editor_free(e); /* drop any previous contents */

	FILE *fp = fopen(path, "r");
	if (!fp) {
		return errno == ENOENT ? 0 : -1; /* Nonexistent file is not an error */
	}

	char *line = NULL;
	size_t cap = 0;
	ssize_t n;
	int rc = 0;
	size_t terminated = 0;  /* lines ended by '\n' */
	int all_cr = 1;         /* whether each of them has '\r' right before the '\n' */
	int last_terminated = 0;
	while ((n = getline(&line, &cap, fp)) >= 0) {
		if (memchr(line, '\0', (size_t)n)) { /* a C string would cut the line here */
			errno = EILSEQ;
			rc = -1;
			break;
		}
		last_terminated = line[n - 1] == '\n';
		if (last_terminated) {
			terminated++;
			if (n < 2 || line[n - 2] != '\r') all_cr = 0;
			line[n - 1] = '\0';
		}
		if (editor_append_line(e, line) < 0) {
			rc = -1;
			break;
		}
	}
	if (rc == 0 && ferror(fp)) rc = -1;
	if (rc == 0 && terminated > 0 && all_cr) {
		/* the whole file has been seen: it is CRLF, so drop the CR of each terminated line (not of a last line without newline) */
		e->crlf = 1;
		for (size_t i = 0; i < e->count - (last_terminated ? 0 : 1); i++) e->lines[i][strlen(e->lines[i]) - 1] = '\0';
	}

	int saved = errno;
	free(line);
	fclose(fp);
	if (rc < 0) {
		editor_free(e);
	}
	errno = saved;
	return rc;
}

/** Clear screen and move home. */
#define CLEAR_HOME "\x1b[H\x1b[2J"
/** Length of CLEAR_HOME. */
#define CLEAR_HOME_LEN 7
/** Upper bound for the cursor sequence "ESC [ row ; col H" with two 20-digit numbers. */
#define CURSOR_SEQ_MAX 44

size_t editor_draw(const editor_t *e, size_t max_rows, size_t max_cols, char *out, size_t out_size) {
	if (out_size < EDITOR_DRAW_OVERHEAD) {
		if (out_size) out[0] = '\0';
		return 0;
	}
	memcpy(out, CLEAR_HOME, CLEAR_HOME_LEN);
	size_t pos = CLEAR_HOME_LEN;
	pos += editor_render(e, max_rows, max_cols, out + pos, out_size - pos - CURSOR_SEQ_MAX);
	size_t row = e->cy >= e->rowoff ? e->cy - e->rowoff : 0;
	size_t col = e->cy < e->count ? display_col(e->lines[e->cy], e->cx) : 0;
	if (max_cols > 0 && col >= max_cols) col = max_cols - 1;
	pos += (size_t)snprintf(out + pos, CURSOR_SEQ_MAX + 1, "\x1b[%zu;%zuH", row + 1, col + 1);
	return pos;
}
