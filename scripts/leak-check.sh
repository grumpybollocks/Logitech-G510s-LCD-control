#!/bin/bash
# leak-check.sh [seconds] -- heap-leak regression test for src/g510_lcd_stats.
#
# Runs the program under gdb with glibc's mtrace for N seconds (default 60), then fails (exit 1) if
# memory allocated in the LAST HALF of the run was never freed. Run it after ANY change to the C
# code -- it is what would have caught the 2026-09-21 FreeType canvas leak (1GB/hour, 8GB in 7.5h)
# in a minute instead of a day. Needs: gdb, python3, glibc's libc_malloc_debug.so.0.
#
# NOTE: the LCD goes dark while this runs (the test copy owns the device). The systemd service is
# stopped for the duration and restarted afterwards, even on failure or Ctrl-C.
set -u
SECS="${1:-60}"
DIR="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$DIR/src/g510_lcd_stats"
DEBUGLIB=/usr/lib/libc_malloc_debug.so.0
UNIT=g510-lcd-stats.service

[ -x "$BIN" ] || { echo "no binary at $BIN -- run install.sh first"; exit 2; }
command -v gdb >/dev/null || { echo "gdb not installed"; exit 2; }
[ -e "$DEBUGLIB" ] || { echo "missing $DEBUGLIB (glibc malloc debug library)"; exit 2; }

WORK="$(mktemp -d)"
was_active=0
systemctl --user is-active --quiet "$UNIT" && was_active=1
restore() {
    [ "$was_active" = 1 ] && systemctl --user start "$UNIT"
    rm -rf "$WORK"
}
trap restore EXIT
[ "$was_active" = 1 ] && systemctl --user stop "$UNIT"

cat > "$WORK/trace.gdb" <<EOF
set startup-with-shell off
set pagination off
set confirm off
set environment LD_PRELOAD $DEBUGLIB
set environment MALLOC_TRACE $WORK/mtrace.log
break main
run
call (void)mtrace()
continue
call (void)muntrace()
kill
EOF

echo "tracing $BIN for ${SECS}s ..."
timeout -s INT "$SECS" gdb -q -batch -x "$WORK/trace.gdb" "$BIN" > "$WORK/gdb.out" 2>&1

# 32KB budget: in-flight allocations at cut-off are a few KB; the leak this guards against was ~3.6MB per 30s.
python3 "$DIR/scripts/mtrace_analyze.py" "$WORK/mtrace.log" --max-late-bytes 32768
