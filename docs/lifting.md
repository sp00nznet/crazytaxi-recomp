# Lifting Crazy Taxi: what it took

The layout is HotD2's: disasm32 catalogues the executable, `run_lift.py`
lifts all of it, and the host (`src/runtime/host.c`) maps the image at
`0x00400000` on pcrecomp's `native32` runtime, where every import is the real
DLL. The lift needed nothing: 6,712 functions, 0 errors. What follows is the
host, in the order it turned up.

## 1. The disc

The game walks the drive letters for its CD. The host passes those calls to
Windows unchanged, so the CD check passes with your disc in the drive or your
disc image mounted. The host finds that drive (`CD_DRIVE=E`, or the first
CD-ROM drive with `CRAZY_TAXI_PC.EXE` at its root) and reads the files the
game opens on it from `game\`.

## 2. libjpeg's error exit

The first fault was a read of `0x1A8` in sub_0048AFEE, which is libjpeg's
`error_exit`: it calls `output_message` and then `longjmp`s to the decoder's
`setjmp`. The game tries every image as a JPEG first, so this happens on
`Font_all.png` at every boot. MSVC's `longjmp` (sub_004B0214) is patched to
the host, as in HotD2: it restores the registers and resumes the lifted frame
that called `setjmp` (sub_004B0290) at the landing pad after the call.

## 3. In front, or not at all

The window code is the DirectX 8 SDK's `CD3DApplication`. Before its main
loop (sub_0040A730), it waits in `GetMessage` until its window is the
foreground window, and minimizes it with `CloseWindow` meanwhile. Started from
a console, it rarely is. Windowed, the host answers `GetForegroundWindow` with
the game's window and ignores `CloseWindow`.

DirectInput devices were taken foreground-only, and the game retries
`Acquire` until it works (sub_00421CB0). The host hooks
`IDirectInput8::CreateDevice` and the device's `SetCooperativeLevel`, so
devices are background and non-exclusive.

## 4. Fullscreen, made windowed

The game creates a 512x384 windowed device, then `Reset`s it to 640x480
fullscreen. The host replaces `IDirect3D8::CreateDevice` and
`IDirect3DDevice8::Reset` in the real vtables. Unless `--fullscreen` is
given, a fullscreen request becomes windowed in the desktop's format, in a
normal window of that size.

## 5. Screenshots

A GDI copy of a windowed Direct3D 8 window comes out blank on Windows 11. So
`--shot` hooks `Present` and reads the back buffer, which is made lockable
for the run. `Present` also counts frames, and the count is printed every five
seconds.
