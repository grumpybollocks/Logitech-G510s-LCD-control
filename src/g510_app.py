#!/usr/bin/env python3
"""
G510 LCD Control App.

Architecture: one QTabWidget, one tab per feature. Adding a new feature
= adding a new tab class + one line in MainWindow.__init__. Nothing in
an existing tab needs to change when a new one is added.

Phase 1: Backlight tab (color/brightness + service control).
         G-Keys tab (record/assign macros to G1-G18 per M1/M2/M3 profile).
Phase 2: Custom Screens tab (AIDA64-style sensor dashboard builder for
         the L2-L5 buttons, with a live preview of the real LCD output).
"""
import sys
import json
import shutil
import subprocess
import datetime
from pathlib import Path

from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QTabWidget, QWidget, QVBoxLayout,
    QHBoxLayout, QGridLayout, QComboBox, QPushButton, QLabel,
    QMessageBox, QDialog, QLineEdit, QSpinBox, QFrame, QScrollArea,
    QFileDialog, QInputDialog, QStatusBar,
)
from PyQt5.QtCore import Qt, QThread, pyqtSignal, QTimer, QRect, QPoint
from PyQt5.QtGui import QImage, QPixmap, QColor, QPainter, QPen, QFontMetrics
import evdev
from evdev import ecodes

import g510_canvas

PROJECT_DIR = Path(__file__).resolve().parent.parent  # repo root (this file lives in src/)
LED_DIR = Path("/sys/class/leds/g15::kbd_backlight")
DEFAULTS_SCRIPT = PROJECT_DIR / "scripts" / "set-backlight-color.sh"
# The udev rule (99-g510-lcd.rules) has this exact path baked in at
# install time (see install.sh), so this location can't just move --
# doing that alone, without also re-running install.sh's sudo udev
# step, would leave the live udev rule pointing at a script that stops
# getting updated (replugging the keyboard would silently restore a
# stale color). Also writing the same content to DATA_DIR so a future
# packaged install (whose /usr/lib/g510-lcd is root-owned -- this
# PROJECT_DIR-relative write would fail there) has a real, current
# copy ready once its own udev rule points at the right place -- a
# deliberately deferred gap, documented in packaging/g510-lcd/README.md.
# (DEFAULTS_SCRIPT_DATA_DIR is set further down, once DATA_DIR exists.)
MAIN_KEYBOARD_DEVICE = "/dev/input/by-id/usb-Logitech_G510s_Gaming_Keyboard-event-kbd"
STATS_BINARY = PROJECT_DIR / "src" / "g510_lcd_stats"

# Your own macros/custom-screens config and imported images live under
# ~/.local/share/g510-lcd, independent of where the program itself is
# installed from (a dev checkout via install.sh, or a real package
# under a fixed /usr/lib/g510-lcd) -- so a package upgrade never
# touches what you've actually configured. Mirrors the identical
# data_dir()/migrate_*_if_needed() logic in g510_lcd_stats.c -- both
# sides need to agree on this path independently since the C binary
# and this GUI both read/write the same files without talking to each
# other directly.
DATA_DIR = Path.home() / ".local" / "share" / "g510-lcd"
DATA_DIR.mkdir(parents=True, exist_ok=True)


def _migrate_file_if_needed(old_path: Path, new_path: Path):
    if new_path.exists() or not old_path.exists():
        return
    new_path.write_bytes(old_path.read_bytes())


def _migrate_dir_if_needed(old_dir: Path, new_dir: Path):
    if not old_dir.is_dir():
        return
    new_dir.mkdir(parents=True, exist_ok=True)
    for f in old_dir.iterdir():
        if f.is_file():
            _migrate_file_if_needed(f, new_dir / f.name)


_migrate_file_if_needed(PROJECT_DIR / "custom_screens.txt", DATA_DIR / "custom_screens.txt")
_migrate_file_if_needed(PROJECT_DIR / "macros.json", DATA_DIR / "macros.json")
_migrate_dir_if_needed(PROJECT_DIR / "custom_screen_images", DATA_DIR / "custom_screen_images")

MACROS_FILE = DATA_DIR / "macros.json"
CUSTOM_SCREENS_FILE = DATA_DIR / "custom_screens.txt"
DEFAULTS_SCRIPT_DATA_DIR = DATA_DIR / "set-backlight-color.sh"
LCD_WIDTH = 160
LCD_HEIGHT = 43

# Shared dark theme, copied verbatim from the sibling G910 app's
# g910_app.py for visual consistency across the two projects.
STYLESHEET = """
QWidget {
    background-color: #17171a;
    color: #e4e4e7;
    font-family: sans-serif;
}
QTabWidget::pane {
    border: 1px solid #2a2a30;
    border-radius: 6px;
    top: -1px;
}
QTabBar::tab {
    background: #1e1e22;
    border: 1px solid #2a2a30;
    padding: 8px 18px;
    margin-right: 2px;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
}
QTabBar::tab:selected {
    background: #26262b;
    border-bottom-color: #26262b;
    color: white;
}
QPushButton {
    background-color: #26262b;
    border: 1px solid #34343a;
    border-radius: 6px;
    padding: 7px 10px;
    text-align: left;
}
QPushButton:hover {
    background-color: #302f36;
    border-color: #46454e;
}
QPushButton:checked {
    background-color: #3a6cc4;
    border-color: #5a8ce0;
    color: white;
}
QPushButton#Primary {
    background-color: #3a6cc4;
    border-color: #5a8ce0;
    color: white;
    text-align: center;
    font-weight: 600;
    padding: 9px 10px;
}
QPushButton#Primary:hover {
    background-color: #4a7cd4;
    border-color: #6a9cf0;
}
QPushButton#PanelPrimary {
    background-color: #3a6cc4;
    border-color: #5a8ce0;
    color: white;
    text-align: center;
    font-weight: 600;
    font-size: 11px;
    padding: 4px 8px;
}
QPushButton#PanelPrimary:hover {
    background-color: #4a7cd4;
    border-color: #6a9cf0;
}
QPushButton#PanelButton {
    font-size: 11px;
    padding: 4px 8px;
    text-align: center;
}
QLabel#Title {
    font-size: 15px;
    font-weight: 600;
    padding: 4px 2px 10px 2px;
}
QWidget#Panel {
    background-color: #1c1c20;
    border: 1px solid #2a2a30;
    border-radius: 8px;
}
QFrame#Separator {
    background-color: #2a2a30;
    border: none;
    max-width: 1px;
    min-width: 1px;
}
QLabel#Status {
    color: #9a9aa2;
    font-size: 11px;
    padding-top: 4px;
}
QPushButton#Discreet {
    background-color: transparent;
    border: 1px solid #2a2a30;
    color: #8a8a92;
    font-size: 11px;
    padding: 4px 6px;
    text-align: center;
}
QPushButton#Discreet:hover {
    background-color: #26262b;
    color: #c0c0c6;
    border-color: #3a3a42;
}
QLineEdit {
    background-color: #1e1e22;
    border: 1px solid #34343a;
    border-radius: 4px;
    padding: 5px;
}
"""


def active_profile_file():
    import os
    runtime = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
    return Path(runtime, "g510_macro_profile")


def read_active_screen_num():
    """Mirrors g510_lcd_buttons.c's screen_state_path()/read_screen() --
    the same runtime state file g510_lcd_buttons.c writes and
    g510_lcd_stats.c itself reads to decide what to actually draw, so
    this reflects reality rather than a guess.

    Corrected after a real report ("the hardcoded L1 screen, not the
    clock, the stats, are not visible") -- 0 and 1 are NOT both "the
    clock". Reading g510_lcd_stats.c's own screen-select logic
    directly: `screen == 1` draws the clock, `2 <= screen <= 5` draws
    a custom L2-L5 screen, and everything else -- which in practice
    means 0, the state before any L-button has ever been pressed --
    draws the CPU/RAM/VRAM/TEMP stats screen. An earlier version of
    this function collapsed 0 into 1, so the mirror silently showed
    the clock instead of stats whenever the real hardware was on 0.
    render_preview()/--preview accept 0-5 directly (same atoi(argv[2])
    the live daemon's read_screen() feeds into), so no translation is
    needed here beyond passing the real value through unchanged.

    Falls back to 0 (stats -- the real default) for anything
    unreadable: state file missing (service not running yet),
    malformed, or a stray value outside the real 0-5 range."""
    import os
    runtime = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
    try:
        s = int(Path(runtime, "g510lcd_screen").read_text().strip())
    except Exception:
        return 0
    return s if 0 <= s <= 5 else 0

COLOR_RGB = {
    "Blue-Violet": (110, 0, 255),
    "Red": (255, 0, 0),
    "Green": (0, 255, 0),
    "Blue": (0, 0, 255),
    "Purple": (128, 0, 128),
    "Cyan": (0, 255, 255),
    "Orange": (255, 100, 0),
    "Pink": (255, 0, 150),
    "White": (255, 255, 255),
}


def read_current_rgb():
    try:
        r, g, b = (int(x) for x in (LED_DIR / "multi_intensity").read_text().split())
        return (r, g, b)
    except Exception:
        return None


def write_defaults_script(rgb, brightness_val):
    script = f"""#!/bin/bash
# Applies the chosen keyboard backlight color. Run automatically by
# 99-g510-lcd.rules whenever the LED device appears (boot or replug).
# Auto-updated by g510_app.py every time you click Apply or Set as Default.
echo {brightness_val} > /sys/class/leds/g15::kbd_backlight/brightness
echo "{rgb[0]} {rgb[1]} {rgb[2]}" > /sys/class/leds/g15::kbd_backlight/multi_intensity
"""
    # The currently-installed udev rule has DEFAULTS_SCRIPT's path baked
    # in, so it has to keep being written for replug-restore to keep
    # working -- but this fails with PermissionError in a packaged
    # install (/usr/lib/g510-lcd is root-owned), which is expected
    # there, not a real error to surface.
    try:
        DEFAULTS_SCRIPT.write_text(script)
        DEFAULTS_SCRIPT.chmod(0o755)
    except PermissionError:
        pass
    DEFAULTS_SCRIPT_DATA_DIR.write_text(script)
    DEFAULTS_SCRIPT_DATA_DIR.chmod(0o755)



# Brightness used to be user-adjustable here, but real testing (writing
# 50/128/200/100 to the sysfs brightness file and reading each back
# immediately) confirmed it's pinned at max_brightness (255) regardless
# of what's written -- a real kernel/driver-level limitation on this
# hardware, not a display bug. Removed rather than kept as dead,
# misleading UI; MAX_BRIGHTNESS documents why 255 is hardcoded below,
# not guessed.
MAX_BRIGHTNESS = 255


def apply_backlight(rgb):
    """Writes the LED sysfs files AND rewrites set-backlight-color.sh so
    this becomes the new permanent boot default (matches the behavior
    already established by g510-backlight-apply.sh -- same contract).
    Takes a real (r, g, b) tuple directly rather than a preset name --
    the picker UI now also accepts an arbitrary hex color, not just
    the fixed COLOR_RGB presets, so a name-keyed lookup here would
    reject anything typed by hand."""
    try:
        (LED_DIR / "multi_intensity").write_text(f"{rgb[0]} {rgb[1]} {rgb[2]}")
        (LED_DIR / "brightness").write_text(str(MAX_BRIGHTNESS))
    except PermissionError as e:
        return False, f"Permission denied writing to {LED_DIR} -- check the udev rule (99-g510-lcd.rules) is installed: {e}"

    write_defaults_script(rgb, MAX_BRIGHTNESS)
    return True, None


def set_as_default(rgb):
    """Persist-only: updates set-backlight-color.sh (the boot default)
    WITHOUT touching the live backlight right now -- distinct from
    Apply, which does both. Lets you keep previewing other colors live
    without losing a default you've already decided on. Same (r, g, b)
    tuple convention as apply_backlight()."""
    write_defaults_script(rgb, MAX_BRIGHTNESS)
    return True, None


ALL_SERVICES = [
    "g510-lcd-stats.service",
    "g510-lcd-buttons.service",
    "g510-macro-daemon.service",
]


def run_systemctl(action):
    try:
        subprocess.run(
            ["systemctl", "--user", action] + ALL_SERVICES,
            check=True, capture_output=True, text=True,
        )
        return True, None
    except subprocess.CalledProcessError as e:
        return False, e.stderr or str(e)


MACRO_RECORD_LED = Path("/sys/class/leds/g15::macro_record/brightness")


def read_macro_record_led():
    try:
        return int(MACRO_RECORD_LED.read_text().strip()) > 0
    except Exception:
        return False


def load_macros():
    if MACROS_FILE.exists():
        try:
            return json.loads(MACROS_FILE.read_text())
        except Exception:
            pass
    return {"M1": {}, "M2": {}, "M3": {}}


def save_macro(profile, gkey, value, kind="keys"):
    """kind is 'keys' (a ydotool key-sequence string) or 'command' (a
    shell command string). Stored as {"type": ..., "value": ...} --
    g510_macro_daemon.py checks 'type' to decide how to replay it."""
    macros = load_macros()
    macros.setdefault(profile, {})[gkey] = {"type": kind, "value": value}
    MACROS_FILE.write_text(json.dumps(macros, indent=2))


def clear_macro(profile, gkey):
    macros = load_macros()
    macros.setdefault(profile, {}).pop(gkey, None)
    MACROS_FILE.write_text(json.dumps(macros, indent=2))


