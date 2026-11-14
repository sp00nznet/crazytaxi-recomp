# Contributing

Issues and pull requests are welcome.

## The one rule

**Nothing from the game goes in the repository.** No game files, no lifted C
(`src/recomp/gen/`), no catalog (`work/`), no disassembly listings, nothing
generated from `CRAZY_TAXI_PC.EXE`. The tool ships; its output is made on
each person's machine from their own copy. `.gitignore` covers the usual
paths; check `git status` before you commit anyway. Screenshots of the game
running are fine.

## Before a pull request

1. Build from a clean lift on pcrecomp `main`:
   `py -3 run_lift.py --all` (expected: `errors 0`), then `build.cmd`.
2. There is no conformance harness yet. Run the scripted fare from
   [docs/testing.md](docs/testing.md) and look at the shot: the taxi on its
   wheels with a customer aboard.

## Where a fix belongs

- A bug in the lifter, the catalog or the native32 runtime belongs in
  [pcrecomp](https://github.com/sp00nznet/pcrecomp), not here.
- A fix to how the game meets Windows (the CD drive, the window, the
  DirectX 8 hooks) is in `src/runtime/host.c`, with a comment that says what
  it fixes and why.
- A wall you got past goes in `docs/lifting.md`: what it looked like, what it
  was, what moved it.
