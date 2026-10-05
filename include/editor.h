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
	size_t cx;    /**< Cursor column: byte index in that line, always at the start of a character (or of an invalid byte). */
	size_t rowoff; /**< Index of the first visible line (vertical scroll offset). */
	size_t wantcol; /**< Wanted display column (Vim's curswant): where up and down try to put the cursor, so that it comes
	                     back to its column after a shorter line. Set by a left or right move that moves; 0 after init, free
	                     and load. @c cx and @c wantcol change together in editor_move_cursor(): code that assigns @c cx
	                     directly must set @c wantcol too. */
	int crlf;     /**< Non-zero if the loaded file used CRLF line endings (lines are stored without the CR);
	                   0 for an LF, mixed or empty file, no file, or after an error. */
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
 * Room editor_draw() needs on top of the rows: the 6-byte hide-cursor and the
 * 3-byte home sequences, the move to the first free row ("ESC[<n>;1H", at most
 * 25 bytes) and the 3-byte erase-to-end-of-screen, an upper bound of 44 bytes
 * for the cursor position sequence, the 6-byte show-cursor sequence, and the
 * NUL: 88 bytes. The rows themselves take their text (up to 4 bytes per
 * column), 3 bytes each for an erase-to-end-of-line (only a row narrower than
 * the screen has one) and 2 for each "\r\n" between them. The rows that fit
 * are those whose bytes are at most the buffer size minus this constant.
 */
#define EDITOR_DRAW_OVERHEAD 88

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
 * character of the line, as in Vim's normal mode. Text is UTF-8: left and
 * right move one character, an invalid byte counts as one character, and a C1
 * control (U+0080 to U+009F) is one character of two bytes. @c cx stays a
 * byte index and never points inside a character.
 *
 * The cursor remembers a wanted display column in @c wantcol, as in Vim. A
 * left or right move that really moves the cursor sets it to the display
 * column of the new @c cx (a tab or a mark counts from its first column). A
 * move that does not move (right on the last character or on an empty line,
 * left at column 0) leaves it alone. Up and down keep it: the new @c cx is the
 * start of the character whose first display column is the largest one not
 * above @c wantcol, so a column inside a tab or a two-column mark picks that
 * tab or mark; if every character of the line starts before @c wantcol the
 * cursor goes to the last character, and on an empty line to 0. Up on the
 * first line and down on the last change nothing, @c wantcol included, so the
 * cursor comes back to its column after passing a shorter line. An editor
 * with no lines keeps the cursor at 0,0.
 *
 * @param e   Editor to modify; must not be NULL.
 * @param dir Direction to move.
 */
void editor_move_cursor(editor_t *e, editor_move_t dir);

/**
 * @brief Render @p max_rows lines, starting at the first visible line, into @p out as a NUL-terminated string.
 *
 * The pure text view of the rows. editor_draw() draws the screen with the same row writer, so this is
 * what the tests and any tool that wants the text without escape sequences use.
 *
 * Lines are joined with "\r\n" (no trailing separator), because raw mode
 * turns off output processing. A blank line counts as a row. A tab is drawn
 * as spaces up to the next multiple of 8 columns (cut at the right edge). Text is UTF-8
 * (RFC 3629): each valid character is one column and is copied as its bytes;
 * each byte that is not part of a valid sequence, and each C1 control
 * (U+0080 to U+009F, which some terminals act on), is drawn as one '?'.
 * Wide and combining characters are not special: they also take one column. A control byte
 * (below 0x20 except tab, or 0x7f) is drawn as a two-column mark such as ^[
 * or ^?, so that a file can never send commands to the terminal; clipping
 * is by columns and never cuts a character or shows half of a mark. Output is
 * truncated to fit @p out_size, leaving room for the NUL.
 *
 * @param e        Editor to render; must not be NULL.
 * @param max_rows Maximum number of lines to render, usually the terminal
 *                 height; 0 renders nothing. The first line rendered is
 *                 line @c rowoff; a @c rowoff past the last line renders nothing.
 * @param max_cols Maximum number of columns of each line to render, usually
 *                 the terminal width (columns, not bytes). Longer lines are clipped on the right,
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
 * The line-ending style is detected and recorded in @c crlf. The file is CRLF
 * if it has at least one line ended by '\n' and every such line has '\r'
 * right before the '\n'; a last line without a newline is ignored for this.
 * For a CRLF file the '\r' of each terminated line is not stored. For any
 * other file (LF, empty or mixed) nothing is removed, @c crlf is 0, and a '\r'
 * is shown as ^M. A last line without a newline keeps a trailing '\r' even in
 * a CRLF file (it is shown as ^M). @c crlf is also 0 for a nonexistent file and
 * after an error. A saver (H4.1) must write "\r\n" after each line when @c crlf
 * is set and "\n" otherwise; the lines of a mixed file keep their '\r' and are
 * written back unchanged.
 *
 * A file that contains a NUL byte is refused, so that a line is never shown
 * cut short and then saved over the original.
 *
 * @param e    Editor to load into; must not be NULL. Left empty on error.
 * @param path Path of the file to load.
 *
 * @return 0 on success (including a nonexistent file), -1 on any other
 *         error (errno is set; EILSEQ for a file with a NUL byte).
 */
