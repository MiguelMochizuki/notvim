# Backlog: notvim

## In progress

(nothing in progress)

## To do

- H2.1: As user, I want to move the cursor with the arrow keys, so I can navigate the text
- H2.2: As user, I want to move the cursor with `h j k l`, as in Vim
- H2.3: As user, I want the screen to scroll when the cursor leaves the visible area, so I can reach every line of a file longer than the terminal
- H3.1: As user, I want to press `i` to enter insert mode and `Esc` to leave it, so typing and commands don't collide
- H3.2: As user, I want to type characters in insert mode and see them in the buffer
- H3.3: As user, I want `Backspace` and `Enter` to work in insert mode
- H4.1: As user, I want to save with `:w`, so I don't lose my work
- H4.2: As user, I want to quit with `:q`, refused when there are unsaved changes, with `:q!` to force
- H5.1: As user, I want a status line with file name, mode and cursor position

## Done

- H0.1: As dev, I want a `Makefile` with `all`, `test` and `clean` for compiling and testing with a single command
- H0.2: As dev, I want Unity integrated and `make test` running, for practicing TDD from the beginning
- H1.1: As user, I want the terminal to enter in raw mode when running `notvim` and to be restored when exiting, so that the editor handles my input without breaking shell
> Notice (H1.1): `enter`/`leave` not unit-tested (touch real terminal). Verified manually: raw mode active, Ctrl+C doesn't kill, terminal restored on exit. Pty-based integration test tracked as H0.3.
- H1.2: As user, I want to leave notvim with `Ctrl+Q`, so I can go back to shell
- H0.3: As dev, I want pty-based integration tests for terminal enter/leave, so that I can catch regressions in raw mode
> Solves Notice (H1.1)
- H1.3: As user, I want to see buffer content at my screen, so I can know what I am editing
- H0.4: As dev, I want every function, struct, macro and file-static variable documented in Doxygen style (`/** @brief ... @param ... @return ... */`), so I can find out what any symbol means without reading its implementation
> Acceptance: convention written in README with one example; public symbols documented once in their header, everything else (statics, `main`, test functions) with a one-line `@brief` where defined; `editor_set_raw_flags` (now `terminal_set_raw_flags`, see H0.5) documented as exposed for testing only. Out of scope: vendored `tests/unity/`, generating HTML / `make docs`.
- H0.5: As dev, I want terminal code separated from editor code and covered by regression tests, so that raw mode and terminal size live in one module and refactors are safe
> Notice (H0.5): regression tests added first (enter twice, leave twice, leave without enter, and a pty test that runs `./notvim` and quits with `Ctrl+Q`); `make test` now builds `notvim` first. Then `editor_enter_raw`/`editor_leave_raw`/`editor_set_raw_flags` moved to `terminal.c` as `terminal_*`, the unused `editor_version` was deleted, and tests were split into `test_editor.c`, `test_terminal.c` and `test_notvim.c`. The pty test cannot check "terminal restored": Linux resets a pty's attributes when the last slave closes.
- H0.6: As dev, I want builds and tests to run under AddressSanitizer and UBSan, so that leaks and memory errors fail `make test` from the beginning
> Notice (H0.6): `SAN` variable in the Makefile, on by default; `make clean && make SAN=` turns it off. Valgrind was considered and dropped: it cannot run together with ASan and adds little here.
- H0.7: As dev, I want a per-test temporary directory helper, so that file-based tests never touch the repo and always clean up
> Notice (H0.7): `tests/tmpdir.c`, created in `setUp` and removed in `tearDown` (which runs after a failed assertion), also at `exit()` and after `SIGINT`/`SIGTERM` (the handler only sets a flag; cleanup happens outside it, because `nftw` is not async-signal-safe). Not covered: crashes, ASan aborts and `SIGKILL` leave the directory in `/tmp`.
- H0.8: As dev, I want the editor text stored as a growable array of lines, so that files of any size load without truncation
> Notice (H0.8): fixed limits were rejected because loading a long file and saving it with `:w` (H4.1) would silently destroy data. `editor_t` is `char **lines` + `count` + `cap`; `editor_append_line` copies the text and leaves the editor unchanged on failure; `editor_free` is safe to call twice. No gap buffer or undo structure (YAGNI). An empty editor has 0 lines, not 1: an empty file round-trips as 0 bytes and `"\n"` as one empty line.
- H1.4: As user, I want to run `notvim file.txt` and see all its lines, up to the terminal height, so I can read the file
> Notice (H1.4): merges former H1.4 (first line) and H1.5 (all lines).
> Decisions: a missing file gives an empty editor and is not created until `:w` (H4.1), as in Vim; any other open or read error makes `editor_load_file` return -1 with `errno` set and an empty editor, and `notvim` prints `notvim: <path>: <reason>` and exits 1 before entering raw mode, so the message shows on a normal terminal. A final newline does not add an empty line. CRLF files and NUL bytes inside lines are not handled (the `\r` stays, a NUL cuts the line).
> Decisions: lines are rendered joined by `\r\n` because raw mode clears `OPOST`, so a bare `\n` would not return to column 0; an OS-dependent line ending macro was discussed and not added (UNIX only). The terminal size is read once at startup with `TIOCGWINSZ`; each dimension falls back to 24 rows / 80 columns when unreadable or 0 (a fresh pty reports 0x0); there is no `SIGWINCH` handling. The render buffer is sized `rows * (cols + 2) + 1`; lines longer than the terminal are not clipped and use up the room of the rows after them (`ponytail:` comment in `main.c`).
> Known gaps: the out-of-memory path of `editor_load_file` and the cleanup after a read error halfway through a file are not tested.
- H0.9: As dev, I want a failing editor test to not leak, so that sanitizer reports never bury the test output
> Notice (H0.9): the tests in `test_editor.c` share one file-level editor that `tearDown` frees (via `test_editor_teardown`), because a failed Unity assertion jumps out of the test and skips any cleanup written at its end. Rule for new tests: allocate through the shared editor or on the stack, never with a bare `malloc` that needs a `free` at the end.
