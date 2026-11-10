#!/usr/bin/env python3
"""Tests g510_macro_daemon._wait_for_device() (added 2026-11-10) with a fake
evdev device: retry while missing, reject + close a wrong device, the
'input' group hint on EACCES, one log line per distinct reason, and the
200 ms -> 3 s backoff. No real keyboard, no real sleeping, HOME is a temp
dir so the module's import-time DATA_DIR setup can't touch real files."""
import contextlib
import importlib.util
import io
import os
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
os.environ["HOME"] = tempfile.mkdtemp()

spec = importlib.util.spec_from_file_location("macro", ROOT / "src" / "g510_macro_daemon.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

fails = 0


def check(cond, msg):
    global fails
    print(("PASS " if cond else "FAIL ") + msg)
    fails += 0 if cond else 1


class Dev:
    def __init__(self, name):
        self.name, self.closed = name, False

    def close(self):
        self.closed = True


def run(script):
    """script: list of results for successive InputDevice() calls
    (an exception instance to raise, or a Dev to return)."""
    calls, sleeps = iter(script), []

    def fake_open(path):
        r = next(calls)
        if isinstance(r, BaseException):
            raise r
        return r

    m.evdev.InputDevice, m.time.sleep = fake_open, sleeps.append
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        dev = m._wait_for_device("/dev/g510-keys", disconnected=True)
    return dev, sleeps, out.getvalue()


wrong, right = Dev("Logitech G510s Gaming Keyboard"), Dev(m.DEVICE_NAME)
missing = FileNotFoundError(2, "No such file or directory")
script = [missing] * 60 + [wrong, PermissionError(13, "Permission denied"), right]
dev, sleeps, log = run(script)

check(dev is right, "returns the Gaming Keys device once it appears")
check(wrong.closed, "a device with the wrong name is closed, not used")
check(log.count("No such file or directory") == 1, "the same reason is logged once, not 60 times")
check("different input device" in log, "a wrong device is reported")
check("'input' group" in log, "permission denied comes with the input-group hint")
check(log.count("keyboard disconnected") == 1 and "is back" in log, "disconnect and reconnect logged")
check(sleeps[0] == m.POLL_FAST_S and sleeps[-1] == m.POLL_SLOW_S, f"backs off {sleeps[0]} s -> {sleeps[-1]} s")
fast = sum(1 for s in sleeps if s == m.POLL_FAST_S)
check(abs(fast * m.POLL_FAST_S - m.POLL_FAST_FOR_S) < 0.5, f"fast polling lasts ~{m.POLL_FAST_FOR_S:.0f} s ({fast} polls)")

print("\nmacro wait: " + ("all passed" if not fails else f"{fails} FAILED"))
sys.exit(1 if fails else 0)
