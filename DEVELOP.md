# Developing CDPL

## Building

You need [Open Watcom 2.0](https://github.com/open-watcom/open-watcom-v2)
on a Linux host. The makefile also uses `mkdir -p`, `rm`, `cp` and `sed`.

```sh
export WATCOM=/opt/watcom          # where Open Watcom is installed
export PATH=$WATCOM/binl64:$PATH   # binl for a 32-bit host
wmake                              # CDPL.EXE, CDPL16.EXE, CDPL32.EXE, DOS4GW.EXE
wmake dist                         # the above plus README.MD, in dist/
wmake EXT=dos32a                   # link CDPL32 for DOS/32A instead (also pmodew)
wmake clean                        # removes obj16/, obj32/, dist/ and the EXEs
```

| Output | Built as |
|---|---|
| `CDPL.EXE` | launcher, 16-bit small model, 8086 code (`-ms -0`) |
| `CDPL16.EXE` | real-mode engine, large model, 8086 code (`-ml -0`), so it runs on an XT |
| `CDPL32.EXE` | DPMI engine, flat model, 386 code (`-mf -3r`), linked for DOS/4GW unless `EXT` says otherwise |
| `DOS4GW.EXE` | copied from `$WATCOM/binw` |

The build treats warnings as errors (`-w4 -we`). `wmake dist` collects the
four EXEs in `dist/`, plus `README.MD` and `LICENSE.TXT`: `README.md` and
`LICENSE` with DOS line endings, so write the README so it still reads as
plain text.

## Source layout

Each layer only uses the ones above it:

| Directory | Contents | Uses |
|---|---|---|
| `src/plat` | DOS platform layer: real-mode interrupts, DOS memory, DMA buffers, IRQ hooks. `plat16.c` is real mode, `plat32.c` is DPMI (0300h interrupts, 0100h DOS memory, 0205h IRQ vectors, 0600h locking). `cpu.c` detects 8086/286/386+ for the 16-bit programs. | - |
| `src/core` | `mscdex`: drive discovery, device requests (INT 2Fh 1510h), TOC, Q-channel, the request log. `sb`: BLASTER, DSP, mixer, ISA DMA. `player`: analog (PLAY AUDIO) and digital (READ LONG to the SB) behind one API. `info`: the drive information lines. No screen code. | plat |
| `src/tui` | Text UI toolkit: `vga` (8-dot mode, font upload), `glyphs` (custom characters, pointer art), `mouse` (INT 33h), `tui` (back buffer, drawing, pointer compositing, events, `/KEYS` scripts). No CD code. | plat |
| `src/app` | `main`: switches, start-up, dispatch. `ui`: the player screen, menus and dialogs. `cli`: the command words. `selftest`: TEST. `state`: CDPL.STA, which carries the state across an engine switch. `opts.h`: the parsed switches. | all |
| `src/launch` | `CDPL.EXE`: runs CDPL16/CDPL32 and switches between them | `cpu.c` |

Only `src/plat` differs between the 16-bit and 32-bit builds. The rest of
the code is the same for both engines, except where it checks
`ENGINE_BITS` / `CDPL16` / `CDPL32` (from `plat.h`). Memory that MSCDEX or
the DMA controller sees always comes from `dos_alloc` / `dma_alloc`, since
it must sit below 1 MB with a real-mode segment.

### Engine switch

The running engine writes `CDPL.STA`, leaves the video mode and font
alone, and exits with code 16 or 32. The launcher then starts the other
engine with `/LAUNCHED`. The new engine sees `switched=1` in the state
file, skips the mode set, and restores the player state and pointer
position. The launcher saves the mouse driver state (INT 33h 16h) before
the first engine and restores it (17h) when CDPL exits.

## Testing in dosemu2

Point the CD drive at an image, and load SHSUCDX on dosemu's CD driver:

```sh
dosemu -I 'cdrom { "/path/to/disc.cue" }'
```
```
C:\> shsucdx /d:MSCD0001
C:\> cdpl /16 te
C:\> cdpl /32 te
```

- `-dumb` gives plain console output, which suits the command words
  (`TE`, `I`, `P 2 W 5 ST L`).
- `-D+C` (CD-ROM) and `-D+m` (mouse) with `-o file` write dosemu's side
  of the conversation to a log.
- `/K:` scripts drive the UI without a keyboard, e.g.
  `cdpl /16 /K:w20,f10,right,right,right,3` switches to the 32-bit engine.
- Set `$_wav_file` to capture what the emulated Sound Blaster plays.
- If you run dosemu under `xvfb-run` for screenshots, unset
  `WAYLAND_DISPLAY`. Otherwise SDL opens the window on your real desktop.

## Releases

`tools/package.sh VERSION` (after `wmake dist`) writes two zips to `pkg/`,
both with 8.3 names:

| Zip | Contents |
|---|---|
| `CDPLnnnn.ZIP` | the four EXEs, `README.MD` and `LICENSE.TXT`; `nnnn` is the version's digits, so 0.1.0 gives `CDPL010.ZIP` (at most 4 digits) |
| `CDPL.ZIP` | FreeDOS package: `SOUND\CDPL\`, `APPINFO\CDPL.LSM`, `LINKS\CDPL.BAT`, `SOURCE\CDPL\SOURCES.ZIP` |

GitHub Actions (`.github/workflows/build.yml`) builds and packages every
push to main and every pull request, and keeps the zips as a build
artifact. Pushing a tag `vX.Y.Z` also creates a GitHub release with
both zips attached:

```sh
git tag v0.1.0 && git push origin v0.1.0
```
