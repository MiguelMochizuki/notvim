/**
 * @file stopsig.c
 * @brief Implementation of stopsig.h. Public symbols are documented there.
 */
#define _POSIX_C_SOURCE 200809L /* sigaction under -std=c11 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "stopsig.h"

/** Number of signals handled. */
#define NSIGNALS 3

/** The handled signals, in the same order as @ref previous. */
static const int signals[NSIGNALS] = { SIGINT, SIGTERM, SIGHUP };
/** Handlers that were set before stopsig_install(). */
static struct sigaction previous[NSIGNALS];
/** The pipe: the handler writes to [1], the main loop polls [0]. -1 when not installed. */
static int pipefd[2] = { -1, -1 };
/** The last signal received, or 0. */
static volatile sig_atomic_t received;

/** @brief Signal handler: record the signal and wake up the poll(). Only async-signal-safe calls. */
static void on_signal(int sig) {
	int saved = errno;
	received = sig;
	if (write(pipefd[1], "x", 1) < 0) { /* a full pipe already holds a wake-up */ }
	errno = saved;
}

/** @brief Make descriptor @p fd non-blocking and close-on-exec; -1 on error. */
static int configure(int fd, int nonblocking) {
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) return -1;
	if (nonblocking && fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0) return -1;
	return 0;
}

int stopsig_install(void) {
	if (pipefd[0] >= 0) return pipefd[0];
	if (pipe(pipefd) < 0) goto fail;
	if (configure(pipefd[0], 0) < 0 || configure(pipefd[1], 1) < 0) goto fail_close;

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	for (int i = 0; i < NSIGNALS; i++) {
		if (sigaction(signals[i], &sa, &previous[i]) < 0) {
			while (i-- > 0) sigaction(signals[i], &previous[i], NULL);
			goto fail_close;
		}
	}
	received = 0;
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

int stopsig_received(void) {
	return received;
}

void stopsig_remove(void) {
	if (pipefd[0] < 0) return;
	for (int i = 0; i < NSIGNALS; i++) sigaction(signals[i], &previous[i], NULL);
	close(pipefd[0]);
	close(pipefd[1]);
	pipefd[0] = pipefd[1] = -1;
	received = 0;
}
