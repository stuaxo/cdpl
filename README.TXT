CDPL - CD audio test player for DOS and DOS emulators
=====================================================

Plays audio CDs through MSCDEX in two ways, from a 16-bit real-mode
build and a 32-bit DPMI build of the same source, with a Norton
Utilities style text UI.

  CDPL.EXE     launcher: runs CDPL16 or CDPL32 and switches between them
  CDPL16.EXE   16-bit real mode engine + UI
  CDPL32.EXE   32-bit protected mode engine + UI (DOS/4GW, DOS4GW.EXE)

Playback modes (Options menu, M key)
  Analog   MSCDEX PLAY AUDIO (1510h cmd 132): the drive or emulator plays
           the audio itself.  Position from the Q-channel IOCTL, end of
           play from the busy bit.
  Digital  READ LONG raw mode (cmd 128, 2352 bytes/sector) streamed to a
           Sound Blaster with auto-init DMA.  SB16: 16-bit stereo 44.1 kHz.
           SB Pro: 8-bit stereo 22 kHz.  SB 2.0: 8-bit mono 22 kHz.
           Needs BLASTER (A, I, D and H for 16-bit).

Engine switch (Options menu, or the 16-bit / 32-bit radio buttons)
  Only when started through CDPL.EXE.  The running engine writes
  CDPL.STA and exits with code 16 or 32, and the launcher starts the
  other build.  Analog play keeps running in the drive across the switch,
  and the new engine resyncs from the busy bit and the Q-channel.  This
  tests that the drive state survives.  Digital play restarts at the
  same position.  The video mode and font are left as they are across
  the switch (no mode set, so no window resize in an emulator), and the
  pointer keeps its position.  The launcher saves the mouse driver state
  (INT 33h 16h) before the first engine and restores it (17h) on exit.
  Started directly, an engine hides the Engine controls.

Command line
  CDPL [switches] [command ...]
  With no command, the full-screen player starts.  Commands are plain
  words that run in the order given; switches start with / (or -) and
  apply to the whole run wherever they are.  Every command and switch has
  a short form.  Anything unknown stops before doing anything (exit 3).

    CDPL I                      drive information
    CDPL /M:D P 3 W             play track 3 digitally until it ends
    CDPL P 3+01:30 W 5 PA ST R W 5 S L
    CDPL /16 /D:G TE            self test in the 16-bit engine on G:

Commands (no UI; each prints a line starting OK or ERROR, and the first
error prints the last MSCDEX requests and stops)
  P  PLAY [pos[-end]]  pos = TRACK | TRACK+MM:SS[.FF] | MM:SS[.FF] (absolute
                       Red Book time, 00:02.00 = HSG sector 0).  Default end:
                       the end of the run of audio tracks; "-TRACK" = end of
                       that track.  e.g. P 3   P 3-5   P 3+01:30-3
  PA PAUSE
  R  RESUME
  S  STOP              analog STOP is sent twice if the first only pauses
  SK SEEK pos          stopped: SEEK request; playing/paused: PLAY from pos
  W  WAIT [secs]       poll and print the position once a second, until
                       play ends, or secs (e.g. 5, 2.5) pass; any key stops
                       it.  Digital play only runs while CDPL does, so a
                       digital script ends with W.
  E  EJECT             open the tray (IOCTL output 0; stops digital play)
  IN INSERT            close the tray (IOCTL output 5) and re-read the TOC
                       Both report the door bit from Device Status.
  I  INFO              the Drive Info dialog's lines, then the TOC
  T  TOC
  ST STATUS
  L  LOG               the MSCDEX request log
  V  VOL 0-255         Audio Channel Control (+ SB voice level when digital)
  SB SBCD 0-255        SB mixer CD input level, raw register value
  TE TEST              self test: PASS/FAIL/SKIP lines, fails if any FAIL
  An argument is the next word when it starts with a digit.
  Analog play carries on after CDPL exits; digital play stops.

Switches
  /M:A  /MODE:ANALOG   playback mode (default: the UI's last mode, or
  /M:D  /MODE:DIGITAL    analog for commands, which ignore CDPL.STA)
  /D:x  /DRIVE:x       CD drive x:
  /RB   /REDBOOK       send PLAY AUDIO with Red Book instead of HSG addresses
  /16 /32              engine; CDPL.EXE starts that one (default: the last
                       one used).  An engine given the other one refuses.
  /NF   /NOFONT        standard CP437 characters and the driver's cursor
  /BF   /BIOSFONT      load the custom glyphs with INT 10h AX=1110h, not
                       plane 2
  /MA   /MABS          pointer from INT 33h fn 3 instead of mickeys (fn 0Bh)
  /K:.. /KEYS:...      scripted UI input for automated runs: comma-separated
                       single characters, key names (tab stab enter esc
                       space up down left right pgup pgdn home end f1 f10
                       alt-x), wN = wait N ticks, mX:Y = move pointer to
                       pixel X,Y, click, rclick.  e.g. CDPL /K:w10,3,w40,i
  /?    /H             help
  Internal: /S:file (state file, default CDPL.STA), /LAUNCHED (set by
  CDPL.EXE; enables the engine switch).

Exit codes
  0 ok, 1 a command failed (including a position not on the disc),
  2 no MSCDEX, 3 bad command line (unknown word or switch, missing
  argument, not a CD drive).  16 and 32 are used between the engines and
  CDPL.EXE.

Keys
  P/Enter play, Space pause/resume, S stop, N/B next/previous track,
  Left/Right seek 5 s, 1-9 track, M analog/digital, E eject, C close,
  I drive info, R raw sector peek (C there toggles cooked/raw),
  L request log, F10 menu, Alt-X exit.  Right click = pause.

The request log at the bottom shows each device request sent through
INT 2Fh AX=1510h, with its status word (ERR / BUSY / DONE bits).  Polled
Q-channel reads and good digital READ LONGs are only logged on error.

UI notes
  Characters 80h-ABh are redefined in the VGA font (checkboxes, radio
  buttons, transport icons, 1-pixel bar fills, and two 3x2-cell buffers
  for the mouse pointer).  The arrow is drawn into copies of the glyphs
  under it, so it moves at pixel precision in text mode.  The
  character clock is set to 8 dots so the glyphs join up.

Building (Linux host, Open Watcom 2.0 on the PATH, WATCOM set)
  wmake               all three EXEs, and copies DOS4GW.EXE
  wmake EXT=dos32a    link CDPL32 for another extender (dos32a, pmodew)
  wmake clean

Testing in dosemu2
  $_cdrom = "/path/disc.cue" in a dosemu config, then load MSCDEX with
  SHSUCDX /D:MSCD0001, then CDPL /16 TE and CDPL /32 TE.
