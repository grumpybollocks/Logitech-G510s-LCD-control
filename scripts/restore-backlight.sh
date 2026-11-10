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

# Optional $1 = user name (the login unit passes %u; a boot-time unit can pass
# one too). It must be an existing account -- checked with getent, not a name
# pattern, so names like "Alex" or "alex.tria" (LDAP/sssd) work. A leading "-"
# is refused so it can never be read as an option.
if [ -n "${1:-}" ]; then
    case "$1" in -*) exit 0 ;; esac
    getent passwd "$1" >/dev/null || exit 0
    USER_NAME=$1
else
    # The ACTIVE session on seat0 (not just the first one listed), and only a
    # real user session -- not the login screen's own greeter user (sddm/gdm).
    SID=$(loginctl show-seat seat0 -p ActiveSession --value 2>/dev/null)
    [ -n "$SID" ] || exit 0
    [ "$(loginctl show-session "$SID" -p Class --value 2>/dev/null)" = user ] || exit 0
    USER_NAME=$(loginctl show-session "$SID" -p Name --value 2>/dev/null)
fi
[ -z "$USER_NAME" ] && exit 0
USER_HOME=$(getent passwd "$USER_NAME" | cut -d: -f6)
[ -z "$USER_HOME" ] && exit 0
SAVED="$USER_HOME/.local/share/g510-lcd/set-backlight-color.sh"

# Read it AS THAT USER (2026-11-11): the file and every folder above it are the
# user's, so root reading it directly could be made to block forever (a FIFO),
# scan a huge file, or -- via a symlinked folder -- read something only root
# can. Dropping to the user's own rights, at most 4 KB, at most 5 s, means it
# can only ever see what the user could read anyway. setpriv, not runuser:
# runuser goes through PAM and would open a whole login session (logind
# session, lastlog entry, journal lines) on every keyboard detection.
if [ "$(id -u)" -eq 0 ]; then
    USER_GID=$(id -g "$USER_NAME" 2>/dev/null) || exit 0
    CONTENT=$(timeout 5 setpriv --reuid="$USER_NAME" --regid="$USER_GID" --init-groups --reset-env -- head -c 4096 -- "$SAVED" 2>/dev/null) || exit 0
else
    CONTENT=$(timeout 5 head -c 4096 -- "$SAVED" 2>/dev/null) || exit 0
fi

byte() { [[ "$1" =~ ^[0-9]{1,3}$ ]] && [ "$1" -le 255 ]; }

BRIGHT=$(printf '%s\n' "$CONTENT" | sed -nE 's#^echo ([0-9]{1,3}) > /sys/class/leds/g15::kbd_backlight/brightness$#\1#p' | head -n1)
RGB=$(printf '%s\n' "$CONTENT" | sed -nE 's#^echo "([0-9]{1,3}) ([0-9]{1,3}) ([0-9]{1,3})" > /sys/class/leds/g15::kbd_backlight/multi_intensity$#\1 \2 \3#p' | head -n1)

if byte "$BRIGHT"; then echo "$BRIGHT" > "$LED/brightness"; fi
read -r R G B <<< "$RGB"
if byte "$R" && byte "$G" && byte "$B"; then echo "$R $G $B" > "$LED/multi_intensity"; fi
exit 0
