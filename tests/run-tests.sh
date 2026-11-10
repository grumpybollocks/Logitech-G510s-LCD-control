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
G15F=$("$PROJECT_DIR/scripts/g15-flags.sh")   # /usr/local paths only where that library is the one loaded
rc=0
for t in test_sensor_lookup test_lcd_missing test_render; do
    echo "== $t"
    if ! gcc -Wall -Wextra $G15F $CF -DPROJECT_DIR="\"$PROJECT_DIR\"" -DTEST_SCRATCH="\"$S\"" \
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
# Per LINE, not per file (2026-11-11): one new rule line matching only one ID
# would otherwise still pass because another line mentions both.
bad=$(grep -niE 'ID_MODEL_ID|idProduct|c22[de]' ../udev/99-g510-lcd.rules | grep -viE 'c22d.*c22e|c22e.*c22d' || true)
if [ -z "$bad" ]; then echo "  ok   every USB-ID rule line in 99-g510-lcd.rules covers c22d AND c22e"
else echo "  FAIL rule line(s) matching only one G510 USB ID:"; echo "$bad" | sed 's/^/       /'; rc=1; fi
# hwdb: each C22D match line needs a C22E twin with the same suffix, and vice versa
hw=../udev/91-g510-stop-to-playpause.hwdb
hwbad=""
while IFS= read -r m; do
    twin=$(printf '%s' "$m" | sed -E 's/(pC22)D/\1@/I; s/(pC22)E/\1D/I; s/(pC22)@/\1E/I')
    grep -qiF -- "$twin" "$hw" || hwbad="$hwbad $m"
done < <(grep -oiE '^evdev:input:b0003v046DpC22[DE][^ ]*' "$hw")
if [ -z "$hwbad" ] && grep -qiE 'pC22D' "$hw" && grep -qiE 'pC22E' "$hw"; then echo "  ok   every hwdb match exists for both c22d and c22e"
else echo "  FAIL hwdb match without its twin:$hwbad"; rc=1; fi
if command -v udevadm >/dev/null && udevadm verify --help >/dev/null 2>&1; then
    udevadm verify ../udev/99-g510-lcd.rules >/dev/null 2>&1 \
        && echo "  ok   udevadm verify" || { echo "  FAIL udevadm verify"; rc=1; }
fi
echo

echo "== test_buttons_reconnect"
./test_buttons_reconnect.sh | sed 's/^/  /'
[ "${PIPESTATUS[0]}" -eq 0 ] || rc=1
echo

echo "== test_restore_backlight"
./test_restore_backlight.sh | sed 's/^/  /'
[ "${PIPESTATUS[0]}" -eq 0 ] || rc=1
echo

echo "== test_macro_wait"
python3 -I ./test_macro_wait.py | sed 's/^/  /'
[ "${PIPESTATUS[0]}" -eq 0 ] || rc=1
echo

[ $rc -eq 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit $rc
