/**
 * @file stopsig.h
 * @brief Catching the signals that ask the editor to stop, without a race.
 *
 * The handlers only record the signal and write one byte to a pipe, which
 * are async-signal-safe. The main loop polls the pipe together with stdin,
 * so a signal that arrives just before it starts waiting is not missed.
 */
#ifndef STOPSIG_H
#define STOPSIG_H

/**
 * @brief Install handlers for SIGINT, SIGTERM and SIGHUP.
 *
 * Calling it again returns the same descriptor.
 *
 * @return The read end of a pipe that becomes readable when one of the
 *         signals arrives, or -1 on error (errno is set).
 */
int stopsig_install(void);

/**
 * @brief The signal received since stopsig_install().
 * @return Its number (for example SIGTERM), or 0 if none arrived.
 */
int stopsig_received(void);

/**
 * @brief Restore the handlers that were set before stopsig_install() and close the pipe.
 *
 * Safe to call without stopsig_install() and safe to call twice.
 */
void stopsig_remove(void);

#endif
