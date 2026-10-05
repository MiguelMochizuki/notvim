/**
 * @file terminal.h
 * @brief Raw terminal mode.
 */
#ifndef TERMINAL_H
#define TERMINAL_H

#include <termios.h>

/**
 * @brief Disable canonical mode, echo, signals and output processing in @p t.
 *
 * Exposed for testing only; not meant to be called outside terminal.c.
 *
 * @param t Terminal attributes to modify in place. Unrelated flags are kept.
 */
void terminal_set_raw_flags(struct termios *t);

/**
 * @brief Save the current terminal attributes of @p fd and switch it to raw mode.
 *
 * Does nothing if raw mode is already active or the terminal can't be read
 * or configured (for example when @p fd is not a tty).
 *
 * @param fd Terminal file descriptor, usually STDIN_FILENO.
 */
void terminal_enter_raw(int fd);

/**
 * @brief Restore the terminal attributes saved by terminal_enter_raw().
 *
 * Does nothing if raw mode is not active, so it is safe to call twice.
 *
 * @param fd Terminal file descriptor passed to terminal_enter_raw().
 */
void terminal_leave_raw(int fd);

#endif
