# CDPL - build with Open Watcom 2.0 wmake (Linux host)
#
#   wmake              CDPL.EXE (launcher), CDPL16.EXE, CDPL32.EXE, DOS4GW.EXE
#   wmake EXT=dos32a   link CDPL32 for DOS/32A instead of DOS/4GW
#                      (also: pmodew, causeway)
#   wmake clean
#
# Source layers (each only uses the ones above it):
#   src/plat    DOS real mode / DPMI platform layer
#   src/core    MSCDEX, Sound Blaster, playback        (no screen code)
#   src/tui     text UI toolkit: VGA font, pointer, widgets (no CD code)
#   src/app     main, the player screen, command words, self test
#   src/launch  CDPL.EXE

!ifndef EXT
EXT = dos4g
!endif

INC = -i=$(%WATCOM)/h -i=src/plat -i=src/core -i=src/tui -i=src/app
CF16 = -zq -ml -bt=dos -2 -s -ox -w4 -we -dCDPL16 $(INC)
CF32 = -zq -mf -bt=dos -3r -s -ox -w4 -we -dCDPL32 $(INC)
CFL  = -zq -ms -bt=dos -0 -s -os -w4 -we -i=$(%WATCOM)/h -i=src/plat

OBJ16 = obj16/plat16.obj obj16/cpu.obj obj16/mscdex.obj obj16/sb.obj &
        obj16/player.obj obj16/info.obj obj16/vga.obj &
        obj16/glyphs.obj obj16/mouse.obj obj16/tui.obj &
        obj16/main.obj obj16/ui.obj obj16/cli.obj obj16/selftest.obj &
        obj16/state.obj
OBJ32 = obj32/plat32.obj obj32/mscdex.obj obj32/sb.obj &
        obj32/player.obj obj32/info.obj obj32/vga.obj &
        obj32/glyphs.obj obj32/mouse.obj obj32/tui.obj &
        obj32/main.obj obj32/ui.obj obj32/cli.obj obj32/selftest.obj &
        obj32/state.obj

HDRS = src/plat/plat.h src/plat/cpu.h src/core/mscdex.h src/core/sb.h src/core/player.h &
       src/core/info.h src/tui/vga.h src/tui/glyphs.h src/tui/mouse.h &
       src/tui/tui.h src/app/opts.h src/app/ui.h src/app/cli.h &
       src/app/selftest.h src/app/state.h

all: obj16 obj32 CDPL.EXE CDPL16.EXE CDPL32.EXE DOS4GW.EXE .SYMBOLIC

obj16: .SYMBOLIC
    @mkdir -p obj16
obj32: .SYMBOLIC
    @mkdir -p obj32

obj16/launch.obj: src/launch/launch.c src/plat/cpu.h
    wcc $(CFL) -fo=$@ src/launch/launch.c
obj16/cpul.obj: src/plat/cpu.c src/plat/cpu.h
    wcc $(CFL) -fo=$@ src/plat/cpu.c
obj16/cpu.obj: src/plat/cpu.c $(HDRS)
    wcc $(CF16) -fo=$@ src/plat/cpu.c

obj16/plat16.obj: src/plat/plat16.c $(HDRS)
    wcc $(CF16) -fo=$@ src/plat/plat16.c
obj32/plat32.obj: src/plat/plat32.c $(HDRS)
    wcc386 $(CF32) -fo=$@ src/plat/plat32.c

obj16/mscdex.obj: src/core/mscdex.c $(HDRS)
    wcc $(CF16) -fo=$@ src/core/mscdex.c
obj32/mscdex.obj: src/core/mscdex.c $(HDRS)
    wcc386 $(CF32) -fo=$@ src/core/mscdex.c

obj16/sb.obj: src/core/sb.c $(HDRS)
    wcc $(CF16) -fo=$@ src/core/sb.c
obj32/sb.obj: src/core/sb.c $(HDRS)
    wcc386 $(CF32) -fo=$@ src/core/sb.c

obj16/player.obj: src/core/player.c $(HDRS)
    wcc $(CF16) -fo=$@ src/core/player.c
obj32/player.obj: src/core/player.c $(HDRS)
    wcc386 $(CF32) -fo=$@ src/core/player.c

obj16/info.obj: src/core/info.c $(HDRS)
    wcc $(CF16) -fo=$@ src/core/info.c
obj32/info.obj: src/core/info.c $(HDRS)
    wcc386 $(CF32) -fo=$@ src/core/info.c

obj16/vga.obj: src/tui/vga.c $(HDRS)
    wcc $(CF16) -fo=$@ src/tui/vga.c
obj32/vga.obj: src/tui/vga.c $(HDRS)
    wcc386 $(CF32) -fo=$@ src/tui/vga.c

obj16/glyphs.obj: src/tui/glyphs.c $(HDRS)
    wcc $(CF16) -fo=$@ src/tui/glyphs.c
obj32/glyphs.obj: src/tui/glyphs.c $(HDRS)
    wcc386 $(CF32) -fo=$@ src/tui/glyphs.c

obj16/mouse.obj: src/tui/mouse.c $(HDRS)
    wcc $(CF16) -fo=$@ src/tui/mouse.c
obj32/mouse.obj: src/tui/mouse.c $(HDRS)
    wcc386 $(CF32) -fo=$@ src/tui/mouse.c

obj16/tui.obj: src/tui/tui.c $(HDRS)
    wcc $(CF16) -fo=$@ src/tui/tui.c
obj32/tui.obj: src/tui/tui.c $(HDRS)
    wcc386 $(CF32) -fo=$@ src/tui/tui.c

obj16/main.obj: src/app/main.c $(HDRS)
    wcc $(CF16) -fo=$@ src/app/main.c
obj32/main.obj: src/app/main.c $(HDRS)
    wcc386 $(CF32) -fo=$@ src/app/main.c

obj16/ui.obj: src/app/ui.c $(HDRS)
    wcc $(CF16) -fo=$@ src/app/ui.c
obj32/ui.obj: src/app/ui.c $(HDRS)
    wcc386 $(CF32) -fo=$@ src/app/ui.c

obj16/cli.obj: src/app/cli.c $(HDRS)
    wcc $(CF16) -fo=$@ src/app/cli.c
obj32/cli.obj: src/app/cli.c $(HDRS)
    wcc386 $(CF32) -fo=$@ src/app/cli.c

obj16/selftest.obj: src/app/selftest.c $(HDRS)
    wcc $(CF16) -fo=$@ src/app/selftest.c
obj32/selftest.obj: src/app/selftest.c $(HDRS)
    wcc386 $(CF32) -fo=$@ src/app/selftest.c

obj16/state.obj: src/app/state.c $(HDRS)
    wcc $(CF16) -fo=$@ src/app/state.c
obj32/state.obj: src/app/state.c $(HDRS)
    wcc386 $(CF32) -fo=$@ src/app/state.c

CDPL.EXE: obj16/launch.obj obj16/cpul.obj
    wlink op q system dos name $@ file { obj16/launch.obj obj16/cpul.obj }

CDPL16.EXE: $(OBJ16)
    wlink op q system dos op stack=8k name $@ file { $(OBJ16) }

CDPL32.EXE: $(OBJ32)
    wlink op q system $(EXT) op stack=32k name $@ file { $(OBJ32) }

DOS4GW.EXE: $(%WATCOM)/binw/dos4gw.exe
    cp $(%WATCOM)/binw/dos4gw.exe DOS4GW.EXE

clean: .SYMBOLIC
    rm -rf obj16 obj32 CDPL.EXE CDPL16.EXE CDPL32.EXE DOS4GW.EXE *.err
