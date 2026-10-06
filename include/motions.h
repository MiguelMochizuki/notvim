/**
 * @file motions.h
 * @brief Cursor motions as pure functions: each returns where the cursor would go, and moves nothing.
 *
 * Every function takes the editor and returns the target for one step from the cursor
 * (@c cy, @c cx); the editor state is not changed. The key handler applies the target;
 * operators such as "d", "c" and "y" can reuse the same functions. They follow Vim 9.1 with
 * its defaults: the target is always on a character (on the last one of a line, never after
 * it), an editor with no lines gives (0, 0), and a motion that cannot go further returns the
 * cursor where Vim would leave it (for example "w" on the last word of the text gives the
 * last character).
 *
 * A "word" is a run of letters, digits and underscore, or a run of other non-blank characters;
 * blanks separate words and an empty line is a word. Letters are those of Vim's 'iskeyword'
 * (ASCII letters and digits, '_', U+00C0 and up) and, above U+00FF, everything but the
 * punctuation, symbols and spaces of U+2000 to U+27FF and U+3001 to U+3020; Hiragana,
 * Katakana, CJK ideographs and Hangul are separate classes, so "ab" and "ひら" are two words.
 * A "WORD" (the @c big variants) is any run of non-blank characters. A blank is a space, a tab
 * or U+00A0 and the other Unicode spaces of Vim's table. An invalid byte is classed as the
 * character with that code.
 */
#ifndef MOTIONS_H
#define MOTIONS_H

#include <stddef.h>
#include "editor.h"

/** A position in the text. */
typedef struct {
	size_t y; /**< Line index. */
	size_t x; /**< Byte index in the line, at the start of a character. */
} motion_pos_t;

/** @brief "0": the first character of the cursor line. */
motion_pos_t motion_line_start(const editor_t *e);

/** @brief "^": the first character that is not a space or a tab; on a line of only blanks the last character, on an empty line 0. */
motion_pos_t motion_first_nonblank(const editor_t *e);

/** @brief "$": the last character of the cursor line (0 on an empty line). */
motion_pos_t motion_line_end(const editor_t *e);

/**
 * @brief "w" or "W": the start of the next word, over line boundaries; an empty line stops it; at the end of the text the last character.
 * @param big Non-zero for "W": a word is any run of non-blanks.
 */
motion_pos_t motion_word_next(const editor_t *e, int big);

/**
 * @brief "b" or "B": the start of the word before the cursor (the start of the current one if the cursor is inside it); an empty line stops it; at the start of the text (0, 0).
 * @param big Non-zero for "B".
 */
motion_pos_t motion_word_prev(const editor_t *e, int big);

/**
 * @brief "e" or "E": the last character of the word under the cursor, or of the next word if the cursor is already on a last character; an empty line is passed over; at the end of the text the last character.
 * @param big Non-zero for "E".
 */
motion_pos_t motion_word_end(const editor_t *e, int big);

/**
 * @brief "G" and "gg": the first non-blank character of line @p n (1-based), as with "^" (the last character of a line of only blanks); an @p n of 0 or past the last line means the last line.
 */
motion_pos_t motion_goto_line(const editor_t *e, size_t n);

#endif
