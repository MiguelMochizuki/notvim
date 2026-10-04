/* Main source file */
#include <stdlib.h>
#include <unistd.h>
#include "editor.h"

int main(void) {
	editor_enter_raw();
	atexit(editor_leave_raw);

	/* Main loop, now allows manual testing for raw mode */
	char c;
	while (read(STDIN_FILENO, &c, 1) == 1) {
		if (c == 'q') break;
	}

	return 0;
}
