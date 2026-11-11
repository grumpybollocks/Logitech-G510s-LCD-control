#!/bin/bash
# Restarts all three services -- works whether they're stopped, crashed, or
# just stuck/unresponsive (a plain "start" does nothing if a service is
# already "running" but hung; "restart" fixes both cases the same way).
# (2026-11-11) Report what actually happened -- this used to say "restarted"
# even when the restart failed or the services weren't installed at all.
systemctl --user restart g510-lcd-stats.service g510-lcd-buttons.service g510-macro-daemon.service 2>/dev/null
sleep 1
failed=""
for s in g510-lcd-stats g510-lcd-buttons g510-macro-daemon; do
    systemctl --user is-active --quiet "$s.service" || failed="$failed $s"
done
# Popup with whatever this desktop has (2026-11-12): zenity if installed, else a
# normal desktop notification, else plain text -- zenity is optional now.
# Each step falls through if it can't show anything (e.g. zenity installed but
# no display). PyQt5 is always installed (the app needs it), so a window is
# always possible even without zenity or a notification daemon.
tell() {   # $1 = info|error, $2 = text
    local urg=()
    [ "$1" = error ] && urg=(-u critical)
    if command -v zenity >/dev/null; then
        zenity --"$1" --title="G510 LCD" --text="$2" --width=300 2>/dev/null
        [ $? -le 1 ] && return     # 0 = OK, 1 = closed; anything else = couldn't show
    fi
    if command -v notify-send >/dev/null && notify-send -a "G510 LCD" "${urg[@]}" "G510 LCD" "$2" 2>/dev/null; then
        return
    fi
    if python3 - "$1" "$2" 2>/dev/null <<'PY'
import sys
from PyQt5.QtWidgets import QApplication, QMessageBox
app = QApplication([])
(QMessageBox.critical if sys.argv[1] == "error" else QMessageBox.information)(None, "G510 LCD", sys.argv[2])
PY
    then
        return
    fi
    echo "G510 LCD: $2"
}
if [ -z "$failed" ]; then
    tell info "Screen, buttons, and macro daemon restarted."
else
    tell error "Not running:$failed

See the View Logs shortcut (or run ./install.sh if they were never installed)."
    exit 1
fi
