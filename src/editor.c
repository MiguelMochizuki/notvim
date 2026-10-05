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

int editor_should_exit(char c) {
	return c == 0x11; /* Ctrl+Q = DC1 */
}

void editor_init(editor_t *e) {
	e->lines = NULL;
	e->count = 0;
	e->cap = 0;
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

/** @brief Last valid column on line @p y: its last character, or 0 for an empty line. */
static size_t last_col(const editor_t *e, size_t y) {
	size_t len = strlen(e->lines[y]);
	return len ? len - 1 : 0;
}

void editor_move_cursor(editor_t *e, editor_move_t dir) {
	if (e->count == 0) return;
	switch (dir) {
	case EDITOR_MOVE_UP:
		if (e->cy > 0) e->cy--;
		break;
	case EDITOR_MOVE_DOWN:
		if (e->cy + 1 < e->count) e->cy++;
		break;
	case EDITOR_MOVE_LEFT:
		if (e->cx > 0) e->cx--;
		break;
	case EDITOR_MOVE_RIGHT:
		e->cx++; /* clamped below */
		break;
	}
	if (e->cx > last_col(e, e->cy)) e->cx = last_col(e, e->cy);
}

size_t editor_render(const editor_t *e, size_t max_rows, char *out, size_t out_size) {
	if (out_size == 0) return 0;
	size_t max = out_size - 1; /* room for the NUL */
	size_t pos = 0;
	size_t avail = e->rowoff < e->count ? e->count - e->rowoff : 0;
	if (avail > max_rows) avail = max_rows;
	for (size_t i = 0; i < avail; i++) {
		const char *line = e->lines[e->rowoff + i];
		if (i > 0) put(out, &pos, max, "\r\n", 2);
		put(out, &pos, max, line, strlen(line));
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
	while ((n = getline(&line, &cap, fp)) >= 0) {
		if (line[n - 1] == '\n') line[n - 1] = '\0';
		if (editor_append_line(e, line) < 0) {
			rc = -1;
			break;
		}
	}
	if (rc == 0 && ferror(fp)) rc = -1;

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

size_t editor_draw(const editor_t *e, size_t max_rows, char *out, size_t out_size) {
	if (out_size < EDITOR_DRAW_OVERHEAD) {
		if (out_size) out[0] = '\0';
		return 0;
	}
	memcpy(out, CLEAR_HOME, CLEAR_HOME_LEN);
	size_t pos = CLEAR_HOME_LEN;
	pos += editor_render(e, max_rows, out + pos, out_size - pos - CURSOR_SEQ_MAX);
	size_t row = e->cy >= e->rowoff ? e->cy - e->rowoff : 0;
	pos += (size_t)snprintf(out + pos, CURSOR_SEQ_MAX + 1, "\x1b[%zu;%zuH", row + 1, e->cx + 1);
	return pos;
}
