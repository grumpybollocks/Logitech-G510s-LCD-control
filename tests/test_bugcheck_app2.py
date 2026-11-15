"""Regression tests for the 2026-11-15 bug-check of the app.

Offscreen, HOME and XDG_RUNTIME_DIR in a scratch dir (your real
~/.local/share/g510-lcd is never touched); the recorder runs against a
fake evdev module, so the real keyboard is never opened or grabbed.
Run via tests/run-tests.sh.
"""
import json
import os
import sys
import tempfile
import types
from pathlib import Path

SCRATCH = tempfile.mkdtemp(prefix="g510-bugcheck-app2-")
os.environ["HOME"] = SCRATCH
os.environ["XDG_RUNTIME_DIR"] = SCRATCH
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))

from PyQt5.QtCore import QThread, pyqtSignal  # noqa: E402
from PyQt5.QtWidgets import QApplication, QFileDialog, QMessageBox  # noqa: E402

app = QApplication.instance() or QApplication([])
import g510_app as g  # noqa: E402

passes = fails = 0


def check(cond, msg):
    global passes, fails
    print(("  ok   " if cond else "  FAIL ") + msg)
    passes += bool(cond)
    fails += not cond


QMessageBox.warning = staticmethod(lambda *a, **k: QMessageBox.Ok)
QMessageBox.information = staticmethod(lambda *a, **k: QMessageBox.Ok)
QMessageBox.critical = staticmethod(lambda *a, **k: QMessageBox.Ok)

# ---- 2a. Enter in the record dialog never starts a recording
started = []


class FakeRecorder(QThread):
    finished_recording = pyqtSignal(str, bool)

    def __init__(self):
        super().__init__()
        started.append(self)

    def stop(self):
        pass

    def run(self):
        self.finished_recording.emit("", True)


real_recorder = g.RecorderThread
g.RecorderThread = FakeRecorder
dlg = g.MacroRecordDialog("M1", "G2")
from PyQt5.QtWidgets import QPushButton  # noqa: E402
buttons = dlg.findChildren(QPushButton)
check(buttons and not any(b.autoDefault() or b.isDefault() for b in buttons),
      "no record-dialog button is (auto-)default, so Enter can't press Record")
dlg.command_edit.setText("notify-send hi")
dlg.command_edit.returnPressed.emit()
saved = json.loads(g.MACROS_FILE.read_text()) if g.MACROS_FILE.exists() else {}
check(not started and saved.get("M1", {}).get("G2") == {"type": "command", "value": "notify-send hi"},
      "Enter in the command field saves the command (no recording started)")
g.RecorderThread = real_recorder

# ---- 2b. the recorder waits for every key to be released before grabbing
calls = []


class FakeDev:
    def __init__(self, path, held):
        self.path, self.held = path, held
        self.fd = os.open(os.devnull, os.O_RDONLY)
        self.grabbed_while_held = None

    def active_keys(self):
        calls.append("active")
        return [28] if self.held > 0 else []

    def grab(self):
        calls.append("grab")
        self.grabbed_while_held = self.held > 0
        rec._stop = True          # end the recording loop right away

    def read(self):
        return []

    def ungrab(self):
        pass

    def close(self):
        os.close(self.fd)


fake_evdev = types.ModuleType("evdev")
fake_evdev.ecodes = types.SimpleNamespace(EV_KEY=1)
devs = []
real_evdev = sys.modules.get("evdev")
sys.modules["evdev"] = fake_evdev
real_sleep, real_mono = g.time.sleep, g.time.monotonic
try:
    clock = [1000.0]

    def fake_sleep(s):
        clock[0] += s
        for d in devs:
            d.held -= 1           # Enter is released after a few polls

    g.time.sleep, g.time.monotonic = fake_sleep, lambda: clock[0]

    def make_dev(path, held):
        d = FakeDev(path, held)
        devs.append(d)
        return d

    fake_evdev.InputDevice = lambda path: make_dev(path, 5)
    rec = g.RecorderThread()
    rec.run()                      # synchronously, in this thread
    check(devs[-1].grabbed_while_held is False and "active" in calls,
          "the keyboard is grabbed only after the held Enter key is released")
    calls.clear()
    fake_evdev.InputDevice = lambda path: make_dev(path, 10 ** 6)   # a key stuck down forever
    t0 = clock[0]
    rec = g.RecorderThread()
    rec.run()
    check(calls and calls[-1] == "grab" and 1.9 <= clock[0] - t0 <= 2.2,
          f"a key that never comes up delays the grab only ~2 s ({clock[0] - t0:.2f} s)")
