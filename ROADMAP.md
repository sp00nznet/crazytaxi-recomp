# Roadmap

## Next

- A gamepad (only the keyboard is tested).
- Sound: check DirectSound and the DirectShow music by ear (`--sound`).
- Pick the frames to keep: the attract demo's frame rate against the original.

- A conformance harness (REPO_RULES section 9): fixed milestones (dry-run
  bind, boot to the title, N attract frames, a `--shot` diff), a pass/fail
  count in the README, wired as the netlab recipe's `QA` with a SKIP line
  on machines without the game.
- `--headless --record out.mp4` (section 10), with scripted keys.

## Then
- Try `Setup.cmd` from the CD (`isextract.py` on `DATA1.CAB`) and from an
  `.iso`; only the installed-folder path matches what was used here.
- Window scaling beyond 640x480.
- Delete the stale `master` branch on GitHub (`main` is the default).
- Linux and macOS: a Wine path (clang-cl cross build), then `setup.sh`.
- An installer release.

## Toolkit (pcrecomp)
- MSVC `setjmp`/`longjmp` into lifted frames, now in two hosts (HotD2, this):
  worth moving into native32.
- COM vtable hooks (`vt_hook`) for Direct3D 8 and DirectInput 8: the same
  windowed and background fixes will be needed by other DX8 games.
