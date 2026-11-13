# Testing

There is no conformance harness yet ([ROADMAP.md](../ROADMAP.md)). What there
is: scripted keys and screenshots on the host, timed on the game's own frames,
so a run can be repeated headless and checked by its pictures.

## Scripted input and shots

- `--key S DIK MS`: from frame S*50 (the game runs at 50), the keyboard's
  `GetDeviceState` answer has key DIK (a DirectInput scan code) held for MS ms
  of game time. Loading stalls the frame clock, so a script survives a slow
  disk. The game's keys (its table at 0x006524F0): Enter starts and pauses,
  A decides, the arrows drive (Up accelerates, Down brakes).
- `--shot S FILE`: the back buffer at frame S*50, as a .bmp (up to 8).
- `--headless` keeps the window hidden; `--watchdog S` stops the run after
  S seconds and says where the game was. Runs are muted unless `--sound`.

## A fare

Enter every 2 s to the mode menu, A three times (Arcade, Arcade rules), then
drive. In `cmd /v:on`:

```
set K=
for /l %t in (2,2,30) do set K=!K! --key %t 0x1C 200
build\crazytaxirecomp.exe --run --headless --watchdog 250 %K% --key 31 0x1E 200 --key 33 0x1E 200 --key 35 0x1E 200 ^
  --key 44 0xC8 1800 --key 45.8 0xD0 2500 --key 49 0xC8 1700 --key 49.1 0xCD 250 --key 50.7 0xD0 2500 --shot 56 fare.bmp
```

Passing looks like [screenshots/fare.png](screenshots/fare.png): the taxi on
its wheels, a customer aboard, bound for the cable car stop. The taxi's path
depends on the frame times, so a run can miss the customer; a lift from
pcrecomp `main` instead leaves the taxi on its side and frozen within a few
seconds.

## Other flags

| Flag | |
|---|---|
| `--run` | enter the game (without it: map, bind and stop) |
| `--fullscreen` | let the game go fullscreen, as the original does |
| `--game DIR`, `--exe FILE` | the game folder (default `game`) and the exe to map (default `work\CRAZY_TAXI_PC.EXE`) |
| `--args "..."` | switches passed to the game's own command line |
| `--sample` | sample where the game spends its time |
| `--native-trace`, `--callbacks` | one line per call into Windows, or back from it |
