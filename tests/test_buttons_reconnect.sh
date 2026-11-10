#!/bin/bash
# Reconnect test for g510_lcd_buttons (added 2026-11-10).
# The G510s drops off USB every time the headset jack is plugged/unplugged;
# the buttons listener must survive that IN-PROCESS (same PID, no systemd
# restart). Simulated with a FIFO as the "device": EOF on it = disconnect,
# deleting it = node gone, recreating it = keyboard back.
# Never touches the real keyboard, the live service, or your real state files.
set -u
cd "$(dirname "$0")/.."
T=$(mktemp -d)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -rf "$T"; }
trap cleanup EXIT
rc=0
ok()   { echo "PASS $*"; }
bad()  { echo "FAIL $*"; rc=1; }

gcc -Wall -Wextra -DG510_KEYS_DEV="\"$T/keys\"" src/g510_lcd_buttons.c -o "$T/btn" || { echo "BUILD FAILED"; exit 1; }

# one key-down input_event (struct timeval = 2 longs, then u16 type, u16 code, s32 value)
press() {
    timeout 5 python3 -I -c 'import struct,sys; open(sys.argv[1],"wb").write(struct.pack("llHHi",0,0,1,int(sys.argv[2]),1))' "$T/keys" "$1"
}
state() { cat "$T/g510lcd_screen" 2>/dev/null; }
wait_state() { for _ in $(seq 50); do [ "$(state)" = "$1" ] && return 0; sleep 0.1; done; return 1; }

HOME="$T" XDG_RUNTIME_DIR="$T" "$T/btn" 2> "$T/err.log" &
PID=$!
sleep 0.5
kill -0 "$PID" 2>/dev/null && ok "starts and waits when the device doesn't exist yet" || bad "exited with no device"

mkfifo "$T/keys"
press 697                          # L2, then the writer closes -> EOF = "disconnect"
wait_state 2 && ok "L2 press handled (screen 2)" || bad "L2 not handled (state='$(state)')"
rm -f "$T/keys"                    # node gone while "unplugged"
sleep 1
kill -0 "$PID" 2>/dev/null && ok "still running after disconnect (no exit, no systemd restart)" || bad "process exited on disconnect"
grep -q "keyboard disconnected" "$T/err.log" && ok "logged the disconnect once" || bad "disconnect not logged"
[ "$(grep -c 'keyboard disconnected' "$T/err.log")" -le 1 ] || bad "disconnect message repeated (log spam)"

mkfifo "$T/keys"                   # keyboard back
press 699                          # L4
wait_state 4 && ok "after reconnect, L4 press handled (screen 4) by the SAME process" || bad "not handled after reconnect (state='$(state)')"
grep -q "is back" "$T/err.log" && ok "logged the reconnect" || bad "reconnect not logged"
kill -0 "$PID" 2>/dev/null && ok "same PID $PID throughout" || bad "PID changed / exited"

# idle while waiting must be ~free: <0.1 s CPU over the whole test
ticks=$(awk '{print $14+$15}' "/proc/$PID/stat" 2>/dev/null || echo 999)
[ "$ticks" -lt 10 ] && ok "CPU while waiting: $ticks ticks (no busy loop)" || bad "CPU $ticks ticks -- busy loop?"

echo
[ $rc -eq 0 ] && echo "buttons reconnect: all passed" || echo "buttons reconnect: FAILED"
exit $rc
