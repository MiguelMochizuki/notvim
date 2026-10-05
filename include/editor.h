/**
 * @file editor.h
 * @brief Editor state, key handling and rendering.
 */
#ifndef EDITOR_H
#define EDITOR_H

#include <stddef.h>

/** Size in bytes of the buffer main() uses to render the screen. */
#define EDITOR_BUFFER_SIZE 1024

/** Editor state: the text as a growable array of lines. */
typedef struct {
	char **lines; /**< Owned array of owned NUL-terminated strings, without newlines. */
	size_t count; /**< Number of lines in use in @ref lines. */
	size_t cap;   /**< Allocated capacity of @ref lines, in lines. */
} editor_t;

/**
 * @brief Tell whether a key press should quit the editor.
 * @param c Byte read from the terminal.
 * @return Non-zero if @p c is Ctrl+Q (0x11), 0 otherwise.
 */
int editor_should_exit(char c);

/**
 * @brief Initialise @p e as an empty editor with no lines. Does not allocate.
 * @param e Editor to initialise; must not be NULL and is not read first.
 */
void editor_init(editor_t *e);

/**
 * @brief Free every line and leave @p e as after editor_init().
 *
 * Safe on a freshly initialised editor and safe to call twice; @p e can be
 * reused afterwards.
 *
 * @param e Editor to free; must have been passed to editor_init().
 */
void editor_free(editor_t *e);

/**
 * @brief Number of lines in @p e.
 * @param e Editor to query; must not be NULL.
 * @return Line count; 0 for an empty editor.
 */
size_t editor_line_count(const editor_t *e);

/**
 * @brief Get line @p i of @p e.
 * @param e Editor to query; must not be NULL.
 * @param i Zero-based line index.
 * @return The NUL-terminated line, valid until the editor is next modified or
 *         freed; NULL if @p i is out of range.
 */
const char *editor_line(const editor_t *e, size_t i);

/**
 * @brief Append a copy of @p text as a new last line of @p e.
 * @param e    Editor to modify; must not be NULL.
 * @param text NUL-terminated line without a newline; "" appends a blank line.
 * @return 0 on success, -1 on failure (errno is ENOMEM); @p e is unchanged on failure.
 */
int editor_append_line(editor_t *e, const char *text);

/**
 * @brief Render the first @p max_rows lines into @p out as a NUL-terminated string.
 *
 * Lines are joined with "\r\n" (no trailing separator), because raw mode
 * turns off output processing. A blank line counts as a row. Output is
 * truncated to fit @p out_size, leaving room for the NUL.
 *
 * @param e        Editor to render; must not be NULL.
 * @param max_rows Maximum number of lines to render, usually the terminal
 *                 height; 0 renders nothing.
 * @param out      Destination buffer.
 * @param out_size Size of @p out in bytes.
 * @return Number of bytes written, excluding the NUL; 0 if @p out_size is 0.
 */
size_t editor_render(const editor_t *e, size_t max_rows, char *out, size_t out_size);

/**
 * @brief Load the file at @p path into @p e, replacing its contents.
 *
 * If @p path does not exist, @p e is left empty and the call succeeds. As in
 * Vim, the file is not created here; it is only created when saved.
 * Each '\n' ends a line and a final newline does not add an empty line, so
 * an empty file gives no lines.
 *
 * @param e    Editor to load into; must not be NULL. Left empty on error.
 * @param path Path of the file to load.
 * @return 0 on success (including a nonexistent file), -1 on any other
 *         error (errno is set).
 */
int editor_load_file(editor_t *e, const char *path);

#endif
