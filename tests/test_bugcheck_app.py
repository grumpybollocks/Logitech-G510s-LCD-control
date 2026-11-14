"""Regression tests for the 2026-11-14 bug-check of the app and the macro service.

Runs offscreen with HOME and XDG_RUNTIME_DIR in a scratch dir (your real
~/.local/share/g510-lcd is never touched); the macro recorder is a fake
thread, so the real keyboard is never grabbed. Run via tests/run-tests.sh.
"""
import json
import os
import sys
import tempfile
import time
import types

SCRATCH = tempfile.mkdtemp(prefix="g510-bugcheck-app-")
os.environ["HOME"] = SCRATCH
os.environ["XDG_RUNTIME_DIR"] = SCRATCH
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))

from PyQt5.QtCore import QThread, pyqtSignal  # noqa: E402
from PyQt5.QtWidgets import QApplication, QMessageBox  # noqa: E402

app = QApplication.instance() or QApplication([])
import g510_app as g  # noqa: E402

passes = fails = 0


def check(cond, msg):
    global passes, fails
    print(("  ok   " if cond else "  FAIL ") + msg)
    passes += bool(cond)
    fails += not cond


# never show real dialogs
QMessageBox.warning = staticmethod(lambda *a, **k: QMessageBox.Ok)
QMessageBox.information = staticmethod(lambda *a, **k: QMessageBox.Ok)
QMessageBox.critical = staticmethod(lambda *a, **k: QMessageBox.Ok)


# ---- 1. closing the record dialog in ANY way stops the recorder (keyboard grab)
class FakeRecorder(QThread):
    finished_recording = pyqtSignal(str, bool)
    instances = []

    def __init__(self):
        super().__init__()
        self._stop = False
        FakeRecorder.instances.append(self)

    def stop(self):
        self._stop = True

    def run(self):
        while not self._stop:
            time.sleep(0.02)
        self.finished_recording.emit("30:1 30:0", True)


g.RecorderThread = FakeRecorder
for action in ("on_clear", "on_save", "on_save_command"):
    dlg = g.MacroRecordDialog("M1", "G1")
    dlg.recorded_sequence = "30:1 30:0"           # a previous take exists
    dlg.save_btn.setEnabled(True)
    dlg.toggle_recording()                         # start a new take
    check(not dlg.save_btn.isEnabled(), f"{action}: Save disabled while a new take is recording")
    rec = FakeRecorder.instances[-1]
    getattr(dlg, action)()
    check(rec.isFinished() and not rec.isRunning(), f"{action}: closing the dialog stopped the recorder (keyboard released)")

# ---- 2. an error in a slot is reported, not fatal
import io  # noqa: E402
_real_err, sys.stderr = sys.stderr, io.StringIO()     # the hook prints the traceback; keep test output clean
try:
    raise OSError(28, "No space left on device")
except OSError:
    g._report_unexpected_error(*sys.exc_info())
finally:
    printed, sys.stderr = sys.stderr.getvalue(), _real_err
check("No space left on device" in printed, "the error is logged with its traceback")
check(True, "unexpected-error hook handles an exception without raising")

# ---- 3 + 4. wrong-shaped / broken macros.json: no crash, nothing lost
for content in ("[]", '{"M1": null}', '{"M1": []}', '{"M1": {"G1": "30:1 30:0"},', ''):
    g.MACROS_BACKUP = None
    g.MACROS_FILE.write_text(content)
    m = g.load_macros()
    shape_ok = all(isinstance(v, dict) for v in m.values()) and {"M1", "M2", "M3"} <= set(m)
    check(shape_ok, f"app load_macros({content!r}) -> usable dict")
    check(g.MACROS_BACKUP is not None and g.MACROS_BACKUP.read_text() == content,
          f"  broken file {content!r} copied aside before anything can overwrite it")
g.MACROS_BACKUP = None
g.MACROS_FILE.write_text('{"M1": {"G1": {"type": "keys", "value": "30:1 30:0"}}, "M2": {}, "M3": {}}')
g.save_macro("M2", "G5", "echo hi", kind="command")
saved = json.loads(g.MACROS_FILE.read_text())
check(saved["M1"]["G1"]["value"] == "30:1 30:0" and saved["M2"]["G5"]["value"] == "echo hi" and g.MACROS_BACKUP is None,
      "a good file is kept as-is, no backup made")

import importlib.util  # noqa: E402
spec = importlib.util.spec_from_file_location(
    "g510_macro_daemon", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "g510_macro_daemon.py"))
