#!/bin/bash
# Tests for scripts/restore-backlight.sh -- the one script udev runs as ROOT on
# input from a user-writable file (added 2026-11-11). Runs unprivileged: the LED
# folder is redirected to a temp dir, loginctl/getent are faked via PATH.
set -u
cd "$(dirname "$0")/.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
rc=0
ok()  { echo "PASS $*"; }
bad() { echo "FAIL $*"; rc=1; }
mkdir -p "$T/led" "$T/home/.local/share/g510-lcd" "$T/bin"
sed "s#^LED=.*#LED=$T/led#" scripts/restore-backlight.sh > "$T/restore.sh"
grep -q "^LED=$T/led" "$T/restore.sh" || { echo "FAIL could not redirect LED in a copy"; exit 1; }
# fake loginctl: active session c2 on seat0; its Class comes from $T/class
cat > "$T/bin/loginctl" <<EOF
#!/bin/sh
case "\$*" in
  "show-seat seat0 -p ActiveSession --value") echo c2 ;;
  "show-session c2 -p Class --value") cat "$T/class" ;;
  "show-session c2 -p Name --value") echo fakeuser ;;
esac
EOF
printf '#!/bin/sh\necho "fakeuser:x:1000:1000::%s:/bin/bash"\n' "$T/home" > "$T/bin/getent"
chmod +x "$T/bin/"*
S="$T/home/.local/share/g510-lcd/set-backlight-color.sh"
run() { : > "$T/led/brightness"; : > "$T/led/multi_intensity"; PATH="$T/bin:$PATH" timeout 20 bash "$T/restore.sh" "$@"; }
led() { echo "$(cat "$T/led/brightness")|$(cat "$T/led/multi_intensity")"; }

echo user > "$T/class"
cp scripts/set-backlight-color.sh "$S"                          # the format the GUI writes
want="$(sed -nE 's#^echo ([0-9]+) > .*/brightness$#\1#p' "$S")|$(sed -nE 's#^echo "([0-9 ]+)" > .*/multi_intensity$#\1#p' "$S")"
run; [ "$(led)" = "$want" ] && ok "normal saved file applied ($want)" || bad "normal file: got '$(led)' want '$want'"

printf 'touch %s/PWNED\necho 999 > /sys/class/leds/g15::kbd_backlight/brightness\necho "1 2 300" > /sys/class/leds/g15::kbd_backlight/multi_intensity\n' "$T" > "$S"
run; [ "$(led)" = "|" ] && [ ! -e "$T/PWNED" ] && ok "malicious file: nothing executed, 999/300 refused" || bad "malicious file: led='$(led)' pwned=$([ -e "$T/PWNED" ] && echo yes)"

rm -f "$S"; mkfifo "$S"
t0=$(date +%s); run; t1=$(date +%s)
[ $((t1 - t0)) -le 8 ] && ok "a FIFO instead of a file can't hang it ($((t1 - t0)) s)" || bad "FIFO blocked it for $((t1 - t0)) s"
rm -f "$S"

{ head -c 8000 /dev/zero | tr '\0' '#'; echo; echo 'echo 200 > /sys/class/leds/g15::kbd_backlight/brightness'; } > "$S"
run; [ "$(led)" = "|" ] && ok "only the first 4 KB is read (value after 8 KB of junk ignored)" || bad "read past 4 KB: '$(led)'"

cp scripts/set-backlight-color.sh "$S"
echo greeter > "$T/class"
run; [ "$(led)" = "|" ] && ok "login-screen (greeter) session skipped" || bad "greeter session applied '$(led)'"
echo user > "$T/class"

run fakeuser; [ "$(led)" = "$want" ] && ok "explicit user argument (boot-time unit) works" || bad "explicit user: '$(led)'"
run 'bad;name'; [ "$(led)" = "|" ] && ok "invalid user argument rejected" || bad "invalid user arg applied '$(led)'"

echo
[ $rc -eq 0 ] && echo "restore-backlight: all passed" || echo "restore-backlight: FAILED"
exit $rc
