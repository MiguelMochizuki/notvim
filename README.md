# notvim

A (not-)Vim reimplementation in C for UNIX/Linux for educational purposes. Built with extreme programming
(TDD, user stories, incremental delivery).

## Status

Early development: notvim opens a file, shows it, lets you move around, type text into it and save it with `:w`. Leave with `:q`, which refuses when there are unsaved changes (`:q!` forces).

What works today:

- Opening a file given on the command line and showing its lines, up to the terminal height minus the status line
- A status line on the last row: file name (or `[No Name]`), `[dos]` for a CRLF file, `[+]` once the text is changed, the mode (`NORMAL` or `INSERT`) and `line,col` at the right
- Moving the cursor and scrolling when it leaves the screen, vertically and horizontally (one line or column at a time)
- Drawing on the terminal's alternate screen, so the shell screen comes back untouched on exit
- Lines wider than the terminal scroll sideways with the cursor, so nothing wraps and scrolls the screen (except double-width characters, see Known limitations); the status line keeps the absolute column

Keys:

| Key | Action |
|-----|--------|
| `←` `↓` `↑` `→` | Move the cursor (it never wraps; in insert mode it can go after the last character of a line) |
| `h` `j` `k` `l` | The same, in normal mode only (in insert mode they are typed) |
| `i` | Enter insert mode at the cursor |
| Any printable key, `Tab` | In insert mode: type it at the cursor (accented and other UTF-8 characters too) |
| `Enter` | In insert mode: split the line at the cursor |
| `Backspace` | In insert mode: delete the character before the cursor; at the start of a line, join it to the previous one |
| `Delete` | In insert mode: delete the character under the cursor; at the end of a line, join the next one |
| `Esc` | Leave insert mode, one character left unless at column 0 |
| `:` | Open the command line on the bottom row: type, `Backspace` deletes (cancels when empty), `Esc` cancels, `Enter` runs. `:w`, `:w!` and `:w name` save (see below); `:q`, `:wq`, `:x` quit (see below); any other command shows `E492` |
| `:w` `:w!` `:w name` | Save to the file, or to `name` (which becomes the file if there is none): `"name" [New] 12L, 345B written`, or `E32: No file name`. The write is atomic (temporary file, `fsync`, `rename`) and keeps the permission bits; a missing final newline is added, as in Vim |
| `:q` `:q!` `:wq` `:x` | Quit; `:q` is refused with `E37: No write since last change (add ! to override)` when the buffer is modified, `:q!` quits without saving, `:wq` writes then quits, `:x` writes only if modified then quits; a failed write does not quit |
| `ZZ` `ZQ` | Normal mode: like `:x` and `:q!` |
| `Ctrl+Q` | Like `:q`: quits, but is refused when there are unsaved changes |

The last row is the status line, or the command line, or a message such as an error (until the next key): `name [dos] [+] NORMAL` on the left, `line,col` (display column) on the right. On a narrow terminal the position stays and the rest is cut from the right (mode first).

## Roadmap

The full list of stories, with the design decisions behind them, is in [BACKLOG.md](BACKLOG.md). In short:

| Next | Stories |
|------|---------|
| Done | A status line with file name, mode and cursor position; insert mode (`i`, `Esc`); typing; `Enter`, `Backspace` and `Delete`; the `:` command line; saving with `:w`; quitting with `:q`, `:q!`, `:wq`, `:x`, `ZZ`, `ZQ`; scrolling long lines horizontally |
| Next | Hardening: double-width characters (H6.12) and the last robustness story (H6.13) |
| Vim motions | `0 ^ $`, `w b e`, `gg G`, counts, page scrolling, `% { }`, `f t F T` |
| Vim editing | `a A I o O`, `x r ~`, `dd D cc C J`, operators with motions, undo and redo, yank and put, `.` repeat, visual mode |
| Search and commands | `/ ?` and `n N`, `* #`, `:s`, `:set number`, `:e`, command-line history and editing |
| Mouse and clipboard | Wheel scrolling that keeps native selection, yanking to the system clipboard, bracketed paste |

## Known limitations

The robustness epic (H6 in [BACKLOG.md](BACKLOG.md)) is finished. What is left:

- A file with mixed line endings (some CRLF, some LF) shows `^M` on its CRLF lines. `:w` writes such a file back unchanged.
- Double-width (CJK, emoji) and combining characters take one column each in notvim, but the terminal draws wide ones in two. A long line of them wraps onto the next row, pushes the rows below down and can scroll the screen; the cursor column is off too (planned as H6.12).
- Horizontal scrolling moves one column at a time; Vim's default recentres the cursor (half a screen) and there is no `sidescrolloff`.
- Invalid UTF-8 and C1 controls are shown as `?` (saved as they were).
- `:w` loses hard links (the rename makes a new file), does not `fsync` the directory, keeps no backup file and does not keep the owner and group.

## Line endings

Linux is LF, and so is this repository (`.gitattributes`, `.editorconfig` and `make check-eol`, which `make test` runs first).
notvim never adds a carriage return to an LF file or to a new file. A file that uses CRLF throughout is recognised (shown as `[dos]` in the status line)
and is written back as it was by `:w`; an LF file, a new file and an empty editor are written with LF only, and a lone `\r` inside a line is kept as data. Converting on purpose (`:set fileformat=unix`) is planned as H10.6.

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

- `src/main.c`: entry point (argument, raw mode, input loop, redraw after each key that does something).
- `src/editor.c`, `include/editor.h`: the text as a growable array of lines,
  the modes, the key dispatch, the cursor and scrolling, loading a file, the path it was loaded from, the status line, rendering and drawing the screen.
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
