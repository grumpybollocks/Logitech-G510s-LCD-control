#!/bin/bash
# Reconnect test for g510_lcd_buttons (added 2026-11-10).
# The G510s drops off USB every time the headset jack is plugged/unplugged;
# the buttons listener must survive that IN-PROCESS (same PID, no systemd
# restart). Simulated without the real keyboard:
#   FIFO as the device          -> a press, then EOF = "disconnect"
#   FIFO unlinked while open    -> node gone at the moment of the disconnect
#                                  (deterministic: no race with the program)
#   empty regular file          -> a node that opens but every read fails at
#                                  once (the re-enumeration case that would
#                                  spin the CPU without the pause)
# Built with G510_KEYS_NAME="" because a FIFO has no evdev name.
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

gcc -Wall -Wextra -DG510_KEYS_DEV="\"$T/keys\"" -DG510_KEYS_NAME='""' \
    src/g510_lcd_buttons.c -o "$T/btn" || { echo "BUILD FAILED"; exit 1; }

# Write one key-down input_event (timeval = 2 longs, u16 type, u16 code,
# s32 value) into the FIFO. With "unplug", the FIFO's name is removed while
# both ends are still open, so the program's EOF finds no node to reopen.
press() {
    timeout 5 python3 -I -c '
import os, struct, sys
path, code, unplug = sys.argv[1], int(sys.argv[2]), sys.argv[3] == "1"
with open(path, "wb") as f:
    if unplug:
        os.unlink(path)
    f.write(struct.pack("llHHi", 0, 0, 1, code, 1))
' "$T/keys" "$1" "${2:-0}"
}
state() { cat "$T/g510lcd_screen" 2>/dev/null; }
wait_state() { for _ in $(seq 50); do [ "$(state)" = "$1" ] && return 0; sleep 0.1; done; return 1; }
ticks() { awk '{print $14+$15}' "/proc/$PID/stat" 2>/dev/null || echo 9999; }

HOME="$T" XDG_RUNTIME_DIR="$T" "$T/btn" 2> "$T/err.log" &
PID=$!
sleep 0.5
kill -0 "$PID" 2>/dev/null && ok "starts and waits when the device doesn't exist yet" || bad "exited with no device"
grep -q "No such file" "$T/err.log" && ok "says why it's waiting (strerror)" || bad "no reason logged: $(cat "$T/err.log")"

mkfifo "$T/keys"
press 697 1                        # L2, then unplug
wait_state 2 && ok "L2 press handled (screen 2)" || bad "L2 not handled (state='$(state)')"
sleep 0.6
kill -0 "$PID" 2>/dev/null && ok "still running after disconnect (no exit, no systemd restart)" || bad "process exited on disconnect"
[ "$(grep -c 'keyboard disconnected' "$T/err.log")" = 1 ] && ok "logged the disconnect exactly once" || bad "disconnect logged $(grep -c 'keyboard disconnected' "$T/err.log") times"

# node that opens but fails every read: must NOT spin the CPU
: > "$T/keys"
l0=$(wc -l < "$T/err.log"); t0=$(ticks); sleep 2; t1=$(ticks); l1=$(wc -l < "$T/err.log")
rm -f "$T/keys"
[ $((t1 - t0)) -lt 20 ] && ok "open-but-unreadable node for 2 s: $((t1 - t0)) CPU ticks (no busy loop)" || bad "busy loop: $((t1 - t0)) ticks in 2 s"
[ $((l1 - l0)) -le 4 ] && ok "flapping node for 2 s: $((l1 - l0)) log lines (backs off, no log spam)" || bad "log spam while flapping: $((l1 - l0)) lines in 2 s"
grep -q "backing off" "$T/err.log" && ok "flapping is reported once" || bad "flapping not reported"

mkfifo "$T/keys"                   # keyboard back
press 699                          # L4
wait_state 4 && ok "after reconnect, L4 press handled (screen 4) by the SAME process" || bad "not handled after reconnect (state='$(state)')"
grep -q "is back" "$T/err.log" && ok "logged the reconnect" || bad "reconnect not logged"
kill -0 "$PID" 2>/dev/null && ok "same PID $PID throughout" || bad "PID changed / exited"

echo
[ $rc -eq 0 ] && echo "buttons reconnect: all passed" || echo "buttons reconnect: FAILED"
exit $rc
