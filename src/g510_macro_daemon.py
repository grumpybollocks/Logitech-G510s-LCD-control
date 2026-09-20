#!/usr/bin/env python3
"""
Watches the G510s's G-keys (G1-G18) and M-keys (M1/M2/M3) on the Gaming
Keys interface, and replays recorded macros via ydotool.

M1/M2/M3 physically switch the LIVE profile (matches real hardware
behavior) -- pressing M2 changes which 18 macros are active immediately.

Macros are stored in macros.json as {"M1": {"G1": {"type": "keys"|"command",
"value": ...}, ...}, ...}. "keys" values are ready-to-use `ydotool key`
argument strings (raw Linux keycode:value pairs); "command" values are
shell commands run directly. Older entries may be a bare string instead
of a dict -- treated as "keys" for backward compatibility.
"""
import json
import subprocess
import time
from pathlib import Path
import evdev
from evdev import ecodes

PROJECT_DIR = Path(__file__).resolve().parent.parent  # repo root (this file lives in src/)

# Same DATA_DIR/migration logic as g510_app.py and g510_lcd_stats.c --
# this daemon is its own systemd --user service, started independently
# at login, so it can't assume the GUI has already run and migrated
# macros.json over. Idempotent either way if both happen to race.
DATA_DIR = Path.home() / ".local" / "share" / "g510-lcd"
DATA_DIR.mkdir(parents=True, exist_ok=True)
_old_macros = PROJECT_DIR / "macros.json"
_new_macros = DATA_DIR / "macros.json"
if not _new_macros.exists() and _old_macros.exists():
    _new_macros.write_bytes(_old_macros.read_bytes())
MACROS_FILE = _new_macros
DEVICE_PATH = "/dev/g510-keys"

G_KEY_CODES = {getattr(ecodes, f"KEY_MACRO{i}"): f"G{i}" for i in range(1, 19)}
M_KEY_CODES = {
    ecodes.KEY_MACRO_PRESET1: "M1",
    ecodes.KEY_MACRO_PRESET2: "M2",
    ecodes.KEY_MACRO_PRESET3: "M3",
}
M_LEDS = {
    "M1": Path("/sys/class/leds/g15::macro_preset1/brightness"),
    "M2": Path("/sys/class/leds/g15::macro_preset2/brightness"),
    "M3": Path("/sys/class/leds/g15::macro_preset3/brightness"),
}


def light_profile_led(active):
    """Lights only the LED for the active profile -- these are simple
    on/off indicators (max_brightness=1), not RGB, separate hardware
    from the keyboard's RGB backlight."""
    for name, path in M_LEDS.items():
        try:
            path.write_text("1" if name == active else "0")
        except Exception:
            pass  # udev rule may not have applied yet on first boot


def load_macros():
    if MACROS_FILE.exists():
        try:
            return json.loads(MACROS_FILE.read_text())
        except Exception:
            pass
    return {"M1": {}, "M2": {}, "M3": {}}


def _wait_for_device(path, poll_seconds=3.0):
    """Real bug found via direct reproduction: evdev.InputDevice(path)
    used to run unguarded in main() -- if the keyboard isn't plugged in
    yet (or the udev rule hasn't applied yet on a fresh boot, a real
    race this project already accounts for elsewhere, e.g.
    light_profile_led's own comment), that's a plain FileNotFoundError,
    and with the systemd unit's Restart=on-failure/RestartSec=3, the
    whole Python process gets killed and relaunched from scratch every
    3 seconds, forever, until someone plugs the keyboard in. Waiting
    here instead means the process starts once and just sits idle
    until the device shows up -- no crash, no repeated interpreter
    startup, no systemd restart spam in the journal."""
    printed = False
    while True:
        try:
            return evdev.InputDevice(path)
        except OSError:
            if not printed:
                print(f"{path} not found yet -- waiting for the keyboard to be plugged in...", flush=True)
                printed = True
            time.sleep(poll_seconds)


def profile_path():
    import os
    runtime = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
    return Path(runtime, "g510_macro_profile")


def write_active_profile(name):
    profile_path().write_text(name)


def main():
    """Real bug, found via live use after the canvas rearchitecture:
    this loop used to be a blocking dev.read_loop() that only ever
    learned about a profile switch from a PHYSICAL M-key press -- a
    GUI click on M1/M2/M3 wrote nothing this daemon could see, so the
    GUI's own poll would revert its highlight back within ~500ms
    (matching the file this daemon owns), while a real physical G-key
    press would still replay whatever THIS daemon's stale in-memory
    active_profile said -- a genuine GUI-shows-one-thing,
    keyboard-does-another mismatch, not just a cosmetic flicker.

    Fixed the same way RecorderThread (g510_app.py) already solved an
    unrelated "need to notice something besides blocking device reads"
    problem: select() with a short timeout instead of a blocking loop,
    so this can also check the profile file for GUI-initiated changes
    a few times a second, and actually adopt them (update the LED,
    update what G-key presses replay) instead of only ever reacting to
    the physical M-keys."""
    import select

    dev = _wait_for_device(DEVICE_PATH)
    pf = profile_path()

    active_profile = "M1"
    write_active_profile(active_profile)
    light_profile_led(active_profile)
    last_mtime = pf.stat().st_mtime

    def adopt_profile(name):
        nonlocal active_profile, last_mtime
        active_profile = name
        light_profile_led(active_profile)
        last_mtime = pf.stat().st_mtime

    while True:
        r, _, _ = select.select([dev.fd], [], [], 0.2)
        if r:
            for event in dev.read():
                if event.type != ecodes.EV_KEY or event.value != 1:  # key-down only
                    continue

                if event.code in M_KEY_CODES:
                    write_active_profile(M_KEY_CODES[event.code])
                    adopt_profile(M_KEY_CODES[event.code])
                    continue

                if event.code in G_KEY_CODES:
                    gkey = G_KEY_CODES[event.code]
                    macros = load_macros()  # reload each time -- app may have just saved a new one
                    entry = macros.get(active_profile, {}).get(gkey)
                    if not entry:
                        continue
                    if isinstance(entry, str):  # legacy format, pre-command-support
                        subprocess.run(["ydotool", "key"] + entry.split())
                    elif entry.get("type") == "command":
                        subprocess.run(entry["value"], shell=True)
                    else:  # type == "keys"
                        subprocess.run(["ydotool", "key"] + entry["value"].split())

        # Pick up a GUI-initiated profile switch too -- the file's
        # mtime only moves when something (us, or the GUI) actually
        # writes it, so this is cheap to check every ~0.2s.
        try:
            mtime = pf.stat().st_mtime
        except FileNotFoundError:
            continue
        if mtime != last_mtime:
            try:
                external = pf.read_text().strip()
            except Exception:
                external = None
            if external in M_LEDS and external != active_profile:
                adopt_profile(external)
            else:
                last_mtime = mtime  # not a valid/new profile -- don't keep re-checking the same write


if __name__ == "__main__":
    main()
