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

size_t editor_render(const editor_t *e, size_t max_rows, char *out, size_t out_size) {
	if (out_size == 0) return 0;
	size_t max = out_size - 1; /* room for the NUL */
	size_t pos = 0;
	for (size_t i = 0; i < e->count && i < max_rows; i++) {
		if (i > 0) put(out, &pos, max, "\r\n", 2);
		put(out, &pos, max, e->lines[i], strlen(e->lines[i]));
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
