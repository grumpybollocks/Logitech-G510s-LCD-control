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

# 2026-11-15: command macros must not live in the macro service's cgroup
# (KillMode=control-group killed every G-key-launched app on a service
# restart) -> own transient unit via systemd-run, direct launch as fallback.
class Proc:
    def __init__(self, args):
        self.args, self.rc = args, None

    def poll(self):
        return self.rc


launched = []


def fake_popen(args, **kw):
    launched.append((args, kw))
    return Proc(args)


real_popen, real_which = m.subprocess.Popen, m.shutil.which
m.subprocess.Popen = fake_popen
try:
    m.shutil.which = lambda name: "/usr/bin/" + name
    p, fb = m.launch_command("firefox --new-window")
    args, kw = launched[-1]
    check(isinstance(args, list) and args[:2] == ["systemd-run", "--user"] and "--collect" in args
          and args[-3:] == ["sh", "-c", "firefox --new-window"] and not kw.get("shell"),
          "a command macro is started in its own systemd-run --user unit")
    check(fb == "firefox --new-window", "the command is kept for a fallback launch")
    p.rc = 0
    check(m.reap_running([(p, fb)]) == [] and len(launched) == 1, "a successful launcher is just reaped")
    p.rc = 1
    with contextlib.redirect_stdout(io.StringIO()):
        still = m.reap_running([(p, fb)])
    check(len(launched) == 2 and launched[-1][0] == "firefox --new-window"
          and launched[-1][1].get("shell") and len(still) == 1 and still[0][1] is None,
          "a failed systemd-run falls back to a direct launch (once)")
    m.shutil.which = lambda name: None
    p, fb = m.launch_command("gimp")
    check(launched[-1][0] == "gimp" and launched[-1][1].get("shell") and fb is None,
          "no systemd-run on PATH -> direct launch")
except AttributeError as e:
    check(False, f"command launch helpers missing: {e}")
finally:
    m.subprocess.Popen, m.shutil.which = real_popen, real_which

print("\nmacro wait: " +("all passed" if not fails else f"{fails} FAILED"))
sys.exit(1 if fails else 0)
