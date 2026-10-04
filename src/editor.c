/* Implementation of editor struct */
#include <unistd.h>
#include <termios.h>
#include "editor.h"

static struct termios saved_termios;
static int raw_active = 0;

int editor_version(void) {
	return 0;
}

void editor_set_raw_flags(struct termios *t) {
	t->c_lflag &= ~(ICANON | ECHO | ISIG);
	t->c_iflag &= ~(IXON | ICRNL);
	t->c_oflag &= ~OPOST;
	t->c_cc[VMIN] = 1;
	t->c_cc[VTIME] = 0;
}

void editor_enter_raw(void) {
	if (raw_active) return;
	if (tcgetattr(STDIN_FILENO, &saved_termios) == -1) return;

	struct termios raw = saved_termios;
	editor_set_raw_flags(&raw);
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) return;

	raw_active = 1;
}

void editor_leave_raw(void) {
	if (!raw_active) return;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
	raw_active = 0;
}

int editor_should_exit(char c) {
    return c == 0x11;  /* Ctrl+Q = DC1 */
}
