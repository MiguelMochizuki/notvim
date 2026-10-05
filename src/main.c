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
#include "terminal.h"

/** @brief atexit() handler that restores the terminal on STDIN_FILENO. */
static void cleanup(void) { terminal_leave_raw(STDIN_FILENO); }

/**
 * @brief Run the editor until Ctrl+Q is pressed or stdin closes.
 *
 * Usage: notvim [file]. The file is loaded before raw mode starts, so a load
 * error is printed on a normal terminal.
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

	/* Initial render */
	int rows, cols;
	terminal_get_size(STDOUT_FILENO, &rows, &cols);
	/* ponytail: sized for rows of cols characters; a longer line uses up the room of the
	 * rows after it. Clip lines to cols (horizontal clipping) if that matters. */
	size_t size = (size_t)rows * ((size_t)cols + 2) + 1;
	char *out = malloc(size);
	if (!out) {
		fprintf(stderr, "notvim: %s\n", strerror(ENOMEM));
		return 1;
	}
	size_t n = editor_render(&e, (size_t)rows, out, size);
	write(STDOUT_FILENO, out, n);
	free(out);

	/* Main loop */
	char c;
	while (read(STDIN_FILENO, &c, 1) == 1) {
		if (editor_should_exit(c)) {
			break;
		}
	}

	editor_free(&e);
	return 0;
}
