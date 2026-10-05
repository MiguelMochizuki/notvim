/**
 * @file main.c
 * @brief Program entry point: terminal setup, initial render and input loop.
 */
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "editor.h"
#include "keys.h"
#include "stopsig.h"
#include "terminal.h"
#include "winch.h"

/** @brief atexit() handler: switch back from the alternate screen, then restore the tty modes. */
static void cleanup(void) {
	terminal_leave_alt_screen(STDOUT_FILENO);
	terminal_leave_raw(STDIN_FILENO);
}

/**
 * @brief Translate an arrow key, or h/j/k/l as in Vim, into a cursor move.
 * @param key Key from key_parser_feed().
 * @param dir Receives the direction when @p key is a movement key.
 * @return 1 if @p key is a movement key, 0 otherwise.
 */
static int key_to_move(int key, editor_move_t *dir) {
	switch (key) {
	case KEY_UP:
	case 'k': *dir = EDITOR_MOVE_UP; return 1;
	case KEY_DOWN:
	case 'j': *dir = EDITOR_MOVE_DOWN; return 1;
	case KEY_LEFT:
	case 'h': *dir = EDITOR_MOVE_LEFT; return 1;
	case KEY_RIGHT:
	case 'l': *dir = EDITOR_MOVE_RIGHT; return 1;
	default: return 0;
	}
}

/** @brief Size in bytes of the draw buffer for a terminal of @p rows by @p cols. */
static size_t draw_buffer_size(int rows, int cols) {
	/* each of the rows lines (the status line is one of them) holds up to cols characters of up to 4 bytes, plus "ESC[K" after it and "\r\n" between them */
	return (size_t)rows * ((size_t)cols * 4 + 3 + 2) + EDITOR_DRAW_OVERHEAD + EDITOR_STATUS_OVERHEAD;
}

/** The terminal size and the buffer one redraw is built in; they always match each other. */
typedef struct {
	int rows;    /**< Terminal height. */
	int cols;    /**< Terminal width. */
	char *buf;   /**< Draw buffer, owned. */
	size_t size; /**< Size of @ref buf in bytes, from draw_buffer_size(). */
} screen_t;

/** @brief Read the terminal size and (re)allocate the buffer for it; on failure @p s keeps its old size and buffer and -1 is returned. */
static int screen_fit(screen_t *s) {
	int rows, cols;
	terminal_get_size(STDOUT_FILENO, &rows, &cols);
	size_t size = draw_buffer_size(rows, cols);
	char *buf = realloc(s->buf, size); /* a NULL s->buf allocates */
	if (!buf) return -1;
	s->buf = buf;
	s->size = size;
	s->rows = rows;
	s->cols = cols;
	return 0;
}

/** @brief Draw the whole screen with the cursor; 0 if it all got written, -1 if the terminal can't be written to. */
static int screen_draw(const screen_t *s, const editor_t *e) {
	size_t n = editor_draw_screen(e, (size_t)s->rows, (size_t)s->cols, s->buf, s->size);
	return terminal_write_all(STDOUT_FILENO, s->buf, n);
}

/**
 * @brief Run the editor until Ctrl+Q is pressed or stdin closes.
 *
 * Usage: notvim [file]. It refuses to run unless stdin and stdout are
 * terminals, so that it never writes escape sequences into a pipe or a file.
 * The file is loaded before raw mode and the alternate
 * screen start, so a load error is printed on the normal terminal. The arrow keys and h/j/k/l move the
 * cursor and the screen is redrawn after each move.
 *
 * SIGWINCH makes it read the terminal size again, resize the draw buffer, scroll
 * so the cursor stays visible and redraw. If the buffer cannot grow, the old size is kept.
 *
 * Every redraw is written with terminal_write_all(); if it fails (the terminal is gone) the loop ends
 * through the normal exit too.
 *
 * SIGINT, SIGTERM and SIGHUP end the editor through the normal exit, so the
 * alternate screen and the tty modes are restored.
 *
 * @return 0 on normal exit, 1 if stdin or stdout is not a terminal, the
 *         file can't be loaded or the screen can't be written to (at startup
 *         or later), 128 plus the signal number after a signal.
 */
