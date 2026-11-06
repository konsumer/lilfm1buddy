#!/bin/sh
# Every case's parts, for each board, with and without a battery: build/cases/*.stl. OpenSCAD makes them.
#   tools/cases.sh
# The page (case/casemaker.js) makes your own, in the browser; this is for a release's files, or to see them here.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/cases
BATTERY="[20,15,4]"  # (the room a 180 mAh cell needs: an XIAO's case is small)
for board in xiaoplus xiao zero; do
  for kind in plain battery; do
    [ "$kind" = battery ] && b="-D battery=true -D bat=$BATTERY" || b="-D battery=false"
    for part in base lid button; do
      out="build/cases/lilfm1buddy-case-$board${kind:+-$kind}-$part.stl"
      openscad -D "part=\"$part\"" -D "board=\"$board\"" $b -o "$out" case/lilfm1buddy-case.scad
    done
  done
done
echo "build/cases: $(ls build/cases | wc -l | tr -d ' ') files"
