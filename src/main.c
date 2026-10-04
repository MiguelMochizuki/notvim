/* Main source file */
#include <stdlib.h>
#include <unistd.h>
#include "editor.h"

static void cleanup(void) { editor_leave_raw(STDIN_FILENO); }

int main(void) {
	editor_t e;
	editor_init(&e);

	editor_enter_raw(STDIN_FILENO);
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

	return 0;
}
