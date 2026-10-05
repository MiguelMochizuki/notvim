/**
 * @file terminal.c
 * @brief Implementation of terminal.h. Public symbols are documented there.
 */
#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>
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

void terminal_get_size(int fd, int *rows, int *cols) {
	struct winsize ws;
	int ok = ioctl(fd, TIOCGWINSZ, &ws) == 0;
	*rows = ok && ws.ws_row > 0 ? ws.ws_row : TERMINAL_DEFAULT_ROWS;
	*cols = ok && ws.ws_col > 0 ? ws.ws_col : TERMINAL_DEFAULT_COLS;
}

/** Switch to the alternate screen buffer. */
#define ALT_SCREEN_ENTER "\x1b[?1049h"
/** Switch back to the normal screen buffer. */
#define ALT_SCREEN_LEAVE "\x1b[?1049l"

/** Non-zero while the alternate screen is active; makes enter/leave idempotent. */
static int alt_active = 0;

void terminal_enter_alt_screen(int fd) {
	if (alt_active) return;
	if (write(fd, ALT_SCREEN_ENTER, sizeof(ALT_SCREEN_ENTER) - 1) < 0) return;
	alt_active = 1;
}

void terminal_leave_alt_screen(int fd) {
	if (!alt_active) return;
	if (write(fd, ALT_SCREEN_LEAVE, sizeof(ALT_SCREEN_LEAVE) - 1) < 0) return;
	alt_active = 0;
}

int terminal_write_all(int fd, const char *buf, size_t n) {
	while (n > 0) {
		ssize_t w = write(fd, buf, n);
		if (w >= 0) {
			buf += w;
			n -= (size_t)w;
		} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
			struct pollfd pfd = { .fd = fd, .events = POLLOUT };
			if (poll(&pfd, 1, -1) < 0 && errno != EINTR) return -1;
		} else if (errno != EINTR) {
			return -1;
		}
	}
	return 0;
}
