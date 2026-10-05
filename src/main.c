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
#include "terminal.h"

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

/** @brief Draw the whole screen with the cursor, using the buffer @p out of @p size bytes. */
static void redraw(const editor_t *e, int rows, int cols, char *out, size_t size) {
	size_t n = editor_draw(e, (size_t)rows, (size_t)cols, out, size);
	write(STDOUT_FILENO, out, n);
}

/**
 * @brief Run the editor until Ctrl+Q is pressed or stdin closes.
 *
 * Usage: notvim [file]. The file is loaded before raw mode and the alternate
 * screen start, so a load error is printed on the normal terminal. The arrow keys and h/j/k/l move the
 * cursor and the screen is redrawn after each move.
 *
 * @return 0 on normal exit, 1 if the file can't be loaded.
 */
int main(int argc, char **argv) {
	editor_t e;
	editor_init(&e);

	if (argc > 1 && editor_load_file(&e, argv[1]) < 0) {
		fprintf(stderr, "notvim: %s: %s\n", argv[1], strerror(errno));
		return 1;
	}

	terminal_enter_raw(STDIN_FILENO);
	terminal_enter_alt_screen(STDOUT_FILENO);
	atexit(cleanup);

	int rows, cols;
	terminal_get_size(STDOUT_FILENO, &rows, &cols);
	/* each of the rows lines is clipped to cols bytes, plus "\r\n" between them */
	size_t size = (size_t)rows * ((size_t)cols + 2) + EDITOR_DRAW_OVERHEAD;
	char *out = malloc(size);
	if (!out) {
		fprintf(stderr, "notvim: %s\n", strerror(ENOMEM));
		return 1;
	}
	redraw(&e, rows, cols, out, size);

	key_parser_t parser;
	key_parser_init(&parser);
	for (;;) {
		/* inside an escape sequence wait only a moment: a lone Esc has nothing after it */
		struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
		int ready = poll(&pfd, 1, key_parser_pending(&parser) ? KEY_ESC_TIMEOUT_MS : -1);
		int key;
		if (ready == 0) {
			key = key_parser_timeout(&parser);
		} else if (ready < 0) {
			if (errno == EINTR) continue;
			break;
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
			editor_scroll(&e, (size_t)rows);
			redraw(&e, rows, cols, out, size);
		}
	}

	free(out);
	editor_free(&e);
	return 0;
}
