CDPL - CD audio test player for DOS and DOS emulators
=====================================================

**AI Disclosure/Warning:**  Built with LLMs directed by an (ancient) developer.

The spelling mistakes are mine though :D


TUI and text mode CD Player, built to exercise emulators and hardware.

CDPL is a perfectly functional TUI CDplayer, it's also designed to
exercise emulators (DOSEMU2 at first).


- Play CDs and extract information
- 16 or 32 bit (386 and up only), togglable from the GUI
- Analogue and digital playback
- Scriptable.


Files:
------

`CDPL.EXE`

Launcher - this is the way to interact with CDPL and launch the TUI


TUI functions:
--------------

### Playback modes (Options menu, M key):

**Analog:**
The native playback function of the drive, on hardware this would send
audio from a cable on the drive to the sound card.

> Note:  Many sound cards default the CD Audio to be 0 or very quiet,
> You can set the volume in CDPL.


**Digital:**
The player itself reads audio data from CD and sends it to the soundcard.

Supported soundcards:

Sound blaster and compatible support:

- SB16: 16-bit stereo 44.1 kHz.
- SB Pro: 8-bit stereo 22 kHz.
- SB 2.0: 8-bit mono 22 kHz.

BLASTER must be set (A, I, D and H for 16-bit).


### 16 / 32 bit toggle:

Switch between 16 bit real mode and 32 bit protected mode.

This is more useful for testing than for users, but you never know.

These options are available if you start from CDPL and are on 386
or higher.

Under the hood there are two copies of the player, switching
between them should save + restore state, I can't promise the
audio won't skip when you do this, but it seems to work pretty
well.


Commandline options:
--------------------

    CDPL [switches] [command ...]

If no commands are specified the TUI launches.

Commands are separated by spaces, most of them have a short version.

Examples:

    CDPL I              Show drive information.
    CDPL /M:D P 3 W     Set [/M]ode to [d]igital, [P]lay track [3] and [W]ait until it finishes.
    CDPL /16 /D:G TE    Self test the G: drive

Chain commands for longer automations / tests:

    CDPL PLAY 3+01:30 WAIT 5 PAUSE STATUS RESUME WAIT 5 STOP LOG

Short form:

    CDPL P 3+01:30 W 5 PA ST R W 5 S L

Unknown commands will exit with error level 3.

### Commands:

      P  PLAY [pos[-end]]  pos = TRACK | TRACK+MM:SS[.FF] | MM:SS[.FF] (absolute
                           Red Book time, 00:02.00 = HSG sector 0).

                           Default end:  end of the run of audio tracks;
                           "-TRACK" = end of that track e.g. P 3   P 3-5   P 3+01:30-3
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


### Switches

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


### Exit codes

    0 OK
    1 Command failed e.g. a position outside this CD.
    2 No MSCDEX
    3 Bad command line (unknown word or switch, missing argument, not a CDROM drive).


### Keys

    P/Enter:    Play
    Space:      Pause / Resume
    S:          Stop
    N/B:        Next track / Previous track,
    Left/Right: Seek 5 s
    1-9:        Go to track 1-9
    M:          Toggle Analog/Digital
    E:          Eject
    C:          Close,
    I:          Drive Info
    R:          Raw sector peek (C there toggles cooked/raw)
    L:          Request Log
    F10:        Menu
    Alt-X:      Exit.

### Mouse
    Right click: pause.

### Request log

Shows device requests sent through INT 2F AX=1510h,
status word bits (ERR / BUSY / DONE).

Errors from Q-channel polled reads and digital READ longs are 
also logged.


Building
--------

(Linux host, Open Watcom 2.0 on the PATH, WATCOM set)

    wmake               all three EXEs, and copies DOS4GW.EXE
    wmake EXT=dos32a    link CDPL32 for another extender (dos32a, pmodew)
    wmake clean
