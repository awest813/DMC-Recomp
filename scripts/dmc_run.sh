#!/bin/bash
# Boots Devil May Cry headless through the title into a new game, then presses Start and Cross every
# two seconds (skips movies and dialogue). Run from the repository root in Git Bash.
# usage: scripts/dmc_run.sh <frames> <log file> [extra ps2run args...]
frames=$1; log=$2; shift 2
presses="--press 300:start --press 600:cross --press 660:cross --press 720:cross --press 780:cross --press 840:cross --press 900:cross"
for ((f = 1300; f < frames; f += 120)); do presses="$presses --press $f:start --press $((f + 60)):cross"; done
mkdir -p games/dmc
exec ./build/native-dmc/ps2run.exe games/dmc/disc/SLUS_202.16 --disc '../Devil May Cry.iso' --memcard games/dmc/explore.ps2 \
    --frames "$frames" $presses "$@" > "$log" 2>&1
