#!/usr/bin/env python3
"""
G510 LCD Control App.

Architecture: one QTabWidget, one tab per feature. Adding a new feature
= adding a new tab class + one line in MainWindow.__init__. Nothing in
an existing tab needs to change when a new one is added.

Phase 1 (this file): Backlight tab (color/brightness + service control).
Phase 2 (later):     Custom Screen tab (text/image placement on screen 6).
"""
import sys
import subprocess
from pathlib import Path

from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QTabWidget, QWidget, QVBoxLayout,
    QHBoxLayout, QComboBox, QSlider, QPushButton, QLabel, QMessageBox,
)
from PyQt5.QtCore import Qt

PROJECT_DIR = Path(__file__).resolve().parent
LED_DIR = Path("/sys/class/leds/g15::kbd_backlight")
DEFAULTS_SCRIPT = PROJECT_DIR / "set-backlight-color.sh"

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
DEFAULT_COLOR = "Blue-Violet"
DEFAULT_BRIGHTNESS_PCT = 100


def read_current_rgb():
    try:
        r, g, b = (int(x) for x in (LED_DIR / "multi_intensity").read_text().split())
        return (r, g, b)
    except Exception:
        return None


def read_current_brightness_pct():
    try:
        val = int((LED_DIR / "brightness").read_text().strip())
        return round(val * 100 / 255)
    except Exception:
        return 100


def apply_backlight(color_name, brightness_pct):
    """Writes the LED sysfs files AND rewrites set-backlight-color.sh so
    this becomes the new permanent boot default (matches the behavior
    already established by g510-backlight-apply.sh -- same contract)."""
    rgb = COLOR_RGB.get(color_name)
    if rgb is None:
        return False, f"Unknown color: {color_name}"
    brightness_val = round(brightness_pct * 255 / 100)
    try:
        (LED_DIR / "multi_intensity").write_text(f"{rgb[0]} {rgb[1]} {rgb[2]}")
        (LED_DIR / "brightness").write_text(str(brightness_val))
    except PermissionError as e:
        return False, f"Permission denied writing to {LED_DIR} -- check the udev rule (99-g510-lcd.rules) is installed: {e}"

    script = f"""#!/bin/bash
# Applies the chosen keyboard backlight color. Run automatically by
# 99-g510-lcd.rules whenever the LED device appears (boot or replug).
# Auto-updated by g510_app.py every time you click Apply.
echo {brightness_val} > /sys/class/leds/g15::kbd_backlight/brightness
echo "{rgb[0]} {rgb[1]} {rgb[2]}" > /sys/class/leds/g15::kbd_backlight/multi_intensity
"""
    DEFAULTS_SCRIPT.write_text(script)
    DEFAULTS_SCRIPT.chmod(0o755)
    return True, None


def run_systemctl(action):
    try:
        subprocess.run(
            ["systemctl", "--user", action,
             "g510-lcd-stats.service", "g510-lcd-buttons.service"],
            check=True, capture_output=True, text=True,
        )
        return True, None
    except subprocess.CalledProcessError as e:
        return False, e.stderr or str(e)


class BacklightTab(QWidget):
    def __init__(self):
        super().__init__()
        layout = QVBoxLayout()

        layout.addWidget(QLabel("<b>Keyboard Backlight</b>"))

        color_row = QHBoxLayout()
        color_row.addWidget(QLabel("Color:"))
        self.color_combo = QComboBox()
        self.color_combo.addItems(COLOR_RGB.keys())
        current_rgb = read_current_rgb()
        for name, rgb in COLOR_RGB.items():
            if rgb == current_rgb:
                self.color_combo.setCurrentText(name)
                break
        color_row.addWidget(self.color_combo)
        layout.addLayout(color_row)

        bright_row = QHBoxLayout()
        bright_row.addWidget(QLabel("Brightness:"))
        self.bright_slider = QSlider(Qt.Horizontal)
        self.bright_slider.setRange(0, 100)
        self.bright_slider.setValue(read_current_brightness_pct())
        self.bright_label = QLabel(f"{self.bright_slider.value()}%")
        self.bright_slider.valueChanged.connect(
            lambda v: self.bright_label.setText(f"{v}%")
        )
        bright_row.addWidget(self.bright_slider)
        bright_row.addWidget(self.bright_label)
        layout.addLayout(bright_row)

        btn_row = QHBoxLayout()
        apply_btn = QPushButton("Apply")
        apply_btn.clicked.connect(self.on_apply)
        defaults_btn = QPushButton("Apply Defaults")
        defaults_btn.clicked.connect(self.on_apply_defaults)
        btn_row.addWidget(apply_btn)
        btn_row.addWidget(defaults_btn)
        layout.addLayout(btn_row)

        layout.addWidget(QLabel("<b>Service Control</b>"))
        svc_row = QHBoxLayout()
        start_btn = QPushButton("Start")
        start_btn.clicked.connect(lambda: self.on_service_action("start"))
        restart_btn = QPushButton("Restart Service")
        restart_btn.clicked.connect(lambda: self.on_service_action("restart"))
        svc_row.addWidget(start_btn)
        svc_row.addWidget(restart_btn)
        layout.addLayout(svc_row)

        layout.addStretch()
        self.setLayout(layout)

    def on_apply(self):
        ok, err = apply_backlight(self.color_combo.currentText(), self.bright_slider.value())
        if not ok:
            QMessageBox.critical(self, "Error", err)

    def on_apply_defaults(self):
        self.color_combo.setCurrentText(DEFAULT_COLOR)
        self.bright_slider.setValue(DEFAULT_BRIGHTNESS_PCT)
        ok, err = apply_backlight(DEFAULT_COLOR, DEFAULT_BRIGHTNESS_PCT)
        if not ok:
            QMessageBox.critical(self, "Error", err)

    def on_service_action(self, action):
        ok, err = run_systemctl(action)
        if not ok:
            QMessageBox.critical(self, "Error", err)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("G510 LCD Control")
        self.resize(420, 300)

        tabs = QTabWidget()
        tabs.addTab(BacklightTab(), "Backlight")
        # Phase 2: tabs.addTab(CustomScreenTab(), "Custom Screen")
        self.setCentralWidget(tabs)


if __name__ == "__main__":
    app = QApplication(sys.argv)
    win = MainWindow()
    win.show()
    sys.exit(app.exec_())
