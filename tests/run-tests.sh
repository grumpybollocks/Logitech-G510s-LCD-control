#!/bin/bash
# Build and run the C unit tests for g510_lcd_stats (added 2026-11-02).
# Safe to run anywhere: builds into a scratch dir, never touches the live
# service, the real LCD device, or your custom_screens.txt.
#   ./tests/run-tests.sh
set -u
cd "$(dirname "$0")"
PROJECT_DIR="$(cd .. && pwd)"
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
CF=$(pkg-config --cflags freetype2); LF=$(pkg-config --libs freetype2)
rc=0
for t in test_sensor_lookup test_lcd_missing; do
    echo "== $t"
    if ! gcc -Wall -Wextra $CF -DPROJECT_DIR="\"$PROJECT_DIR\"" -DTEST_SCRATCH="\"$S\"" \
            "$t.c" -o "$S/$t" -lg15render $LF -lm -ldl; then
        echo "BUILD FAILED: $t"; rc=1; continue
    fi
    "$S/$t" || rc=1
    echo
done
[ $rc -eq 0 ] && echo "ALL C TESTS PASSED" || echo "SOME C TESTS FAILED"
exit $rc
