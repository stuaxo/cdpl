CDPL - CD audio test player for DOS and DOS emulators
=====================================================

AI Disclosue/Warning:  Built with LLMs directed by an (ancient) developer,


TUI and text mode CD Player, built to exercise emulators and hardware.

CDPL is a perfectly functional TUI CDplayer, it's also designed to
exercise emulators (DOSEMU2 at first).


Top level features:

- Play CDs and extract information
- 16 or 32 bit, togglable from the GUI
- Analogue and digital playback
- Scriptable.


Files:
CDPL.EXE

Launcher - this is the way to interact with CDPL and launch the TUI

TUI functions:

Playback modes (Options menu, M key):

Analog:
The native playback function of the drive, on hardware this would send
audio from a cable on the drive to the sound card.

Note:  Many sound cards default the CD Audio to be 0 or very quiet,
       You can set the volume in CDPL.



Digital:
The player itself reads audio data from CD and sends it to the soundcard.

Note:  Requires BLASTER to be set.
Supports:
SB16: 16-bit stereo 44.1 kHz.
SB Pro: 8-bit stereo 22 kHz.  SB 2.0: 8-bit mono 22 kHz.
Requires BLASTER to be set (A, I, D and H for 16-bit).


16 / 32 bit toggle:
When run from CDPL you can switch between 16 and 32 bit, this is
useful for testing emulators.

Player state, mouse position etc are saved and then restored after
the switch, so to the user it should be fairly smooth.

Interally this CDPL16 will quit and the launcher will start CDPL32.


Commandline options:

CDPL [switches] [command ...]

By default CDPL with no commands launches the TUI.

Commands are words seperated by spaces, most of them have short forms.

Some examples:

CDPL I              Show drive information.
CDPL /M:D P 3 W     Set [/M]ode to [d]igital], [P]lay track [3] and [W]ait until it finishes.
CDPL /16 /D:G TE    Self test the G: drive

You can chain these together for longer tests:
CDPL P 3+01:30 W 5 PA ST R W 5 S L

Though at that point the longer form may be more readable.
CDPL PLAY 3+01:30 WAIT 5 PAUSE STATUS RESUME WAIT 5 STOP LOG

If a command is unknown CDPL exits with errorlevel 3.


Commands:
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
                       one used; always 16 below a 386).  An engine given
                       the other one refuses.
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
