# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Scripted keyboard input, `--key S DIK MS`, timed on presented frames, and
  `--shot` on the same clock. A scripted run picks up a fare.
- `NOTICE.md`, `CONTRIBUTING.md`, `CONTRIBUTORS.md`, `docs/building.md`,
  `docs/testing.md`, and a `setup.sh` that says there is no Linux or macOS
  build yet.
- `Setup.cmd` (`tools/setup.ps1`): checks and offers to install Python,
  capstone, pcrecomp and the Visual Studio C++ tools; takes the installed
  game, the CD or an `.iso`; catalogs, lifts and builds; leaves a
  `Crazy Taxi` shortcut. Reruns skip finished steps.
- `launcher/launcher.json` and a hand-run installer workflow for
  recomp-launcher.
- netlab recipe `crazytaxi-recomp`.

### Changed
- The CD check runs against a real CD-ROM drive: your disc, or your disc
  image mounted (`CD_DRIVE` names the drive). Setup mounts an `.iso` or
  `.cue`/`.bin` for you.
- Setup, the launcher and the docs use pcrecomp's `pcrecomp-stp` branch: on a
  lift from `main` the taxi ends up on its side and frozen a few seconds into
  a run. From it: 8,509 cataloged, 8,510 lifted, 0 errors.
- README in ra2's order; `NOTICE` is now `NOTICE.md`.

## [0.1.0] - 2026-11-08

### Added
- First playable: `CRAZY_TAXI_PC.EXE` lifted to C (6,712 functions, 0 errors)
  on pcrecomp's native32 host. It boots past the CD check to the SEGA logo and
  the title, and the attract demo plays in 3D.
- Host: MSVC `longjmp` into
  lifted frames (libjpeg's error exit); always in front when windowed;
  background DirectInput; fullscreen made windowed (`--fullscreen` for the
  original); `--shot` from the back buffer, and a frame count.
- `docs/lifting.md`.