finally:
    g.time.sleep, g.time.monotonic = real_sleep, real_mono
    if real_evdev is not None:
        sys.modules["evdev"] = real_evdev
    else:
        sys.modules.pop("evdev", None)

# ---- 5. the original G510 (different USB product string) is found too
byid = Path(SCRATCH) / "by-id"
byid.mkdir()
real_main = g.MAIN_KEYBOARD_DEVICE
try:
    g.MAIN_KEYBOARD_DEVICE = str(byid / "usb-Logitech_G510s_Gaming_Keyboard-event-kbd")
    (byid / "usb-Logitech_G510_Gaming_Keyboard-event-kbd").touch()
    (byid / "usb-Logitech_G510_Gaming_Keyboard-event-if01").touch()
    check(g.main_keyboard_device() == str(byid / "usb-Logitech_G510_Gaming_Keyboard-event-kbd"),
          "G510 (no 's') keyboard device found by glob")
    Path(g.MAIN_KEYBOARD_DEVICE).touch()
    check(g.main_keyboard_device() == g.MAIN_KEYBOARD_DEVICE, "the exact G510s name is still preferred")
finally:
    g.MAIN_KEYBOARD_DEVICE = real_main

# ---- 4. a long (multi-byte) picture name still fits the LCD program's path buffer
from PIL import Image  # noqa: E402
tab = g.CustomScreensTab()
tab.select_screen("L2")
long_name = Path(SCRATCH) / ("Ünïcödé_řeally_lóng_picture_name_" * 5 + ".png")
Image.new("RGB", (40, 20), "white").save(long_name)
QFileDialog.getOpenFileName = staticmethod(lambda *a, **k: (str(long_name), ""))
tab.on_import_image()
tab.on_import_image()
imgs = [el for el in tab.config["L2"] if el.get("kind") == "image"]
paths = [el["path"] for el in imgs]
check(len(imgs) == 2 and all(len(p.encode()) <= 127 for p in paths),
      f"imported image paths fit 127 bytes ({[len(p.encode()) for p in paths]})")
check(len(set(paths)) == 2 and all((g.DATA_DIR / p).exists() for p in paths),
      "two imports of the same long name still get distinct files")
check(all(p.encode().decode("utf-8") for p in paths), "the name is cut on a UTF-8 character boundary")

# ---- 9. an image resize that finishes after a screen switch is still saved
el = imgs[0]


class DoneWorker:
    def wait(self):
        pass


tab._image_resize_worker = DoneWorker()
tab._image_resize_screen = "L2"
tab._image_resize_el = el
tab._image_resize_next = None
tab.select_screen("L3")
index = tab.config["L2"].index(el)
tab._on_image_resize_finished(index, 30, 15, True)
saved_text = g.CUSTOM_SCREENS_FILE.read_text()
check(el["width"] == 30 and el["height"] == 15 and f"path={el['path']} width=30 height=15" in saved_text,
      "new width/height applied to the screen the resize started on, and saved")
other = {"kind": "image", "path": "x.bin", "src": None, "x": 0, "y": 0, "width": 10, "height": 10}
tab.config["L2"].insert(0, other)            # the original element is no longer at `index`
tab._image_resize_worker = DoneWorker()
tab._image_resize_screen = "L2"
tab._image_resize_el = el
tab._on_image_resize_finished(index, 50, 25, True)
check(other["width"] == 10 and el["width"] == 30, "identity check still stops a misapplied resize")
tab.config["L2"].remove(other)

print(f"\nbugcheck app (2026-11-15): {passes} passed, {fails} failed")
sys.exit(1 if fails else 0)
