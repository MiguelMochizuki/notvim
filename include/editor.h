/**
 * @file editor.h
 * @brief Editor state, key handling and rendering.
 */
#ifndef EDITOR_H
#define EDITOR_H

#include <stddef.h>
#include "cmdline.h"

/** Modes of the editor. */
typedef enum {
	EDITOR_MODE_NORMAL, /**< Normal mode: keys are commands; the cursor is always on a character. */
	EDITOR_MODE_INSERT, /**< Insert mode: the cursor may also sit after the last character of the line. */
	EDITOR_MODE_COMMAND /**< Command-line mode: ':' was typed in normal mode; the bottom row shows ':' and @c cmd and the cursor does not move in the text. */
} editor_mode_t;

/** Value of @c wantcol after "$" (Vim's MAXCOL): up and down go to the end of every line. */
#define EDITOR_WANTCOL_EOL ((size_t)-1)

/** Largest count editor_handle_key() keeps: a longer one stays at this value, so typing digits cannot overflow. */
#define EDITOR_COUNT_MAX 99999999u

/**
 * The pending state: what normal mode has read of a command that is not finished. ONE mechanism for every prefix:
 * editor_handle_key() takes it at the start of each key and clears it, and only a key that continues the command stores it again.
 */
typedef struct {
	char prefix;  /**< 0, or the prefix key that waits for the next key: 'Z' (for "ZZ" and "ZQ"), 'g' (for "gg") or 'f', 't', 'F', 'T' (which wait for the character to find; its bytes are
	                   collected in @ref editor_t::pend, so a half UTF-8 character keeps the prefix pending). */
	size_t count; /**< The count typed so far, 0 for none, at most @ref EDITOR_COUNT_MAX. */
} editor_pending_t;

/** The last character search, for ";" and ",": what "f", "t", "F" or "T" was asked for (even if it found nothing, as in Vim). */
typedef struct {
	char cmd;   /**< 0 if no search was made yet, else 'f', 't', 'F' or 'T'. */
	char ch[5]; /**< The character searched for, as the NUL-terminated bytes of one UTF-8 character (or one other byte). */
} editor_find_t;

