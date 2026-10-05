# notvim

A (not-)Vim reimplementation in C for UNIX/Linux for educational purposes. Built with extreme programming
(TDD, user stories, incremental delivery).

## Status

Early development. Currently supports:

- Raw mode input (no echo, no line buffering)
- Exit with `Ctrl+Q`
- Opening a file given on the command line and showing its lines, up to the terminal height (read-only)

Not yet implemented: navigation, scrolling, insertion mode, saving, command mode. See [BACKLOG.md](BACKLOG.md) for the roadmap.

## Build

```bash
    make        # build the notvim binary
    make test   # run the test suite
    make clean  # remove build artifacts
```

Requires a C11 compiler (gcc or clang), make, and libutil (for pty tests).
Builds use AddressSanitizer and UBSan, so memory errors and leaks fail
`make test`; `make clean && make SAN=` builds without them.


## Usage

```bash
    ./notvim [file]
```

With a file, its first lines (as many as fit the terminal) are shown; a missing
file starts an empty buffer, and a file that cannot be read prints an error and
exits with status 1. The editor then waits for `Ctrl+Q`.
More will come as histories land.

## Layout

- `src/main.c`: entry point (argument, raw mode, first render, wait for `Ctrl+Q`).
- `src/editor.c`, `include/editor.h`: the text as a growable array of lines,
  loading a file, rendering up to a number of rows.
- `src/terminal.c`, `include/terminal.h`: raw mode and terminal size.
- `tests/`: Unity tests, one file per module, plus `test_notvim.c`, which runs
  the built `./notvim` on a pty, and `tmpdir.c`, a per-test temporary directory.
  `tests/unity/` is vendored.

Design decisions and the history of the refactors are recorded in
[BACKLOG.md](BACKLOG.md).

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
