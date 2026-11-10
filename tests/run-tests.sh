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
# Regression (2026-11-10): the G510s re-enumerates as 046d:c22d when the
# headset jack is unplugged and 046d:c22e when it is plugged in. Every
# USB-ID match in udev/ must cover BOTH, or the LCD (and the hwdb remap)
# silently die whenever the jack is pulled.
echo "== udev_both_usb_ids"
for f in ../udev/99-g510-lcd.rules ../udev/91-g510-stop-to-playpause.hwdb; do
    for id in c22d c22e; do
        if grep -qi "$id" "$f"; then echo "  ok   $id in $(basename "$f")"
        else echo "  FAIL $id missing from $(basename "$f")"; rc=1; fi
    done
done
if command -v udevadm >/dev/null && udevadm verify --help >/dev/null 2>&1; then
    udevadm verify ../udev/99-g510-lcd.rules >/dev/null 2>&1 \
        && echo "  ok   udevadm verify" || { echo "  FAIL udevadm verify"; rc=1; }
fi
echo

echo "== test_buttons_reconnect"
./test_buttons_reconnect.sh | sed 's/^/  /'
[ "${PIPESTATUS[0]}" -eq 0 ] || rc=1
echo

echo "== test_macro_wait"
python3 -I ./test_macro_wait.py | sed 's/^/  /'
[ "${PIPESTATUS[0]}" -eq 0 ] || rc=1
echo

[ $rc -eq 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit $rc