/** Editor state: the text as a growable array of lines. */
typedef struct {
	char **lines; /**< Owned array of owned NUL-terminated strings, without newlines. */
	size_t count; /**< Number of lines in use in @ref lines. */
	size_t cap;   /**< Allocated capacity of @ref lines, in lines. */
	size_t cy;    /**< Cursor row: index of the line the cursor is on. */
	size_t cx;    /**< Cursor column: byte index in that line, always at the start of a character (or of an invalid byte), or
	                   the length of the line in insert mode. */
	size_t rowoff; /**< Index of the first visible line (vertical scroll offset). */
	size_t coloff; /**< Display column of the first visible column of every line (horizontal scroll offset); 0 after init, free and load.
	                    Kept by editor_scroll_cols(); render and draw show each line from this column on. */
	size_t wantcol; /**< Wanted display column (Vim's curswant): where up and down try to put the cursor, so that it comes
	                     back to its column after a shorter line. Set by a left or right move that moves, by a motion key (@ref EDITOR_WANTCOL_EOL after "$"); 0 after init, free
	                     and load. @c cx and @c wantcol change together in editor_move_cursor(): code that assigns @c cx
	                     directly must set @c wantcol too. */
	size_t winrows; /**< Height of the text window in lines, as last given to editor_set_window_height(); 0 (after init) means unknown and the page keys do nothing.
	                     Kept across free and load. */
	size_t scroll;  /**< Vim's 'scroll': the lines "Ctrl+d" and "Ctrl+u" move; set by a count given to either key, 0 (after init, free and load) means half the window. */
	editor_mode_t mode; /**< Current mode; @ref EDITOR_MODE_NORMAL after init, free and load. In insert mode @c cx may equal the
	                         length of the line (the cursor is after the last character); in normal mode it never does. */
	int modified; /**< Non-zero once the text has been changed (any edit: typing, Enter, Backspace, Delete); 0 after init, free and
	                   load, and after a successful ":w" to the editor's own file (writing to another name leaves it). The
	                   status line shows "[+]" while it is set. */
	cmdline_t cmd; /**< The command line being typed in @ref EDITOR_MODE_COMMAND; emptied when that mode starts and ends. */
	char *msg;     /**< Owned message, or NULL: shown on the bottom row instead of the status line until the next key (see editor_set_message()). */
	int quit;      /**< Non-zero once a quit was asked for (":q", ":wq", ":x", "ZZ", "ZQ", Ctrl+Q) and allowed; the caller then ends its input loop
	                    and exits with status 0. 0 after init, free and load; a refused quit leaves it 0. */
	editor_pending_t pending; /**< The unfinished normal-mode command (a count and a prefix key); all zero after init, free and load, and cleared by every key
	                               that does not continue it and whenever the mode is not normal. */
	editor_find_t lastfind; /**< The last "f", "t", "F" or "T"; all zero after init, free and load. */
	char pend[4]; /**< Bytes of a UTF-8 character typed in insert mode, or after "f", "t", "F" or "T", that is still incomplete; see @ref pend_len. */
	size_t pend_len; /**< Number of bytes in @ref pend; 0 when no character is half typed. Reset by any key that is not a
	                      continuation byte, and by leaving insert mode. */
	int crlf;     /**< Non-zero if the loaded file used CRLF line endings (lines are stored without the CR);
	                   0 for an LF, mixed or empty file, no file, or after an error. */
	char *path;   /**< Owned copy of the path given to the last successful editor_load_file(), even if that file does not
	                   exist (a saver creates it), or of the name that ":w name" wrote first when there was none; NULL after editor_init(), after editor_free() and after a failed load. */
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
 * @brief Scroll horizontally so that the cursor is inside the text window.
 *
 * Changes only @c coloff, by the least amount (one column at a time as the cursor moves, unlike Vim's default
 * 'sidescroll' of 0, which recentres the cursor): left if the display column of the cursor is left of the window, right
 * if the cell under the cursor does not fit before the right edge. A control-byte mark must fit whole; a tab only needs
 * its first column, and the cursor after the last character (insert mode) needs one. Does nothing if @p cols is 0 or
 * there is no cursor line. The caller calls it after every key that may move the cursor or edit, and after a resize.
 *
 * @param e    Editor to modify; must not be NULL.
 * @param cols Width of the text window in columns, usually the terminal width.
 */
void editor_scroll_cols(editor_t *e, size_t cols);

/**
 * Room editor_draw_text() needs on top of the rows: the 6-byte hide-cursor and the
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
 * Room editor_draw_screen() needs, on top of EDITOR_DRAW_OVERHEAD and of the text rows, for the status row: the move to
 * it ("ESC[<row>;1H", at most 25 bytes), the 4-byte reverse-video start and the 3-byte reverse-video end, 32 bytes. The
 * status text itself takes at most 4 bytes per column. A buffer of rows * (cols * 4 + 5) + EDITOR_DRAW_OVERHEAD +
 * EDITOR_STATUS_OVERHEAD bytes always holds a whole screen of @c rows rows with its status row.
 */
#define EDITOR_STATUS_OVERHEAD 32

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
 * character of the line, as in Vim's normal mode; in insert mode (see editor_handle_key()) it may also go past the last
 * character, onto the end of the line. Text is UTF-8: left and
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
 * @brief Tell the editor the height of the text window, which the page keys ("Ctrl+f", "Ctrl+b", "Ctrl+d", "Ctrl+u") need.
 *
 * The one place the height is kept: the program calls it at startup and after every resize with editor_text_rows() of the terminal height.
 *
 * @param e      Editor to modify; must not be NULL.
 * @param height Lines of text the window shows; 0 means unknown.
 */
void editor_set_window_height(editor_t *e, size_t height);

/**
 * @brief Handle one key (from key_parser_feed()) in the current mode.
 *
 * Normal mode: 'i' enters insert mode with the cursor where it is; the arrow keys and h/j/k/l move the cursor with
 * editor_move_cursor(); anything else does nothing. Insert mode: KEY_ESC leaves it, moving the cursor one character
 * left unless it is at column 0, as Vim does, and sets @c wantcol to the new column; only the arrow keys move the
 * cursor. Every other key is typed: a printable ASCII byte or a Tab (as a tab character) is inserted at the cursor,
 * and so is a UTF-8 character, but only once all its bytes have arrived (they come as consecutive keys; the first
 * ones are kept in @c pend and the key returns 0). A stray or invalid byte, an incomplete character that is followed
 * by a key that does not continue it, and every other control key are dropped. The first insertion into an editor
 * with no lines creates its first line. After an insertion the cursor and @c wantcol are after the new character,
 * @c modified is set and the key returns non-zero; the caller scrolls with editor_scroll(). A failed allocation drops
 * the character and returns 0. KEY_ESC does nothing in normal mode.
 *
 * Insert mode also edits the line structure. Enter (byte 0x0d or 0x0a) splits the line at the cursor: the text from the
 * cursor on becomes a new line below, and the cursor goes to its start (an editor with no lines gets two empty lines).
 * Backspace (0x7f or 0x08) deletes the character before the cursor, a whole UTF-8 character; at column 0 it joins the line
 * to the end of the previous one, with the cursor at the join, and at column 0 of the first line it does nothing.
 * KEY_DELETE deletes the character under the cursor, a whole one; at the end of a line it joins the next line to it,
 * the cursor staying at the join, and at the end of the last line (or with no lines) it does nothing. Each of these sets
 * @c modified and @c wantcol (to 0 after a split, to the display column of the cursor otherwise) and returns non-zero when
 * it changed the text; one that does nothing returns 0 and leaves @c modified alone. They also drop a half typed
 * character. In normal mode they do nothing yet. @c crlf is never touched; the caller scrolls.
 *
 * In insert mode editor_move_cursor() lets the cursor go one past the last character: right stops at the end of the
 * line, left comes back from it, and up and down put the cursor on the character whose columns contain @c wantcol, or
 * at the end of the line if @c wantcol is at or past the end of it (an empty line: 0).
 *
 * Motions (normal mode only; in insert mode the keys are typed): "0", "^" and "$" go to the start, the first non-blank
 * and the last character of the line, "w", "b" and "e" by word and "W", "B" and "E" by WORD (see motions.h). The cursor goes to the
 * target of the motion; @c wantcol is set from it even if the cursor does not move, and after "$" to @ref EDITOR_WANTCOL_EOL. They
 * return non-zero only if the cursor moved, never set @c modified, and do nothing with no lines.
 *
 * Counts and "g": in normal mode the digits 1 to 9 start a count and 0 continues it (with no count pending "0" is the motion).
 * The count repeats the next motion: "h", "j", "k", "l" and the arrows, "w", "b", "e", "W", "B", "E" repeat their one-step
 * motion and stop early at the ends of the text, as Vim does; "0", "^" and "$" ignore it, except that "{n}$" also goes
 * n-1 lines down. "G" goes to line {count} (the last line with no count) and "gg" to line {count} (the first with no count),
 * a count past the end meaning the last line; the cursor goes to the first non-blank character of that line and @c wantcol is set from it.
 * "g" waits for a second key: anything but "g" (Esc too) is dropped with it, and with the count. The typed count is not
 * shown. Every key that does not continue the command, and Esc, clear the count; counts and "g" do nothing in insert and
 * command mode, where they are typed. Typing a digit or "g" returns 0 (nothing to redraw).
 *
 * Paragraphs and brackets (normal mode): "}" and "{" go to the next and previous empty line and "%" to the matching bracket (see motions.h).
 * "}" and "{" repeat with a count, and the whole command fails (the cursor stays) if a step before the last finds nothing more, as in Vim; "%" ignores a count and does nothing without a match. @c wantcol is set from the target
 * as for the other motions, even if the cursor does not move; they return non-zero only if the cursor moved and never set @c modified.
 *
 * Character search (normal mode): "f{char}" moves to the next occurrence of {char} on the cursor line, "t{char}" to the character before it, "F" and "T" the same backwards;
 * {char} is one complete UTF-8 character (or one other byte, a tab or a control byte included; its bytes may arrive in separate keys, Esc or anything that is not the rest of the
 * character cancels). A count N means the Nth occurrence; if there are fewer, or none, the cursor stays and @c wantcol is unchanged. Every search is stored in @c lastfind, found or not.
 * ";" repeats it in the same direction and "," in the opposite one (nothing without a stored search); a repeated "t" or "T" with a count of 1 does not stop on the match right next to the
 * cursor but goes on to the next (Vim's default 'cpoptions'). The result sets @c wantcol from the target, even if the cursor does not move; they return non-zero only if the cursor moved
 * and never set @c modified. In insert and command mode the keys are typed. See motions.h for the pure target function.
 *
 * Page keys (normal mode, window height from editor_set_window_height(); h is that height, the window has the lines @c rowoff to @c rowoff + h - 1; with a height of
 * 0 or no lines they do nothing). The ruling comes from Vim's documentation, because Vim's -es mode has a window height but does not scroll: "Ctrl+f" (0x06)
 * scrolls forward {count} pages of h - 2 lines (at least 1): @c rowoff grows by that, but never past the last line, and stays 0 if the whole text
 * fits in the window; the cursor goes to the first line of the window if it is above it. "Ctrl+b" (0x02) scrolls back the same, @c rowoff not below 0, and the cursor goes to the last
 * line of the new window (checked in a real Vim); at @c rowoff 0 or if the text fits in the window it does nothing, the cursor stays. "Ctrl+d" (0x04) and "Ctrl+u" (0x15) scroll down and up @c scroll lines (a count sets it; with none set, h / 2, at least 1) and
 * move the cursor the same number of lines; "Ctrl+d" scrolls at most until the last line is the last of the window and then only the cursor moves, "Ctrl+u" stops at @c rowoff 0 and
 * line 0; the cursor stops at the last and first line. After a page key the cursor is on the first non-blank of its line and @c wantcol is set from it ('startofline'). A key that
 * changes neither @c rowoff nor the cursor (already at an end) does nothing and returns 0; a changing one returns non-zero. They never set @c modified; in insert and command mode they are dropped as before.
 *
 * Quitting: in normal mode "Z" sets @c pending.prefix and returns 0; the next key then clears it and, if it is 'Z' or 'Q', runs
 * ":x" or ":q!" (see commands_run()); any other key is handled as if "Z" had not been typed. In insert and command mode
 * 'Z' is a plain character. Ctrl+Q (byte 0x11, in every mode) asks to quit like ":q": it sets @c quit, or, when
 * @c modified is set, shows "E37: No write since last change (add ! to override)" and the editor goes on.
 *
 * In normal mode ':' opens the command line (@ref EDITOR_MODE_COMMAND; in insert mode ':' is typed as a colon). In
 * command mode a printable ASCII byte or a whole UTF-8 character is appended to @c cmd (the bytes of a character
 * arrive as consecutive keys, as in insert mode; at CMDLINE_MAX bytes more are dropped); Backspace deletes the last
 * character and on an EMPTY command line cancels, as Vim does; KEY_ESC cancels; Enter runs the command and returns to
 * normal mode. Cancel and run empty the command line and leave the cursor where it was. Every other key is ignored.
 * Running: an empty command (only spaces and colons) does nothing; any other, known or not, sets the message
 * "E492: Not an editor command: <text>" unless commands_run() knows it. Every call first clears the
 * message, and then returns non-zero if there was one, so the screen is redrawn without it.
 *
 * @param e   Editor to modify; must not be NULL.
 * @param key A byte (0-255) or a KEY_ constant of keys.h.
 * @return Non-zero if the key was a command of the current mode, so the screen must be redrawn (even if the cursor
 *         did not move); 0 if it was ignored.
 */
int editor_handle_key(editor_t *e, int key);

/**
 * @brief Render @p max_rows lines, starting at the first visible line, into @p out as a NUL-terminated string.
 *
 * The pure text view of the rows. editor_draw_text() draws the screen with the same row writer, so this is
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
 *                 the terminal width (columns, not bytes). Each line is shown from display column @c coloff on
 *                 and clipped on the right, so no line wraps and scrolls the terminal. A tab that straddles
 *                 @c coloff shows its remaining spaces; a mark that straddles it shows one space, so the cells after
 *                 it stay in place; a cell cut by the right edge follows the rules above.
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
 * @c e->path is a copy of @p path once the call succeeds, also for a nonexistent file; the previous path is
 * dropped first like the previous contents, so on an error it is NULL.
 *
 * @param e    Editor to load into; must not be NULL. Left empty on error.
 * @param path Path of the file to load.
 *
 * @return 0 on success (including a nonexistent file), -1 on any other
 *         error (errno is set; EILSEQ for a file with a NUL byte, ENOMEM if the path can't be copied).
 */
int editor_load_file(editor_t *e, const char *path);

/**
 * @brief Height of the text window of a terminal of @p rows rows: the last row is the status line.
 * @param rows Terminal height.
 * @return @p rows - 1 for 2 or more rows; @p rows for 0 or 1 (a single row shows text and has no status line).
 */
size_t editor_text_rows(size_t rows);

/**
 * @brief Label of the mode of @p e, as the status line shows it.
 * @param e Editor to describe; must not be NULL.
 * @return "NORMAL", "INSERT" or "COMMAND"; a string literal, never NULL.
 */
const char *editor_mode_label(const editor_t *e);

/**
 * @brief Write the status line of @p e, as drawn, into @p out as a NUL-terminated string of exactly @p cols columns.
 *
 * Pure text, no escape sequences (editor_draw_screen() puts it in reverse video). The layout is
 * "<name> <mode>" (or "<name> [dos] <mode>" if @c crlf is set, a CRLF file that is kept as it is; " [+]" follows the name
 * and the [dos] when @c modified is set: "<name> [dos] [+] <mode>"), then spaces, then "<line>,<col>" ending in the last column, where @c name is @c e->path ("[No Name]" if it is NULL
 * or empty) and @c mode is editor_mode_label(). @c line is @c cy + 1 (1 for an editor with no lines) and
 * @c col is the display column of the cursor plus 1 (1 if @c cy is not a line), counted as the cursor is drawn (a tab or a mark counts from its
 * first column, a character or '?' is one column), not clipped to the width. The name goes through the same rules as
 * the text of a line: a control byte is a two-column mark such as ^A, a tab is spaces to the next multiple of 8 columns
 * from the start of the status, valid UTF-8 is copied and any other byte or a C1 control is '?'.
 *
 * It is cut to the width by columns, never inside a character or a mark, and padded with spaces. The position has
 * priority: if it does not fit (@p cols no wider than it) it alone is written cut on the right ("1,1" at 2 columns is
 * "1,"); otherwise the left part is cut on the right to the @p cols minus the position minus 1 columns that remain
 * (so the mode label goes first, then [dos], then the name), at least one space separates it from the position, and the line
 * is padded to @p cols. 0 columns give "".
 *
 * @param e        Editor to describe; must not be NULL.
 * @param cols     Width of the terminal in columns.
 * @param out      Destination buffer.
 * @param out_size Size of @p out in bytes; @p cols * 4 + 1 always holds the whole line. A smaller buffer gets the line
 *                 cut at a byte, leaving room for the NUL.
 * @return Number of bytes written, excluding the NUL; 0 if @p out_size is 0.
 */
size_t editor_status(const editor_t *e, size_t cols, char *out, size_t out_size);

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
 * column of cx minus @c coloff plus 1 ("ESC[row;colH"; counted in characters; marks are two
 * columns wide and a tab goes to the next tab stop; the cursor is on the
 * first column of a mark or tab) and shows it ("ESC[?25h"). An editor with no
 * lines draws "ESC[?25l ESC[H ESC[1;1H ESC[J ESC[1;1H ESC[?25h". There is never
 * a clear-screen sequence ("ESC[2J").
 *
 * Text window only (@p max_rows is the number of text rows): the program draws with editor_draw_screen(), which adds the
 * status line; this one is the text part of it.
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
size_t editor_draw_text(const editor_t *e, size_t max_rows, size_t max_cols, char *out, size_t out_size);

/**
 * @brief Write a full redraw of a terminal of @p rows rows into @p out: the text window and the status line.
 *
 * Like editor_draw_text() with @c editor_text_rows(rows) text rows (same text, row ends, erase of the unused text rows,
 * rules about never cutting an escape sequence), then, for @p rows of 2 or more, the status line of editor_status() on
 * the last row: the move "ESC[<rows>;1H", reverse video "ESC[7m", the @p max_cols columns of the status, "ESC[m". It
 * comes after the erase of the unused text rows ("ESC[<n+1>;1H ESC[J"), which would otherwise wipe it, and it has no
 * "ESC[K" because it takes the full width (a character in the last column leaves the cursor in pending wrap). Last
 * come the cursor position and "ESC[?25h", as in editor_draw_text(), but the row of the cursor never goes below the last text
 * row, so the cursor is never on the status line. With @p rows 0 or 1 there is no status line and the output is
 * that of editor_draw_text() with the same @p rows.
 *
 * Call editor_scroll() with editor_text_rows(@p rows) first. The status line is reserved before the rows: it is drawn
 * if its bytes (the move, ESC[7m, the status text, ESC[m) fit in @p out_size - EDITOR_DRAW_OVERHEAD, and the text rows
 * then keep the room that remains; if it does not fit it is left out whole and the rows get all the room. Nothing is
 * written if @p out_size is smaller than EDITOR_DRAW_OVERHEAD.
 *
 * @param e        Editor to draw; must not be NULL.
 * @param rows     Height of the terminal, status line included.
 * @param max_cols Width of the terminal; see editor_draw_text().
 * @param out      Destination buffer.
 * @param out_size Size of @p out in bytes; rows * (@p max_cols * 4 + 5) + EDITOR_DRAW_OVERHEAD + EDITOR_STATUS_OVERHEAD
 *                 always holds the whole screen.
 * @return Number of bytes written, excluding the NUL; 0 if @p out_size is smaller than EDITOR_DRAW_OVERHEAD.
 */
size_t editor_draw_screen(const editor_t *e, size_t rows, size_t max_cols, char *out, size_t out_size);

/**
 * @brief Show @p text on the bottom row instead of the status line, until the next key press.
 *
 * The message mechanism: @p e keeps a copy in @c msg (a previous message is dropped). It is drawn like the status line
 * is, as one row of the full width cut by columns and padded with spaces, but in plain video, and through the same
 * render rules as buffer text (marks for control bytes, '?' for invalid UTF-8), so bytes of a file or a path can be
 * passed safely. The next call of editor_handle_key(), whatever the key, clears it (and asks for a redraw). A resize
 * does not clear it. A terminal of one row has no bottom row: the message is kept but not drawn.
 *
 * @param e    Editor to modify; must not be NULL.
 * @param text NUL-terminated message.
 * @return 0 on success, -1 on failure (errno is ENOMEM); the old message is kept on failure.
 */
int editor_set_message(editor_t *e, const char *text);

/**
 * @brief Write the bottom row of @p e, as drawn, into @p out as a NUL-terminated string of exactly @p cols columns.
 *
 * In command mode it is ':' and the command text; else the message if there is one; else editor_status(). It is cut to
 * @p cols columns by the render rules (never inside a character or mark) and padded with spaces. Pure text.
 *
 * @param e        Editor to describe; must not be NULL.
 * @param cols     Width of the terminal in columns.
 * @param out      Destination buffer; @p cols * 4 + 1 bytes always hold the whole row.
 * @param out_size Size of @p out in bytes.
 * @return Number of bytes written, excluding the NUL; 0 if @p out_size is 0.
 */
size_t editor_bottom_line(const editor_t *e, size_t cols, char *out, size_t out_size);

#endif