class RecorderThread(QThread):
    """Captures real keystrokes from the main keyboard while recording,
    grabbing the device so they don't also leak into whatever window has
    focus. Emits the final ydotool-ready 'code:value code:value ...'
    string when stopped, plus an ok flag -- same convention as
    ImageResizeWorker's finished_resize(..., bool) below, not a
    separate error signal.

    Real bug found via direct reproduction (crashed the whole process,
    SIGABRT, twice): evdev.InputDevice(MAIN_KEYBOARD_DEVICE) used to run
    unguarded -- if the keyboard isn't plugged in (or MAIN_KEYBOARD_DEVICE
    is ever stale), the exception escaped this QThread.run() entirely,
    which PyQt5 does not route through Python's normal exception
    handling -- it aborts the process. ImageResizeWorker (added later,
    see its own docstring) already wraps its whole run() in try/except
    for exactly this reason; this thread predates that pattern and
    never got the same treatment."""
    finished_recording = pyqtSignal(str, bool)  # sequence, ok

    def __init__(self):
        super().__init__()
        self._stop = False
        self._events = []

    def stop(self):
        self._stop = True

    def run(self):
        import select
        try:
            dev = evdev.InputDevice(MAIN_KEYBOARD_DEVICE)
            dev.grab()
        except OSError:
            self.finished_recording.emit("", False)
            return
        try:
            while not self._stop:
                # Poll with a short timeout instead of a blocking read --
                # read_loop() only checks _stop between events, so it can
                # hang forever if the user stops without pressing another
                # key. This checks _stop every 0.1s regardless.
                r, _, _ = select.select([dev.fd], [], [], 0.1)
                if not r:
                    continue
                for event in dev.read():
                    if event.type == ecodes.EV_KEY:
                        self._events.append(f"{event.code}:{event.value}")
        finally:
            dev.ungrab()
            dev.close()
        self.finished_recording.emit(" ".join(self._events), True)


class MacroRecordDialog(QDialog):
    def __init__(self, profile, gkey, parent=None):
        super().__init__(parent)
        self.profile = profile
        self.gkey = gkey
        self.recorder = None
        self.recorded_sequence = None

        self.setWindowTitle(f"{profile} / {gkey}")
        layout = QVBoxLayout()

        macros = load_macros()
        existing = macros.get(profile, {}).get(gkey)
        if isinstance(existing, dict) and existing.get("type") == "command":
            status_text = f"Currently runs: {existing.get('value', '')}"
        elif existing:
            status_text = "Currently assigned (recorded keystrokes)."
        else:
            status_text = "Nothing assigned yet."
        self.status_label = QLabel(status_text)
        layout.addWidget(self.status_label)

        self.record_btn = QPushButton("Record")
        self.record_btn.clicked.connect(self.toggle_recording)
        layout.addWidget(self.record_btn)

        btn_row = QHBoxLayout()
        self.save_btn = QPushButton("Save")
        self.save_btn.clicked.connect(self.on_save)
        self.save_btn.setEnabled(False)
        clear_btn = QPushButton("Clear")
        clear_btn.clicked.connect(self.on_clear)
        cancel_btn = QPushButton("Cancel")
        cancel_btn.clicked.connect(self.reject)
        btn_row.addWidget(self.save_btn)
        btn_row.addWidget(clear_btn)
        btn_row.addWidget(cancel_btn)
        layout.addLayout(btn_row)

        layout.addWidget(QLabel("<b>Or run a command instead:</b>"))
        cmd_row = QHBoxLayout()
        self.command_edit = QLineEdit()
        self.command_edit.setPlaceholderText("e.g. notify-send hello")
        if isinstance(existing, dict) and existing.get("type") == "command":
            self.command_edit.setText(existing.get("value", ""))
        save_cmd_btn = QPushButton("Save Command")
        save_cmd_btn.clicked.connect(self.on_save_command)
        cmd_row.addWidget(self.command_edit)
        cmd_row.addWidget(save_cmd_btn)
        layout.addLayout(cmd_row)

        self.setLayout(layout)

    def toggle_recording(self):
        if self.recorder is None:
            self.status_label.setText("Recording... press your key combo, then click Stop.")
            self.record_btn.setText("Stop")
            self.recorder = RecorderThread()
            self.recorder.finished_recording.connect(self.on_recorded)
            self.recorder.start()
        else:
            self.record_btn.setEnabled(False)  # ignore clicks until the thread actually finishes
            self.status_label.setText("Stopping...")
            self.recorder.stop()

    def reject(self):
        if self.recorder is not None:
            self.recorder.stop()
            self.recorder.wait(2000)  # let it clean up (ungrab) before the dialog closes
        super().reject()

    def on_recorded(self, sequence, ok):
        self.recorded_sequence = sequence
        if self.recorder is not None:
            # The signal can arrive just before Qt finishes tearing down
            # the OS thread -- wait() blocks until that's truly done
            # before we drop the last Python reference. Skipping this
            # causes "QThread: Destroyed while thread is still running"
            # and a hard process abort (confirmed -- this crashed twice).
            self.recorder.wait()
        self.recorder = None
        self.record_btn.setText("Record")
        self.record_btn.setEnabled(True)
        if not ok:
            self.status_label.setText("Keyboard not found -- is it plugged in?")
            self.save_btn.setEnabled(False)
            return
        self.status_label.setText(f"Captured {len(sequence.split())} events. Click Save to keep it.")
        self.save_btn.setEnabled(bool(sequence))

    def on_save(self):
        if self.recorded_sequence:
            save_macro(self.profile, self.gkey, self.recorded_sequence, kind="keys")
        self.accept()

    def on_save_command(self):
        cmd = self.command_edit.text().strip()
        if cmd:
            save_macro(self.profile, self.gkey, cmd, kind="command")
        self.accept()

    def on_clear(self):
        clear_macro(self.profile, self.gkey)
        self.accept()


