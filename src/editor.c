/**
 * @file editor.c
 * @brief Implementation of editor.h. Public symbols are documented there.
 */
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include "editor.h"

/** Terminal attributes saved by editor_enter_raw(), restored by editor_leave_raw(). */
static struct termios saved_termios;
/** Non-zero while the terminal is in raw mode; makes enter/leave idempotent. */
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

void editor_enter_raw(int fd) {
	if (raw_active) return;
	if (tcgetattr(fd, &saved_termios) == -1) return;

	struct termios raw = saved_termios;
	editor_set_raw_flags(&raw);
	if (tcsetattr(fd, TCSAFLUSH, &raw) == -1) return;

	raw_active = 1;
}

void editor_leave_raw(int fd) {
	if (!raw_active) return;
	tcsetattr(fd, TCSAFLUSH, &saved_termios);
	raw_active = 0;
}

int editor_should_exit(char c) {
	return c == 0x11; /* Ctrl+Q = DC1 */
}

void editor_init(editor_t *e) {
	memset(e->buffer, 0, sizeof(e->buffer));
	e->len = 0;
}

size_t editor_render(const editor_t *e, char *out, size_t out_size) {
	if (out_size == 0) return 0;
	size_t n = e->len < out_size - 1 ? e->len : out_size - 1;
	memcpy(out, e->buffer, n);
	out[n] = '\0';
	return n;
}
