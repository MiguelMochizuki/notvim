/**
 * @file editor.h
 * @brief Editor state, raw terminal mode and rendering.
 */
#ifndef EDITOR_H
#define EDITOR_H

#include <stddef.h>
#include <termios.h>

/** Capacity of the editor buffer in bytes, including the terminating NUL. */
#define EDITOR_BUFFER_SIZE 1024

/** Editor state: a single-line text buffer. */
typedef struct {
	char buffer[EDITOR_BUFFER_SIZE]; /**< Buffer contents (not necessarily NUL-terminated). */
	size_t len;                      /**< Number of bytes in use in @ref buffer. */
} editor_t;

/**
 * @brief Return the editor version.
 * @return Always 0 for now; placeholder.
 */
int editor_version(void);

/**
 * @brief Disable canonical mode, echo, signals and output processing in @p t.
 *
 * Exposed for testing only; not meant to be called outside editor.c.
 *
 * @param t Terminal attributes to modify in place. Unrelated flags are kept.
 */
void editor_set_raw_flags(struct termios *t);

/**
 * @brief Save the current terminal attributes of @p fd and switch it to raw mode.
 *
 * Does nothing if raw mode is already active or the terminal can't be read
 * or configured (for example when @p fd is not a tty).
 *
 * @param fd Terminal file descriptor, usually STDIN_FILENO.
 */
void editor_enter_raw(int fd);

/**
 * @brief Restore the terminal attributes saved by editor_enter_raw().
 *
 * Does nothing if raw mode is not active, so it is safe to call twice.
 *
 * @param fd Terminal file descriptor passed to editor_enter_raw().
 */
void editor_leave_raw(int fd);

/**
 * @brief Tell whether a key press should quit the editor.
 * @param c Byte read from the terminal.
 * @return Non-zero if @p c is Ctrl+Q (0x11), 0 otherwise.
 */
int editor_should_exit(char c);

/**
 * @brief Reset @p e to an empty buffer.
 * @param e Editor to initialise; must not be NULL.
 */
void editor_init(editor_t *e);

/**
 * @brief Copy the buffer into @p out as a NUL-terminated string.
 *
 * Output is truncated to fit @p out_size, leaving room for the NUL.
 *
 * @param e        Editor to render; must not be NULL.
 * @param out      Destination buffer.
 * @param out_size Size of @p out in bytes.
 * @return Number of bytes written, excluding the NUL; 0 if @p out_size is 0.
 */
size_t editor_render(const editor_t *e, char *out, size_t out_size);

#endif
