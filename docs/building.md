# Building by hand

What `Setup.cmd` does, step by step. Windows only: the host runs the game on
the real Direct3D 8, DirectInput 8, DirectSound and DirectShow DLLs
(pcrecomp `native32`), so there is no Linux or macOS build yet.

## Prerequisites

Windows 10/11, **Python 3.10+** (`py -3 --version`), **git**, **Visual
Studio 2022** (or its Build Tools) with *Desktop development with C++* (CMake
and Ninja come with it), and the pcrecomp toolkit cloned **beside** this
repository as `pcrecomp`, on its **`pcrecomp-stp`** branch:

```
some-folder\
  pcrecomp\            <- git clone -b pcrecomp-stp https://github.com/sp00nznet/pcrecomp
  crazytaxi-recomp\    <- this repository
```

`pcrecomp-stp` is not merged into pcrecomp `main` yet. A lift from `main`
boots and plays the attract demo, but a few seconds into a run the taxi ends
up on its side, wheels off, frozen.

## Steps

1. Python package:
   ```
   py -3 -m pip install --user capstone
   ```
2. The game into `game\`, and its exe into `work\`. Either copy the installed
   game's folder, or unpack the CD:
   ```
   py -3 ..\pcrecomp\tools\assets\isextract.py E:\DATA1.HDR E:\DATA1.CAB -o game
   copy E:\CRAZY_TAXI_PC.EXE game\
   mkdir work & copy game\CRAZY_TAXI_PC.EXE work\
   ```
   The CD path has not been tried on this disc (the copy here was unpacked
   with `i6comp`); if it fails, install from the CD and copy that folder.
3. The function catalog (about a minute):
   ```
   py -3 ..\pcrecomp\tools\disasm\disasm32.py work\CRAZY_TAXI_PC.EXE -o work\functions.json
   ```
   Expected, at the end (pcrecomp-stp c3fe185):
   ```
   [*] Functions: 8509  (thunks=4, leaves=2664)
   [*] Byte coverage: 758,207 / 786,432 (96.4% of code range)
   ```
4. Recompile to C (about ten minutes):
   ```
   py -3 run_lift.py --all
   ```
   Expected:
   ```
     lifted 8510   not-lifted stubs 0   errors 0   no terminator 95
     2,368,209 lines of C in 22 files, 575.5s
   ```
5. Build (about 10 minutes the first time):
   ```
   build.cmd
   ```
   Expected, at the end: `Linking C executable crazytaxirecomp.exe`.
6. Check it without starting the game: `build\crazytaxirecomp.exe` maps and
   binds the image, then prints
   `(dry run: image mapped and bound; --run enters 0x...)`.
7. Run: `build\crazytaxirecomp.exe --run --sound`.

`PCRECOMP` (Python) and `-DPCRECOMP=` (CMake, through `set CMAKE_ARGS=...`
for `build.cmd`) point at a toolkit checkout other than `..\pcrecomp`; the
lifter and the runtime must come from the same tree.

The usual trip-ups: `python` opening the Microsoft Store (Windows' alias
placeholder; use `py -3`, or turn the alias off in *Settings > Apps >
Advanced app settings > App execution aliases*); a freshly installed Python
not on `PATH` until you open a new window; and a `CRAZY_TAXI_PC.EXE` from
another release, which `run_lift.py` refuses by its PE timestamp
(`0x3D466348`, the US CD of 2002-07-30).

## netlab

The recipe is `projects/crazytaxi-recomp.env` (not `crazytaxi.env`, which is
the Dreamcast project): `netlab build crazytaxi-recomp`.
