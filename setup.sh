#!/bin/sh
# Linux and macOS: not supported yet. The host runs the game on the real
# Windows Direct3D 8, DirectInput 8 and DirectShow DLLs (pcrecomp native32),
# so there is no native build, and no Wine path has been tried. Use Setup.cmd
# on Windows.
echo "Crazy Taxi recomp builds on Windows only for now: run Setup.cmd there." >&2
echo "See ROADMAP.md." >&2
exit 1
