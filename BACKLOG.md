# Backlog: notvim

## In progress

- H0.3: As dev, I want pty-based integration tests for terminal enter/leave, so that I can catch regressions in raw mode

## To do

- H1.3: As user, I want to see buffer content at my screen, so I can know what I am editing
- H1.4: As user, I want to run `notvim file.txt` and see first line of the file

(More histories coming...)

## Done

- H0.1: As dev, I want a `Makefile` with `all`, `test` and `clean` for compiling and testing with a single command
- H0.2: As dev, I want Unity integrated and `make test` running, for practicing TDD from the beginning
- H1.1: As user, I want the terminal to enter in raw mode when running `notvim` and to be restored when exiting, so that the editor handles my input without breaking shell
> Notice (H1.1): `enter`/`leave` not unit-tested (touch real terminal). Verified manually: raw mode active, Ctrl+C doesn't kill, terminal restored on exit. Pty-based integration test tracked as H0.3.
- H1.2: As user, I want to leave notvim with `Ctrl+Q`, so I can go back to shell
