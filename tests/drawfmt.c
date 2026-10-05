/**
 * @file drawfmt.c
 * @brief Implementation of drawfmt.h.
 */
#include <stdio.h>
#include <string.h>
#include "drawfmt.h"

/** @brief Number of UTF-8 characters in the first @p len bytes of @p s: its display width once rendered. */
static size_t width_of(const char *s, size_t len) {
	size_t w = 0;
	for (size_t i = 0; i < len; i++) if (((unsigned char)s[i] & 0xc0) != 0x80) w++;
	return w;
}

void draw_expected(char *buf, size_t size, const char *text, size_t max_rows, size_t max_cols, int row, int col) {
	size_t pos = (size_t)snprintf(buf, size, "\x1b[?25l\x1b[H");
	size_t rows = 0;
	if (text) {
		const char *p = text;
		for (;;) {
			const char *sep = strstr(p, "\r\n");
			size_t len = sep ? (size_t)(sep - p) : strlen(p);
			pos += (size_t)snprintf(buf + pos, size - pos, "%.*s%s%s", (int)len, p,
			                        width_of(p, len) < max_cols ? "\x1b[K" : "", sep ? "\r\n" : "");
			rows++;
			if (!sep) break;
			p = sep + 2;
		}
	}
	if (rows < max_rows) pos += (size_t)snprintf(buf + pos, size - pos, "\x1b[%zu;1H\x1b[J", rows + 1);
	snprintf(buf + pos, size - pos, "\x1b[%d;%dH\x1b[?25h", row, col);
}

void draw_expected_status(char *buf, size_t size, const char *text, size_t max_rows, size_t max_cols, const char *status,
                          size_t status_row, int row, int col) {
	draw_expected(buf, size, text, max_rows, max_cols, row, col);
	if (!status) return;
	char tail[64];
	size_t tail_len = (size_t)snprintf(tail, sizeof(tail), "\x1b[%d;%dH\x1b[?25h", row, col);
	size_t pos = strlen(buf) - tail_len; /* the status goes in front of the cursor tail */
	pos += (size_t)snprintf(buf + pos, size - pos, "\x1b[%zu;1H\x1b[7m%s\x1b[m", status_row, status);
	snprintf(buf + pos, size - pos, "%s", tail);
}

void status_expected(char *buf, size_t size, const char *name, int dos, int modified, const char *mode, size_t line, size_t col, size_t cols) {
	char left[512], right[64];
	snprintf(left, sizeof(left), "%s%s%s %s", name ? name : "[No Name]", dos ? " [dos]" : "", modified ? " [+]" : "", mode);
	size_t right_w = (size_t)snprintf(right, sizeof(right), "%zu,%zu", line, col);
	if (right_w >= cols) {
		snprintf(buf, size, "%.*s", (int)cols, right);
	} else {
		size_t avail = cols - right_w - 1;
		size_t left_w = strlen(left) < avail ? strlen(left) : avail;
		snprintf(buf, size, "%.*s%*s%s", (int)left_w, left, (int)(cols - right_w - left_w), "", right);
	}
}
