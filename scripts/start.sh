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
tell() {   # $1 = info|error, $2 = text
    if command -v zenity >/dev/null; then
        zenity --"$1" --title="G510 LCD" --text="$2" --width=300 2>/dev/null
    elif command -v notify-send >/dev/null; then
        notify-send -a "G510 LCD" $([ "$1" = error ] && echo "-u critical") "G510 LCD" "$2" 2>/dev/null
    else
        echo "G510 LCD: $2"
    fi
}
if [ -z "$failed" ]; then
    tell info "Screen, buttons, and macro daemon restarted."
else
    tell error "Not running:$failed

See the View Logs shortcut (or run ./install.sh if they were never installed)."
    exit 1
fi
