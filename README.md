# CDPL - CD audio test player for DOS

> **Warning:** this code was written with AI assistance (Claude). It was
> built mostly to help test how DOS emulators handle CD audio, though it
> should also work as a usable CD player.

CDPL plays audio CDs through MSCDEX in the two ways DOS programs did:

- **Analog**: MSCDEX PLAY AUDIO. The drive, or the emulator, plays the
  audio itself. Position comes from the Q-channel, the end of play from
  the busy bit.
- **Digital**: raw READ LONG of the audio sectors (2352 bytes each),
  streamed to a Sound Blaster with auto-init DMA. SB16 at 16-bit stereo
  44.1 kHz, falling back to 8-bit 22 kHz on SB Pro and SB 2.0.

The same source builds two engines:

- **CDPL16.EXE**: 16-bit real mode.
- **CDPL32.EXE**: 32-bit protected mode under DOS/4GW (or another
  extender).

**CDPL.EXE** is a small launcher that runs one engine and switches to the
other when you pick it in the UI, so the two feel like one program. Analog
play carries on through the switch, which tests that the drive state
survives. On an 8086 or 286 only the 16-bit engine runs, and the UI just
says so.

The full-screen UI is in the style of Norton Utilities 8. It uses a
redefined VGA font for check boxes, icons, 1-pixel bar fills and a
pixel-precise mouse pointer in text mode. Every request sent to MSCDEX is
shown in a log with its status word.

Everything can also be driven from the command line, for dumb consoles
and scripts:

```
CDPL I                        drive information
CDPL /M:D P 3 W               play track 3 digitally until it ends
CDPL P 3+01:30 W 5 PA ST R W 5 S L
CDPL /16 /D:G TE              self test in the 16-bit engine on drive G:
CDPL /?                       help
```

[README.TXT](README.TXT) is the full manual: commands, switches, keys and
exit codes. It ships with the binaries.

## Building

You need [Open Watcom 2.0](https://github.com/open-watcom/open-watcom-v2)
on a Linux host. The makefile uses `mkdir -p`, `rm` and `cp`.

```sh
export WATCOM=/opt/watcom          # where Open Watcom is installed
export PATH=$WATCOM/binl64:$PATH   # binl for a 32-bit host
wmake                              # CDPL.EXE, CDPL16.EXE, CDPL32.EXE, DOS4GW.EXE
wmake EXT=dos32a                   # link CDPL32 for DOS/32A instead (also pmodew)
wmake clean
```

The build treats warnings as errors (`-we`). The output goes in the top
directory, and DOS4GW.EXE is copied from `$WATCOM/binw`. Copy the four EXEs
to the DOS machine or emulator drive, and run `CDPL`.

## Running

CDPL needs an MSCDEX-compatible driver (MSCDEX, SHSUCDX, ...) with a CD
drive. For digital play it also needs a Sound Blaster and the `BLASTER`
variable (`A`, `I`, `D`, and `H` for 16-bit).

In dosemu2, point the CD drive at an image and load SHSUCDX on dosemu's
CD driver:

```
dosemu -I 'cdrom { "/path/to/disc.cue" }'
C:\> shsucdx /d:MSCD0001
C:\> cdpl
```

## Source layout

Each layer only uses the ones above it:

| Directory    | What | Uses |
|--------------|------|------|
| `src/plat`   | DOS platform layer: real-mode interrupts, DOS memory, DMA buffers, IRQs. `plat16.c` is real mode, `plat32.c` is DPMI, and `cpu.c` detects 8086/286/386+. | - |
| `src/core`   | `mscdex` (device requests, TOC, Q-channel, request log), `sb` (DSP, mixer, DMA), `player` (analog and digital back ends behind one API), `info` (drive information text). No screen code. | plat |
| `src/tui`    | Text UI toolkit: `vga` (8-dot mode, font upload), `glyphs` (custom characters), `mouse` (INT 33h), `tui` (back buffer, drawing, pointer, events). No CD code. | plat |
| `src/app`    | `main` (switches, start-up, dispatch), `ui` (the player screen, menus and dialogs), `cli` (command words), `selftest` (TEST), `state` (CDPL.STA for engine switches) | all |
| `src/launch` | `CDPL.EXE`: runs CDPL16/CDPL32 and switches between them | - |

The 16/32-bit differences stay in `src/plat`. The rest of the code is
the same for both engines, apart from `ENGINE_BITS`.
