/**
 * @file main.c
 * @brief Program entry point: terminal setup, initial render and input loop.
 */
#include <stdlib.h>
#include <unistd.h>
#include "editor.h"
#include "terminal.h"

/** @brief atexit() handler that restores the terminal on STDIN_FILENO. */
static void cleanup(void) { terminal_leave_raw(STDIN_FILENO); }

/**
 * @brief Run the editor until Ctrl+Q is pressed or stdin closes.
 * @return 0 on normal exit.
 */
int main(void) {
	editor_t e;
	editor_init(&e);

	terminal_enter_raw(STDIN_FILENO);
	atexit(cleanup);

	/* Initial render */
	char out[EDITOR_BUFFER_SIZE];
	size_t n = editor_render(&e, out, sizeof(out));
	write(STDOUT_FILENO, out, n);

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
