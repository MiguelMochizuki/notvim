/**
 * @file winch.c
 * @brief Implementation of winch.h. Public symbols are documented there.
 */
#define _POSIX_C_SOURCE 200809L /* sigaction under -std=c11 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "winch.h"

/** Handler that was set before winch_install(). */
static struct sigaction previous;
/** The pipe: the handler writes to [1], the main loop polls [0]. -1 when not installed. */
static int pipefd[2] = { -1, -1 };

/** @brief Signal handler: wake up the poll(). Only async-signal-safe calls. */
static void on_signal(int sig) {
	(void)sig;
	int saved = errno;
	if (write(pipefd[1], "x", 1) < 0) { /* a full pipe already holds a wake-up */ }
	errno = saved;
}

/** @brief Make descriptor @p fd close-on-exec and non-blocking; -1 on error. */
static int configure(int fd) {
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) return -1;
	if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0) return -1;
	return 0;
}

int winch_install(void) {
	if (pipefd[0] >= 0) return pipefd[0];
	if (pipe(pipefd) < 0) goto fail;
	if (configure(pipefd[0]) < 0 || configure(pipefd[1]) < 0) goto fail_close;

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGWINCH, &sa, &previous) < 0) goto fail_close;
	return pipefd[0];

fail_close: {
		int saved = errno;
		close(pipefd[0]);
		close(pipefd[1]);
		errno = saved;
	}
fail:
	pipefd[0] = pipefd[1] = -1;
	return -1;
}

int winch_drain(void) {
	if (pipefd[0] < 0) return 0;
	char buf[64];
	int resized = 0;
	while (read(pipefd[0], buf, sizeof(buf)) > 0) resized = 1;
	return resized;
}

void winch_remove(void) {
	if (pipefd[0] < 0) return;
	sigaction(SIGWINCH, &previous, NULL);
	close(pipefd[0]);
	close(pipefd[1]);
	pipefd[0] = pipefd[1] = -1;
}
