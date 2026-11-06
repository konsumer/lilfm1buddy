#!/bin/sh
# The case's STLs for each board (and the XIAOs with a battery), in build/cases/. The page (site/casemaker.js) makes
# your own in the browser; this is to see them here, and to check the model:
#   tools/cases.sh           the STLs
#   tools/cases.sh --check   the fit checks, for every board: each must come out empty (fit_reach must not: it's each
#                            button's peg on its switch, just past where it rests)
# Needs openscad (2025 or later, for its manifold backend).
set -e
cd "$(dirname "$0")/.."
SCAD=case/lilfm1buddy-case.scad
BAT='[12,30,4]'
out=build/cases
mkdir -p "$out"

run() {
  openscad --backend=manifold "$@" "$SCAD" 2>&1
}

if [ "$1" = --check ]; then
  bad=0
  for board in zero xiao xiaoplus; do
    for battery in false true; do
      [ "$board" = zero ] && [ "$battery" = true ] && continue
      for check in fit_board fit_buttons fit_press fit_base fit_usb fit_battery fit_reach; do
        log=$(run -D "board=\"$board\"" -D "battery=$battery" -D "bat=$BAT" -D "part=\"$check\"" -o "$out/check.stl" || true)
        if echo "$log" | grep -q 'ERROR\|Assertion'; then
          result="error: $(echo "$log" | grep 'ERROR\|Assertion' | head -1)"
        elif echo "$log" | grep -q 'top level object is empty'; then
          result=empty
        else
          result=touches
        fi
        want=empty
        [ "$check" = fit_reach ] && want=touches
        if [ "$result" = "$want" ]; then
          echo "ok    $board battery=$battery $check"
        else
          echo "FAIL  $board battery=$battery $check: $result"
          bad=1
        fi
      done
    done
  done
  rm -f "$out/check.stl"
  exit $bad
fi

for board in zero xiao xiaoplus; do
  for battery in false true; do
    [ "$board" = zero ] && [ "$battery" = true ] && continue
    name="lilfm1buddy-case-$board"
    [ "$battery" = true ] && name="$name-battery"
    for part in shell base buttons; do
      run -q -D "board=\"$board\"" -D "battery=$battery" -D "bat=$BAT" -D "part=\"$part\"" --export-format=binstl -o "$out/$name-$part.stl"
    done
    echo "$name"
  done
done
