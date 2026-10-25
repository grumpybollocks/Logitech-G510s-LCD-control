#!/bin/bash
# g510-watch — zero-token overnight watcher for the G510 LCD service (added 2026-09-21).
#   g510-watch            append one reading to ~/.local/state/g510-watch.log (run by a 5-min systemd timer)
#   g510-watch --summary  print min/max/latest memory, restarts and any alerts from the log
# Healthy = rss ~3-6 MB and FLAT. The leak this watches for was +1 GB/hour.
LOG="$HOME/.local/state/g510-watch.log"
ALERTED="$HOME/.local/state/g510-watch.alerted"
mkdir -p "$(dirname "$LOG")"

if [ "${1:-}" = "--summary" ]; then
    [ -s "$LOG" ] || { echo "no log yet: $LOG"; exit 1; }
    awk '
      { for (i=1;i<=NF;i++) { split($i,kv,"="); v[kv[1]]=kv[2] } n++
        r=v["rss_kb"]+0; if (n==1||r<min) min=r; if (r>max) max=r
        a=v["app_rss_kb"]+0; if (a>0) { an++; if (an==1||a<amin) amin=a; if (a>amax) amax=a; if (an==1) afirst=a; alast=a }
        if (n==1) { first=$1; firstr=r }
        last=$1; lastr=r; rs=v["restarts"]; pa=v["parec"]+0; if (pa>maxparec) maxparec=pa
        if (v["state"]!="active") bad++
        if (/ALERT/) alerts++ }
      END { printf "readings: %d   from %s   to %s\n", n, first, last
            printf "g510_lcd_stats RSS: first %d KB, min %d KB, max %d KB, latest %d KB  (growth first->latest: %+d KB)\n", firstr, min, max, lastr, lastr-firstr
            if (an>0) printf "g510_app.py RSS (since field added): first %d KB, min %d KB, max %d KB, latest %d KB  (growth: %+d KB)\n", afirst, amin, amax, alast, alast-afirst
            printf "service restarts: %s   max parec processes seen: %d   readings not active: %d   alerts: %d\n", rs, maxparec, bad+0, alerts+0
            if (max > 30000 || (an>0 && amax > 300000) || maxparec > 1 || bad > 0 || alerts > 0) print "VERDICT: CHECK — something tripped a limit (see ALERT lines: grep ALERT " ENVIRON["HOME"] "/.local/state/g510-watch.log)"
            else print "VERDICT: HEALTHY — flat memory, no restarts, no orphans" }' "$LOG"
    exit 0
fi

U=g510-lcd-stats.service
pid=$(systemctl --user show "$U" -p MainPID --value)
rss=0
[ "${pid:-0}" -gt 0 ] 2>/dev/null && rss=$(ps -o rss= -p "$pid" | tr -d ' ')
state=$(systemctl --user is-active "$U")
restarts=$(systemctl --user show "$U" -p NRestarts --value)
cg=$(systemctl --user show "$U" -p MemoryCurrent --value)
parec=$(pgrep -c -x parec)
# The GUI app (g510_app.py, unmanaged -- no systemd unit) had its own FreeType canvas leak
# fixed 2026-09 (same bug class as g510_lcd_stats); watch its RSS too so a regression there
# shows up here instead of just looking "big" in a system monitor one day.
app_pid=$(pgrep -f -o 'python3 .*g510_app\.py$')
app_rss=0
[ -n "${app_pid:-}" ] && app_rss=$(ps -o rss= -p "$app_pid" 2>/dev/null | tr -d ' ')
app_rss=${app_rss:-0}
tmpfs=$(df -m --output=used "/run/user/$(id -u)" | tail -1 | tr -d ' ')
avail=$(awk '/MemAvailable/{printf "%d",$2/1024}' /proc/meminfo)
swap=$(awk '/SwapTotal/{t=$2} /SwapFree/{f=$2} END{printf "%d",(t-f)/1024}' /proc/meminfo)
top=$(ps -eo rss=,comm= --sort=-rss | head -1 | awk '{printf "%dMB:%s",$1/1024,$2}')

alert=""; key=""
[ "$state" != "active" ]        && { alert="$alert service-$state";          key="$key service-$state"; }
[ "${rss:-0}" -gt 30000 ]       && { alert="$alert rss-over-30MB";           key="$key rss-over-30MB"; }
[ "${parec:-0}" -gt 1 ]         && { alert="$alert orphan-parec($parec)";    key="$key orphan-parec"; }
[ "${restarts:-0}" -gt 0 ]      && { alert="$alert restarts($restarts)";     key="$key restarts"; }
# tmpfs baseline sits ~850MB (portals/shm, not this app) as of 2026-10-25; alert only on real growth.
[ "${tmpfs:-0}" -gt 1200 ]      && { alert="$alert tmpfs-${tmpfs}MB";        key="$key tmpfs-$((tmpfs/100))00MB"; }
# app_rss baseline is ~30-50MB; 300MB is well above that but still catches a leak early.
[ "${app_rss:-0}" -gt 300000 ]  && { alert="$alert app-rss-over-300MB";     key="$key app-rss-over-300MB"; }

echo "$(date +%F_%T) state=$state pid=$pid rss_kb=$rss cg_bytes=$cg restarts=$restarts parec=$parec tmpfs_mb=$tmpfs avail_mb=$avail swap_mb=$swap app_rss_kb=$app_rss top=$top${alert:+ ALERT:$alert}" >> "$LOG"

# One desktop notification per distinct problem category (key strips live numbers so
# jitter, e.g. tmpfs wobbling 847/848/849MB, can't defeat the de-dup and spam every 5 min).
if [ -n "$alert" ]; then
    if [ "$(cat "$ALERTED" 2>/dev/null)" != "$key" ]; then
        printf '%s' "$key" > "$ALERTED"
        notify-send -u critical -i dialog-warning "G510 LCD watcher" "Problem:$alert (rss=${rss}KB). Log: $LOG" 2>/dev/null
    fi
else
    rm -f "$ALERTED"
fi
