# notvim

A (not-)Vim reimplementation in C for UNIX/Linux for educational purposes. Built with extreme programming
(TDD, user stories, incremental delivery).

## Status

Early development: notvim is a **read-only viewer** for now. It opens a file, shows it, and lets you move around.

What works today:

- Opening a file given on the command line and showing its lines, up to the terminal height
- Moving the cursor and scrolling vertically when it leaves the screen
- Drawing on the terminal's alternate screen, so the shell screen comes back untouched on exit
- Lines wider than the terminal are clipped, so nothing wraps and scrolls the screen

Keys:

| Key | Action |
|-----|--------|
| `←` `↓` `↑` `→` or `h` `j` `k` `l` | Move the cursor (it never wraps or leaves the text) |
| `Ctrl+Q` | Quit |

## Roadmap

The full list of stories, with the design decisions behind them, is in [BACKLOG.md](BACKLOG.md). In short:

| Next | Stories |
|------|---------|
| In progress | CRLF files shown without a stray `^M` |
| Robustness | The limitations below, in order of harm (epic H6) |
| Editing | Insert mode, typing characters, `Backspace` and `Enter` |
| Navigation | Scrolling long lines horizontally |
| Files | Saving with `:w`, quitting with `:q` and `:q!` |
| Interface | A status line with file name, mode and cursor position |

## Known limitations

These are being fixed before new features (see epic H6 in [BACKLOG.md](BACKLOG.md)). Until then:

- CRLF line endings are not handled (a CRLF file shows `^M` at the end of each line).
- Double-width (CJK) and combining characters take one column each. Invalid UTF-8 and C1 controls are shown as `?`.
- The cursor column is a byte index, so it can drift left when you move through lines with accents (see H6.10).

## Build

```bash
    make        # build the notvim binary
    make test   # run the test suite
    make clean  # remove build artifacts
```

Requires a C11 compiler (gcc or clang), make, libutil (for the pty tests) and a terminal that understands
the xterm sequences for the alternate screen.

Builds use AddressSanitizer and UBSan, so memory errors and leaks fail `make test`.
`make clean && make SAN=` builds without them.

## Usage

```bash
    ./notvim [file]
```

- With a file, its lines are shown, as many as fit the terminal.
- A file that does not exist starts an empty buffer; it is not created.
- A file that cannot be read prints `notvim: <path>: <reason>` and exits with status 1.
- Without an argument, notvim starts with an empty buffer.

## Layout

- `src/main.c`: entry point (argument, raw mode, input loop, key mapping, redraw after each move).
- `src/editor.c`, `include/editor.h`: the text as a growable array of lines,
  the cursor and vertical scrolling, loading a file, rendering and drawing a window of rows and columns.
- `src/keys.c`, `include/keys.h`: decoding of input bytes into keys (arrows, a lone Esc after a timeout).
- `src/stopsig.c`, `include/stopsig.h`: catching SIGINT, SIGTERM and SIGHUP through a pipe, so the terminal is restored.
- `src/terminal.c`, `include/terminal.h`: raw mode, alternate screen and terminal size.
- `tests/`: Unity tests, one file per module, plus `test_notvim.c`, which runs
  the built `./notvim` on a pty, and `tmpdir.c`, a per-test temporary directory.
  `tests/unity/` is vendored.

Design decisions and the history of the refactors are recorded in [BACKLOG.md](BACKLOG.md).

## Documentation convention

Everything is documented in Doxygen style, except the vendored `tests/unity/`.
Every `.c` and `.h` file starts with `@file` and `@brief`.

- Public functions, structs and macros are documented once, in their header
  (`@brief`, `@param`, `@return`); struct fields use `/**< ... */`. Their
  definitions in the `.c` file are not repeated.
- Everything else gets a short `/** @brief ... */` where it is defined:
  static functions and variables, `main`, and test functions.

```c
/**
 * @brief Initialise @p e as an empty editor with no lines. Does not allocate.
 * @param e Editor to initialise; must not be NULL and is not read first.
 */
void editor_init(editor_t *e);
```

Code uses real tabs for indentation.

## License

MIT, see [LICENSE](LICENSE).
