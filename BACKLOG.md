# Backlog: notvim

## In progress

- H1.4: As user, I want to run `notvim file.txt` and see all its lines, up to the terminal height, so I can read the file
> Notice: merges former H1.4 (first line) and H1.5 (all lines). Requires a multi-line buffer and the terminal size (`TIOCGWINSZ`); changes `editor_t` and `editor_render`. A nonexistent path opens an empty buffer; the file is only created by `:w` (H4.1), as in Vim.

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
> Acceptance: convention written in README with one example; public symbols documented once in their header, everything else (statics, `main`, test functions) with a one-line `@brief` where defined; `editor_set_raw_flags` documented as exposed for testing only. Out of scope: vendored `tests/unity/`, generating HTML / `make docs`.
