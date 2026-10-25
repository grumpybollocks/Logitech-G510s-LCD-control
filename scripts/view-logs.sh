#!/bin/bash
echo "=== Stats/buttons/macro-daemon logs (Ctrl+C to stop following) ==="
echo "=== Button presses log: ~/.local/share/g510lcd-buttons.log ==="
echo ""
journalctl --user -u g510-lcd-stats.service -u g510-lcd-buttons.service -u g510-macro-daemon.service -f
