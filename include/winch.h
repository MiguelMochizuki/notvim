/**
 * @file winch.h
 * @brief Noticing terminal resizes (SIGWINCH) without a race.
 *
 * Same self-pipe pattern as stopsig.h: the handler only writes one byte to a
 * pipe, which is async-signal-safe, and the main loop polls the pipe together
 * with stdin. SIGWINCH is not a stop signal, so it has its own pipe.
 */
#ifndef WINCH_H
#define WINCH_H

/**
 * @brief Install a handler for SIGWINCH.
 *
 * Calling it again returns the same descriptor.
 *
 * @return The read end of a pipe that becomes readable when the terminal is
 *         resized, or -1 on error (errno is set). It is for poll() only:
 *         the caller must not read it, winch_drain() does. It is
 *         non-blocking, close-on-exec and valid until winch_remove().
 */
int winch_install(void);

/**
 * @brief Empty the pipe and tell whether the terminal was resized since the last call.
 *
 * Never blocks. Several resizes in a row count as one, so the caller reads
 * the size once and the last size wins.
 *
 * @return 1 if at least one SIGWINCH arrived since the last call, 0 otherwise
 *         (also when winch_install() was not called).
 */
int winch_drain(void);

/**
 * @brief Restore the handler that was set before winch_install() and close the pipe.
 *
 * Safe to call without winch_install() and safe to call twice.
 */
void winch_remove(void);

#endif
