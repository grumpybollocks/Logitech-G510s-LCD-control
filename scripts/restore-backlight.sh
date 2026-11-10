#!/bin/bash
# Restores the last-applied keyboard backlight color on hotplug (boot,
# replug, or the headset jack re-enumerating the keyboard). Run as ROOT
# by 99-g510-lcd.rules whenever the LED device appears.
#
# udev has no per-user $HOME, so this finds the user with the active
# seat0 session via loginctl and reads their saved color from
# ~/.local/share/g510-lcd/set-backlight-color.sh (written by g510_app.py
# on Apply / Set as Default). Single-seat desktop hardware; multi-user
# handling is out of scope.
#
# SECURITY (2026-11-10): that file is writable by the user and anything
# running as them, so it is NEVER executed here -- this used to `exec`
# it as root, a straight user->root escalation on every keyboard
# enumeration. Only the two values are parsed out (strict patterns,
# numbers 0-255) and written by this script itself.
#
# Installed at a FIXED location by both install.sh and the PKGBUILD, so
# the udev rule never needs to know which install method was used.
LED=/sys/class/leds/g15::kbd_backlight

# Optional $1 = user name, for a boot-time unit that runs before anyone
# has logged in (no seat0 session yet). Must be an existing account.
if [ -n "${1:-}" ]; then
    [[ "$1" =~ ^[a-z_][a-z0-9_-]*$ ]] || exit 0
    USER_NAME=$1
else
    USER_NAME=$(loginctl list-sessions --no-legend 2>/dev/null | awk '$4=="seat0"{print $3; exit}')
fi
[ -z "$USER_NAME" ] && exit 0
USER_HOME=$(getent passwd "$USER_NAME" | cut -d: -f6)
[ -z "$USER_HOME" ] && exit 0
SAVED="$USER_HOME/.local/share/g510-lcd/set-backlight-color.sh"
[ -f "$SAVED" ] && [ ! -L "$SAVED" ] || exit 0

byte() { [[ "$1" =~ ^[0-9]{1,3}$ ]] && [ "$1" -le 255 ]; }

BRIGHT=$(sed -nE 's#^echo ([0-9]{1,3}) > /sys/class/leds/g15::kbd_backlight/brightness$#\1#p' "$SAVED" | head -n1)
RGB=$(sed -nE 's#^echo "([0-9]{1,3}) ([0-9]{1,3}) ([0-9]{1,3})" > /sys/class/leds/g15::kbd_backlight/multi_intensity$#\1 \2 \3#p' "$SAVED" | head -n1)

if byte "$BRIGHT"; then echo "$BRIGHT" > "$LED/brightness"; fi
read -r R G B <<< "$RGB"
if byte "$R" && byte "$G" && byte "$B"; then echo "$R $G $B" > "$LED/multi_intensity"; fi
exit 0
