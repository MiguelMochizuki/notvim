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

#endif
