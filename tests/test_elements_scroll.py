"""Custom Screens: a long element list stays reachable (2026-11-16).

Report: "i cant scroll the list of added displaying elements, when theyre
too many they overflow under the window and i cant see them". Builds the
real main window offscreen with HOME pointed at a scratch dir (your real
~/.local/share/g510-lcd is never touched). Run via tests/run-tests.sh.
"""
import os
import sys
import tempfile

SCRATCH = tempfile.mkdtemp(prefix="g510-scroll-")
os.environ["HOME"] = SCRATCH
os.environ["XDG_RUNTIME_DIR"] = SCRATCH
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))

from PyQt5.QtWidgets import QApplication, QTabWidget  # noqa: E402

app = QApplication.instance() or QApplication([])
import g510_app as g  # noqa: E402

passes = fails = 0


def check(cond, msg):
    global passes, fails
    print(("  ok   " if cond else "  FAIL ") + msg)
    if cond:
        passes += 1
    else:
        fails += 1


def settle():
    for _ in range(5):
        app.processEvents()


w = g.MainWindow()
w.show()
settle()
tabs = w.findChild(QTabWidget)
tabs.setCurrentIndex(next(i for i in range(tabs.count()) if "Custom" in tabs.tabText(i)))
tab = w.findChild(g.CustomScreensTab)
tab.select_screen("L2")
el = {"kind": "sensor", "sensor": "CPU_TEMP", "style": "number", "x": 0, "y": 0, "width": 40, "font_size": 0}

# 1080p-like window: the list takes the free panel height and scrolls
w.resize(1244, 807)
tab.config["L2"] = [dict(el) for _ in range(20)]
tab.refresh_elements_list()
settle()
lb = tab.elements_scroll.verticalScrollBar()
check(lb.maximum() > 0 and lb.isVisible(), f"20 elements: list scrolls with a visible bar (range {lb.maximum()})")
check(tab.elements_scroll.height() >= 160, f"list uses the free panel height ({tab.elements_scroll.height()} px, was a fixed 180)")
check(not tab.panel_scroll.verticalScrollBar().isVisible(), "tall window: the panel itself needs no scrolling")
tall_list = tab.elements_scroll.height()
w.resize(1244, 1000)
settle()
check(tab.elements_scroll.height() > tall_list, f"bigger window -> longer list ({tall_list} -> {tab.elements_scroll.height()} px)")

# adding one more scrolls it into view
w.resize(1244, 807)
settle()
lb.setValue(0)
tab.config["L2"].append(dict(el))
tab.refresh_elements_list()
settle()
check(lb.value() == lb.maximum() and lb.maximum() > 0, "a newly added element is scrolled into view")
# switching screens doesn't jump to the bottom
tab.config["L3"] = [dict(el) for _ in range(20)]
tab.select_screen("L3")
settle()
check(lb.value() == 0 or lb.value() < lb.maximum(), "switching screens doesn't jump to the end")

# short screen: the whole panel scrolls, nothing is under the window edge
w.resize(1244, 500)
settle()
pb = tab.panel_scroll.verticalScrollBar()
check(pb.isVisible() and pb.maximum() > 0, f"short window: the panel scrolls (range {pb.maximum()})")
panel_bottom = tab.panel_scroll.mapTo(w, tab.panel_scroll.rect().bottomLeft()).y()
check(panel_bottom <= w.height(), f"panel ends inside the window ({panel_bottom} <= {w.height()})")

# FanPilot rows show their own name, not just "FanPilot: ..."
g.SENSOR_LABELS["FP:rpm.it8688/pwm2"] = "FanPilot: System fan 1 (SYS_FAN1) speed"
tab.config["L4"] = [dict(el, sensor="FP:rpm.it8688/pwm2")]
tab.select_screen("L4")
settle()
from PyQt5.QtCore import QCoreApplication, QEvent  # noqa: E402
from PyQt5.QtWidgets import QLabel  # noqa: E402
QCoreApplication.sendPostedEvents(None, QEvent.DeferredDelete)   # drop the previous screen's rows
texts = [lab.text() for lab in tab.elements_scroll.widget().findChildren(QLabel)]
check(texts and texts[-1].startswith("FP Syst"), f"FanPilot row starts with its own name ({texts[-1:]})")

print(f"elements scroll: {passes} passed, {fails} failed")
sys.exit(1 if fails else 0)
