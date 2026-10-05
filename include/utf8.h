/**
 * @file utf8.h
 * @brief Decoding UTF-8 text one character at a time (RFC 3629), for the editor.
 *
 * Every function works on a NUL-terminated string and never reads past its
 * NUL. A "cell" is what the editor treats as one character: a valid UTF-8
 * sequence, or a single byte that is not part of a valid sequence. The
 * cells of a line are found by decoding from its start, so they are the same
 * for every function here.
 */
#ifndef UTF8_H
#define UTF8_H

#include <stddef.h>

/**
 * @brief Byte length of the valid UTF-8 sequence at the start of @p s.
 *
 * Overlong forms (such as 0xC0 0x80), surrogates (U+D800 to U+DFFF), code
 * points above U+10FFFF, truncated sequences, a stray continuation byte and
 * the bytes 0xC0, 0xC1 and 0xF5 to 0xFF are invalid. A byte below 0x80 is a
 * valid sequence of length 1, including control bytes.
 *
 * @param s NUL-terminated string; must not be NULL.
 * @return 1 to 4, or 0 if @p s is empty or does not start with a valid sequence.
 */
size_t utf8_valid_len(const char *s);

/**
 * @brief Number of bytes of the cell at the start of @p s.
 * @param s NUL-terminated string; must not be NULL.
 * @return utf8_valid_len() if it is not 0, otherwise 1 for an invalid byte;
 *         0 only if @p s is empty.
 */
size_t utf8_cell_len(const char *s);

/**
 * @brief Whether @p s starts with a C1 control character, U+0080 to U+009F (bytes 0xC2 0x80 to 0xC2 0x9F).
 *
 * Some terminals act on these (U+009B is a CSI), so the editor does not draw them.
 *
 * @param s NUL-terminated string; must not be NULL.
 * @return 1 if the valid sequence at @p s is a C1 control, 0 otherwise.
 */
int utf8_is_c1(const char *s);

/**
 * @brief Start of the cell that contains byte @p i - 1 of @p line.
 *
 * For @p i at the start of a cell (or at the end of the line) this is the
 * previous cell; for @p i inside a cell it is the start of that cell, so
 * utf8_prev(line, cx + 1) snaps @p cx back to a cell start.
 *
 * @param line NUL-terminated string; must not be NULL.
 * @param i    Byte index, from 0 to the length of @p line; a larger value is treated as the length.
 * @return The start of that cell, or 0 if @p i is 0.
 */
size_t utf8_prev(const char *line, size_t i);

#endif
