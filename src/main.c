/**
 * @file main.c
 * @brief Program entry point: terminal setup, initial render and input loop.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "editor.h"
#include "keys.h"
#include "terminal.h"

/** @brief atexit() handler that restores the terminal on STDIN_FILENO. */
static void cleanup(void) { terminal_leave_raw(STDIN_FILENO); }

/**
 * @brief Translate an arrow key into a cursor move.
 * @param key Key from key_parser_feed().
 * @param dir Receives the direction when @p key is an arrow key.
 * @return 1 if @p key is an arrow key, 0 otherwise.
 */
static int key_to_move(int key, editor_move_t *dir) {
	switch (key) {
	case KEY_UP: *dir = EDITOR_MOVE_UP; return 1;
	case KEY_DOWN: *dir = EDITOR_MOVE_DOWN; return 1;
	case KEY_LEFT: *dir = EDITOR_MOVE_LEFT; return 1;
	case KEY_RIGHT: *dir = EDITOR_MOVE_RIGHT; return 1;
	default: return 0;
	}
}

/** @brief Draw the whole screen with the cursor, using the buffer @p out of @p size bytes. */
static void redraw(const editor_t *e, int rows, char *out, size_t size) {
	size_t n = editor_draw(e, (size_t)rows, out, size);
	write(STDOUT_FILENO, out, n);
}

/**
 * @brief Run the editor until Ctrl+Q is pressed or stdin closes.
 *
 * Usage: notvim [file]. The file is loaded before raw mode starts, so a load
 * error is printed on a normal terminal. The arrow keys move the cursor and
 * the screen is redrawn after each move.
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
	atexit(cleanup);

	int rows, cols;
	terminal_get_size(STDOUT_FILENO, &rows, &cols);
	/* ponytail: sized for rows of cols characters; a longer line uses up the room of the
	 * rows after it. Clip lines to cols (horizontal clipping) if that matters. */
	size_t size = (size_t)rows * ((size_t)cols + 2) + EDITOR_DRAW_OVERHEAD;
	char *out = malloc(size);
	if (!out) {
		fprintf(stderr, "notvim: %s\n", strerror(ENOMEM));
		return 1;
	}
	redraw(&e, rows, out, size);

	key_parser_t parser;
	key_parser_init(&parser);
	char c;
	while (read(STDIN_FILENO, &c, 1) == 1) {
		int key = key_parser_feed(&parser, (unsigned char)c);
		if (key == KEY_NONE) continue;
		if (key < 256 && editor_should_exit((char)key)) break;
		editor_move_t dir;
		if (key_to_move(key, &dir)) {
			editor_move_cursor(&e, dir);
			redraw(&e, rows, out, size);
		}
	}

	free(out);
	editor_free(&e);
	return 0;
}
