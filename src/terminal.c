/**
 * @file terminal.c
 * @brief Implementation of terminal.h. Public symbols are documented there.
 */
#include <unistd.h>
#include <termios.h>
#include "terminal.h"

/** Terminal attributes saved by terminal_enter_raw(), restored by terminal_leave_raw(). */
static struct termios saved_termios;
/** Non-zero while the terminal is in raw mode; makes enter/leave idempotent. */
static int raw_active = 0;

void terminal_set_raw_flags(struct termios *t) {
	t->c_lflag &= ~(ICANON | ECHO | ISIG);
	t->c_iflag &= ~(IXON | ICRNL);
	t->c_oflag &= ~OPOST;
	t->c_cc[VMIN] = 1;
	t->c_cc[VTIME] = 0;
}

void terminal_enter_raw(int fd) {
	if (raw_active) return;
	if (tcgetattr(fd, &saved_termios) == -1) return;

	struct termios raw = saved_termios;
	terminal_set_raw_flags(&raw);
	if (tcsetattr(fd, TCSAFLUSH, &raw) == -1) return;

	raw_active = 1;
}

void terminal_leave_raw(int fd) {
	if (!raw_active) return;
	tcsetattr(fd, TCSAFLUSH, &saved_termios);
	raw_active = 0;
}