int editor_load_file(editor_t *e, const char *path);

/**
 * @brief Write a full screen redraw into @p out as a NUL-terminated string.
 *
 * The screen is not cleared first, so it does not flicker (the first draw
 * needs no clear either: the alternate screen starts blank). The output hides
 * the cursor and moves home ("ESC[?25l ESC[H"), then draws each visible row
 * (the text of editor_render(), line by line, from @c rowoff, at most
 * @p max_rows), separated by "\r\n". A row narrower than @p max_cols is
 * followed by erase-to-end-of-line ("ESC[K"); a row of exactly @p max_cols
 * columns is not, because after a character in the last column the cursor is
 * still on it ("pending wrap") and the erase would remove that character.
 * If fewer than @p max_rows rows were drawn, it then moves to the first free
 * row and erases from there to the end of the screen ("ESC[<n+1>;1H ESC[J",
 * which clears what an older, longer screen left below); a full screen has
 * nothing to erase and gets neither. Last it moves the cursor to row
 * cy-rowoff+1 (row 1 if the cursor is above the window) at the display
 * column of cx plus 1 ("ESC[row;colH"; counted in characters; marks are two
 * columns wide and a tab goes to the next tab stop; the cursor is on the
 * first column of a mark or tab) and shows it ("ESC[?25h"). An editor with no
 * lines draws "ESC[?25l ESC[H ESC[1;1H ESC[J ESC[1;1H ESC[?25h". There is never
 * a clear-screen sequence ("ESC[2J").
 *
 * It does not scroll: call editor_scroll() first. No escape sequence or
 * character is ever cut: the tail is always complete, and a row that does not
 * fit whole in the room left (its text and its "ESC[K", and the "\r\n" before
 * it) is left out together with the rows after it, and the erase clears their
 * place. If @p out_size is smaller than EDITOR_DRAW_OVERHEAD nothing is
 * written. A cursor below the window is reported at its real row and the
 * terminal clamps it.
 *
 * @param e        Editor to draw; must not be NULL.
 * @param max_rows Maximum number of lines to render, usually the terminal height.
 * @param max_cols Maximum number of columns of each line, usually the terminal
 *                 width; see editor_render(). A cursor column past it is drawn
 *                 on the last column.
 * @param out      Destination buffer.
 * @param out_size Size of @p out in bytes; EDITOR_DRAW_OVERHEAD plus the rows
 *                 (up to 4 bytes per column, plus 3 per row for "ESC[K" and 2
 *                 between rows) always holds the whole screen.
 * @return Number of bytes written, excluding the NUL; 0 if @p out_size is
 *         smaller than EDITOR_DRAW_OVERHEAD.
 */
size_t editor_draw(const editor_t *e, size_t max_rows, size_t max_cols, char *out, size_t out_size);

#endif
