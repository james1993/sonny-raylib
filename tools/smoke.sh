#!/bin/sh
# Open every screen, headless, and fail if any of them crashes or trips a
# sanitizer. Not a comparison -- what is drawn depends on the GL underneath --
# but everything the game draws is drawn, every asset a screen names is read,
# and the fight runs long enough to play out moves.
#
#   tools/smoke.sh build/sonny_san        (needs a display: xvfb-run in CI)
set -u
BIN=${1:-build/sonny}
OUT=${SMOKE_OUT:-build/smoke}
mkdir -p "$OUT/saves"
export SONNY_SILENT=1 SONNY_SEED=7 SONNY_WINDOW=800x575
export SONNY_SAVES="$OUT/saves"
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1}

failed=0
run() {
    name=$1; shift
    log="$OUT/$name.log"
    if ! env "$@" SONNY_SHOT="$OUT/$name.png" "$BIN" >"$log" 2>&1; then
        echo "FAIL $name (exit $?)"; failed=1; return
    fi
    if grep -q "runtime error\|ERROR: AddressSanitizer\|SUMMARY: " "$log"; then
        echo "FAIL $name (sanitizer)"; grep -m5 "runtime error\|ERROR\|SUMMARY" "$log"
        failed=1; return
    fi
    if [ ! -s "$OUT/$name.png" ]; then
        echo "FAIL $name (no capture)"; failed=1; return
    fi
    echo "ok   $name"
}

for screen in title start slots class manual settings ending lost gameover \
              hub map inventory talents shop victory; do
    run "$screen" SONNY_SCREEN=$screen SONNY_STEPS=30
done
run intro SONNY_SCREEN=intro SONNY_STEPS=600
run hub_note SONNY_SCREEN=hub SONNY_PROGRESS=3 SONNY_STEPS=30
run ally SONNY_SCREEN=inventory SONNY_PROGRESS=15 \
    SONNY_CLICKS=5:112:385,20:240:318 SONNY_STEPS=40
run tooltip SONNY_SCREEN=inventory SONNY_MOUSE=263:212 SONNY_STEPS=30
run ring SONNY_SCREEN=battle SONNY_HOVER=2 SONNY_STEPS=60
run battle SONNY_SCREEN=battle SONNY_STEPS=1500
run boss SONNY_SCREEN=battle SONNY_BATTLE=9 SONNY_STEPS=1500
run hub_to_fight SONNY_SCREEN=hub SONNY_CLICKS=60:316:264 SONNY_STEPS=400
exit $failed