int main(int argc, char **argv) {
	if (!isatty(STDIN_FILENO)) {
		fprintf(stderr, "notvim: input is not a terminal\n");
		return 1;
	}
	if (!isatty(STDOUT_FILENO)) {
		fprintf(stderr, "notvim: output is not a terminal\n");
		return 1;
	}

	editor_t e;
	editor_init(&e);

	if (argc > 1 && editor_load_file(&e, argv[1]) < 0) {
		const char *reason = errno == EILSEQ ? "binary file (contains NUL bytes)" : strerror(errno);
		fprintf(stderr, "notvim: %s: %s\n", argv[1], reason);
		return 1;
	}

	/* before the terminal changes, so a signal can never arrive unhandled afterwards */
	int sigfd = stopsig_install();
	if (sigfd < 0) {
		fprintf(stderr, "notvim: cannot catch signals: %s\n", strerror(errno));
		return 1;
	}

	/* same reason: a resize right after raw mode starts must not be lost */
	int winchfd = winch_install();
	if (winchfd < 0) {
		fprintf(stderr, "notvim: cannot catch resizes: %s\n", strerror(errno));
		return 1;
	}

	terminal_enter_raw(STDIN_FILENO);
	terminal_enter_alt_screen(STDOUT_FILENO);
	atexit(cleanup);

	screen_t screen = { 0, 0, NULL, 0 };
	if (screen_fit(&screen) < 0) {
		fprintf(stderr, "notvim: %s\n", strerror(ENOMEM));
		return 1;
	}
	if (screen_draw(&screen, &e) < 0) {
		free(screen.buf);
		editor_free(&e);
		return 1; /* the terminal is gone: leave through the normal exit, which restores what it can */
	}

	int write_failed = 0;
	key_parser_t parser;
	key_parser_init(&parser);
	for (;;) {
		/* inside an escape sequence wait only a moment: a lone Esc has nothing after it */
		struct pollfd pfds[3] = {
			{ .fd = STDIN_FILENO, .events = POLLIN },
			{ .fd = sigfd, .events = POLLIN },
			{ .fd = winchfd, .events = POLLIN },
		};
		int ready = poll(pfds, 3, key_parser_pending(&parser) ? KEY_ESC_TIMEOUT_MS : -1);
		int key;
		if (ready == 0) {
			key = key_parser_timeout(&parser);
		} else if (ready < 0) {
			if (errno == EINTR) continue;
			break;
		} else if (pfds[1].revents & POLLIN) {
			break; /* SIGINT, SIGTERM or SIGHUP: leave through the normal exit so the terminal is restored */
		} else if (pfds[2].revents & POLLIN) {
			winch_drain();
			if (screen_fit(&screen) < 0) continue; /* keep the old size and buffer: they still match each other */
			editor_scroll(&e, editor_text_rows((size_t)screen.rows));
			if (screen_draw(&screen, &e) < 0) {
				write_failed = 1;
				break;
			}
			continue; /* a pending escape sequence stays pending */
		} else {
			char c;
			if (read(STDIN_FILENO, &c, 1) != 1) break;
			key = key_parser_feed(&parser, (unsigned char)c);
		}
		if (key == KEY_NONE) continue;
		if (key < 256 && editor_should_exit((char)key)) break;
		editor_move_t dir;
		if (key_to_move(key, &dir)) {
			editor_move_cursor(&e, dir);
			editor_scroll(&e, editor_text_rows((size_t)screen.rows));
			if (screen_draw(&screen, &e) < 0) {
				write_failed = 1;
				break;
			}
		}
	}

	int sig = stopsig_received();
	free(screen.buf);
	editor_free(&e);
	return write_failed ? 1 : sig ? 128 + sig : 0;
}
