#!/usr/bin/env python3
"""Analyze a glibc mtrace log for heap leaks.

usage: mtrace_analyze.py LOG [--max-late-bytes N]

Startup allocations (fonts, glib, loader) are one-time and all happen EARLY. A real leak keeps
producing unfreed blocks for the whole run, so the number that matters is how much unfreed memory
was allocated during the LAST HALF of the run. Exits 1 if that exceeds --max-late-bytes.
(A few KB is normal: whatever was in flight at the instant the trace was cut off.)
"""
import re, sys, collections

if len(sys.argv) < 2:
    sys.exit(__doc__)
limit = None
if "--max-late-bytes" in sys.argv:
    limit = int(sys.argv[sys.argv.index("--max-late-bytes") + 1])

events = []
for line in open(sys.argv[1], errors="replace"):
    m = re.match(r"@ (\S+) ([+<>-]) (0x[0-9a-f]+)(?: (0x[0-9a-f]+))?", line.rstrip())
    if m:
        events.append(m.groups())
n = len(events)
if n == 0:
    sys.exit("no allocation events found -- did mtrace() run? (needs LD_PRELOAD=libc_malloc_debug.so.0)")

live = {}
for i, (caller, op, ptr, size) in enumerate(events):
    if op in ("+", ">"):
        live[ptr] = (int(size, 16) if size else 0, caller, i)
    elif op in ("-", "<"):
        live.pop(ptr, None)


def summarize(title, items):
    by = collections.defaultdict(lambda: [0, 0])
    for s, c, _ in items:
        by[c][0] += 1
        by[c][1] += s
    total = sum(v[1] for v in by.values())
    print(f"{title}: {sum(v[0] for v in by.values())} allocs, {total:,} bytes")
    for c, (k, b) in sorted(by.items(), key=lambda kv: -kv[1][1])[:6]:
        print(f"    {k:>6} {b:>10,}  {c}")
    return total


print(f"events: {n:,}")
summarize("unfreed, whole run        ", live.values())
late = summarize("unfreed, made in LAST HALF", [v for v in live.values() if v[2] >= n // 2])
if limit is not None:
    if late > limit:
        print(f"LEAK: {late:,} bytes unfreed from the last half of the run (limit {limit:,})")
        sys.exit(1)
    print(f"OK: {late:,} bytes <= limit {limit:,}")
