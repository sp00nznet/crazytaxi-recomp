# Notice

The MIT license in [LICENSE](LICENSE) covers what is ours in this repository:
the host (`src/runtime/host.c`), the lift driver, the setup scripts, the
launcher config and the documentation. It does not cover the game.

*Crazy Taxi* is © SEGA, and is not ours to license. Its executable, data,
movies, music and art are not in this repository and are excluded by
`.gitignore`.

- You need your own copy of the game to use any of this. Setup copies your
  install (or unpacks your CD) into `game/`; the pipeline reads it from there.
- Lifted code is a derivative work of the binary it came from. Anything the
  pipeline writes into `work/` or `src/recomp/gen*/` carries whatever license
  the original carries, not this one. The MIT grant stops at our own work.

The host started from the
[House of the Dead 2 recomp](https://github.com/sp00nznet/hotd2-recomp)'s and
runs on [pcrecomp](https://github.com/sp00nznet/pcrecomp)'s `native32`
runtime; both are ours and MIT. There is no third-party code in this
repository.
