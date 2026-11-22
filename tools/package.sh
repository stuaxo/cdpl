#!/bin/sh
# package.sh VERSION - build the release zips from dist/ (run wmake dist first)
#
#   pkg/CDPLnnnn.ZIP   plain zip: the EXEs and README.MD
#   pkg/CDPL.ZIP       FreeDOS package: SOUND\CDPL, APPINFO\CDPL.LSM,
#                      LINKS\CDPL.BAT, SOURCE\CDPL\SOURCES.ZIP
#
# Both names are 8.3: the version's digits go after CDPL, so 0.1.0 gives
# CDPL010.ZIP, and a version with more than 4 digits is refused.
set -eu

ver=${1:?usage: package.sh VERSION}
ver=${ver#v}
digits=$(printf %s "$ver" | tr -cd 0-9)
[ -n "$digits" ] && [ ${#digits} -le 4 ] || {
    echo "package.sh: $ver: need 1-4 digits for an 8.3 zip name" >&2; exit 1; }
for f in CDPL.EXE CDPL16.EXE CDPL32.EXE DOS4GW.EXE README.MD; do
    [ -f "dist/$f" ] || { echo "package.sh: dist/$f missing (wmake dist)" >&2; exit 1; }
done

top=$(pwd)
out=$top/pkg
rm -rf "$out"
mkdir -p "$out"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
crlf() { sed 's/$/\r/'; }
export TZ=UTC

# plain zip
(cd dist && zip -q -X -9 "$out/CDPL$digits.ZIP" CDPL.EXE CDPL16.EXE CDPL32.EXE DOS4GW.EXE README.MD)

# FreeDOS package
fd=$tmp/fd
mkdir -p "$fd/APPINFO" "$fd/SOUND/CDPL" "$fd/LINKS" "$fd/SOURCE/CDPL"
cp dist/CDPL.EXE dist/CDPL16.EXE dist/CDPL32.EXE dist/DOS4GW.EXE dist/README.MD "$fd/SOUND/CDPL/"
printf '@%%DOSDIR%%\\SOUND\\CDPL\\CDPL.EXE %%1 %%2 %%3 %%4 %%5 %%6 %%7 %%8 %%9\n' | crlf > "$fd/LINKS/CDPL.BAT"
crlf > "$fd/APPINFO/CDPL.LSM" <<LSM
Begin3
Title:          CDPL
Version:        $ver
Entered-date:   $(date +%Y-%m-%d)
Description:    CD audio test player for DOS and DOS emulators
Summary:        Text mode CD player (analog PLAY AUDIO, or digital READ LONG
                to a Sound Blaster), in 16-bit real mode and 32-bit DPMI
                builds, built to exercise emulators and hardware.
Keywords:       cd, cd-rom, audio, player, mscdex, sound blaster
Author:         Stuart Axon
Maintained-by:  Stuart Axon
Primary-site:   https://github.com/stuaxo/cdpl
Platforms:      DOS, FreeDOS
Copying-policy: ${CDPL_LICENSE:-see README.MD}
End
LSM
git archive --format=zip -9 -o "$fd/SOURCE/CDPL/SOURCES.ZIP" HEAD
(cd "$fd" && zip -q -X -9 -r "$out/CDPL.ZIP" APPINFO SOUND LINKS SOURCE)

ls -l "$out"
