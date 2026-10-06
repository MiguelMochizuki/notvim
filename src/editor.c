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
#include "commands.h"
#include "keys.h"
#include "motions.h"
#include "utf8.h"

void editor_init(editor_t *e) {
	e->lines = NULL;
	e->count = 0;
	e->cap = 0;
	e->crlf = 0;
	e->modified = 0;
	e->quit = 0;
	e->zpend = 0;
	e->pend_len = 0;
	e->mode = EDITOR_MODE_NORMAL;
	e->wantcol = 0;
	e->cy = 0;
	e->cx = 0;
	e->rowoff = 0;
	e->coloff = 0;
	e->path = NULL;
	e->msg = NULL;
	cmdline_clear(&e->cmd);
}

void editor_free(editor_t *e) {
	free(e->path);
	free(e->msg);
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

/** @brief Copy up to @p len bytes of @p src to @p out at *pos without passing @p max; with a NULL @p out only advance *pos (a dry run to measure). */
static void put(char *out, size_t *pos, size_t max, const char *src, size_t len) {
	size_t room = max - *pos;
	size_t n = len < room ? len : room;
	if (out) memcpy(out + *pos, src, n);
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

void editor_scroll_cols(editor_t *e, size_t cols) {
	if (cols == 0 || e->cy >= e->count) return;
	const char *line = e->lines[e->cy];
	size_t col = display_col(line, e->cx);
	size_t w = line[e->cx] && line[e->cx] != '\t' ? cell_width(line + e->cx, col) : 1; /* a mark must show whole; a tab may be cut */
	if (w > cols) w = cols;
	if (col < e->coloff) {
		e->coloff = col;
	} else if (col + w > e->coloff + cols) {
		e->coloff = col + w - cols;
	}
}

/** @brief Start of the last cell on line @p y (a character or an invalid byte), or 0 for an empty line. */
static size_t last_col(const editor_t *e, size_t y) {
	return utf8_prev(e->lines[y], strlen(e->lines[y]));
}

/**
 * @brief Byte index of the character of @p line whose first display column is the largest one not above @p want; the last character if @p line is shorter, 0 if it is empty.
 *
 * With @p past_end (insert mode) a @p want at or past the display column where the line ends gives the end of the line (its length) instead.
 */
static size_t col_to_cx(const char *line, size_t want, int past_end) {
	size_t col = 0, best = 0;
	for (size_t i = 0; line[i]; i += utf8_cell_len(line + i)) {
		if (col > want) return best; /* compared without adding to @p want, so a huge value cannot wrap */
		best = i;
		col += cell_width(line + i, col);
	}
	return past_end && col <= want ? strlen(line) : best;
}

void editor_move_cursor(editor_t *e, editor_move_t dir) {
	if (e->count == 0) return;
	const char *line = e->lines[e->cy];
	int insert = e->mode == EDITOR_MODE_INSERT;
	size_t old_cx = e->cx;
	switch (dir) {
	case EDITOR_MOVE_UP:
		if (e->cy > 0) e->cx = col_to_cx(e->lines[--e->cy], e->wantcol, insert);
		return;
	case EDITOR_MOVE_DOWN:
		if (e->cy + 1 < e->count) e->cx = col_to_cx(e->lines[++e->cy], e->wantcol, insert);
		return;
	case EDITOR_MOVE_LEFT:
		e->cx = utf8_prev(line, e->cx);
		break;
	case EDITOR_MOVE_RIGHT:
		e->cx += utf8_cell_len(line + e->cx); /* clamped below */
		break;
	}
	size_t last = insert ? strlen(line) : last_col(e, e->cy);
	if (e->cx > last) e->cx = last;
	if (e->cx != old_cx) e->wantcol = display_col(line, e->cx); /* only a move that moves forgets the old column, as in Vim */
}

/**
 * @brief Append @p line to @p out at *pos as drawn from display column @p skip on, clipped to @p max_cols columns without cutting a character or a mark; return the columns it takes.
 *
 * A tab that straddles @p skip shows its remaining spaces; a mark that straddles it shows one space in its remaining column, so the cells after it stay in place.
 */
static size_t put_line(const char *line, size_t skip, size_t max_cols, char *out, size_t *pos, size_t max) {
	static const char spaces[TAB_STOP + 1] = "        ";
	size_t col = 0, shown = 0; /* col counts from the start of the line, shown from @p skip */
	for (const char *p = line; *p; p += utf8_cell_len(p)) {
		size_t w = cell_width(p, col);
		size_t room = max_cols - shown;
		if (col + w <= skip) {
			col += w; /* left of the window */
		} else if (col < skip) {
			size_t n = col + w - skip; /* what is left of a cell that straddles the left edge */
			if (n > room) n = room;
			put(out, pos, max, spaces, n);
			shown += n;
			col += w;
		} else if (*p == '\t') {
			size_t n = w < room ? w : room; /* spaces can be cut anywhere */
			put(out, pos, max, spaces, n);
			shown += n; /* if the tab was cut, shown == max_cols and the next cell ends the loop */
			col += w;
		} else if (w > room) {
			break; /* never show half of a mark, and room is 0 at the right edge */
		} else if (is_control((unsigned char)*p)) {
			char mark[2] = { '^', *p == 0x7f ? '?' : (char)(*p ^ 0x40) };
			put(out, pos, max, mark, 2);
			shown += w;
			col += w;
		} else if (utf8_valid_len(p) == 0 || utf8_is_c1(p)) {
			put(out, pos, max, "?", 1); /* cannot act on the terminal */
			shown += w;
			col += w;
		} else {
			put(out, pos, max, p, utf8_cell_len(p));
			shown += w;
			col += w;
		}
	}
	return shown;
}

size_t editor_render(const editor_t *e, size_t max_rows, size_t max_cols, char *out, size_t out_size) {
	if (out_size == 0) return 0;
	size_t max = out_size - 1; /* room for the NUL */
	size_t pos = 0;
	size_t avail = e->rowoff < e->count ? e->count - e->rowoff : 0;
	if (avail > max_rows) avail = max_rows;
	for (size_t i = 0; i < avail; i++) {
		if (i > 0) put(out, &pos, max, "\r\n", 2);
		put_line(e->lines[e->rowoff + i], e->coloff, max_cols, out, &pos, max);
	}
	out[pos] = '\0';
	return pos;
}

/** @brief Drop the last byte, a CR, of each of the first @p n lines of @p e. Every one of them must end in a CR. */
static void drop_crs(editor_t *e, size_t n) {
	for (size_t i = 0; i < n; i++) e->lines[i][strlen(e->lines[i]) - 1] = '\0';
}

int editor_load_file(editor_t *e, const char *path) {
	char *copy = strdup(path); /* before the old contents go: @p path may be e->path itself */
	editor_free(e); /* drop any previous contents */
	if (!copy) {
		errno = ENOMEM;
		return -1;
	}

	FILE *fp = fopen(copy, "r"); /* not @p path: it may have been freed with the old path */
	if (!fp) {
		if (errno != ENOENT) {
			int err = errno;
			free(copy);
			errno = err;
			return -1;
		}
		e->path = copy; /* Nonexistent file is not an error: the name is kept for the saver */
		return 0;
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
		/* the whole file has been seen: it is CRLF */
		e->crlf = 1;
		drop_crs(e, e->count - (last_terminated ? 0 : 1));
	}

	int saved = errno;
	free(line);
	fclose(fp);
	e->path = copy;
	if (rc < 0) {
		editor_free(e); /* frees the path too */
	}
	errno = saved;
	return rc;
}

/** Hide the cursor and move home: the start of every draw. */
#define HIDE_HOME "\x1b[?25l\x1b[H"
/** Length of HIDE_HOME. */
#define HIDE_HOME_LEN 9

/**
 * @brief Draw @p max_rows text rows and, if @p status_row is not 0, the status line on that terminal row; see editor_draw_text() and editor_draw_screen().
 *
 * The status line is reserved before the rows and left out whole if it does not fit; with a status line the cursor row never goes below the last text row.
 */
static size_t draw(const editor_t *e, size_t max_rows, size_t max_cols, size_t status_row, char *out, size_t out_size) {
	if (out_size < EDITOR_DRAW_OVERHEAD) {
		if (out_size) out[0] = '\0';
		return 0;
	}
	char head[48], *bar = NULL;
	size_t reserve = 0, head_len = 0, bar_len = 0;
	int command = e->mode == EDITOR_MODE_COMMAND;
	size_t tail_len = e->msg || command ? 0 : 3; /* the status line is in reverse video, "ESC[m" ends it; a message or the command line is plain */
	if (status_row) {
		head_len = (size_t)snprintf(head, sizeof(head), tail_len ? "\x1b[%zu;1H\x1b[7m" : "\x1b[%zu;1H", status_row);
		bar = malloc(max_cols * 4 + 1);
		if (bar) {
			bar_len = editor_bottom_line(e, max_cols, bar, max_cols * 4 + 1);
			if (head_len + bar_len + tail_len <= out_size - EDITOR_DRAW_OVERHEAD) reserve = head_len + bar_len + tail_len;
		}
	}
	memcpy(out, HIDE_HOME, HIDE_HOME_LEN);
	size_t pos = HIDE_HOME_LEN;
	size_t rows_end = out_size - EDITOR_DRAW_OVERHEAD - reserve + HIDE_HOME_LEN; /* where the rows must stop: the rest is for the tail */
	size_t avail = e->rowoff < e->count ? e->count - e->rowoff : 0;
	if (avail > max_rows) avail = max_rows;
	size_t drawn = 0;
	for (; drawn < avail; drawn++) {
		const char *line = e->lines[e->rowoff + drawn];
		size_t need = pos + (drawn > 0 ? 2 : 0);
		size_t width = put_line(line, e->coloff, max_cols, NULL, &need, (size_t)-1); /* measure first: a row is whole or not at all */
		if (width < max_cols) need += 3;
		if (need > rows_end) break;
		if (drawn > 0) put(out, &pos, rows_end, "\r\n", 2);
		put_line(line, e->coloff, max_cols, out, &pos, rows_end);
		if (width < max_cols) put(out, &pos, rows_end, "\x1b[K", 3); /* not after the last column: the cursor is still on it */
	}
	if (drawn < max_rows) pos += (size_t)snprintf(out + pos, out_size - pos, "\x1b[%zu;1H\x1b[J", drawn + 1);
	if (reserve) { /* after the erase, which would wipe it; no erase of its own: it takes the whole width */
		memcpy(out + pos, head, head_len);
		pos += head_len;
		memcpy(out + pos, bar, bar_len);
		pos += bar_len;
		memcpy(out + pos, "\x1b[m", tail_len);
		pos += tail_len;
	}
	free(bar);
	size_t row = e->cy >= e->rowoff ? e->cy - e->rowoff : 0;
	if (status_row && row >= max_rows) row = max_rows - 1; /* never on the status line */
	size_t col = e->cy < e->count ? display_col(e->lines[e->cy], e->cx) : 0;
	col = col > e->coloff ? col - e->coloff : 0;
	if (status_row && command) { /* the cursor is at the end of the typed text on the bottom row */
		row = status_row - 1;
		col = 0;
		for (const char *p = e->cmd.text; *p; p += utf8_cell_len(p)) col++; /* typed characters are valid and printable */
		col++; /* the ':' */
	}
	if (max_cols > 0 && col >= max_cols) col = max_cols - 1;
	pos += (size_t)snprintf(out + pos, out_size - pos, "\x1b[%zu;%zuH\x1b[?25h", row + 1, col + 1);
	return pos;
}

size_t editor_draw_text(const editor_t *e, size_t max_rows, size_t max_cols, char *out, size_t out_size) {
	return draw(e, max_rows, max_cols, 0, out, out_size);
}

size_t editor_draw_screen(const editor_t *e, size_t rows, size_t max_cols, char *out, size_t out_size) {
	return draw(e, editor_text_rows(rows), max_cols, rows >= 2 ? rows : 0, out, out_size);
}

const char *editor_mode_label(const editor_t *e) {
	return e->mode == EDITOR_MODE_INSERT ? "INSERT" : e->mode == EDITOR_MODE_COMMAND ? "COMMAND" : "NORMAL";
}

/**
 * @brief Insert the @p n bytes of @p s (no NUL among them) into line @p y of @p e at byte index @p at, growing the line with realloc.
 *
 * Sets @c modified. The cursor is not touched. line_remove(), line_split() and line_join() are its counterparts.
 *
 * @return 0 on success, -1 with the line unchanged if the memory can't be had.
 */
static int line_insert(editor_t *e, size_t y, size_t at, const char *s, size_t n) {
	size_t len = strlen(e->lines[y]);
	char *line = realloc(e->lines[y], len + n + 1);
	if (!line) return -1;
	memmove(line + at + n, line + at, len - at + 1); /* with the NUL */
	memcpy(line + at, s, n);
	e->lines[y] = line;
	e->modified = 1;
	return 0;
}

/** @brief Remove the @p n bytes at byte index @p at of line @p y of @p e (they must be inside it). Sets @c modified; the cursor is not touched. */
static void line_remove(editor_t *e, size_t y, size_t at, size_t n) {
	char *line = e->lines[y];
	memmove(line + at, line + at + n, strlen(line + at + n) + 1);
	e->modified = 1;
}

/**
 * @brief Split line @p y of @p e at byte index @p at: the bytes from @p at on become a new line @p y + 1. Sets @c modified; the cursor is not touched.
 * @return 0 on success, -1 with the text unchanged if the memory can't be had.
 */
static int line_split(editor_t *e, size_t y, size_t at) {
	char *tail = strdup(e->lines[y] + at);
	if (!tail) return -1;
	if (e->count == e->cap) {
		size_t cap = e->cap ? e->cap * 2 : 8;
		char **lines = realloc(e->lines, cap * sizeof(*lines));
		if (!lines) {
			free(tail);
			return -1;
		}
		e->lines = lines;
		e->cap = cap;
	}
	memmove(e->lines + y + 2, e->lines + y + 1, (e->count - y - 1) * sizeof(*e->lines));
	e->lines[y + 1] = tail;
	e->lines[y][at] = '\0'; /* the line keeps its allocation: shrinking in place cannot fail */
	e->count++;
	e->modified = 1;
	return 0;
}

/**
 * @brief Join line @p y + 1 of @p e to the end of line @p y and drop it (@p y + 1 must exist). Sets @c modified; the cursor is not touched.
 * @return 0 on success, -1 with the text unchanged if the memory can't be had.
 */
static int line_join(editor_t *e, size_t y) {
	char *next = e->lines[y + 1];
	if (line_insert(e, y, strlen(e->lines[y]), next, strlen(next)) < 0) return -1;
	free(next);
	memmove(e->lines + y + 1, e->lines + y + 2, (e->count - y - 2) * sizeof(*e->lines));
	e->count--;
	return 0;
}

/** @brief Enter: split the line at the cursor (an editor with no lines gets two) and put the cursor at the start of the new line. */
static int enter_key(editor_t *e) {
	if (e->count == 0 && editor_append_line(e, "") < 0) return 0;
	if (line_split(e, e->cy, e->cx) < 0) return 0;
	e->cy++;
	e->cx = 0;
	e->wantcol = 0;
	return 1;
}

/** @brief Backspace: delete the character before the cursor, or at column 0 join the line to the previous one with the cursor at the join; nothing at the start of the text. */
static int backspace_key(editor_t *e) {
	if (e->count == 0) return 0;
	if (e->cx > 0) {
		size_t prev = utf8_prev(e->lines[e->cy], e->cx);
		line_remove(e, e->cy, prev, e->cx - prev);
		e->cx = prev;
	} else if (e->cy > 0) {
		size_t join = strlen(e->lines[e->cy - 1]);
		if (line_join(e, e->cy - 1) < 0) return 0;
		e->cy--;
		e->cx = join;
	} else {
		return 0;
	}
	e->wantcol = display_col(e->lines[e->cy], e->cx);
	return 1;
}

/** @brief Delete: remove the character under the cursor, or at the end of the line join the next line to it; nothing at the end of the text. */
static int delete_key(editor_t *e) {
	if (e->count == 0) return 0;
	const char *line = e->lines[e->cy];
	if (line[e->cx]) {
		line_remove(e, e->cy, e->cx, utf8_cell_len(line + e->cx));
	} else if (e->cy + 1 >= e->count || line_join(e, e->cy) < 0) {
		return 0;
	}
	e->wantcol = display_col(e->lines[e->cy], e->cx);
	return 1;
}

/** @brief Type the @p n bytes of one character at the cursor: create the first line if there is none, insert, and move the cursor and the wanted column past it. */
static int type_char(editor_t *e, const char *s, size_t n) {
	if (e->count == 0 && editor_append_line(e, "") < 0) return 0;
	if (line_insert(e, e->cy, e->cx, s, n) < 0) return 0;
	e->cx += n;
	e->wantcol = display_col(e->lines[e->cy], e->cx);
	return 1;
}

/** @brief Append one typed character to the command line (a tab is not typed there); return non-zero if it was added. */
static int cmd_put(editor_t *e, const char *s, size_t n) {
	return !(n == 1 && s[0] == '\t') && cmdline_append(&e->cmd, s, n) == 0;
}

/** @brief Return from command mode to normal mode with an empty command line. */
static void cmd_leave(editor_t *e) {
	e->mode = EDITOR_MODE_NORMAL;
	e->pend_len = 0;
	cmdline_clear(&e->cmd);
}

/** @brief Backspace in command mode: delete the last character; on an empty command line cancel it, as Vim does. */
static int cmd_backspace(editor_t *e) {
	if (e->cmd.len == 0) cmd_leave(e);
	else cmdline_backspace(&e->cmd);
	return 1;
}

/** @brief Enter in command mode: run the command and go back to normal mode. */
static int cmd_enter(editor_t *e) {
	char text[CMDLINE_MAX + 1];
	memcpy(text, e->cmd.text, sizeof(text)); /* commands_run() may not see the command line change under it */
	cmd_leave(e);
	commands_run(e, text);
	return 1;
}

/** @brief A complete character typed in insert mode goes into the text, in command mode into the command line. */
static int put_char(editor_t *e, const char *s, size_t n) {
	return e->mode == EDITOR_MODE_COMMAND ? cmd_put(e, s, n) : type_char(e, s, n);
}

/**
 * @brief Insert-mode key @p c (a byte) of typing: collect the bytes of a UTF-8 character in @c pend and type it when it is complete and valid.
 * @return Non-zero if a character was typed.
 */
static int type_byte(editor_t *e, unsigned char c) {
	if (e->pend_len > 0 && (c & 0xc0) == 0x80) {
		e->pend[e->pend_len++] = (char)c;
		unsigned char lead = (unsigned char)e->pend[0];
		size_t need = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : 2;
		if (e->pend_len < need) return 0;
		char buf[5];
		memcpy(buf, e->pend, e->pend_len);
		buf[e->pend_len] = '\0';
		size_t n = e->pend_len;
		e->pend_len = 0;
		return utf8_valid_len(buf) == n ? put_char(e, buf, n) : 0;
	}
	e->pend_len = 0; /* a half character followed by anything else is dropped */
	int cmd = e->mode == EDITOR_MODE_COMMAND;
	if (c == 0x7f || c == 0x08) return cmd ? cmd_backspace(e) : backspace_key(e);
	if (c == '\r' || c == '\n') return cmd ? cmd_enter(e) : enter_key(e);
	if (c >= 0xc2 && c <= 0xf4) { /* lead of a multibyte character; the rest decides if it is valid */
		e->pend[e->pend_len++] = (char)c;
		return 0;
	}
	if (c == '\t' || (c >= 0x20 && c < 0x7f)) return put_char(e, (const char *)&c, 1);
	return 0; /* other control keys, stray continuation and invalid bytes */
}

/** @brief Leave insert mode: one character left unless at column 0, as Vim does; the wanted column follows even when the cursor stays. */
static void leave_insert(editor_t *e) {
	e->mode = EDITOR_MODE_NORMAL;
	if (e->cy >= e->count) return;
	e->cx = utf8_prev(e->lines[e->cy], e->cx);
	e->wantcol = display_col(e->lines[e->cy], e->cx);
}

/**
 * @brief Put the cursor on motion target @p t and set the wanted column from it, or to the end of every line if @p to_eol.
 *
 * The wanted column is set even when the cursor does not move, as in Vim.
 * @return Non-zero if the cursor moved (the screen needs a redraw).
 */
static int goto_target(editor_t *e, motion_pos_t t, int to_eol) {
	if (e->count == 0) return 0;
	int moved = t.y != e->cy || t.x != e->cx;
	e->cy = t.y;
	e->cx = t.x;
	e->wantcol = to_eol ? EDITOR_WANTCOL_EOL : display_col(e->lines[t.y], t.x);
	return moved;
}

/** @brief editor_handle_key() without the message: the message was cleared by the caller. */
static int handle_key(editor_t *e, int key) {
	editor_move_t dir;
	int zpend = e->zpend; /* any key, Ctrl+Q included, ends a pending Z */
	e->zpend = 0;
	if (key == 0x11) {
		e->pend_len = 0;
		if (e->mode == EDITOR_MODE_COMMAND) cmd_leave(e); /* a refusal shows in the message, which the command line would hide */
		commands_run(e, "q");
		return 1;
	}
	if (zpend) {
		if (key == 'Z' || key == 'Q') {
			commands_run(e, key == 'Z' ? "x" : "q!");
			return 1;
		}
	}
	if (e->mode == EDITOR_MODE_COMMAND && key == KEY_ESC) {
		cmd_leave(e);
		return 1;
	}
	if (e->mode == EDITOR_MODE_COMMAND && key >= 256) { /* arrows and Delete do nothing on the command line */
		e->pend_len = 0;
		return 0;
	}
	if (e->mode != EDITOR_MODE_NORMAL && key < 256) return type_byte(e, (unsigned char)key);
	e->pend_len = 0;
	switch (key) {
	case KEY_UP: dir = EDITOR_MOVE_UP; break;
	case KEY_DOWN: dir = EDITOR_MOVE_DOWN; break;
	case KEY_LEFT: dir = EDITOR_MOVE_LEFT; break;
	case KEY_RIGHT: dir = EDITOR_MOVE_RIGHT; break;
	case 'k': case 'j': case 'h': case 'l':
		dir = key == 'k' ? EDITOR_MOVE_UP : key == 'j' ? EDITOR_MOVE_DOWN : key == 'h' ? EDITOR_MOVE_LEFT : EDITOR_MOVE_RIGHT;
		break;
	case '0': return goto_target(e, motion_line_start(e), 0);
	case '^': return goto_target(e, motion_first_nonblank(e), 0);
	case '$': return goto_target(e, motion_line_end(e), 1);
	case 'w': case 'W': return goto_target(e, motion_word_next(e, key == 'W'), 0);
	case 'b': case 'B': return goto_target(e, motion_word_prev(e, key == 'B'), 0);
	case 'e': case 'E': return goto_target(e, motion_word_end(e, key == 'E'), 0);
	case 'i':
		e->mode = EDITOR_MODE_INSERT;
		return 1;
	case 'Z':
		e->zpend = 1;
		return 0;
	case ':':
		e->mode = EDITOR_MODE_COMMAND;
		cmdline_clear(&e->cmd);
		return 1;
	case KEY_DELETE:
		return e->mode == EDITOR_MODE_INSERT && delete_key(e);
	case KEY_ESC:
		if (e->mode != EDITOR_MODE_INSERT) return 0;
		leave_insert(e);
		return 1;
	default: return 0;
	}
	editor_move_cursor(e, dir);
	return 1;
}

int editor_handle_key(editor_t *e, int key) {
	int had_message = e->msg != NULL;
	free(e->msg);
	e->msg = NULL;
	return handle_key(e, key) || had_message;
}

size_t editor_text_rows(size_t rows) {
	return rows > 1 ? rows - 1 : rows;
}

size_t editor_status(const editor_t *e, size_t cols, char *out, size_t out_size) {
	if (out_size == 0) return 0;
	size_t max = out_size - 1, pos = 0;
	char right[48];
	size_t col = e->cy < e->count ? display_col(e->lines[e->cy], e->cx) + 1 : 1;
	size_t right_w = (size_t)snprintf(right, sizeof(right), "%zu,%zu", e->cy + 1, col);
	if (right_w >= cols) {
		put(out, &pos, max, right, cols); /* the position alone, cut on the right */
	} else {
		const char *name = e->path && e->path[0] ? e->path : "[No Name]";
		const char *mode = editor_mode_label(e);
		char *left = malloc(strlen(name) + strlen(mode) + 12); /* name, " [dos]", " [+]", " ", mode, NUL */
		size_t used = 0;
		if (left) {
			sprintf(left, "%s%s%s %s", name, e->crlf ? " [dos]" : "", e->modified ? " [+]" : "", mode);
			used = put_line(left, 0, cols - right_w - 1, out, &pos, max);
			free(left);
		}
		for (; used < cols - right_w; used++) put(out, &pos, max, " ", 1);
		put(out, &pos, max, right, right_w);
	}
	out[pos] = '\0';
	return pos;
}

int editor_set_message(editor_t *e, const char *text) {
	char *copy = strdup(text);
	if (!copy) return -1;
	free(e->msg);
	e->msg = copy;
	return 0;
}

/** @brief Write @p prefix and @p text as one bottom row of exactly @p cols columns (cut by the render rules, padded with spaces) into @p out; return its width in columns before padding and set *len to its bytes. */
static size_t plain_row(const char *prefix, const char *text, size_t cols, char *out, size_t out_size, size_t *len) {
	size_t max = out_size - 1, pos = 0;
	char *line = malloc(strlen(prefix) + strlen(text) + 1);
	size_t used = 0;
	if (line) {
		sprintf(line, "%s%s", prefix, text);
		used = put_line(line, 0, cols, out, &pos, max);
		free(line);
	}
	size_t width = used;
	for (; used < cols; used++) put(out, &pos, max, " ", 1);
	out[pos] = '\0';
	*len = pos;
	return width;
}

size_t editor_bottom_line(const editor_t *e, size_t cols, char *out, size_t out_size) {
	if (out_size == 0) return 0;
	size_t len;
	if (e->mode == EDITOR_MODE_COMMAND) plain_row(":", e->cmd.text, cols, out, out_size, &len);
	else if (e->msg) plain_row("", e->msg, cols, out, out_size, &len);
	else return editor_status(e, cols, out, out_size);
	return len;
}