daemon = importlib.util.module_from_spec(spec)
spec.loader.exec_module(daemon)
daemon.MACROS_FILE = g.MACROS_FILE
for content in ("[]", '{"M1": null}', "not json"):
    g.MACROS_FILE.write_text(content)
    m = daemon.load_macros()
    check(isinstance(m.get("M1"), dict) and m["M1"].get("G1") is None,
          f"macro service load_macros({content!r}) -> usable dict (was a crash on every G-key)")

# active-profile file: written atomically by both, and actually written
daemon.write_active_profile("M3")
check(daemon.profile_path().read_text() == "M3", "macro service writes the active profile (os imported)")
check(not any(p.endswith(".tmp") for p in os.listdir(SCRATCH)), "no temp files left behind")

# ---- 5. Save All never turns a merely previewed colour into the boot colour
g.DEFAULTS_SCRIPT_DATA_DIR.write_text('#!/bin/bash\necho 255 > /x/brightness\necho "110 0 255" > /x/multi_intensity\n')
fake = types.SimpleNamespace(
    custom_tab=types.SimpleNamespace(config={k: [] for k in g.CUSTOM_SCREEN_KEYS}),
    keyboard_tab=types.SimpleNamespace(_pending_rgb=(255, 0, 0)),
    status=types.SimpleNamespace(showMessage=lambda *a: None))
g.MainWindow.on_save_all(fake)
check('"110 0 255"' in g.DEFAULTS_SCRIPT_DATA_DIR.read_text(), "Save All keeps the chosen boot colour (preview not saved)")

# ---- 6-8. custom_screens.txt: unicode spaces, bad bytes, huge numbers
g.CUSTOM_SCREENS_FILE.write_bytes(
    "SCREEN L2\nTEXT content=Hello World x=1 y=2 font=0\n".encode() +
    b"TEXT content=bad\xffbyte x=1 y=12\n"
    b"ELEMENT sensor=CPU_PCT style=number x=3000000000 y=-99999999999999 width=999999\n"
    b"IMAGE path=a.bin x=1 y=1 width=500 height=8\n")
cfg = g.load_custom_screens()
texts = [e["content"] for e in cfg["L2"] if e["kind"] == "text"]
check("Hello World" in texts, "text with a non-breaking space survives (was cut to 'Hello')")
check(any(t.startswith("bad") for t in texts), "a non-UTF-8 byte doesn't stop the app from loading")
el = next(e for e in cfg["L2"] if e["kind"] == "sensor")
check((el["x"], el["y"], el["width"]) == (g.COORD_MAX, g.COORD_MIN, 40), "huge coordinates/width clamped like the C side")
check(not any(e["kind"] == "image" for e in cfg["L2"]), "an image wider than the LCD is dropped, like the C side")
tab = g.CustomScreensTab()
canvas = tab.preview_canvas
canvas._elements = cfg["L2"]
try:
    canvas._element_at(g.QPoint(5, 5))      # what every mouse move over the preview does
    check(True, "mouse-over with clamped coordinates doesn't overflow")
except OverflowError:
    check(False, "mouse-over with clamped coordinates doesn't overflow")

# ---- 9. image converter: refuses absurd sizes, shrinks big ones cheaply
from PIL import Image  # noqa: E402
import subprocess  # noqa: E402
conv = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "png-to-lcd.py")
big = os.path.join(SCRATCH, "big.png")
Image.new("RGB", (4000, 3000), "white").save(big)       # 12 MP, small file
out = os.path.join(SCRATCH, "big.bin")
r = subprocess.run([sys.executable, "-I", conv, big, out, "60", "43", ], capture_output=True, text=True, timeout=60)
check(r.returncode == 0 and r.stdout.split()[0] == "57", f"12 MP picture converts (to {r.stdout.strip()})")
huge = os.path.join(SCRATCH, "huge.png")
Image.new("1", (9000, 8000)).save(huge)                  # 72 MP, tiny file
r = subprocess.run([sys.executable, "-I", conv, huge, out, "60", "43"], capture_output=True, text=True, timeout=60)
check(r.returncode != 0 and "too large" in r.stderr, "72 MP picture refused with a clear message")

# ---- 10. secondary disk: the app asks which drive, once
from PyQt5.QtWidgets import QFileDialog  # noqa: E402
cfg_file = g.DATA_DIR / "secondary_disk"
if cfg_file.exists():
    cfg_file.unlink()
asked = []
QFileDialog.getExistingDirectory = staticmethod(lambda *a, **k: asked.append(1) or "")
check(tab._choose_secondary_disk() is False and not cfg_file.exists(), "cancelling the drive picker adds nothing")
QFileDialog.getExistingDirectory = staticmethod(lambda *a, **k: asked.append(1) or SCRATCH)
check(tab._choose_secondary_disk() is True and cfg_file.read_text().strip() == SCRATCH, "picked folder saved for the LCD program")
n = len(asked)
check(tab._choose_secondary_disk() is True and len(asked) == n, "not asked again while that folder exists")

