# Backlog: notvim

Stories are numbered `H<epic>.<n>`. A story is "As user" (what the editor does) or "As dev" (how the project is built).
Under a story, **Design** is how it was built, **Decisions** are the choices behind it, and **Known gaps** is what is left out on purpose.

| Epic | Theme |
|------|-------|
| H0 | Development foundations: build, tests, documentation, memory safety |
| H1 | Terminal and display |
| H2 | Navigation |
| H3 | Insert mode |
| H4 | Saving and quitting |
| H5 | Interface |
| H6 | Robustness with real files and terminals |

## In progress

(nothing in progress)

## To do

### H6 Robustness with real files and terminals

Each story below was reproduced against the real binary on a pty.
They are ordered by harm: data loss first, then anything that corrupts or commands the terminal, then display correctness, then usability.
All of them are done (see Done below): this epic is finished.


### H0 Development foundations

- **H0.11** As dev, I want the remaining test gaps closed where practical
  - The out-of-memory path of `editor_load_file` and `editor_append_line` is not tested.
  - The cleanup of a half-loaded editor is now tested through the NUL case (H6.2); a real read error halfway through a file still is not.
  - The pty test cannot check that the terminal is restored (H0.5), and a crash leaves the temporary directory behind (H0.7).

### H2 Navigation

- **H2.5** As user, I want long lines to scroll horizontally with the cursor, so I can read and reach the part of a line that is clipped
  - Same mechanism as `rowoff`, for columns: `coloff`, `editor_scroll` with the width, the drawn cursor column relative to it.
  - Until then the cursor stops on the last visible column of a clipped line.

### H3 Insert mode

- **H3.1** As user, I want to press `i` to enter insert mode and `Esc` to leave it, so typing and commands don't collide
  - `Esc` is now recognised (H6.1): the decoder reports `KEY_ESC`.
  - The `h j k l` mapping (H2.2) must apply only in normal mode, because `h` has to type `h` in insert mode.
- **H3.2** As user, I want to type characters in insert mode and see them in the buffer
- **H3.3** As user, I want `Backspace` and `Enter` to work in insert mode

### H4 Saving and quitting

- **H4.1** As user, I want to save with `:w`, so I don't lose my work
  - A missing file is created here, not on load (H1.4).
  - An editor with 0 lines saves as an empty file, and one empty line as `"\n"` (H0.8).
  - Loading never truncates, so saving a long file must write every line (H0.8).
  - Saving must honour `editor_t.crlf` (write `\r\n` after each line when set, `\n` otherwise) and write back invalid UTF-8 bytes unchanged (H6.8, H6.9).
- **H4.2** As user, I want to quit with `:q`, refused when there are unsaved changes, with `:q!` to force

### H5 Interface

- **H5.1** As user, I want a status line with file name, mode and cursor position
  - `main` scrolls with the full terminal height today; the status line needs `rows - 1` (H2.3).

## Done

### H0 Development foundations

- **H0.1** As dev, I want a `Makefile` with `all`, `test` and `clean` for compiling and testing with a single command
- **H0.2** As dev, I want Unity integrated and `make test` running, for practicing TDD from the beginning
- **H0.3** As dev, I want pty-based integration tests for terminal enter/leave, so that I can catch regressions in raw mode
  - Solves the H1.1 gap: `enter`/`leave` could not be unit-tested because they touch the real terminal.
- **H0.4** As dev, I want every function, struct, macro and file-static variable documented in Doxygen style (`/** @brief ... @param ... @return ... */`), so I can find out what any symbol means without reading its implementation
  - Public symbols are documented once, in their header. Everything else (statics, `main`, test functions) gets a one-line `@brief` where it is defined.
  - The convention is written in the README with one example.
  - `terminal_set_raw_flags` (named `editor_set_raw_flags` then) is documented as exposed for testing only.
  - Out of scope: the vendored `tests/unity/`, and generating HTML (`make docs`).
