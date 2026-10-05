/**
 * @file drawfmt.h
 * @brief The screen that editor_draw() must produce, spelled in one place for the tests.
 */
#ifndef DRAWFMT_H
#define DRAWFMT_H

#include <stddef.h>

/**
 * @brief Build the expected output of editor_draw() for rows of text.
 *
 * The format is hide cursor and home, the rows separated by "\r\n", each followed by
 * erase-to-end-of-line only if its display width is less than @p max_cols (after a
 * character in the last column the cursor would still be on it and the erase would
 * remove it), then, if fewer than @p max_rows rows were drawn, a move to the first free
 * row and erase to the end of the screen, then the cursor position and show cursor.
 * There is no clear-screen sequence.
 *
 * @param buf      Destination, NUL-terminated.
 * @param size     Size of @p buf.
 * @param text     The rows as drawn (already rendered: marks, '?' and clipping applied) joined with "\r\n",
 *                 so "a\r\n\r\nb" is three rows, the middle one blank; "" is one blank row;
 *                 NULL is no row at all (an empty editor). The width of a row is its number of UTF-8 characters.
 * @param max_rows Rows of the screen.
 * @param max_cols Columns of the screen.
 * @param row      Cursor row, 1-based.
 * @param col      Cursor column, 1-based.
 */
void draw_expected(char *buf, size_t size, const char *text, size_t max_rows, size_t max_cols, int row, int col);

/**
 * @brief Build the expected output of editor_draw_screen(): draw_expected() for the text rows, with the status line before the cursor.
 *
 * After the text rows and the erase of the unused ones, "ESC[<status_row>;1H ESC[7m <status> ESC[m", then the cursor
 * position and show cursor. No ESC[K follows the status: it takes the whole width.
 *
 * @param status     The status line as drawn, already padded to the width; NULL for none (a terminal of one row).
 * @param status_row Row of the status line, 1-based: the terminal height.
 * @note The other parameters are those of draw_expected(); @p max_rows is the number of TEXT rows.
 */
void draw_expected_status(char *buf, size_t size, const char *text, size_t max_rows, size_t max_cols, const char *status,
                          size_t status_row, int row, int col);

/**
 * @brief Build the status line of a plain ASCII file name the way editor_status() must, for the pty tests.
 *
 * "<name> [dos] <mode>" ("[dos] " only if @p dos), padding, "<line>,<col>" at the right, cut as editor_status() documents;
 * @p name must be printable ASCII.
 *
 * @param buf  Destination, NUL-terminated.
 * @param size Size of @p buf.
 * @param name File name, or NULL for "[No Name]".
 * @param dos  Non-zero for a CRLF file: " [dos]" follows the name.
 * @param mode Mode label, such as "NORMAL".
 * @param line Line of the cursor, 1-based.
 * @param col  Display column of the cursor, 1-based.
 * @param cols Width of the terminal.
 */
void status_expected(char *buf, size_t size, const char *name, int dos, const char *mode, size_t line, size_t col, size_t cols);

#endif