# ---- review of the fixes (2026-11-14)
# a: 16-bit grayscale and big transparent palette images still convert
i16 = os.path.join(SCRATCH, "i16.png")
Image.new("I;16", (300, 200), 30000).save(i16)
r = subprocess.run([sys.executable, "-I", conv, i16, out, "60", "43"], capture_output=True, text=True, timeout=60)
check(r.returncode == 0, f"16-bit grayscale PNG converts ({r.stderr.strip()[:60]})")
pal = Image.new("P", (3000, 2000), 0)
pal.putpalette([255, 255, 255, 0, 0, 0] + [0] * 762)
pal.paste(1, (1000, 600, 2000, 1400))                   # black square on a transparent background
pal.info["transparency"] = 0
palp = os.path.join(SCRATCH, "pal.png")
pal.save(palp, transparency=0)
r = subprocess.run([sys.executable, "-I", conv, palp, out, "60", "43"], capture_output=True, text=True, timeout=60)
raw = open(out, "rb").read() if r.returncode == 0 else b""
check(r.returncode == 0 and any(raw) and not all(b == 0xFF for b in raw),
      "big transparent palette image converts (background stays background)")

# b: a SECOND, different broken macros.json gets its own copy too
g.MACROS_BACKUP, g._MACROS_BACKED_UP_SIG, g.MACROS_NEW_BACKUP = None, None, False
g.MACROS_FILE.write_text('{"M1": {"G1": "a"},')
g.load_macros()
first = g.MACROS_BACKUP
g.MACROS_FILE.write_text('{"M1": {"G1": {"type": "keys", "value": "30:1"}}, "M2": {}, "M3": {}}')
g.load_macros()
time.sleep(0.01)
g.MACROS_FILE.write_text('{"M2": {"G9": "b"},,')
g.save_macro("M3", "G1", "echo x", kind="command")
second = g.MACROS_BACKUP
check(first is not None and second is not None and first != second and second.read_text() == '{"M2": {"G9": "b"},,',
      "a later, different broken file is copied aside before the save")
check(g.MACROS_NEW_BACKUP, "and the dialog is told to warn right away")

# c: Save All with no boot-colour file yet (package installs) is not a failure
g.DEFAULTS_SCRIPT_DATA_DIR.unlink()
results = []
QMessageBox.critical = staticmethod(lambda *a, **k: results.append("critical") or QMessageBox.Ok)
QMessageBox.information = staticmethod(lambda *a, **k: results.append("info") or QMessageBox.Ok)
g.MACROS_FILE.write_text('{"M1": {}, "M2": {}, "M3": {}}')
g.MainWindow.on_save_all(fake)
check(results == ["info"], f"Save All with no boot colour chosen yet -> verified, not FAILED ({results})")
g.DEFAULTS_SCRIPT_DATA_DIR.write_text('echo 255 > /x/brightness\necho "999 0 0" > /x/multi_intensity\n')
results.clear()
g.MainWindow.on_save_all(fake)
check(results == ["info"], "an out-of-range saved colour is reported as none chosen, like restore-backlight.sh treats it")

# d: existing secondary-disk screens are asked for the drive once, after updating
if cfg_file.exists():
    cfg_file.unlink()
asked.clear()
QFileDialog.getExistingDirectory = staticmethod(lambda *a, **k: asked.append(1) or "/")
tab2 = g.CustomScreensTab()
tab2.config = {k: [] for k in g.CUSTOM_SCREEN_KEYS}
tab2.config["L3"] = [{"kind": "sensor", "sensor": "DISK_SECONDARY_PCT", "style": "number", "x": 0, "y": 0,
                      "width": 40, "font_size": 0}]
tab2.show()
app.processEvents()
tab2.hide()
tab2.show()
app.processEvents()
check(len(asked) == 1 and cfg_file.read_text().strip() == "/", "a screen already using the secondary disk asks once")

# e: the same error from a timer does not pop up again and again
pops = []
QMessageBox.warning = staticmethod(lambda *a, **k: pops.append(1) or QMessageBox.Ok)
_real_err, sys.stderr = sys.stderr, io.StringIO()
for _ in range(5):
    try:
        raise OSError(5, "same failure")
    except OSError:
        g._report_unexpected_error(*sys.exc_info())
sys.stderr = _real_err
check(len(pops) == 1, f"a repeating error shows one popup, not one per tick ({len(pops)})")

print(f"bugcheck app: {passes} passed, {fails} failed")
sys.exit(1 if fails else 0)
