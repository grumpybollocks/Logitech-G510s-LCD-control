#!/bin/bash
# Restores the last-applied keyboard backlight color on hotplug (boot
# or replug). Run automatically by 99-g510-lcd.rules whenever the LED
# device appears.
#
# Runs as root via udev, which has no per-user $HOME to resolve
# directly -- finds whichever user actually has the active seat0
# session via loginctl instead, then reads their own saved color from
# ~/.local/share/g510-lcd/set-backlight-color.sh (written by
# g510_app.py every time Apply/Set as Default is clicked). This is
# genuinely single-seat desktop hardware; broader multi-user handling
# is out of scope.
#
# Installed at a FIXED location by both install.sh and the PKGBUILD
# (unlike the old __PROJECT_DIR__-substituted approach) so this exact
# file -- and the udev rule line that calls it -- never needs to know
# whether it's a dev checkout or a real package.
USER_NAME=$(loginctl list-sessions --no-legend 2>/dev/null | awk '$4=="seat0"{print $3; exit}')
[ -z "$USER_NAME" ] && exit 0

USER_HOME=$(getent passwd "$USER_NAME" | cut -d: -f6)
[ -z "$USER_HOME" ] && exit 0

SCRIPT="$USER_HOME/.local/share/g510-lcd/set-backlight-color.sh"
[ -x "$SCRIPT" ] && exec "$SCRIPT"
exit 0
