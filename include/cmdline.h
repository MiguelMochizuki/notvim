/**
 * @file cmdline.h
 * @brief The text of the ':' command line and the parser that splits a command into name, bang and argument.
 */
#ifndef CMDLINE_H
#define CMDLINE_H

#include <stddef.h>

/** Most bytes the command line can hold (the NUL not counted). */
#define CMDLINE_MAX 256

/** The text typed after ':'; a plain value, no allocation. */
typedef struct {
	char text[CMDLINE_MAX + 1]; /**< NUL-terminated text, UTF-8, only whole characters. */
	size_t len;                 /**< Bytes in use in @ref text. */
} cmdline_t;

/** A command split by cmd_parse(). */
typedef struct {
	char name[CMDLINE_MAX + 1]; /**< Leading ASCII letters ("w", "wq"); "" if there are none. */
	int bang;                   /**< Non-zero if the name is followed by '!'. */
	char arg[CMDLINE_MAX + 1];  /**< The rest, without the spaces around it; "" if there is none. */
} cmd_t;

/**
 * @brief Empty the command line.
 * @param c Command line; must not be NULL.
 */
void cmdline_clear(cmdline_t *c);

/**
 * @brief Append the @p n bytes of @p s (one whole character) to the command line.
 * @param c Command line; must not be NULL.
 * @param s Bytes to append.
 * @param n Number of bytes.
 * @return 0 on success, -1 if they do not fit in CMDLINE_MAX bytes (the line is unchanged).
 */
int cmdline_append(cmdline_t *c, const char *s, size_t n);

/**
 * @brief Delete the last character (all its bytes); does nothing on an empty line.
 * @param c Command line; must not be NULL.
 */
void cmdline_backspace(cmdline_t *c);

/**
 * @brief Split command text into name, bang and argument.
 *
 * Leading spaces and ':' are skipped; the name is the run of ASCII letters after them, then comes an optional '!',
 * then the argument with the spaces around it trimmed: "w", "w!", "w name", " q! ", "wq", "x". Nothing is
 * validated: an unknown name is parsed like a known one, and an empty text gives an empty name, no bang and no argument.
 *
 * @param text NUL-terminated command text of at most CMDLINE_MAX bytes.
 * @param out  Receives the parts.
 */
void cmd_parse(const char *text, cmd_t *out);

#endif
