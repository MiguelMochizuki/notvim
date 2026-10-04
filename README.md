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

## License

MIT, see [LICENSE](LICENSE).