class KeyboardTab(QWidget):
    """Unified Backlight + G-Keys view: a real keyboard-shaped canvas
    (see g510_canvas.py) next to a control panel, same overall pattern
    as the sibling G910 app's canvas+sidebar layout -- adapted here for
    a single-zone backlight (one live color for the whole board, not
    per-key) and 18 G-keys instead of 9.

    G-keys on the canvas open the existing macro-record dialog
    unchanged; M1/M2/M3 switch the active profile unchanged; a poll
    timer keeps the canvas's active-M-key highlight and MR indicator in
    sync with the daemon/hardware, exactly like the old GKeysTab did."""
    def __init__(self):
        super().__init__()
        self.current_profile = "M1"

        root = QHBoxLayout()

        canvas_col = QVBoxLayout()
        self.canvas = g510_canvas.G510Canvas()
        current_rgb = read_current_rgb()
        if current_rgb:
            self.canvas.set_board_color(QColor(*current_rgb))
        self.canvas.gkey_clicked.connect(self.open_key_dialog)
        self.canvas.mkey_clicked.connect(self.select_profile)
        self.refresh_assigned_keys()
        canvas_col.addWidget(self.canvas)

        hint = QLabel("Click a G-key to record a macro  •  click M1/M2/M3 to switch profiles  •  gold border = macro assigned")
        hint.setObjectName("Status")
        canvas_col.addWidget(hint)
        canvas_col.addStretch()
        root.addLayout(canvas_col, stretch=1)

        panel = QWidget()
        panel.setObjectName("Panel")
        panel.setFixedWidth(220)
        panel_layout = QVBoxLayout()

        title = QLabel("Backlight")
        title.setObjectName("Title")
        panel_layout.addWidget(title)

        # Swatch-grid + hex-entry picker, matching the sibling G910
        # app's own picker (direct request: "a colour picker like you
        # did for g910") -- replaces the old plain dropdown. Doesn't
        # apply anything by itself; sets self._pending_rgb, which the
        # existing Apply/Set as Default buttons below act on, keeping
        # G510s's own already-working live/persist distinction (G910's
        # picker has no separate "default" concept to preserve).
        self._pending_rgb = current_rgb or next(iter(COLOR_RGB.values()))

        panel_layout.addWidget(QLabel("Color"))
        self.preview_swatch = QLabel()
        self.preview_swatch.setFixedHeight(26)
        self._set_preview_style(self._pending_rgb)
        panel_layout.addWidget(self.preview_swatch)

        panel_layout.addSpacing(6)
        panel_layout.addWidget(QLabel("<b>Hex code</b>"))
        hex_row = QHBoxLayout()
        self.hex_edit = QLineEdit()
        self.hex_edit.setPlaceholderText("8000ff")
        self.hex_edit.returnPressed.connect(self.on_apply_hex)
        hex_apply_btn = QPushButton("Apply")
        hex_apply_btn.setObjectName("PanelPrimary")
        hex_apply_btn.clicked.connect(self.on_apply_hex)
        hex_row.addWidget(self.hex_edit)
        hex_row.addWidget(hex_apply_btn)
        panel_layout.addLayout(hex_row)

        panel_layout.addSpacing(8)
        panel_layout.addWidget(QLabel("Presets"))
        swatch_grid = QGridLayout()
        swatch_grid.setSpacing(4)
        for i, (name, rgb) in enumerate(COLOR_RGB.items()):
            swatch_btn = QPushButton()
            swatch_btn.setToolTip(name)
            swatch_btn.setFixedSize(28, 28)
            hexval = "#%02x%02x%02x" % rgb
            swatch_btn.setStyleSheet(
                f"background-color: {hexval}; border: 1px solid #34343a; border-radius: 4px;"
            )
            swatch_btn.clicked.connect(lambda _, c=rgb: self.set_pending_color(c))
            swatch_grid.addWidget(swatch_btn, i // 3, i % 3)
        swatch_row = QHBoxLayout()
        swatch_row.addStretch()
        swatch_row.addLayout(swatch_grid)
        swatch_row.addStretch()
        panel_layout.addLayout(swatch_row)
        panel_layout.addSpacing(4)

        apply_btn = QPushButton("Apply")
        apply_btn.setObjectName("PanelPrimary")
        apply_btn.clicked.connect(self.on_apply)
        panel_layout.addWidget(apply_btn)
        set_default_btn = QPushButton("Set as Default")
        set_default_btn.setObjectName("PanelButton")
        set_default_btn.clicked.connect(self.on_set_as_default)
        panel_layout.addWidget(set_default_btn)

        sep = QFrame()
        sep.setFrameShape(QFrame.HLine)
        sep.setObjectName("Separator")
        panel_layout.addSpacing(10)
        panel_layout.addWidget(sep)
        panel_layout.addSpacing(10)

        service_label = QLabel("Service Control")
        service_label.setObjectName("Status")
        panel_layout.addWidget(service_label)
        service_row = QHBoxLayout()
        service_row.setSpacing(4)
        for label, action in (("Start", "start"), ("Stop", "stop"), ("Restart", "restart")):
            btn = QPushButton(label)
            btn.setObjectName("Discreet")
            btn.clicked.connect(lambda _, a=action: self.on_service_action(a))
            service_row.addWidget(btn)
        panel_layout.addLayout(service_row)

        panel_layout.addStretch()
        panel.setLayout(panel_layout)
        root.addWidget(panel)

        self.setLayout(root)

        # Physical M1/M2/M3 presses and the MR LED are hardware/daemon
        # state this GUI doesn't own -- poll and reflect them, same
        # pattern as the old GKeysTab's profile poll timer.
        self.poll_timer = QTimer(self)
        self.poll_timer.timeout.connect(self.poll_hardware_state)
        self.poll_timer.start(500)

        # Live-mirrors whatever's actually showing on the physical LCD
        # into the canvas's LCD cell. Separate, much slower timer than
        # poll_hardware_state above -- this one shells out to the real
        # C binary each tick (render_preview()), so 500ms would mean 2
        # subprocess spawns/sec for a screen that doesn't need to be
        # that responsive; 2s roughly matches the real stats refresh
        # cadence (see G510_README.md) instead.
        self.lcd_mirror_timer = QTimer(self)
        self.lcd_mirror_timer.timeout.connect(self.refresh_lcd_mirror)
        self.lcd_mirror_timer.start(2000)
        self.refresh_lcd_mirror()  # don't wait 2s for the first frame

    def select_profile(self, name):
        self.current_profile = name
        self.canvas.set_active_mkey(name)
        self.refresh_assigned_keys()
        # Real bug, found via live use: clicking M1/M2/M3 here used to
        # be GUI-only -- the daemon (which decides what a physical
        # G-key press actually replays) never learned about it, and
        # the poll below would revert the highlight right back within
        # 500ms. Writing the same file the daemon itself writes makes
        # this a real profile switch, not a cosmetic one -- and makes
        # this call idempotent when poll_hardware_state calls it after
        # reading an unchanged file (same value written back, harmless).
        try:
            active_profile_file().write_text(name)
        except Exception:
            pass  # matches the daemon's own best-effort LED write

    def refresh_assigned_keys(self):
        """Which G-keys have a macro in the CURRENT profile -- drawn
        with a gold border on the canvas so it's visible at a glance,
        without opening each key's dialog to check."""
        assigned = load_macros().get(self.current_profile, {}).keys()
        self.canvas.set_assigned_keys(assigned)

    def refresh_lcd_mirror(self):
        """Renders whatever screen is actually active right now
        (read_active_screen_num()) and pushes it into the canvas's LCD
        cell -- same render_preview() the Custom Screens editor uses,
        so this is provably pixel-identical to the real hardware, not
        a guess. A render failure (binary not built yet, transient
        error) just leaves the canvas showing its last good frame (or
        the placeholder, before the first one) rather than clearing it
        -- a missed tick shouldn't blank a screen that was fine a
        moment ago."""
        pixmap, _bounds, err = render_preview(read_active_screen_num(), tag="canvas")
        if pixmap is not None:
            self.canvas.set_lcd_pixmap(pixmap)

    def poll_hardware_state(self):
        try:
            live = active_profile_file().read_text().strip()
        except Exception:
            live = None
        if live in ("M1", "M2", "M3") and live != self.current_profile:
            self.select_profile(live)
        self.canvas.set_mr_active(read_macro_record_led())

    def open_key_dialog(self, gkey):
        dlg = MacroRecordDialog(self.current_profile, gkey, self)
        dlg.exec_()
        self.refresh_assigned_keys()  # a macro may have been saved or cleared

    def _set_preview_style(self, rgb):
        hexval = "#%02x%02x%02x" % rgb
        self.preview_swatch.setStyleSheet(
            f"background-color: {hexval}; border: 1px solid #34343a; border-radius: 4px;"
        )

    def set_pending_color(self, rgb):
        """Updates the preview swatch + what Apply/Set as Default will
        act on -- does NOT touch the live backlight itself. Called by
        clicking a preset swatch or a valid hex Apply, same as G910's
        picker, but G510s keeps its own separate live/persist step
        rather than applying instantly on every click."""
        self._pending_rgb = rgb
        self._set_preview_style(rgb)

    def on_apply_hex(self):
        """Exact-value path alongside the presets, for anyone who
        already knows the hex they want or wants to match a color
        precisely rather than pick from the fixed preset list."""
        text = self.hex_edit.text().strip().lstrip("#")
        if len(text) != 6 or any(c not in "0123456789abcdefABCDEF" for c in text):
            QMessageBox.critical(self, "Invalid color", "Use 6 hex digits, e.g. 8000ff")
            return
        rgb = tuple(int(text[i:i + 2], 16) for i in (0, 2, 4))
        self.set_pending_color(rgb)

    def on_apply(self):
        ok, err = apply_backlight(self._pending_rgb)
        if not ok:
            QMessageBox.critical(self, "Error", err)
            return
        self.canvas.set_board_color(QColor(*self._pending_rgb))

    def on_set_as_default(self):
        ok, err = set_as_default(self._pending_rgb)
        if not ok:
            QMessageBox.critical(self, "Error", err)

    def on_service_action(self, action):
        ok, err = run_systemctl(action)
        if not ok:
            QMessageBox.critical(self, "Error", err)


# --- Custom Screens (v1.1): dashboards for L2-L5, mirrors the sensor
# table in g510_lcd_stats.c's SENSORS[] array. Keep the two in sync if a
# sensor is ever added or renamed -- there's no shared source of truth
# because one side is C and the other Python, by design (no JSON/IPC
# schema needed for something this small).
SENSOR_CHOICES = [
    ("CPU_PCT", "CPU %"),
    ("CPU_GHZ", "CPU GHz"),
    ("CPU_TEMP", "CPU Temp"),
    ("RAM_PCT", "RAM %"),
    ("RAM_AMOUNT", "RAM Used (amount)"),
    ("VRAM_PCT", "VRAM %"),
    ("VRAM_AMOUNT", "VRAM Used (amount)"),
    ("MAXTEMP", "Max Temp Seen"),
    ("GPU_PCT", "GPU %"),
    ("GPU_EDGE_TEMP", "GPU Edge Temp"),
    ("GPU_HOTSPOT_TEMP", "GPU Hotspot Temp"),
    ("GPU_VRAM_TEMP", "GPU VRAM Temp"),
    ("SWAP_PCT", "Swap %"),
    ("DISK_ROOT_PCT", "Disk % (root)"),
    ("DISK_SECONDARY_PCT", "Disk % (secondary)"),
    ("UPTIME", "Uptime"),
    ("NET_DOWN", "Network Download Speed"),
    ("NET_UP", "Network Upload Speed"),
    ("TIME", "Time"),
    ("DATE", "Date"),
    ("MEDIA_TITLE", "Media: Song Title"),
    ("MEDIA_ARTIST", "Media: Artist"),
    ("MEDIA_ELAPSED", "Media: Time Elapsed"),
    ("MB_TEMP1", "Motherboard Temp 1 (unlabeled)"),
    ("MB_TEMP2", "Motherboard Temp 2 (unlabeled)"),
    ("MB_TEMP3", "Motherboard Temp 3 (unlabeled)"),
    ("MB_TEMP4", "Motherboard Temp 4 (unlabeled)"),
    ("MB_TEMP5", "Motherboard Temp 5 (unlabeled)"),
    ("MB_TEMP6", "Motherboard Temp 6 (unlabeled)"),
]
SENSOR_LABELS = dict(SENSOR_CHOICES)

# Direct feedback: "the functions are weird, i dont know from that
# list what is what and exactly what they do." Plain-language
# one-liners shown live under the sensor dropdown -- the ones that
# aren't self-evident (MAXTEMP, the GPU Edge/Hotspot/VRAM temp split,
# the unlabeled motherboard sensors) are worded from the *real*
# semantics in g510_lcd_stats.c (checked directly, not assumed): MAXTEMP
# is "since this program started", not all-time; Edge vs Hotspot are
# two distinct, real AMD GPU sensors, not a duplicate/typo.
SENSOR_DESCRIPTIONS = {
    "CPU_PCT": "Overall CPU load, averaged across all cores.",
    "CPU_GHZ": "Current CPU clock speed.",
    "CPU_TEMP": "CPU package temperature.",
    "RAM_PCT": "RAM used, as a percentage of total.",
    "RAM_AMOUNT": "RAM used, as an actual amount (e.g. 12.3 GB).",
    "VRAM_PCT": "GPU VRAM used, as a percentage of total.",
    "VRAM_AMOUNT": "GPU VRAM used, as an actual amount.",
    "MAXTEMP": "Highest CPU temp seen since the LCD service last started -- not an all-time record.",
    "GPU_PCT": "GPU utilization.",
    "GPU_EDGE_TEMP": "The GPU die's main temperature sensor -- what most tools just call \"GPU temp\".",
    "GPU_HOTSPOT_TEMP": "The single hottest point on the GPU die -- usually a bit higher than Edge Temp.",
    "GPU_VRAM_TEMP": "GPU memory (VRAM) temperature -- separate from the GPU die itself.",
    "SWAP_PCT": "Swap space used, as a percentage of total swap.",
    "DISK_ROOT_PCT": "Disk space used on your root (/) filesystem.",
    "DISK_SECONDARY_PCT": "Disk space used on an optional secondary drive, if you have one configured.",
    "UPTIME": "How long this PC has been running since boot.",
    "NET_DOWN": "Current network download speed.",
    "NET_UP": "Current network upload speed.",
    "TIME": "Current time (same clock as the L1 screen).",
    "DATE": "Current date (same clock as the L1 screen).",
    "MEDIA_TITLE": "Currently playing song title, via any MPRIS-compatible player (Spotify, browser tabs, VLC, etc). Blank if nothing's playing.",
    "MEDIA_ARTIST": "Currently playing artist, same source as Media: Song Title.",
    "MEDIA_ELAPSED": "Playback position, e.g. \"1:23/3:45\". Blank if nothing's playing.",
    "MB_TEMP1": "One of your motherboard's temperature sensors -- your hardware doesn't label what it measures.",
    "MB_TEMP2": "One of your motherboard's temperature sensors -- your hardware doesn't label what it measures.",
    "MB_TEMP3": "One of your motherboard's temperature sensors -- your hardware doesn't label what it measures.",
    "MB_TEMP4": "One of your motherboard's temperature sensors -- your hardware doesn't label what it measures.",
    "MB_TEMP5": "One of your motherboard's temperature sensors -- your hardware doesn't label what it measures.",
    "MB_TEMP6": "One of your motherboard's temperature sensors -- your hardware doesn't label what it measures.",
}

# Only sensors with an honest 0-100 scale (a true percent, or a
# temperature via the 0-90C convention already used on the main stats
# screen) can be shown as a bar -- matches is_percent/is_temp in the C
# SENSORS[] table exactly. Anything else offered as "Bar" would need a
# guessed scale, which we don't do.
BAR_CAPABLE_SENSORS = {
    "CPU_PCT", "CPU_TEMP", "RAM_PCT", "VRAM_PCT", "MAXTEMP",
    "GPU_PCT", "GPU_EDGE_TEMP", "GPU_HOTSPOT_TEMP", "GPU_VRAM_TEMP",
    "SWAP_PCT", "DISK_ROOT_PCT", "DISK_SECONDARY_PCT",
    "MB_TEMP1", "MB_TEMP2", "MB_TEMP3", "MB_TEMP4", "MB_TEMP5", "MB_TEMP6",
}

# Renamed from DISK_FRIGIDER_PCT (a personal nickname for one specific
# drive) to a generic name -- this alias means an existing
# custom_screens.txt element saved under the old key still loads and
# renders correctly instead of silently disappearing; the very next
# auto-save (this app saves on every edit, no separate Save button)
# rewrites it under the new key, a one-time silent migration, same
# pattern as this project's other old-path/old-key migrations.
LEGACY_SENSOR_ALIASES = {"DISK_FRIGIDER_PCT": "DISK_SECONDARY_PCT"}

CUSTOM_SCREEN_KEYS = ["L2", "L3", "L4", "L5"]
# L1 is the built-in clock screen (drawn by draw_clock_screen() in C,
# not draw_custom_screen()) -- it has no ELEMENT lines of its own and
# was never meant to be edited here. Included in the tab's screen
# selector as a read-only preview only, per user request ("make a
# preview panel for L1 too") -- kept separate from CUSTOM_SCREEN_KEYS
# so load/save_custom_screens (which only know about real SCREEN
# blocks) are untouched.
SCREEN_PREVIEW_KEYS = ["L1"] + CUSTOM_SCREEN_KEYS


MAX_IMAGES_PER_SCREEN = 2  # matches MAX_IMAGES in g510_lcd_stats.c
MAX_TEXTS_PER_SCREEN = 4  # matches MAX_TEXTS in g510_lcd_stats.c
MAX_VISUALIZERS_PER_SCREEN = 1  # matches MAX_VISUALIZERS in g510_lcd_stats.c -- only one persistent audio-capture stream exists process-wide, a second visualizer element would just duplicate the same bar data
CUSTOM_SCREENS_PANEL_WIDTH = 230  # single source of truth -- also used to compute how much a row's label can show before it needs eliding, see refresh_elements_list()
DEFAULT_VISUALIZER_WIDTH = 70
DEFAULT_VISUALIZER_HEIGHT = 37
TEXT_CHAR_PX = 6  # rough estimate for G15_TEXT_SMALL's per-character width -- no real font-metric access from Python for freeform text (same reason sensor elements originally needed the .meta sidecar), but text elements deliberately skip that mechanism (see on_add_text's docstring) so this stays an approximation, not pixel-perfect

# Direct request: "id like to be able to resize elements". libg15render
# only has 4 discrete built-in text sizes (checked the header: no
# continuous scaling exists), so "resize" for Number/Text elements
# means picking one of these, not a drag handle. Names shown in the
# UI; values are g510_lcd_stats.c's G15_TEXT_SMALL/MED/LARGE/HUGE (0-3).
FONT_SIZE_CHOICES = [(0, "Small"), (1, "Medium"), (2, "Large"), (3, "Huge")]
# Self-audit finding: a bar's own height stays fixed regardless of its
# value text's size (BAR_H doesn't scale), so Large/Huge next to a bar
# rendered as a big chunky number next to a thin line -- correct, just
# visually unbalanced. Bar values are capped at Medium; Number/Text
# elements can still use the full range.
MAX_BAR_FONT_SIZE = 1
# Real measured width/height scale relative to SMALL (rendered "88" on
# an actual canvas and scanned the lit-pixel bounding box, same
# technique used throughout this project for font metrics -- not
# guessed): SMALL=7x5, MED=9x6, LARGE=15x7, HUGE=13x10. Used only to
# scale TEXT_CHAR_PX/ELEMENT_HIT_H for freeform text's own estimated
# hit-box (see _element_rect) -- sensor elements get their bounds from
# the C-side .meta sidecar instead, unaffected by this.
FONT_SIZE_SCALE = {0: (1.0, 1.0), 1: (9 / 7, 6 / 5), 2: (15 / 7, 7 / 5), 3: (13 / 7, 10 / 5)}

CUSTOM_SCREEN_IMAGES_DIR = DATA_DIR / "custom_screen_images"


def load_custom_screens():
    """Returns {"L2": [ {kind:"sensor",sensor,style,x,y,width} or
    {kind:"image",path,x,y,width,height}, ... ], "L3": [...], ...}"""
    config = {k: [] for k in CUSTOM_SCREEN_KEYS}
    if not CUSTOM_SCREENS_FILE.exists():
        return config
    current = None
    for line in CUSTOM_SCREENS_FILE.read_text().splitlines():
        line = line.strip()
        if line.startswith("SCREEN "):
            key = line.split(" ", 1)[1]
            current = key if key in config else None
        elif line.startswith("ELEMENT ") and current:
            el = {"kind": "sensor", "sensor": "", "style": "number", "x": 0, "y": 0, "width": 40, "font_size": 0}
            for tok in line[len("ELEMENT "):].split():
                if "=" not in tok:
                    continue
                k, v = tok.split("=", 1)
                if k in ("x", "y", "width"):
                    try:
                        el[k] = int(v)
                    except ValueError:
                        pass
                elif k == "font":
                    try:
                        el["font_size"] = int(v) if int(v) in (0, 1, 2, 3) else 0
                    except ValueError:
                        pass
                elif k in ("sensor", "style"):
                    el[k] = v
            if el["sensor"]:
                el["sensor"] = LEGACY_SENSOR_ALIASES.get(el["sensor"], el["sensor"])
                config[current].append(el)
        elif line.startswith("IMAGE ") and current:
            # "src" is optional and only present for images imported
            # since drag-resize was added: a copy of the original
            # source picture (not the already-downscaled/dithered
            # .bin) kept alongside it so a resize can re-convert from
            # real source quality instead of upscaling a 1-bit bitmap.
            # Older IMAGE lines without src= parse fine -- src just
            # stays absent, and those images fall back to no resize
            # handle (same as today: re-import to change size).
            im = {"kind": "image", "path": "", "x": 0, "y": 0, "width": 0, "height": 0, "src": None}
            for tok in line[len("IMAGE "):].split():
                if "=" not in tok:
                    continue
                k, v = tok.split("=", 1)
                if k in ("x", "y", "width", "height"):
                    try:
                        im[k] = int(v)
                    except ValueError:
                        pass
                elif k in ("path", "src"):
                    im[k] = v
            if im["path"] and im["width"] > 0 and im["height"] > 0:
                config[current].append(im)
        elif line.startswith("TEXT ") and current:
            # Underscores decode to spaces -- matches g510_lcd_stats.c's
            # own decoding exactly, keeps the space-delimited line
            # parser on both sides simple (no quoted-string handling).
            tx = {"kind": "text", "content": "", "x": 0, "y": 0, "font_size": 0}
            for tok in line[len("TEXT "):].split():
                if "=" not in tok:
                    continue
                k, v = tok.split("=", 1)
                if k in ("x", "y"):
                    try:
                        tx[k] = int(v)
                    except ValueError:
                        pass
                elif k == "font":
                    try:
                        tx["font_size"] = int(v) if int(v) in (0, 1, 2, 3) else 0
                    except ValueError:
                        pass
                elif k == "content":
                    tx[k] = v.replace("_", " ")
            if tx["content"]:
                config[current].append(tx)
        elif line.startswith("VISUALIZER ") and current:
            vz = {"kind": "visualizer", "x": 0, "y": 0, "width": 0, "height": 0}
            for tok in line[len("VISUALIZER "):].split():
                if "=" not in tok:
                    continue
                k, v = tok.split("=", 1)
                if k in ("x", "y", "width", "height"):
                    try:
                        vz[k] = int(v)
                    except ValueError:
                        pass
            if vz["width"] > 0 and vz["height"] > 0:
                config[current].append(vz)
    return config


def save_custom_screens(config):
    lines = []
    for key in CUSTOM_SCREEN_KEYS:
        lines.append(f"SCREEN {key}")
        for el in config[key]:
            if el.get("kind") == "image":
                src_part = f" src={el['src']}" if el.get("src") else ""
                lines.append(
                    f"IMAGE path={el['path']} width={el['width']} height={el['height']} "
                    f"x={el['x']} y={el['y']}{src_part}"
                )
            elif el.get("kind") == "text":
                # Spaces -> underscores, matching exactly what
                # load_custom_screens() and g510_lcd_stats.c both
                # decode back. A literal underscore the user typed
                # will round-trip as a space on reload -- a known,
                # accepted limitation (documented in
                # load_custom_screens()), not silent data corruption.
                content = el["content"].replace(" ", "_") or "_"
                lines.append(f"TEXT content={content} x={el['x']} y={el['y']} font={el.get('font_size', 0)}")
            elif el.get("kind") == "visualizer":
                lines.append(f"VISUALIZER x={el['x']} y={el['y']} width={el['width']} height={el['height']}")
            else:
                width = el.get("width", 40)
                lines.append(
                    f"ELEMENT sensor={el['sensor']} style={el['style']} "
                    f"x={el['x']} y={el['y']} width={width} font={el.get('font_size', 0)}"
                )
    CUSTOM_SCREENS_FILE.write_text("\n".join(lines) + "\n")


def _parse_bounds_meta(meta_path):
    """Parses the .meta sidecar --preview writes alongside the image for
    custom screens: real per-element pixel bounds computed with the
    label font's actual glyph metrics, which Python has no way to
    compute itself. Returns {index: {"label_x1":.., ..., "bar_x1":.. (bar
    elements only)}}. Missing/unreadable file -> empty dict, callers
    fall back to an approximation rather than crashing."""
    bounds = {}
    try:
        text = meta_path.read_text()
    except Exception:
        return bounds
    for line in text.splitlines():
        parts = line.split()
        if not parts:
            continue
        try:
            idx = int(parts[0])
        except ValueError:
            continue
        entry = {}
        for tok in parts[1:]:
            if "=" not in tok:
                continue
            k, v = tok.split("=", 1)
            try:
                entry[k] = int(v)
            except ValueError:
                pass
        bounds[idx] = entry
    return bounds


def render_preview(screen_num, tag="app"):
    """Runs the same C binary that draws the real LCD, in one-shot
    --preview mode, and returns (QPixmap, bounds, err) -- the pixmap is
    guaranteed pixel-identical to what the real screen shows, since
    it's the same drawing code. bounds is real per-element pixel
    geometry (see _parse_bounds_meta), empty dict for non-custom
    screens or if the sidecar wasn't written.

    `tag` picks the output file so independent callers (the Custom
    Screens editor's own live preview, the canvas's LCD-mirror
    thumbnail) never race on the same /tmp file -- two callers hitting
    this at once with the same tag could still interleave a partial
    read; distinct tags avoid that entirely rather than relying on
    both finishing fast enough not to matter."""
    if not STATS_BINARY.exists():
        return None, {}, "Not built yet -- run install.sh or rebuild the C programs."
    out_path = Path(f"/tmp/g510_app_preview_{tag}.ppm")
    meta_path = Path(str(out_path) + ".meta")
    try:
        result = subprocess.run(
            [str(STATS_BINARY), "--preview", str(screen_num), str(out_path)],
            capture_output=True, text=True, timeout=5,
        )
        if result.returncode != 0:
            return None, {}, f"Preview render failed: {result.stderr.strip()}"
    except Exception as e:
        return None, {}, f"Couldn't run preview: {e}"

    try:
        data = out_path.read_bytes()
    except Exception as e:
        return None, {}, f"Couldn't read preview output: {e}"

    # Minimal hand-rolled P6 PPM parser -- avoids depending on Qt's
    # optional ppm plugin being present on whatever system this runs on.
    if not data.startswith(b"P6"):
        return None, {}, "Preview output wasn't a valid PPM image."
    parts = data.split(b"\n", 3)
    if len(parts) < 4:
        return None, {}, "Malformed PPM header."
    try:
        w, h = (int(x) for x in parts[1].split())
    except ValueError:
        return None, {}, "Malformed PPM header."
    pixels = parts[3]
    img = QImage(w, h, QImage.Format_RGB888)
    if len(pixels) < w * h * 3:
        return None, {}, "Truncated PPM data."
    for y in range(h):
        row_start = y * w * 3
        img.scanLine(y)  # ensure detach
        for x in range(w):
            i = row_start + x * 3
            img.setPixel(x, y, (pixels[i] << 16) | (pixels[i + 1] << 8) | pixels[i + 2])
    bounds = _parse_bounds_meta(meta_path)
    return QPixmap.fromImage(img), bounds, None


PREVIEW_SCALE = 4
# Approximate click/drag hit box per element, in real LCD pixels (not
# scaled) -- there's no exact per-element width/height available from
# Python (the C renderer computes real text width using font metrics
# Python doesn't have access to), so this is a deliberately generous
# fixed size rather than pixel-perfect. Good enough to click and drag
# elements that are reasonably spaced, which is the normal case for an
# 8-element-max, 160x43 screen.
ELEMENT_HIT_W = 50
ELEMENT_HIT_H = 10
# Direct report: "the handle is always too small, or i cant find it"
# -- was 5 (a 40x40 screen-px target at PREVIEW_SCALE=4), bumped up
# for a meaningfully bigger, easier-to-land-on square, both visually
# (easier to spot once hovering reveals it) and for the actual click
# hit-test (same rect drives both).
RESIZE_HANDLE_PX = 8   # half-size of the little resize square, in LCD px
MIN_BAR_WIDTH = 10
MAX_BAR_WIDTH = 140
MIN_IMAGE_SIZE = 4     # smallest an image can be dragged down to, in LCD px


class ScreenPreviewCanvas(QWidget):
    """The live LCD preview, but interactive: click and drag an element
    to move it, or drag its resize handle (bar elements only) to change
    the bar's length -- both re-render the preview (throttled) as you
    go, same underlying --preview mechanism as before, just wired to
    mouse events instead of typed X/Y/width numbers."""
    element_moved = pyqtSignal(int, int, int)   # index, new_x, new_y (LCD-space)
    element_resized = pyqtSignal(int, int)        # index, new_width (LCD-space) -- bars only
    image_resize_requested = pyqtSignal(int, int, int)  # index, target_width, target_height (LCD-space)
    drag_started = pyqtSignal()
    drag_finished = pyqtSignal()

    def __init__(self):
        super().__init__()
        self._pixmap = None
        self._elements = []
        self._bounds = {}   # index -> real pixel geometry from the C renderer, see _parse_bounds_meta
        self._drag_index = None
        self._drag_offset = QPoint(0, 0)
        self._resize_index = None
        self._resize_kind = None  # "bar" or "image" -- which resize gesture is active
        self._resize_start_x = 0
        self._resize_start_width = 0
        # Live target size while dragging an image's corner handle --
        # separate from el["width"/"height"] on purpose: the actual
        # bitmap only gets regenerated by a background ImageResizeWorker
        # (see CustomScreensTab), so mutating the element's real size
        # here would let the drawn dashed box (and then the next
        # --preview render) claim a size the .bin file on disk doesn't
        # have yet -- same class of bug the atomic tmp+rename write fix
        # addressed on the C side, just on the Python/file side instead.
        # This is purely the visual target for the dashed selection box
        # while the real regeneration catches up in the background.
        self._image_resize_preview_wh = None
        self._hover_index = None  # bar element the mouse is currently near -- only ITS handle is drawn
        self.setFixedSize(LCD_WIDTH * PREVIEW_SCALE, LCD_HEIGHT * PREVIEW_SCALE)
        self.setMouseTracking(True)
        self.setCursor(Qt.ArrowCursor)

    def set_data(self, pixmap, elements, bounds=None):
        self._pixmap = pixmap
        self._elements = elements
        self._bounds = bounds or {}
        self.update()

    def _element_rect(self, index):
        el = self._elements[index]
        if el.get("kind") in ("image", "visualizer"):
            # Both kinds know their own exact size directly (image: set
            # at import time by png-to-lcd.py; visualizer: just a plain
            # x/y/width/height box, no font-metric unknown either) --
            # no need for the C-reported .meta bounds sensor elements need.
            return QRect(el["x"] * PREVIEW_SCALE, el["y"] * PREVIEW_SCALE,
                         el["width"] * PREVIEW_SCALE, el["height"] * PREVIEW_SCALE)
        if el.get("kind") == "text":
            # Estimated box (TEXT_CHAR_PX per character) -- same
            # category of approximation images already use, since
            # there's no real font-metric measurement for freeform
            # text (see on_add_text's docstring for why that's a
            # deliberate scope decision, not an oversight). Scaled by
            # FONT_SIZE_SCALE's real measured width/height ratios so a
            # Medium/Large/Huge text field's hit-box roughly tracks its
            # actual larger size instead of staying stuck at Small's.
            w_ratio, h_ratio = FONT_SIZE_SCALE.get(el.get("font_size", 0), (1.0, 1.0))
            est_w = max(20, round(len(el["content"]) * TEXT_CHAR_PX * w_ratio))
            est_h = round(ELEMENT_HIT_H * h_ratio)
            return QRect(el["x"] * PREVIEW_SCALE, el["y"] * PREVIEW_SCALE,
                         est_w * PREVIEW_SCALE, est_h * PREVIEW_SCALE)
        b = self._bounds.get(self._sensor_rank(index))
        if b and "label_x1" in b:
            # Real geometry: covers the label through the real end of
            # the VALUE TEXT (which for bar-style is drawn AFTER the
            # bar, not the bar's own edge) -- direct report: "the blue
            # bars... dont contain the whole variable element(name)".
            # value_x2 is a REAL measured width from the C side (see
            # measure_builtin_text_width() in g510_lcd_stats.c) -- the
            # old code used bar_x2 (missing bar-style's own trailing
            # value text entirely) or a fixed "+30" guess for
            # number-style, which badly undershot real long values
            # like a song title. Falls back to the old guess only if
            # value_x2 is somehow missing (e.g. a stale .meta from
            # before this field existed).
            x1, y1 = b["label_x1"], b["label_y1"]
            x2 = b.get("value_x2", b["label_x2"] + 30)
            y2 = max(b["label_y2"], b.get("bar_y2", 0))
            return QRect(x1 * PREVIEW_SCALE, y1 * PREVIEW_SCALE,
                         (x2 - x1) * PREVIEW_SCALE, (y2 - y1) * PREVIEW_SCALE)
        # Fallback for the brief window before the first real preview
        # has come back (or if the metadata sidecar is ever missing) --
        # approximate, not pixel-accurate, but never crashes.
        return QRect(
            el["x"] * PREVIEW_SCALE, el["y"] * PREVIEW_SCALE,
            ELEMENT_HIT_W * PREVIEW_SCALE, ELEMENT_HIT_H * PREVIEW_SCALE,
        )

    def _sensor_rank(self, index):
        """Real bug found by self-audit: g510_lcd_stats.c's --preview
        mode draws sensors, images, and text in three SEPARATE loops
        (see draw_custom_screen()), each with its own counter -- the
        .meta bounds sidecar's indices are therefore a sensor's rank
        among SENSOR elements only (0, 1, 2...), not its position in
        this combined sensor+image+text list. Looking bounds up by the
        raw combined index silently returns wrong (or missing) data for
        any sensor that has a text/image element before it in the list
        -- confirmed by building a sensor+image+sensor screen and
        diffing the returned rect against the real C-measured bounds
        directly, not assumed. This computes the correct lookup key."""
        return sum(1 for e in self._elements[:index] if e.get("kind", "sensor") == "sensor")

    def _resize_handle_rect(self, index):
        el = self._elements[index]
        b = self._bounds.get(self._sensor_rank(index))
        r = RESIZE_HANDLE_PX * PREVIEW_SCALE
        if el.get("kind") in ("image", "visualizer"):
            # Bottom-right corner of the element's own known box -- no
            # font-metric guess needed here (same reasoning as
            # _element_rect's image/visualizer case above).
            hx = (el["x"] + el["width"]) * PREVIEW_SCALE
            hy = (el["y"] + el["height"]) * PREVIEW_SCALE
            return QRect(hx - r, hy - r, r * 2, r * 2)
        if b and "bar_x2" in b:
            # Exactly where the real bar ends -- this is the fix for
            # the handle drifting away from the actual bar, reported
            # directly as the preview being "hard to control."
            hx = b["bar_x2"] * PREVIEW_SCALE
            hy = ((b["bar_y1"] + b["bar_y2"]) * PREVIEW_SCALE) // 2
            return QRect(hx - r, hy - r, r * 2, r * 2)
        hx = el["x"] * PREVIEW_SCALE + ELEMENT_HIT_W * PREVIEW_SCALE
        hy = el["y"] * PREVIEW_SCALE + (ELEMENT_HIT_H * PREVIEW_SCALE) // 2
        return QRect(hx - r, hy - r, r * 2, r * 2)

    def _element_at(self, pos):
        # Last element in the list is drawn/added most recently -- check
        # in reverse so an overlapping newer element wins, matching what
        # you'd visually expect to grab.
        for i in range(len(self._elements) - 1, -1, -1):
            if self._element_rect(i).contains(pos):
                return i
        return None

    def _resizable(self, el):
        if el.get("style") == "bar":
            return True
        if el.get("kind") == "visualizer":
            # Always resizable -- just a plain box, no re-conversion
            # from a source file needed the way images require.
            return True
        # Only images imported since drag-resize was added carry a
        # "src" (a kept copy of the real source picture) -- an older
        # image element has none, and re-converting from its own
        # already-downscaled 1-bit .bin would just upscale a dithered
        # bitmap into something worse, not a real resize. No handle
        # offered for those; re-importing still works as before.
        return el.get("kind") == "image" and bool(el.get("src"))

    def _resize_handle_at(self, pos):
        for i in range(len(self._elements) - 1, -1, -1):
            el = self._elements[i]
            if self._resizable(el) and self._resize_handle_rect(i).contains(pos):
                return i
        return None

    def paintEvent(self, event):
        painter = QPainter(self)
        if self._pixmap is not None:
            painter.drawPixmap(0, 0, self._pixmap)
        # Only the bar you're actively resizing, or the one you're
        # currently hovering near, gets its handle drawn -- showing
        # every bar's handle at once (the original approach) cluttered
        # a screen this small badly enough to be reported directly as
        # confusing ("wtf are the blue squares for?").
        handle_owner = self._resize_index if self._resize_index is not None else self._hover_index
        if handle_owner is not None and 0 <= handle_owner < len(self._elements) \
                and self._resizable(self._elements[handle_owner]):
            painter.setPen(QPen(QColor(120, 120, 120), 1))
            painter.setBrush(QColor(70, 140, 230, 180))
            painter.drawRect(self._resize_handle_rect(handle_owner))
        active = self._drag_index if self._drag_index is not None else self._resize_index
        if active is not None:
            painter.setPen(QPen(QColor(70, 140, 230), 2, Qt.DashLine))
            painter.setBrush(Qt.NoBrush)
            if self._resize_kind == "image" and self._image_resize_preview_wh:
                el = self._elements[active]
                w, h = self._image_resize_preview_wh
                rect = QRect(el["x"] * PREVIEW_SCALE, el["y"] * PREVIEW_SCALE,
                             w * PREVIEW_SCALE, h * PREVIEW_SCALE)
            else:
                rect = self._element_rect(active)
            painter.drawRect(rect)

    def mousePressEvent(self, event):
        if event.button() != Qt.LeftButton:
            return
        r_idx = self._resize_handle_at(event.pos())
        if r_idx is not None:
            el = self._elements[r_idx]
            self._resize_index = r_idx
            if el.get("kind") == "image":
                self._resize_kind = "image"
                self._image_resize_preview_wh = (el["width"], el["height"])
            elif el.get("kind") == "visualizer":
                # Unlike an image, no subprocess/re-conversion is ever
                # needed to change a visualizer's box size -- resize is
                # a cheap, direct, synchronous width/height update.
                self._resize_kind = "visualizer"
            else:
                self._resize_kind = "bar"
                self._resize_start_x = event.pos().x()
                self._resize_start_width = el.get("width", 40)
            self.update()
            self.drag_started.emit()
            return
        idx = self._element_at(event.pos())
        if idx is None:
            return
        self._drag_index = idx
        el = self._elements[idx]
        el_pos = QPoint(el["x"] * PREVIEW_SCALE, el["y"] * PREVIEW_SCALE)
        self._drag_offset = event.pos() - el_pos
        self.update()
        self.drag_started.emit()

    def mouseMoveEvent(self, event):
        if self._resize_index is not None and self._resize_kind == "image":
            self.setCursor(Qt.SizeFDiagCursor)
            el = self._elements[self._resize_index]
            target_w = round(event.pos().x() / PREVIEW_SCALE) - el["x"]
            target_h = round(event.pos().y() / PREVIEW_SCALE) - el["y"]
            target_w = max(MIN_IMAGE_SIZE, min(LCD_WIDTH - el["x"], target_w))
            target_h = max(MIN_IMAGE_SIZE, min(LCD_HEIGHT - el["y"], target_h))
            if self._image_resize_preview_wh != (target_w, target_h):
                self._image_resize_preview_wh = (target_w, target_h)
                self.image_resize_requested.emit(self._resize_index, target_w, target_h)
            self.update()
            return

        if self._resize_index is not None and self._resize_kind == "visualizer":
            self.setCursor(Qt.SizeFDiagCursor)
            el = self._elements[self._resize_index]
            target_w = round(event.pos().x() / PREVIEW_SCALE) - el["x"]
            target_h = round(event.pos().y() / PREVIEW_SCALE) - el["y"]
            target_w = max(MIN_IMAGE_SIZE, min(LCD_WIDTH - el["x"], target_w))
            target_h = max(MIN_IMAGE_SIZE, min(LCD_HEIGHT - el["y"], target_h))
            if el["width"] != target_w or el["height"] != target_h:
                el["width"], el["height"] = target_w, target_h
                self.element_resized.emit(self._resize_index, target_w)
            self.update()
            return

        if self._resize_index is not None:
            self.setCursor(Qt.SizeHorCursor)
            delta = round((event.pos().x() - self._resize_start_x) / PREVIEW_SCALE)
            new_width = max(MIN_BAR_WIDTH, min(MAX_BAR_WIDTH, self._resize_start_width + delta))
            el = self._elements[self._resize_index]
            if el.get("width", 40) != new_width:
                el["width"] = new_width
                self.element_resized.emit(self._resize_index, new_width)
            self.update()
            return

        if self._drag_index is None:
            idx = self._element_at(event.pos())
            over_handle = self._resize_handle_at(event.pos()) is not None
            new_hover = idx if (idx is not None and self._resizable(self._elements[idx])) else None
            if new_hover != self._hover_index:
                self._hover_index = new_hover
                self.update()
            if over_handle:
                is_image = idx is not None and self._elements[idx].get("kind") == "image"
                self.setCursor(Qt.SizeFDiagCursor if is_image else Qt.SizeHorCursor)
            elif idx is not None:
                self.setCursor(Qt.OpenHandCursor)
            else:
                self.setCursor(Qt.ArrowCursor)
            return
        self.setCursor(Qt.ClosedHandCursor)
        new_pos = event.pos() - self._drag_offset
        # Real bug found by self-audit, and it matches a direct report
        # ("elements still overflow under the screen where i cant take
        # out of"): this clamp only ever bounded the element's TOP-LEFT
        # corner to the visible screen, never its actual size -- so an
        # element could be dragged until most of its real content hung
        # off the bottom/right edge, at which point its now off-screen
        # hit-box made it hard or impossible to grab back. Measures the
        # element's real current size (via _element_rect, which already
        # has the fixed sensor-rank bounds lookup) and clamps so its
        # FAR edge can never leave the visible 160x43 screen either.
        rect = self._element_rect(self._drag_index)
        w_lcd = max(1, round(rect.width() / PREVIEW_SCALE))
        h_lcd = max(1, round(rect.height() / PREVIEW_SCALE))
        x = max(0, min(LCD_WIDTH - w_lcd, round(new_pos.x() / PREVIEW_SCALE)))
        y = max(0, min(LCD_HEIGHT - h_lcd, round(new_pos.y() / PREVIEW_SCALE)))
        el = self._elements[self._drag_index]
        if el["x"] != x or el["y"] != y:
            el["x"], el["y"] = x, y
            self.element_moved.emit(self._drag_index, x, y)
        self.update()

    def mouseReleaseEvent(self, event):
        if event.button() != Qt.LeftButton:
            return
        if self._resize_index is not None:
            self._resize_index = None
            self._resize_kind = None
            self._image_resize_preview_wh = None
            self.setCursor(Qt.ArrowCursor)
            self.update()
            self.drag_finished.emit()
            return
        if self._drag_index is None:
            return
        self._drag_index = None
        self.setCursor(Qt.ArrowCursor)
        self.update()
        self.drag_finished.emit()

    def leaveEvent(self, event):
        if self._hover_index is not None:
            self._hover_index = None
            self.update()


class ImageResizeWorker(QThread):
    """Runs png-to-lcd.py's conversion off the GUI thread. Direct
    finding from a self-audit: doing this subprocess call synchronously
    inside the 120ms drag_refresh_timer tick was measured (not assumed)
    at ~85-90ms per call -- meaning the UI thread was blocked roughly
    75% of the time while actively dragging an image's resize handle.
    Same QThread pattern already used for macro recording (see
    RecorderThread above) -- the app already trusts this approach for
    keeping the UI responsive during slower work."""
    finished_resize = pyqtSignal(int, int, int, bool)  # index, width, height, ok

    def __init__(self, index, src_path, out_path, target_w, target_h, png_to_lcd_path):
        super().__init__()
        self.index = index
        self.src_path = src_path
        self.out_path = out_path
        self.target_w = target_w
        self.target_h = target_h
        self.png_to_lcd_path = png_to_lcd_path

    def run(self):
        try:
            result = subprocess.run(
                [sys.executable, str(self.png_to_lcd_path), str(self.src_path), str(self.out_path),
                 str(self.target_w), str(self.target_h)],
                capture_output=True, text=True, timeout=15,
            )
        except Exception:
            self.finished_resize.emit(self.index, 0, 0, False)
            return
        if result.returncode != 0:
            self.finished_resize.emit(self.index, 0, 0, False)
            return
        try:
            w, h = (int(x) for x in result.stdout.split())
        except ValueError:
            self.finished_resize.emit(self.index, 0, 0, False)
            return
        self.finished_resize.emit(self.index, w, h, True)


class CustomScreensTab(QWidget):
    """AIDA64-style dashboard builder for the L2-L5 buttons. Pick a
    screen, add sensors with a display style, drag them into place on
    the live preview -- the preview is the real LCD-drawing code
    running in a one-shot mode, so what you see here is exactly what
    the keyboard will show."""
    def __init__(self):
        super().__init__()
        self.current_screen = "L2"
        self.config = load_custom_screens()

        root = QHBoxLayout()

        canvas_col = QVBoxLayout()
        canvas_col.setSpacing(6)

        screen_row = QHBoxLayout()
        screen_row.setSpacing(8)
        self.screen_buttons = {}
        for key in SCREEN_PREVIEW_KEYS:
            btn = QPushButton(key)
            btn.setCheckable(True)
            btn.setStyleSheet(
                "QPushButton { font-weight: bold; padding: 4px 12px; text-align: center; }"
                "QPushButton:checked { background-color: #4a90d9; color: white; }"
            )
            btn.clicked.connect(lambda _, k=key: self.select_screen(k))
            screen_row.addWidget(btn)
            self.screen_buttons[key] = btn
        screen_row.addStretch()
        self.screen_buttons["L2"].setChecked(True)
        canvas_col.addLayout(screen_row)

        # Live, interactive preview -- scaled up 4x (160x43 -> 640x172)
        # so it's actually readable, tinted to match the real
        # green-on-black LCD. Elements are click-and-drag movable
        # directly on this; bar elements also get a small drag handle
        # to resize their length.
        self.preview_canvas = ScreenPreviewCanvas()
        self.preview_canvas.element_moved.connect(self.on_element_dragged)
        self.preview_canvas.element_resized.connect(self.on_element_resized)
        self.preview_canvas.image_resize_requested.connect(self.on_image_resize_requested)
        self.preview_canvas.drag_started.connect(self.on_drag_started)
        self.preview_canvas.drag_finished.connect(self.on_drag_finished)
        # Image-resize regen runs on a background ImageResizeWorker (see
        # class above) instead of blocking the GUI thread every timer
        # tick. At most one worker is ever in flight per drag -- a newer
        # request that arrives while one is still running is coalesced
        # into _image_resize_next rather than spawning overlapping
        # subprocesses; _image_resize_screen guards against a result
        # landing on the wrong screen if you somehow switch screens
        # while one is in flight. _image_resize_el is the actual element
        # dict object the worker was started for -- a real self-audit
        # bug: removing a different element earlier in the list while a
        # resize was in flight could shift a surviving element into the
        # same index, silently misapplying the resize to it. Checking
        # identity (`elements[index] is el_token`), not just the index
        # bound, is what actually closes that.
        self._image_resize_worker = None
        self._image_resize_next = None
        self._image_resize_screen = None
        self._image_resize_el = None
        canvas_col.addWidget(self.preview_canvas, alignment=Qt.AlignHCenter)

        drag_hint = QLabel(
            "Drag anything to move it. Hover a bar or a freshly-imported "
            "image to reveal its resize handle."
        )
        drag_hint.setWordWrap(True)
        drag_hint.setObjectName("Status")
        canvas_col.addWidget(drag_hint)
        canvas_col.addStretch()
        root.addLayout(canvas_col, stretch=1)

        panel = QWidget()
        panel.setObjectName("Panel")
        panel.setFixedWidth(CUSTOM_SCREENS_PANEL_WIDTH)
        panel_layout = QVBoxLayout()

        title = QLabel("Custom Screens")
        title.setObjectName("Title")
        panel_layout.addWidget(title)

        panel_layout.addWidget(QLabel("Add Element:"))
        self.sensor_combo = QComboBox()
        for key, label in SENSOR_CHOICES:
            self.sensor_combo.addItem(label, key)
        self.sensor_combo.currentIndexChanged.connect(self.on_sensor_changed)
        panel_layout.addWidget(self.sensor_combo)

        # Direct feedback: "i dont know from that list what is what
        # and exactly what they do" -- live one-line explanation of
        # whatever's currently selected, not just a label to guess at.
        self.sensor_desc_label = QLabel("")
        self.sensor_desc_label.setObjectName("Status")
        self.sensor_desc_label.setWordWrap(True)
        panel_layout.addWidget(self.sensor_desc_label)

        self.style_combo = QComboBox()
        self.style_combo.addItem("Number", "number")
        self.style_combo.addItem("Bar", "bar")
        self.style_combo.currentIndexChanged.connect(self.on_style_changed)
        panel_layout.addWidget(self.style_combo)

        # Direct request: "id like to be able to resize elements".
        # Shared between new sensor elements and new text elements --
        # both just render through g15r_renderString at whatever size
        # this picks (see FONT_SIZE_CHOICES). Applies to the sensor's
        # VALUE only; the label always stays the fixed custom font.
        panel_layout.addWidget(QLabel("Text Size:"))
        self.font_size_combo = QComboBox()
        for value, label in FONT_SIZE_CHOICES:
            self.font_size_combo.addItem(label, value)
        panel_layout.addWidget(self.font_size_combo)
        self.on_style_changed()  # style_combo starts on "Number" -- sync Large/Huge availability now

        self.bar_hint_label = QLabel(
            "No honest 0-100 scale for this sensor -- shows as a number."
        )
        self.bar_hint_label.setObjectName("Status")
        self.bar_hint_label.setWordWrap(True)
        self.bar_hint_label.hide()
        panel_layout.addWidget(self.bar_hint_label)
        # connect() above ran before this label existed, and Qt doesn't
        # retroactively fire currentIndexChanged for an already-selected
        # index -- populate the description for the startup selection.
        self.on_sensor_changed()

        self.add_btn = add_btn = QPushButton("Add")
        add_btn.setObjectName("PanelPrimary")
        add_btn.clicked.connect(self.on_add_element)
        panel_layout.addWidget(add_btn)

        # Direct report: "gui buttons are ambigious and not that well
        # placed" -- Import Image/Add Text/Add Visualizer were stacked
        # directly under "Add" with nothing to show they're independent
        # actions, not related to the sensor/style/size dropdowns above.
        # A visible separator + label makes the grouping unambiguous.
        elements_sep = QFrame()
        elements_sep.setFrameShape(QFrame.HLine)
        elements_sep.setObjectName("Separator")
        panel_layout.addSpacing(8)
        panel_layout.addWidget(elements_sep)
        panel_layout.addSpacing(4)
        other_elements_label = QLabel("Other elements:")
        other_elements_label.setObjectName("Status")
        panel_layout.addWidget(other_elements_label)

        self.import_image_btn = QPushButton("Import Image...")
        self.import_image_btn.setObjectName("PanelButton")
        self.import_image_btn.clicked.connect(self.on_import_image)
        panel_layout.addWidget(self.import_image_btn)

        self.add_text_btn = QPushButton("Add Text...")
        self.add_text_btn.setObjectName("PanelButton")
        self.add_text_btn.clicked.connect(self.on_add_text)
        panel_layout.addWidget(self.add_text_btn)

        self.add_visualizer_btn = QPushButton("Add Visualizer")
        self.add_visualizer_btn.setObjectName("PanelButton")
        self.add_visualizer_btn.setToolTip(
            "A live audio bar visualizer -- reacts to whatever's actually "
            "playing on this PC (any player: Brave, Spotify, VLC, etc)."
        )
        self.add_visualizer_btn.clicked.connect(self.on_add_visualizer)
        panel_layout.addWidget(self.add_visualizer_btn)

        sep = QFrame()
        sep.setFrameShape(QFrame.HLine)
        sep.setObjectName("Separator")
        panel_layout.addSpacing(6)
        panel_layout.addWidget(sep)
        panel_layout.addSpacing(6)

        # Recovery for the drag-clamp bug: an element dragged before
        # this fix could have its content still hanging off the visible
        # screen edge, with its now off-screen hit-box making it hard
        # to grab back. Only shown when something's actually off-screen.
        self.fix_overflow_btn = QPushButton("Bring off-screen elements back")
        self.fix_overflow_btn.setObjectName("PanelButton")
        self.fix_overflow_btn.clicked.connect(self.on_fix_overflow)
        self.fix_overflow_btn.hide()
        panel_layout.addWidget(self.fix_overflow_btn)

        panel_layout.addWidget(QLabel("Elements on this screen:"))
        # Fixed-height scroll area -- previously a long unbounded list
        # of elements would push the whole layout down past the
        # preview as you added more (reported directly: "if i add too
        # many items they just fall under the preview screen"). This
        # caps it so the window shape never depends on element count.
        elements_scroll = QScrollArea()
        elements_scroll.setWidgetResizable(True)
        elements_scroll.setFixedHeight(180)
        elements_scroll.setFrameShape(QFrame.NoFrame)
        elements_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        elements_container = QWidget()
        self.elements_layout = QVBoxLayout()
        self.elements_layout.setSpacing(2)
        self.elements_layout.setContentsMargins(0, 0, 0, 0)
        self.elements_layout.addStretch()
        elements_container.setLayout(self.elements_layout)
        elements_scroll.setWidget(elements_container)
        panel_layout.addWidget(elements_scroll)

        panel_layout.addStretch()
        panel.setLayout(panel_layout)
        root.addWidget(panel)

        self.setLayout(root)

        self.on_sensor_changed()
        self.refresh_elements_list()
        self.refresh_preview()

        # Keeps the preview live (matches the real daemon's own refresh
        # cadence for these screens) so it feels the same as watching
        # the actual keyboard.
        self.preview_timer = QTimer(self)
        self.preview_timer.timeout.connect(self.refresh_preview)
        self.preview_timer.start(1000)

        # Separate, faster timer -- only running while actively
        # dragging -- so the preview visibly follows your mouse instead
        # of waiting up to a second for the next idle tick.
        self.drag_refresh_timer = QTimer(self)
        self.drag_refresh_timer.timeout.connect(self.refresh_preview)

    def select_screen(self, key):
        self.current_screen = key
        for k, btn in self.screen_buttons.items():
            btn.setChecked(k == key)
        editable = key != "L1"
        l1_reason = "L1 is the built-in clock -- it can't be edited." if not editable else ""
        self.sensor_combo.setEnabled(editable)
        self.sensor_combo.setToolTip(l1_reason)
        self.style_combo.setEnabled(editable)
        self.style_combo.setToolTip(l1_reason)
        self.add_btn.setEnabled(editable)
        self.add_btn.setToolTip(l1_reason)
        self.import_image_btn.setEnabled(editable)
        self.import_image_btn.setToolTip(l1_reason)
        self.add_text_btn.setEnabled(editable)
        self.add_text_btn.setToolTip(l1_reason)
        self.add_visualizer_btn.setEnabled(editable)
        self.add_visualizer_btn.setToolTip(
            l1_reason if not editable else
            "A live audio bar visualizer -- reacts to whatever's actually "
            "playing on this PC (any player: Brave, Spotify, VLC, etc)."
        )
        self.refresh_elements_list()
        self.refresh_preview()

    def on_sensor_changed(self):
        sensor_key = self.sensor_combo.currentData()
        capable = sensor_key in BAR_CAPABLE_SENSORS
        self.bar_hint_label.setVisible(not capable)
        self.sensor_desc_label.setText(SENSOR_DESCRIPTIONS.get(sensor_key, ""))

    def on_style_changed(self):
        # Self-audit finding: Large/Huge next to a bar looks visually
        # unbalanced (the bar's own height doesn't scale). Grey those
        # two options out while "Bar" is selected instead of silently
        # capping whatever you picked -- so what's shown always matches
        # what you'll actually get.
        is_bar = self.style_combo.currentData() == "bar"
        model = self.font_size_combo.model()
        for value, _ in FONT_SIZE_CHOICES:
            item = model.item(value)
            item.setEnabled(not (is_bar and value > MAX_BAR_FONT_SIZE))
        if is_bar and self.font_size_combo.currentData() > MAX_BAR_FONT_SIZE:
            self.font_size_combo.setCurrentIndex(MAX_BAR_FONT_SIZE)

    def screen_number(self):
        # L1 used to hardcode to 1 (the clock) always -- but L1 really
        # toggles between two real screens (stats and clock, see
        # read_active_screen_num()'s own docstring). Direct report:
        # "the main page preview shows L1 stats but the custom page
        # still shows just the time" -- this tab's own "reference"
        # preview was the one still hardcoded.
        #
        # Not a blind copy of read_active_screen_num() though: if the
        # real current screen is 2-5 (some OTHER screen is live right
        # now), showing that here would make the "L1" tab display an
        # unrelated custom screen's content, which is wrong regardless
        # of what's live elsewhere. g510_lcd_buttons.c's own L1 button
        # handler (`cur >= 2 ? 0 : ...`) always resets to stats (0)
        # when pressed from any other screen -- read directly from the
        # real source, not assumed -- so that's the accurate answer
        # for "what would L1 show right now" whenever L1 isn't the one
        # currently active.
        if self.current_screen == "L1":
            live = read_active_screen_num()
            return live if live in (0, 1) else 0
        return int(self.current_screen[1])  # "L2" -> 2

    def on_add_element(self):
        if self.current_screen == "L1":
            return  # button is disabled for this case, this is just a safety guard
        elements = self.config[self.current_screen]
        sensor_count = sum(1 for el in elements if el.get("kind", "sensor") == "sensor")
        if sensor_count >= 8:
            QMessageBox.warning(self, "Screen full", "Each screen supports up to 8 sensor elements.")
            return
        # No X/Y fields anymore -- new elements land at a default spot
        # (stacked below whatever's already there) and you drag them
        # into their real place on the preview. y wraps back to the top
        # once it'd run off the bottom of the 43px screen, rather than
        # placing something permanently off-screen.
        #
        # Self-audit bug: this used to stagger by sensor_count alone,
        # so the Nth sensor and the Nth text element (or image) landed
        # on the exact same default spot -- e.g. the first-ever text
        # field and the first-ever sensor both defaulted to (6,6),
        # confirmed by reproducing it directly. Staggering by the
        # TOTAL element count on screen instead means a freshly-added
        # element never lands on top of whatever was added right
        # before it, regardless of kind.
        default_y = (6 + 12 * len(elements)) % LCD_HEIGHT
        elements.append({
            "kind": "sensor",
            "sensor": self.sensor_combo.currentData(),
            "style": self.style_combo.currentData(),
            "x": 6,
            "y": default_y,
            "width": 40,
            "font_size": self.font_size_combo.currentData(),
        })
        save_custom_screens(self.config)
        self.refresh_elements_list()
        self.refresh_preview()

    def on_import_image(self):
        if self.current_screen == "L1":
            return  # button is disabled for this case, this is just a safety guard
        elements = self.config[self.current_screen]
        image_count = sum(1 for el in elements if el.get("kind") == "image")
        if image_count >= MAX_IMAGES_PER_SCREEN:
            QMessageBox.warning(
                self, "Screen full",
                f"Each screen supports up to {MAX_IMAGES_PER_SCREEN} images -- "
                "the 160x43 screen is small, more than that rarely fits usefully anyway."
            )
            return

        src_path, _ = QFileDialog.getOpenFileName(
            self, "Choose an image", str(Path.home()),
            "Images (*.png *.jpg *.jpeg *.bmp *.gif)",
        )
        if not src_path:
            return

        CUSTOM_SCREEN_IMAGES_DIR.mkdir(exist_ok=True)
        # A stable, filesystem-safe name derived from the source file,
        # with a numeric suffix if that name's already taken -- so
        # re-importing the same file twice (or two different files with
        # the same name) doesn't silently clobber an existing one.
        stem = "".join(c if c.isalnum() else "_" for c in Path(src_path).stem) or "image"
        out_path = CUSTOM_SCREEN_IMAGES_DIR / f"{stem}.bin"
        n = 1
        while out_path.exists():
            out_path = CUSTOM_SCREEN_IMAGES_DIR / f"{stem}_{n}.bin"
            n += 1

        # Keep a copy of the real source picture alongside the
        # converted .bin (same disambiguated stem, original extension)
        # -- needed so a later resize can re-convert from real
        # quality instead of upscaling the already-downscaled 1-bit
        # bitmap. Not the same file the user picked: that path could
        # move or be deleted later, this copy is self-contained under
        # DATA_DIR like everything else this app persists.
        src_ext = Path(src_path).suffix or ".png"
        src_copy_path = out_path.with_suffix(src_ext)
        try:
            shutil.copyfile(src_path, src_copy_path)
        except OSError:
            src_copy_path = None  # resize just won't be offered for this image

        # Initial import always goes through png-to-lcd.py's own
        # automatic resize + dithering (already Floyd-Steinberg,
        # confirmed the right algorithm for this) at a sane default
        # size -- 60px is a reasonable default max width for a 160px
        # screen that likely also has sensor elements on it. Dragging
        # the resize handle afterward (see ScreenPreviewCanvas) reruns
        # this exact same converter at a new target size, so quality
        # stays consistent between import and resize.
        max_width = 60
        png_to_lcd = PROJECT_DIR / "src" / "png-to-lcd.py"
        try:
            result = subprocess.run(
                [sys.executable, str(png_to_lcd), src_path, str(out_path), str(max_width), str(LCD_HEIGHT)],
                capture_output=True, text=True, timeout=15,
            )
        except Exception as e:
            QMessageBox.critical(self, "Import failed", f"Couldn't run the converter: {e}")
            return
        if result.returncode != 0:
            QMessageBox.critical(self, "Import failed", result.stderr.strip() or "Unknown error converting the image.")
            return
        try:
            w, h = (int(x) for x in result.stdout.split())
        except ValueError:
            QMessageBox.critical(self, "Import failed", f"Unexpected converter output: {result.stdout!r}")
            return

        # Stagger by the TOTAL element count on screen, not just other
        # images -- same cross-kind collision fix as on_add_element
        # (an image and a same-numbered sensor/text used to be able to
        # land on the same default spot).
        default_x = min(6 + 20 * len(elements), max(0, LCD_WIDTH - w))
        default_y = min(6 + 15 * len(elements), max(0, LCD_HEIGHT - h))
        elements.append({
            "kind": "image",
            "path": f"custom_screen_images/{out_path.name}",  # relative -- matches how the C side resolves it against data_dir()
            "src": f"custom_screen_images/{src_copy_path.name}" if src_copy_path else None,
            "x": default_x, "y": default_y,
            "width": w, "height": h,
        })
        save_custom_screens(self.config)
        self.refresh_elements_list()
        self.refresh_preview()

    def on_add_text(self):
        """Freeform text, not tied to a sensor -- direct request: "L3
        L4 L5 have hard coded text i cant edit move do anything with.
        add an option for me to add text fields too" (referring to the
        "not set up yet" placeholder). Deliberately skips the .meta
        bounds mechanism sensor elements use (that needs real
        font-metric measurement from the C side, keyed to a shared
        index between C's sensor-only array and Python's combined
        list -- extending that correctly for a third element kind
        wasn't worth the risk for a freeform-text feature); hit-testing
        instead uses a simple estimated box, the same category of
        approximation images already accept for their own bounds."""
        if self.current_screen == "L1":
            return  # button is disabled for this case, this is just a safety guard
        elements = self.config[self.current_screen]
        text_count = sum(1 for el in elements if el.get("kind") == "text")
        if text_count >= MAX_TEXTS_PER_SCREEN:
            QMessageBox.warning(
                self, "Screen full",
                f"Each screen supports up to {MAX_TEXTS_PER_SCREEN} text fields -- "
                "the 160x43 screen is small, more than that rarely fits usefully anyway."
            )
            return

        content, ok = QInputDialog.getText(self, "Add text", "Text to show on the screen:")
        content = content.strip()
        if not ok or not content:
            return

        # Same staggered-default-position pattern as on_add_element/
        # on_import_image, staggered by the TOTAL element count (see
        # on_add_element for the cross-kind collision this fixes) --
        # otherwise a second text field (or a sensor/image added right
        # after one) could land exactly on top of it.
        default_y = (6 + 12 * len(elements)) % LCD_HEIGHT
        elements.append({
            "kind": "text", "content": content, "x": 6, "y": default_y,
            "font_size": self.font_size_combo.currentData(),
        })
        save_custom_screens(self.config)
        self.refresh_elements_list()
        self.refresh_preview()

    def on_add_visualizer(self):
        """Real-time audio bar visualizer -- direct request: "add ...
        a little visualizer" alongside media info, refined with a
        reference image to a segmented, blocky multi-bar look. Capped
        at 1 per screen (see MAX_VISUALIZERS_PER_SCREEN) since there's
        exactly one persistent audio-capture stream process-wide."""
        if self.current_screen == "L1":
            return  # button is disabled for this case, this is just a safety guard
        elements = self.config[self.current_screen]
        viz_count = sum(1 for el in elements if el.get("kind") == "visualizer")
        if viz_count >= MAX_VISUALIZERS_PER_SCREEN:
            QMessageBox.warning(
                self, "Screen full",
                f"Each screen supports up to {MAX_VISUALIZERS_PER_SCREEN} visualizer -- "
                "only one audio stream is captured at a time, a second would just repeat it."
            )
            return

        default_x = min(6, max(0, LCD_WIDTH - DEFAULT_VISUALIZER_WIDTH))
        default_y = min(3, max(0, LCD_HEIGHT - DEFAULT_VISUALIZER_HEIGHT))
        elements.append({
            "kind": "visualizer", "x": default_x, "y": default_y,
            "width": DEFAULT_VISUALIZER_WIDTH, "height": DEFAULT_VISUALIZER_HEIGHT,
        })
        save_custom_screens(self.config)
        self.refresh_elements_list()
        self.refresh_preview()

    def on_remove_element(self, index):
        del self.config[self.current_screen][index]
        save_custom_screens(self.config)
        self.refresh_elements_list()
        self.refresh_preview()

    def on_reset_position(self, index):
        # Direct request: "add a 'reset position' button for elements,
        # in case i lose them off screen too" -- always snaps back to
        # the visible top-left corner (0,0), regardless of element
        # kind or how it ended up off-screen (a hand-edited
        # custom_screens.txt, a stale migrated position, etc.) --
        # simpler and more predictable than trying to guess "where it
        # should go back to".
        el = self.config[self.current_screen][index]
        el["x"], el["y"] = 0, 0
        save_custom_screens(self.config)
        self.refresh_elements_list()
        self.refresh_preview()

    def on_fix_overflow(self):
        # Same size estimate refresh_elements_list uses to decide
        # whether to show this button -- clamps each offending element
        # back so its far edge stays on the visible 160x43 screen,
        # same rule the drag-clamp fix now enforces for new drags.
        for el in self.config[self.current_screen]:
            if el.get("kind") in ("image", "visualizer"):
                w, h = el.get("width", 0), el.get("height", 0)
            elif el.get("kind") == "text":
                _, h_ratio = FONT_SIZE_SCALE.get(el.get("font_size", 0), (1.0, 1.0))
                w, h = TEXT_CHAR_PX, round(ELEMENT_HIT_H * h_ratio)
            else:
                w, h = ELEMENT_HIT_W, ELEMENT_HIT_H
            el["x"] = max(0, min(el["x"], LCD_WIDTH - w))
            el["y"] = max(0, min(el["y"], LCD_HEIGHT - h))
        save_custom_screens(self.config)
        self.refresh_elements_list()
        self.refresh_preview()

    def on_cycle_font_size(self, index):
        el = self.config[self.current_screen][index]
        # Bars cap at Medium -- same reasoning as on_style_changed's
        # greyed-out Large/Huge options, applied here too so an
        # existing bar element can't cycle past it either.
        is_bar = el.get("kind") == "sensor" and el.get("style") == "bar"
        max_size = MAX_BAR_FONT_SIZE if is_bar else len(FONT_SIZE_CHOICES) - 1
        el["font_size"] = (el.get("font_size", 0) + 1) % (max_size + 1)
        save_custom_screens(self.config)
        self.refresh_elements_list()
        self.refresh_preview()

    def on_element_dragged(self, index, x, y):
        # Called continuously while dragging. Saving here (a cheap text
        # write, not the render itself) is what actually makes the live
        # preview live -- render_preview() runs the real C binary,
        # which reads custom_screens.txt fresh from disk every time.
        # Without saving mid-drag, the drag_refresh_timer's periodic
        # refresh_preview() calls kept re-rendering the OLD saved
        # position the whole time you were dragging -- only the dashed
        # selection outline moved with the mouse, the actual rendered
        # bar/label stayed frozen until release. Real bug, reported
        # directly as the preview being "hard to control."
        save_custom_screens(self.config)
        self.refresh_elements_list()

    def on_element_resized(self, index, width):
        # Same idea and same fix as on_element_dragged.
        save_custom_screens(self.config)
        self.refresh_elements_list()

    def on_image_resize_requested(self, index, target_w, target_h):
        # Deliberately NOT the same pattern as on_element_resized: a bar
        # resize is just a number, cheap to save on every mouse-move
        # event. An image resize has to re-run png-to-lcd.py and rewrite
        # the .bin file on disk -- self-audit measured that at ~85-90ms
        # per call, so it runs on a background ImageResizeWorker instead
        # of blocking the GUI thread. If one's already running, the
        # latest target is coalesced into _image_resize_next rather than
        # piling up overlapping subprocesses; _start_image_resize_worker
        # picks that up the moment the current one finishes.
        if self._image_resize_worker is not None and self._image_resize_worker.isRunning():
            self._image_resize_next = (index, target_w, target_h)
            return
        self._start_image_resize_worker(index, target_w, target_h)

    def _start_image_resize_worker(self, index, target_w, target_h):
        elements = self.config.get(self.current_screen, [])
        if index >= len(elements):
            return
        el = elements[index]
        if el.get("kind") != "image" or not el.get("src"):
            return
        src_full = DATA_DIR / el["src"]
        out_full = DATA_DIR / el["path"]
        png_to_lcd = PROJECT_DIR / "src" / "png-to-lcd.py"
        self._image_resize_screen = self.current_screen
        # Real bug found by self-audit: an index alone isn't a stable
        # identity across an async gap. Removing (or reordering) an
        # earlier element while this worker is still running would
        # leave a DIFFERENT element sitting at `index` by the time the
        # result comes back -- confirmed with a real "remove element 0
        # while resizing element 0" race, which misapplied the resize
        # to the surviving element that shifted into that slot. `el`
        # itself is the same dict object Python already holds a
        # reference to in the list -- keeping that reference lets
        # _on_image_resize_finished verify identity (`is`), not just
        # a bounds check, before writing anything.
        self._image_resize_el = el
        worker = ImageResizeWorker(index, src_full, out_full, target_w, target_h, png_to_lcd)
        worker.finished_resize.connect(self._on_image_resize_finished)
        self._image_resize_worker = worker
        worker.start()

    def _on_image_resize_finished(self, index, w, h, ok):
        started_on = self._image_resize_screen
        el_token = self._image_resize_el
        self._image_resize_worker = None
        self._image_resize_el = None
        if ok and started_on == self.current_screen:
            elements = self.config.get(self.current_screen, [])
            if index < len(elements) and elements[index] is el_token:
                elements[index]["width"], elements[index]["height"] = w, h
                save_custom_screens(self.config)
                self.refresh_preview()
        if self._image_resize_next is not None:
            next_index, next_w, next_h = self._image_resize_next
            self._image_resize_next = None
            self._start_image_resize_worker(next_index, next_w, next_h)

    def on_drag_started(self):
        # Pause the 1s idle timer while the 120ms drag timer takes
        # over -- both ultimately call the same refresh_preview(),
        # writing to the same file; no reason to run both at once.
        self.preview_timer.stop()
        self.drag_refresh_timer.start(120)

    def on_drag_finished(self):
        self.drag_refresh_timer.stop()
        save_custom_screens(self.config)
        self.refresh_preview()  # one final, accurate, untimed refresh
        self.preview_timer.start(1000)

    def refresh_elements_list(self):
        # Clear everything including the trailing stretch, then rebuild
        # it fresh each time -- simplest way to keep the stretch at the
        # end regardless of how many rows there are now.
        while self.elements_layout.count():
            item = self.elements_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()

        if self.current_screen == "L1":
            self.fix_overflow_btn.hide()
            info = QLabel("L1 is the built-in clock screen -- shown here for reference, not editable.")
            info.setWordWrap(True)
            info.setObjectName("Status")
            self.elements_layout.addWidget(info)
            self.elements_layout.addStretch()
            return

        elements = self.config[self.current_screen]
        # Recovery-button visibility: rough estimate (not pixel-perfect
        # -- this is just "does it look like something's clipped",
        # matching the same category of approximation already used
        # elsewhere for kinds without exact measured bounds) of whether
        # any element's content likely extends past the visible screen.
        overflowing = False
        for el in elements:
            if el.get("kind") in ("image", "visualizer"):
                w, h = el.get("width", 0), el.get("height", 0)
            elif el.get("kind") == "text":
                _, h_ratio = FONT_SIZE_SCALE.get(el.get("font_size", 0), (1.0, 1.0))
                w, h = TEXT_CHAR_PX, round(ELEMENT_HIT_H * h_ratio)
            else:
                w, h = ELEMENT_HIT_W, ELEMENT_HIT_H
            if el["x"] + w > LCD_WIDTH or el["y"] + h > LCD_HEIGHT:
                overflowing = True
                break
        self.fix_overflow_btn.setVisible(overflowing)

        if not elements:
            self.elements_layout.addWidget(QLabel("Nothing on this screen yet."))
            self.elements_layout.addStretch()
            return
        for i, el in enumerate(elements):
            row = QHBoxLayout()
            row.setContentsMargins(0, 0, 0, 0)
            # Position/width shown on the canvas itself now (drag to
            # move, drag the handle to resize) -- repeating exact
            # coordinates here just made rows overflow the narrow panel
            # and need a horizontal scrollbar, so this stays short.
            if el.get("kind") == "image":
                name = Path(el["path"]).stem
                text = QLabel(f"Image: {name}")
                text.setToolTip(f"x={el['x']} y={el['y']} {el['width']}x{el['height']}px")
            elif el.get("kind") == "text":
                shown = el["content"] if len(el["content"]) <= 20 else el["content"][:17] + "..."
                text = QLabel(f'Text: "{shown}"')
                text.setToolTip(f"x={el['x']} y={el['y']}")
            elif el.get("kind") == "visualizer":
                text = QLabel("Visualizer (live audio)")
                text.setToolTip(f"x={el['x']} y={el['y']} {el['width']}x{el['height']}px")
            else:
                label = SENSOR_LABELS.get(el["sensor"], el["sensor"])
                text = QLabel(f"{label} – {el['style']}")
                text.setToolTip(f"x={el['x']} y={el['y']}" + (f" width={el.get('width', 40)}" if el.get("style") == "bar" else ""))
            text.setStyleSheet("font-size: 11px;")
            # Real regression found by direct testing: a long row label
            # (e.g. "Media: Song Title – number") could push the ✕
            # remove button clean off the visible panel -- "the remove
            # vutton is gone from the sloppy gui". Elide with Qt's own
            # real font-metric-based truncation (not a guessed character
            # count) to whatever's actually left after the size/remove
            # buttons, so the row's total width can never exceed the
            # panel and the remove button is always reachable. Full text
            # stays in the tooltip.
            has_size_btn = el.get("kind") in ("sensor", "text")
            reserved = 24 + 24 + 20 + (56 if has_size_btn else 0)  # remove_btn + reset_btn + panel margins/spacing + size_btn if present
            elide_budget = max(20, CUSTOM_SCREENS_PANEL_WIDTH - reserved)
            full_text = text.text()
            text.setText(QFontMetrics(text.font()).elidedText(full_text, Qt.ElideRight, elide_budget))
            if text.toolTip():
                text.setToolTip(full_text + "\n" + text.toolTip())
            else:
                text.setToolTip(full_text)
            row.addWidget(text)
            row.addStretch()
            if el.get("kind") in ("sensor", "text"):
                # Direct request: "id like to be able to resize
                # elements". No drag handle here on purpose -- the
                # library only has 4 discrete text sizes (no continuous
                # scaling), so cycling through them with a click is the
                # honest equivalent of resizing for these element kinds.
                size_label = FONT_SIZE_CHOICES[el.get("font_size", 0)][1]
                size_btn = QPushButton(size_label)
                size_btn.setFixedWidth(52)
                size_btn.setStyleSheet("padding: 1px; font-size: 10px;")
                size_btn.setToolTip("Click to cycle text size: Small -> Medium -> Large -> Huge")
                size_btn.clicked.connect(lambda _, idx=i: self.on_cycle_font_size(idx))
                row.addWidget(size_btn)
            # Direct request: "add a 'reset position' button for
            # elements, in case i lose them off screen too" -- snaps
            # x/y back to the visible top-left corner regardless of
            # element kind, so a dragged-off-screen element is always
            # recoverable without having to guess at coordinates.
            reset_btn = QPushButton("⌂")
            reset_btn.setFixedWidth(24)
            reset_btn.setStyleSheet("padding: 1px;")
            reset_btn.setToolTip("Reset position to top-left (use if you've dragged this off-screen)")
            reset_btn.clicked.connect(lambda _, idx=i: self.on_reset_position(idx))
            row.addWidget(reset_btn)
            remove_btn = QPushButton("✕")
            remove_btn.setFixedWidth(24)
            remove_btn.setStyleSheet("padding: 1px;")
            remove_btn.clicked.connect(lambda _, idx=i: self.on_remove_element(idx))
            row.addWidget(remove_btn)
            container = QWidget()
            container.setLayout(row)
            self.elements_layout.addWidget(container)
        self.elements_layout.addStretch()

    def refresh_preview(self):
        # Image resize regen now happens on a background worker (see
        # ImageResizeWorker/_on_image_resize_finished) which calls this
        # itself once the .bin file + el["width"/"height"] are updated
        # together -- nothing to apply synchronously here anymore.
        pixmap, bounds, err = render_preview(self.screen_number(), tag="editor")
        if pixmap is None:
            print(f"Custom Screens preview error: {err}")  # surfaced in the panel below instead of blocking the canvas
            return
        scaled = pixmap.scaled(
            LCD_WIDTH * PREVIEW_SCALE, LCD_HEIGHT * PREVIEW_SCALE,
            Qt.KeepAspectRatio, Qt.FastTransformation,
        )
        elements = self.config.get(self.current_screen, [])  # L1 isn't a key in self.config -- no draggable elements there
        self.preview_canvas.set_data(scaled, elements, bounds)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("G510 LCD Control")
        self.setStyleSheet(STYLESHEET)

        self.keyboard_tab = KeyboardTab()
        self.custom_tab = CustomScreensTab()

        tabs = QTabWidget()
        tabs.addTab(self.keyboard_tab, "Backlight + G-Keys")
        tabs.addTab(self.custom_tab, "Custom Screens")
        self.setCentralWidget(tabs)

        # Direct request: "make me a save button/feature o dont trust
        # my settings will survive reboots" -- every individual action
        # in this app (dragging an element, clicking Apply/Set as
        # Default, recording a macro) already writes straight to disk
        # immediately, so there was never actually a missing autosave
        # path to add. What WAS missing is visible proof of that: this
        # button re-saves everything explicitly, then reads each file
        # straight back off disk and compares it against what's
        # supposed to be there -- a real verification, not just "the
        # write call didn't throw" -- and reports exactly what it
        # confirmed (or, honestly, what failed) in the status bar. A
        # QToolBar (not a wrapper around centralWidget()) so it's
        # visible from every tab without disturbing anything that
        # assumes centralWidget() is the QTabWidget itself.
        toolbar = self.addToolBar("Save")
        toolbar.setMovable(False)
        self.save_all_btn = QPushButton("\U0001F4BE  Save All Settings Now")
        self.save_all_btn.setObjectName("saveAllBtn")
        self.save_all_btn.clicked.connect(self.on_save_all)
        toolbar.addWidget(self.save_all_btn)

        self.status = QStatusBar()
        self.setStatusBar(self.status)
        self.status.showMessage(
            "Every change here already saves to disk immediately -- "
            "use Save All Settings Now any time you want that verified."
        )

        # A hardcoded resize() goes stale the moment tab content's
        # natural size differs (bit us on the sibling G910 app) --
        # adjustSize() sizes the window to what's actually in it.
        self.adjustSize()

    def on_save_all(self):
        """Real disk round-trip verification, not a cosmetic no-op:
        re-writes each real data store this app owns, then reads it
        back off disk and checks it actually matches, so "Saved" here
        means "confirmed on disk right now", not just "the write call
        didn't raise". Direct feedback on the first version of this
        button: "kinda sucks, offers no assurance anything is saved" --
        a status-bar line at the bottom of the window is too easy to
        miss entirely. This now pops a modal confirmation dialog (can't
        be missed, has to be dismissed) that itemizes exactly what got
        verified PER SCREEN -- also directly answers "does it also save
        the L screens?": yes, `self.custom_tab.config` already holds
        every screen (L2-L5) in one dict, saved/reloaded together, so
        the breakdown below always lists all of them, even ones with 0
        elements, as visible proof none were silently skipped."""
        problems = []
        detail_lines = []

        # 1. Custom Screens -- ALL screens (L2, L3, L4, L5) live in one
        # config dict and save_custom_screens()/load_custom_screens()
        # always write/read the whole thing together, never a single
        # screen in isolation, so there's no way for this to silently
        # save only the currently-open one.
        try:
            save_custom_screens(self.custom_tab.config)
            reloaded = load_custom_screens()
            if reloaded != self.custom_tab.config:
                problems.append("Custom Screens: saved but didn't read back identically")
            else:
                for screen in sorted(self.custom_tab.config.keys()):
                    n = len(self.custom_tab.config[screen])
                    detail_lines.append(f"    {screen}: {n} element(s)")
        except OSError as e:
            problems.append(f"Custom Screens: {e}")

        # 2. Backlight boot default (set-backlight-color.sh). Apply and
        # Set as Default both already write this immediately -- this
        # re-persists whatever color is CURRENTLY shown/pending so even
        # an in-progress pick gets a real boot-default, then verifies
        # the exact RGB triple is really in the file on disk.
        try:
            rgb = self.keyboard_tab._pending_rgb
            set_as_default(rgb)
            on_disk = DEFAULTS_SCRIPT_DATA_DIR.read_text()
            if f"{rgb[0]} {rgb[1]} {rgb[2]}" not in on_disk:
                problems.append("Backlight: saved but didn't read back identically")
            else:
                detail_lines.append(f"    Boot color: #{rgb[0]:02x}{rgb[1]:02x}{rgb[2]:02x}")
        except OSError as e:
            problems.append(f"Backlight: {e}")

        # 3. Macros -- each one already writes to macros.json the
        # instant it's recorded (no separate in-memory "pending" macro
        # state exists to re-save), so this is a read-back sanity check
        # only: confirm the file exists, is valid JSON, and count what's
        # actually assigned per profile.
        try:
            macro_counts = {}
            if MACROS_FILE.exists():
                data = json.loads(MACROS_FILE.read_text())
                for profile, keys in data.items():
                    if keys:
                        macro_counts[profile] = len(keys)
            if macro_counts:
                for profile in sorted(macro_counts):
                    detail_lines.append(f"    {profile}: {macro_counts[profile]} macro(s) assigned")
            else:
                detail_lines.append("    (no macros assigned yet)")
        except (OSError, json.JSONDecodeError) as e:
            problems.append(f"Macros: {e}")

        now = datetime.datetime.now().strftime("%H:%M:%S")
        if problems:
            msg = "Save FAILED at " + now + ":\n\n" + "\n".join(problems)
            self.status.showMessage("✗ " + msg.replace("\n", " "))
            QMessageBox.critical(self, "Save failed", msg)
        else:
            msg = (
                f"Verified on disk at {now}:\n\n"
                "Custom Screens:\n" + "\n".join(detail_lines[:len(self.custom_tab.config)]) + "\n\n"
                "Backlight + Macros:\n" + "\n".join(detail_lines[len(self.custom_tab.config):])
            )
            self.status.showMessage(f"✓ Saved & verified on disk at {now}")
            QMessageBox.information(self, "Saved", msg)


if __name__ == "__main__":
    app = QApplication(sys.argv)
    win = MainWindow()
    win.show()
    sys.exit(app.exec_())
