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

- **H6.1** As user, I want a lone `Esc` to be recognised after a short timeout, so that it never swallows the next key I press
  - Reproduced: after `Esc`, even a second later, the next `j` does nothing (the decoder is still waiting for an escape sequence, H2.1). A key is lost today.
  - Proposed design: `main` waits on stdin with `poll()`.
    - If no byte follows an `ESC` within a short time (about 50 ms), the decoder reports `KEY_ESC`.
    - This leaves the raw-mode flags and their tests alone. The alternative is `VMIN`/`VTIME`.
  - Needed by H3.1.

## To do

### H6 Robustness with real files and terminals

Each story below was reproduced against the real binary on a pty.
They are ordered by harm: data loss first, then anything that corrupts or commands the terminal, then display correctness, then usability.

- **H6.2** As user, I want a file with NUL bytes to be refused with a clear error, so that it is never loaded cut short and then saved over
  - Reproduced: a line `a\0b` is drawn as `a`. With `:w` (H4.1) the rest of the line would be lost.
  - Refusing is the cheap, safe choice; keeping NUL inside lines would mean storing a length per line.
- **H6.3** As user, I want control characters in a file shown as visible marks (such as `^[`), so that a file can never send commands to my terminal
  - Reproduced: a file containing `ESC [ 2 J` clears the screen when it is displayed.
  - The marks are wider than the byte they replace, so the cursor and the clipping must count columns (shared with H6.4 and H6.8).
- **H6.4** As user, I want tabs shown as spaces up to the next tab stop (8 columns), so that lines with tabs do not wrap and scroll the screen
  - Reproduced: 24 lines of 20 tabs are 21 bytes each but 161 columns, and need 72 rows on a 24-row terminal.
  - Introduces the difference between a byte index and a display column, for the cursor and for clipping.
- **H6.5** As user, I want notvim to refuse to start when stdin or stdout is not a terminal, so that it never writes escape sequences into a pipe or a file
  - Reproduced: `./notvim file | cat` writes the alternate-screen and cursor sequences into the pipe.
- **H6.6** As user, I want the terminal restored when notvim is stopped by `SIGTERM` or `SIGHUP`, so that I do not end up on the alternate screen in raw mode
  - Reproduced: after `SIGTERM` the switch back from the alternate screen is never written.
  - The handler only sets a flag and the main loop does the cleanup, as in H0.7. A crash or `SIGKILL` stays out of reach.
- **H6.7** As user, I want notvim to follow terminal resizes (`SIGWINCH`), so that the screen is always drawn for the current size
  - Reproduced: after the terminal shrinks to 8 rows, notvim still draws 24.
  - The draw buffer must be reallocated for the new size.
- **H6.8** As user, I want text with accents or other UTF-8 characters shown and navigated by character, so that the cursor never lands inside a character and clipping never cuts one
  - Reproduced: clipping a 201-byte line to 80 bytes gives invalid UTF-8 and only 40 characters; two `l` presses put the cursor inside a character.
  - Out of scope: double-width (CJK) and combining characters.
- **H6.9** As user, I want CRLF files shown without a stray `\r`, and saved back with CRLF
  - Reproduced: `abc\r\ndef\r\n` is drawn as `abc\r\r\ndef\r`.
  - The saving half belongs with H4.1.
- **H6.10** As user, I want the cursor to remember the column I was on when I move through shorter lines, as in Vim
- **H6.11** As user, I want the screen to update without clearing it first, and every write to complete, so that it does not flicker or lose output
  - Today every key clears and redraws the whole screen. Proposed: overwrite each row and erase to the end of the line with `ESC [ K`, and loop on `write()` for partial writes.

### H0 Development foundations

- **H0.11** As dev, I want the remaining test gaps closed where practical
  - The out-of-memory path of `editor_load_file` and `editor_append_line`, and the cleanup after a read error halfway through a file, are not tested.
  - The pty test cannot check that the terminal is restored (H0.5), and a crash leaves the temporary directory behind (H0.7).

### H2 Navigation

- **H2.5** As user, I want long lines to scroll horizontally with the cursor, so I can read and reach the part of a line that is clipped
  - Same mechanism as `rowoff`, for columns: `coloff`, `editor_scroll` with the width, the drawn cursor column relative to it.
  - Until then the cursor stops on the last visible column of a clipped line.

### H3 Insert mode

- **H3.1** As user, I want to press `i` to enter insert mode and `Esc` to leave it, so typing and commands don't collide
  - Depends on H6.1: `Esc` must be recognised.
  - The `h j k l` mapping (H2.2) must apply only in normal mode, because `h` has to type `h` in insert mode.
- **H3.2** As user, I want to type characters in insert mode and see them in the buffer
- **H3.3** As user, I want `Backspace` and `Enter` to work in insert mode

### H4 Saving and quitting

- **H4.1** As user, I want to save with `:w`, so I don't lose my work
  - A missing file is created here, not on load (H1.4).
  - An editor with 0 lines saves as an empty file, and one empty line as `"\n"` (H0.8).
  - Loading never truncates, so saving a long file must write every line (H0.8).
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
  - Terminal size: read once at startup with `TIOCGWINSZ`; each dimension falls back to 24 rows / 80 columns when unreadable or 0 (a fresh pty reports 0x0). There is no `SIGWINCH` handling (H6.7).
  - Known gaps:
    - CRLF files and NUL bytes inside lines are not handled (the `\r` stays, a NUL cuts the line): see H6.9 and H6.2.
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
    - No remembered "wanted column" when passing through short lines (Vim's curswant): see H6.10.
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
  - Known gaps: no rows are reserved for a status line yet (H5.1). The whole screen is still redrawn after every key (H6.11).
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