- **H0.5** As dev, I want terminal code separated from editor code and covered by regression tests, so that raw mode and terminal size live in one module and refactors are safe
  - Regression tests came first: enter twice, leave twice, leave without enter, and a pty test that runs `./notvim` and quits with `Ctrl+Q`. `make test` now builds `notvim` first.
  - Then raw mode moved from `editor.c` to `terminal.c` as `terminal_*`, the unused `editor_version` was deleted, and the tests were split into `test_editor.c`, `test_terminal.c` and `test_notvim.c`.
  - Known gap: the pty test cannot check that the terminal is restored, because Linux resets a pty's attributes when the last slave closes (H0.11).
- **H0.6** As dev, I want builds and tests to run under AddressSanitizer and UBSan, so that leaks and memory errors fail `make test` from the beginning
  - The Makefile has a `SAN` variable, on by default; `make clean && make SAN=` turns it off.
  - Valgrind was considered and dropped: it cannot run together with ASan and adds little here.
- **H0.7** As dev, I want a per-test temporary directory helper, so that file-based tests never touch the repo and always clean up
  - `tests/tmpdir.c`: created in `setUp`, removed in `tearDown` (which runs after a failed assertion), also at `exit()` and after `SIGINT`/`SIGTERM`.
  - The signal handler only sets a flag and cleanup happens outside it, because `nftw` is not async-signal-safe.
  - Known gap: a crash, an ASan abort or `SIGKILL` leaves the directory in `/tmp` (H0.11).
- **H0.8** As dev, I want the editor text stored as a growable array of lines, so that files of any size load without truncation
  - Design: `editor_t` is `char **lines` + `count` + `cap`. `editor_append_line` copies the text and leaves the editor unchanged on failure. `editor_free` is safe to call twice.
  - Decision: fixed limits were rejected, because loading a long file and saving it with `:w` (H4.1) would silently destroy data.
  - Decision: an empty editor has 0 lines, not 1. An empty file round-trips as 0 bytes and `"\n"` as one empty line.
  - No gap buffer or undo structure (YAGNI).
- **H0.9** As dev, I want a failing editor test to not leak, so that sanitizer reports never bury the test output
  - The tests in `test_editor.c` share one file-level editor that `tearDown` frees (`test_editor_teardown`).
  - Why: a failed Unity assertion jumps out of the test and skips any cleanup written at its end.
  - Rule for new tests: use the shared editor or the stack, never a bare `malloc` that needs a `free` at the end.
- **H0.10** As dev, I want objects rebuilt when an included header changes, so that a struct change cannot leave stale objects behind
  - Found while adding the cursor fields to `editor_t`: `main.o` was built against the old, smaller struct and `notvim` crashed in `editor_init`.
  - The Makefile now uses `-MMD -MP` and includes the `.d` files; `make clean` removes them.

### H1 Terminal and display

- **H1.1** As user, I want the terminal to enter in raw mode when running `notvim` and to be restored when exiting, so that the editor handles my input without breaking shell
  - Verified by hand at the time: raw mode active, `Ctrl+C` does not kill, terminal restored on exit. Automated later by H0.3.
- **H1.2** As user, I want to leave notvim with `Ctrl+Q`, so I can go back to shell
- **H1.3** As user, I want to see buffer content at my screen, so I can know what I am editing
- **H1.4** As user, I want to run `notvim file.txt` and see all its lines, up to the terminal height, so I can read the file
  - Merges the former H1.4 (first line) and H1.5 (all lines).
  - Loading: a missing file gives an empty editor and is not created until `:w` (H4.1), as in Vim. A final newline does not add an empty line.
  - Loading errors: any other open or read error makes `editor_load_file` return -1 with `errno` set and an empty editor.
  - `notvim` then prints `notvim: <path>: <reason>` and exits 1 before entering raw mode, so the message shows on a normal terminal.
  - Rendering: lines are joined by `\r\n`, because raw mode clears `OPOST` and a bare `\n` would not return to column 0. An OS-dependent line-ending macro was discussed and not added (UNIX only).
  - Terminal size: read once at startup with `TIOCGWINSZ`; each dimension falls back to 24 rows / 80 columns when unreadable or 0 (a fresh pty reports 0x0). It is read again on `SIGWINCH` (H6.7).
  - Known gaps:
    - CRLF files and NUL bytes inside lines were not handled at this point (since fixed: see H6.9 and H6.2).
    - The out-of-memory path of `editor_load_file`, and the cleanup after a read error halfway through a file, are not tested: see H0.11.
  - Lines wider than the terminal were not clipped here; solved in H2.4.

