/**
 * @file editor.h
 * @brief Editor state, key handling and rendering.
 */
#ifndef EDITOR_H
#define EDITOR_H

#include <stddef.h>

/** Editor state: the text as a growable array of lines. */
typedef struct {
	char **lines; /**< Owned array of owned NUL-terminated strings, without newlines. */
	size_t count; /**< Number of lines in use in @ref lines. */
	size_t cap;   /**< Allocated capacity of @ref lines, in lines. */
	size_t cy;    /**< Cursor row: index of the line the cursor is on. */
	size_t cx;    /**< Cursor column: byte index in that line. */
	size_t rowoff; /**< Index of the first visible line (vertical scroll offset). */
} editor_t;

/** Directions for editor_move_cursor(). */
typedef enum {
	EDITOR_MOVE_UP,    /**< One line up. */
	EDITOR_MOVE_DOWN,  /**< One line down. */
	EDITOR_MOVE_LEFT,  /**< One column left. */
	EDITOR_MOVE_RIGHT  /**< One column right. */
} editor_move_t;

/**
 * @brief Scroll vertically so that the cursor line is inside the window.
 *
 * Changes only @c rowoff, by the least amount: up if the cursor is above the
 * window, down if it is below it. Does nothing if @p rows is 0.
 *
 * @param e    Editor to modify; must not be NULL.
 * @param rows Height of the window in lines, usually the terminal height.
 */
void editor_scroll(editor_t *e, size_t rows);

/**
 * Room editor_draw() needs on top of the rendered text: the 7-byte clear and
 * home prefix, an upper bound of 44 bytes for the cursor position sequence,
 * and the NUL.
 */
#define EDITOR_DRAW_OVERHEAD 52

/**
 * @brief Tell whether a key press should quit the editor.
 * @param c Byte read from the terminal.
 * @return Non-zero if @p c is Ctrl+Q (0x11), 0 otherwise.
 */
int editor_should_exit(char c);

/**
 * @brief Initialise @p e as an empty editor with no lines and the cursor at 0,0 with no scrolling. Does not allocate.
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
 * @brief Move the cursor one step in direction @p dir.
 *
 * The cursor never wraps and never leaves the text: up and down stop at the
 * first and last line, left stops at column 0, and right stops on the last
 * character of the line, as in Vim's normal mode. Moving to a shorter line
 * clamps the column to its last character; an empty line has column 0. The
 * column is not remembered across lines. An editor with no lines keeps the
 * cursor at 0,0.
 *
 * @param e   Editor to modify; must not be NULL.
 * @param dir Direction to move.
 */
void editor_move_cursor(editor_t *e, editor_move_t dir);

/**
 * @brief Render @p max_rows lines, starting at the first visible line, into @p out as a NUL-terminated string.
 *
 * Lines are joined with "\r\n" (no trailing separator), because raw mode
 * turns off output processing. A blank line counts as a row. A tab is drawn
 * as spaces up to the next multiple of 8 columns (cut at the right edge). A control byte
 * (below 0x20 except tab, or 0x7f) is drawn as a two-column mark such as ^[
 * or ^?, so that a file can never send commands to the terminal; clipping
 * never shows half of a mark. Output is
 * truncated to fit @p out_size, leaving room for the NUL.
 *
 * @param e        Editor to render; must not be NULL.
 * @param max_rows Maximum number of lines to render, usually the terminal
 *                 height; 0 renders nothing. The first line rendered is
 *                 line @c rowoff; a @c rowoff past the last line renders nothing.
 * @param max_cols Maximum number of columns of each line to render, usually
 *                 the terminal width. Longer lines are clipped on the right,
 *                 so no line wraps and scrolls the terminal; there is no
 *                 horizontal scrolling yet.
 * @param out      Destination buffer.
 * @param out_size Size of @p out in bytes.
 * @return Number of bytes written, excluding the NUL; 0 if @p out_size is 0.
 */
size_t editor_render(const editor_t *e, size_t max_rows, size_t max_cols, char *out, size_t out_size);

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
 * A file that contains a NUL byte is refused, so that a line is never shown
 * cut short and then saved over the original.
 *
 * @return 0 on success (including a nonexistent file), -1 on any other
 *         error (errno is set; EILSEQ for a file with a NUL byte).
 */
int editor_load_file(editor_t *e, const char *path);

/**
 * @brief Write a full screen redraw into @p out as a NUL-terminated string.
 *
 * The output clears the screen and moves home, then holds editor_render() of
 * @p max_rows lines from @c rowoff, then moves the cursor to row
 * cy-rowoff+1 (row 1 if the cursor is above the window), at the display
 * column of cx plus 1 (marks are two columns wide and a tab goes to the next
 * tab stop; the cursor is on the first column of a mark or tab). It
 * does not scroll: call editor_scroll() first. The cursor sequence is always complete: if @p out_size is too small
 * the text is cut, and if it is smaller than EDITOR_DRAW_OVERHEAD nothing is
 * written. A cursor below the window is reported at its real row and the
 * terminal clamps it.
 *
 * @param e        Editor to draw; must not be NULL.
 * @param max_rows Maximum number of lines to render, usually the terminal height.
 * @param max_cols Maximum number of bytes of each line, usually the terminal
 *                 width; see editor_render(). A cursor column past it is drawn
 *                 on the last column.
 * @param out      Destination buffer.
 * @param out_size Size of @p out in bytes; it should be at least
 *                 EDITOR_DRAW_OVERHEAD plus the rendered text.
 * @return Number of bytes written, excluding the NUL; 0 if @p out_size is
 *         smaller than EDITOR_DRAW_OVERHEAD.
 */
size_t editor_draw(const editor_t *e, size_t max_rows, size_t max_cols, char *out, size_t out_size);

#endif
