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
if [ -z "$failed" ]; then
    zenity --info --title="G510 LCD" --text="Screen, buttons, and macro daemon restarted." --width=250 2>/dev/null
else
    zenity --error --title="G510 LCD" --text="Not running:$failed\n\nSee the View Logs shortcut (or run ./install.sh if they were never installed)." --width=320 2>/dev/null
    exit 1
fi
