# notvim

A (not-)Vim reimplementation in C for UNIX/Linux for educational purposes. Built with extreme programming
(TDD, user stories, incremental delivery).

## Status

Early development. Currently supports:

- Raw mode input (no echo, no line buffering)
- Exit with `Ctrl+Q`
- Single-line buffer with rendering

Not yet implemented: opening files, navigation, insertion mode, saving,
command mode. See [BACKLOG.md](BACKLOG.md) for the roadmap.

## Build

```bash
    make        # build the notvim binary
    make test   # run the test suite
    make clean  # remove build artifacts
```

Requires a C11 compiler (gcc or clang), make, and libutil (for pty tests).

## Usage

```bash
    ./notvim
```

Currently the editor starts with an empty buffer and waits for `Ctrl+Q`.
More will come as histories land.

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
 * @brief Reset @p e to an empty buffer.
 * @param e Editor to initialise; must not be NULL.
 */
void editor_init(editor_t *e);
```

Code uses real tabs for indentation.

## License

MIT, see [LICENSE](LICENSE).