### H2 Navigation

- **H2.1** As user, I want to move the cursor with the arrow keys, so I can navigate the text
  - Design, step 1: a pure key decoder (`keys.c`), fed one byte at a time.
    - `ESC [ A/B/C/D` become `KEY_UP/DOWN/RIGHT/LEFT`.
    - Any other escape sequence (for example `ESC [ 3 ~`) is ignored without breaking the next key.
    - Ordinary bytes pass through, `Ctrl+Q` included.
  - Design, step 2: a cursor in `editor_t` (`cy`, `cx`, byte indexes) with `editor_move_cursor`.
    - No wrapping; up and down clamp to the lines, and the column to the line length.
    - Right stops on the last character (Vim normal mode); an empty editor keeps the cursor at 0,0.
  - Design, step 3: `editor_draw` writes clear screen + home + `editor_render` + a cursor-position sequence.
    - The main loop redraws after each key. `editor_render` stays pure text.
  - `editor_draw` redraws the whole screen after every move (no diffing) and reserves `EDITOR_DRAW_OVERHEAD` bytes so the cursor sequence is never cut.
  - `main` checks `key < 256` before `editor_should_exit`, so a key code above 255 can never alias a byte. This is an equivalent mutant today, kept on purpose.
  - Ordinary keys and swallowed sequences do not redraw.
  - Known gaps:
    - A lone `ESC` is never reported (needs a timeout): see H6.1.
    - `cx` counts bytes, so UTF-8 text puts the cursor mid-character: see H6.8.
    - No remembered "wanted column" when passing through short lines (Vim's curswant): done in H6.10.
- **H2.2** As user, I want to move the cursor with `h j k l`, as in Vim
  - The mapping is `key_to_move` in `main.c`, tested end to end on a pty. It behaves exactly like the arrows, so the clamping rules are not retested.
  - Uppercase `H J K L` are left unmapped on purpose (Vim gives them other meanings).
  - To do with H3.1: apply the mapping only in normal mode.
- **H2.3** As user, I want the screen to scroll when the cursor leaves the visible area, so I can reach every line of a file longer than the terminal
  - Design: `editor_t` gets `rowoff`, the index of the first visible line.
    - `editor_scroll(e, rows)` keeps the cursor inside `[rowoff, rowoff + rows)` by changing only `rowoff`.
    - `editor_render` starts at `rowoff`, and `editor_draw` puts the cursor on row `cy - rowoff + 1`.
    - `main` calls `editor_scroll` after every move; loading resets `rowoff` to 0.
  - Decisions: vertical only, one line at a time (no half-page jumps or `scrolloff` margin). `editor_draw` does not scroll by itself, so it stays a pure function of the editor state.
  - Known gaps: no rows are reserved for a status line yet (H5.1). The whole screen is still redrawn after every key, without clearing it first since H6.11.
- **H2.4** As user, I want the editor to draw on the alternate screen and clip lines to the terminal width, so that my shell screen is left untouched and the first lines never scroll out of view
  - Found by running `./notvim BACKLOG.md` in a real terminal emulator, which the pty tests could not show.
  - Clipping: lines wider than the terminal wrapped onto extra rows, so the screen overflowed and the terminal scrolled the title away.
    - The first 24 lines of `BACKLOG.md` needed 35 rows, and the cursor row still said 1.
    - `editor_render` and `editor_draw` now take `max_cols` and clip each line; the drawn cursor column stops at the last column.
  - Alternate screen: `ESC [ 2 J` does not wipe a terminal emulator's screen, it pushes it into scrollback and leaves blank space (like `Ctrl+L`).
    - `main` enters the alternate screen (`ESC [ ? 1049 h`) after raw mode and leaves it (`l`) at exit, before restoring the tty modes.
    - The shell screen and scrollback are untouched, as in Vim.
  - `terminal_enter_alt_screen`/`terminal_leave_alt_screen` are idempotent like raw mode. A load error is printed before either starts, so it stays on the normal screen.
  - Known gaps:
    - Widths are counted in bytes, so tabs and UTF-8 text can still make a line wider than the terminal: see H6.4 and H6.8.
    - The tests check the exact byte stream on a pty; what a real emulator shows was checked by hand.

### H6 Robustness with real files and terminals

- **H6.1** As user, I want a lone `Esc` to be recognised after a short timeout, so that it never swallows the next key I press
  - Reproduced: after `Esc`, even a second later, the next `j` did nothing (the decoder was still waiting for an escape sequence, H2.1).
  - Design: the decoder stays pure and gets the time from outside. `key_parser_pending` says whether it is inside a sequence, and `key_parser_timeout` turns a lone `ESC` into `KEY_ESC` or abandons a longer sequence.
  - `main` waits on stdin with `poll()`: without limit when idle, `KEY_ESC_TIMEOUT_MS` (50 ms) while a sequence is pending. The raw-mode flags and their tests are unchanged (`VMIN`/`VTIME` was the alternative).
  - `KEY_ESC` is reported but nothing uses it yet; H3.1 will leave insert mode with it.
  - Known gap: a terminal that sends the bytes of an arrow key more than 50 ms apart (a very slow link) would be read as `Esc` followed by `[` and a letter.
- **H6.2** As user, I want a file with NUL bytes to be refused with a clear error, so that it is never loaded cut short and then saved over
  - Reproduced: a line `a\0b` was drawn as `a`. With `:w` (H4.1) the rest of the line would have been lost.
  - `editor_load_file` fails with `errno` `EILSEQ` when a line contains a NUL byte, and leaves the editor empty, including any lines already loaded.
  - `main` prints `notvim: <path>: binary file (contains NUL bytes)` and exits 1 before raw mode, so it shows on the normal screen.
  - Decision: refusing is the cheap, safe choice; keeping NUL inside lines would mean storing a length per line.
  - Side effects: the "NUL after good lines" test exercises the cleanup of a half-loaded editor (part of H0.11). `tmpdir_write_bytes` was added for the tests, and it now refuses names with a `/`, after `../x` wrote outside the temporary directory.
- **H6.3** As user, I want control characters in a file shown as visible marks (such as `^[`), so that a file can never send commands to my terminal
  - Reproduced: a file containing `ESC [ 2 J` cleared the screen when it was displayed.
  - A control byte (below `0x20` except tab, and `0x7f`) is drawn as a two-column mark: `^[`, `^A`, `^?`. A `\r` shows as `^M`, so the stray CR of CRLF files is gone too (H6.9 will hide it for real CRLF files).
  - Design: `cell_width`, `display_col` and `put_line` in `editor.c` separate a byte index from a display column. Clipping counts columns and never shows half a mark; the drawn cursor column is the display column of `cx` (on the first column of a mark).
  - Tabs and bytes of `0x80` and above are left alone here: H6.4 extends the same helpers for tabs, and H6.8 for UTF-8.
  - The pty test checks that the only clear-screen sequence in the output is the one `notvim` sends itself.
- **H6.4** As user, I want tabs shown as spaces up to the next tab stop (8 columns), so that lines with tabs do not wrap and scroll the screen
  - Reproduced: 24 lines of 20 tabs are 21 bytes each but 161 columns, and needed 72 rows on a 24-row terminal. Now they are drawn as 24 lines of 80 columns with no raw tab.
  - `cell_width` now takes the current column, so a tab goes to the next multiple of 8 and a two-column mark before it is counted. Clipping and the drawn cursor column use the same widths.
  - Decisions: a tab at the right edge is cut to the room left (spaces can be cut anywhere, a mark cannot). The cursor sits on the first column of a tab, not the last as Vim does in normal mode.
  - Known gaps: the tab stop is fixed at 8 (no `tabstop` setting), and nothing stops a later edit from needing the tab as a character (H3.2 types a tab as `\t`).
- **H6.5** As user, I want notvim to refuse to start when stdin or stdout is not a terminal, so that it never writes escape sequences into a pipe or a file
  - Reproduced: `./notvim file | cat` wrote the alternate-screen and cursor sequences into the pipe.
  - `main` checks `isatty` on stdin, then stdout, before anything else: it prints `notvim: input is not a terminal` or `notvim: output is not a terminal` on stderr and exits 1. The messages are like Vim's.
  - The tests start `notvim` with explicit descriptors (`/dev/null`, a pipe, a pty slave) and check the message, the exit status and that nothing at all reaches the redirected stdout.
  - Known gap: stderr is not checked, so `notvim file 2>/dev/null` still starts; only the two streams the editor uses are.
- **H6.6** As user, I want the terminal restored when notvim is stopped by `SIGTERM` or `SIGHUP`, so that I do not end up on the alternate screen in raw mode
  - Reproduced: after `SIGTERM` the switch back from the alternate screen was never written.
  - Design: a new module `stopsig` installs handlers for `SIGINT`, `SIGTERM` and `SIGHUP` that only record the signal and write one byte to a pipe (async-signal-safe). `main` polls that pipe together with stdin, which avoids the race of a plain flag: a signal arriving just before the wait would be missed until the next key.
  - A signal ends the loop and `main` returns through the normal exit, so the alternate screen and the tty modes are restored. The exit status is 128 plus the signal number, as shells report it (143, 129, 130).
  - `SIGINT` matters although `Ctrl+C` is only a byte in raw mode: `kill -INT` still delivers it. `stopsig_remove` restores the previous handlers, because the test runner's own temporary-directory handlers (H0.7) use the same signals.
  - Known gaps: a signal during file loading uses the default action, which is safe because nothing has been changed yet. A crash or `SIGKILL` still leaves the terminal as it is. `SIGTSTP` and `SIGQUIT` are not handled (`Ctrl+Z` and `Ctrl+\` are bytes in raw mode).
- **H6.7** As user, I want notvim to follow terminal resizes (`SIGWINCH`), so that the screen is always drawn for the current size
  - Reproduced: after the terminal shrinks to 8 rows by 30 columns, notvim still drew 24 rows.
  - Design: a new module `winch`, the same self-pipe pattern as `stopsig`, with its own pipe because `SIGWINCH` must not end the loop. `winch_drain` empties the non-blocking pipe, so a burst of resizes is one redraw.
  - On a resize `main` re-reads the size, reallocates the draw buffer, calls `editor_scroll` and redraws. `draw_buffer_size()` is the single place that computes the buffer size.
  - Decisions: the handler is installed before raw mode, like `stopsig`. If `realloc` fails the old buffer and size are kept and nothing is redrawn. A resize in the middle of an escape sequence redraws and leaves the sequence pending.
  - Known gaps: the write end of the pipe is not tested for being non-blocking (needs 64 KB of signals). A resize while the file is loading is lost, which is harmless because the size is read afterwards.
- **H6.8** As user, I want text with accents or other UTF-8 characters shown and navigated by character, so that the cursor never lands inside a character and clipping never cuts one
  - Reproduced: clipping a 201-byte line to 80 bytes gave invalid UTF-8 and only 40 characters; two `l` presses put the cursor inside a character.
  - Design: a pure module `utf8` (RFC 3629: no overlong forms, surrogates or code points above U+10FFFF) with `utf8_valid_len`, `utf8_cell_len`, `utf8_is_c1` and `utf8_prev`. A cell is a valid character or one invalid byte.
  - `editor.c` uses cells in `put_line`, `display_col` and `editor_move_cursor`. Each valid character is one column and is copied as its bytes; clipping is by columns and never cuts a character.
  - `draw_buffer_size()` now allows 4 bytes per column (`rows * (cols * 4 + 2) + EDITOR_DRAW_OVERHEAD`), tested end to end with 2, 3 and 4-byte characters, at startup and on a resize.
  - Decisions: each invalid byte is its own `?` (a truncated euro sign is `??`). A C1 control (U+0080 to U+009F, some terminals act on them) is one cell of two bytes drawn as one `?`.
  - A vertical move that lands inside a character snaps back to its start; a clamp goes to the start of the last character. `cx` stays a byte index.
  - Known gaps: double-width (CJK) and combining characters take one column each. `cx` is a byte index, which made the cursor drift left on multi-byte lines: closed by H6.10 (remembered display column).
  - Known gaps: the mutant "Left steps one byte" survives because the snap puts the cursor back on the character start (equivalent). Invalid UTF-8 is shown as `?`, so saving (H4.1) must keep the original bytes.
- **H6.9** As user, I want CRLF files shown without a stray `\r`, and saved back with CRLF
  - Reproduced: `abc\r\ndef\r\n` was drawn as `abc\r\r\ndef\r`.
  - Design: `editor_load_file` reads every line as before while counting the lines ended by `\n` and whether each has a `\r` before it. After the whole file is read, a CRLF file loses that `\r` on each terminated line and sets the new field `editor_t.crlf`.
  - Decisions: a file is CRLF only if it has at least one terminated line and every one has the `\r`. A last line without a newline is ignored for the detection and keeps a trailing `\r`. LF, empty and mixed files are left alone, so a stray `\r` shows as `^M`.
  - `crlf` is reset by `editor_init`, `editor_free` and every load, failed or not. No change to the rendering. The tests use files of 60 KB and a 10000-byte line so that a decision from the first read block would fail.
  - Known gaps: saving is not built (H4.1) and must write `\r\n` when `crlf` is set. A mixed file keeps its `\r` on the lines that have it, and the editor cannot tell the user the file is mixed.
- **H6.10** As user, I want the cursor to remember the column I was on when I move through shorter lines, as in Vim
  - Design: `editor_t.wantcol` is the wanted display column (Vim's curswant). Up and down keep it and pick, with `col_to_cx`, the character whose first display column is the largest one not above it; the last character if the line is shorter, 0 on an empty line.
  - Because it works in display columns it is right with tabs, `^X` marks, `?` cells and UTF-8 characters. The comparison never adds to `wantcol`, so a huge value cannot overflow. `init`, `free` and every load reset it to 0.
  - Decisions (checked against Vim 9.1): only a left or right move that really moves the cursor sets `wantcol`. `l` on the last character or on an empty line, and `h` at column 0, keep it: `5l j l j` still ends on column 5. A successful `h` resets it (`5l j h j` gives 0). Up on the first line and down on the last keep it.
  - Code that assigns `cx` directly (future insert mode) must set `wantcol` too. The wanted column does not depend on scrolling (tested on a pty).
  - Known gaps: on a tab Vim puts the cursor on its last column and remembers that; notvim uses the first column (H6.4), so going down from a tab lands one tab-width left of where Vim would. Wide (CJK) characters count one column (H6.8).
- **H6.11** As user, I want the screen to update without clearing it first, and every write to complete, so that it does not flicker or lose output
  - Reproduced: every key sent `ESC[2J` and redrew everything, and a short `write()` (full pty or pipe, a signal) lost the rest.
  - Design: `editor_draw` hides the cursor, goes home, draws each row (`ESC[K` after a row narrower than the screen),
    moves to the first free row and erases below, positions the cursor and shows it. `editor_render` stays pure text.
  - `terminal_write_all` continues partial writes, retries `EINTR`, waits with `poll` on `EAGAIN`, returns 0 or -1 with `errno`.
    `main` writes every redraw with it and leaves the loop if it fails.
  - Decisions: a character in the last column leaves the terminal in "pending wrap", and `ESC[K` or `ESC[J` would erase it.
    So a full-width row gets no `ESC[K`, and the erase below starts with an explicit move to the next row (checked by hand in GNU screen).
  - A row that does not fit the buffer is dropped whole, never cut: no escape sequence or character is ever half written.
  - `EDITOR_DRAW_OVERHEAD` is 88 (worst-case tail); `draw_buffer_size()` allows 4 bytes per column, `ESC[K` and CR LF per row.
  - SIGPIPE is not handled: stdout must be a terminal (H6.5), and a terminal never raises it.
  - Known gaps: byte tests cannot show what a terminal draws, so the screen itself was only checked by hand.
    A failing redraw ends the loop (exit 1 at startup); that is not tested. The whole window is still sent after each key (no diffing).
