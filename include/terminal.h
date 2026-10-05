/**
 * @file terminal.h
 * @brief Raw terminal mode.
 */
#ifndef TERMINAL_H
#define TERMINAL_H

#include <termios.h>

/** Rows assumed when the terminal size can't be read. */
#define TERMINAL_DEFAULT_ROWS 24
/** Columns assumed when the terminal size can't be read. */
#define TERMINAL_DEFAULT_COLS 80

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

/**
 * @brief Switch @p fd to the terminal's alternate screen buffer.
 *
 * The editor draws there, so the shell screen and its scrollback are left
 * untouched until terminal_leave_alt_screen(). Does nothing if already active.
 *
 * @param fd Terminal file descriptor to write to, usually STDOUT_FILENO.
 */
void terminal_enter_alt_screen(int fd);

/**
 * @brief Switch back from the alternate screen buffer, restoring the shell screen.
 *
 * Does nothing if terminal_enter_alt_screen() was not called, so it is safe
 * to call twice.
 *
 * @param fd Terminal file descriptor passed to terminal_enter_alt_screen().
 */
void terminal_leave_alt_screen(int fd);

/**
 * @brief Get the size of the terminal on @p fd.
 *
 * Each dimension falls back to TERMINAL_DEFAULT_ROWS / TERMINAL_DEFAULT_COLS
 * if the size can't be read (for example @p fd is not a terminal) or is
 * reported as 0, as a fresh pty does.
 *
 * @param fd   Terminal file descriptor, usually STDOUT_FILENO.
 * @param rows Receives the number of rows; must not be NULL.
 * @param cols Receives the number of columns; must not be NULL.
 */
void terminal_get_size(int fd, int *rows, int *cols);

#endif
