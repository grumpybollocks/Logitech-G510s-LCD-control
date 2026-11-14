"""FanPilot sensors in the Custom Screens tab (2026-11-14).

Builds the real CustomScreensTab offscreen with HOME pointed at a scratch
dir (so your real ~/.local/share/g510-lcd is never touched) and a fake
FanPilot sensors file. Run via tests/run-tests.sh.
"""
import os
import sys
import tempfile
import time

SCRATCH = tempfile.mkdtemp(prefix="g510-fp-gui-")
os.environ["HOME"] = SCRATCH
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))

from PyQt5.QtWidgets import QApplication  # noqa: E402

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


SENS = os.path.join(SCRATCH, "sensors.tsv")
NOT_INSTALLED = os.path.join(SCRATCH, "no-such-fanpilotd")
INSTALLED = os.path.join(SCRATCH, "fanpilotd")
open(INSTALLED, "w").close()
ROWS = ("t.k10temp/temp1\ttemp\tCPU\tCPU (Tctl)\t55.6\n"
        "rpm.it8688/pwm2\trpm\tSYS1\tSystem fan 1 (SYS_FAN1) speed\t1180\n"
        "pct.it8688/pwm2\tpct\tSY1%\tSystem fan 1 (SYS_FAN1) duty\t\n")


def write(stamp=None, rows=ROWS):
    with open(SENS, "w") as f:
        f.write(f"# fanpilot-sensors 1 {int(stamp if stamp is not None else time.time())}\n{rows}")


g.FANPILOT_SENSORS_FILE = SENS
g.FANPILOT_DAEMON = NOT_INSTALLED
base = len(g.SENSOR_CHOICES)

tab = g.CustomScreensTab()
combo = tab.sensor_combo
check(combo.count() == base, f"no FanPilot: dropdown unchanged ({combo.count()} items)")

g.FANPILOT_DAEMON = INSTALLED
tab.reload_fanpilot_choices()
hint = combo.model().item(combo.count() - 1)
check(combo.count() == base + 2 and hint is not None and not hint.isEnabled(),
      "installed but not running: separator + greyed-out hint")

write()
tab.reload_fanpilot_choices()
labels = [combo.itemText(i) for i in range(combo.count())]
check(combo.count() == base + 1 + 3, f"running: separator + 3 FanPilot entries ({combo.count() - base - 1})")
check("FanPilot: CPU (Tctl)" in labels and "FanPilot: System fan 1 (SYS_FAN1) speed" in labels,
      "entries use FanPilot's friendly names")

i_temp = combo.findData("FP:t.k10temp/temp1")
combo.setCurrentIndex(i_temp)
check(tab.bar_hint_label.isHidden(), "temperature: bar allowed")
check("right now 55.6 °C" in tab.sensor_desc_label.text() and '"CPU"' in tab.sensor_desc_label.text(),
      "description shows the live value and LCD label")
combo.setCurrentIndex(combo.findData("FP:rpm.it8688/pwm2"))
check(not tab.bar_hint_label.isHidden(), "rpm: no honest 0-100 scale -> number only")
combo.setCurrentIndex(combo.findData("FP:pct.it8688/pwm2"))
check("no reading right now" in tab.sensor_desc_label.text(), "empty value described honestly")

# review finding: the description follows the live value even when the list is unchanged
combo.setCurrentIndex(i_temp)
write(rows=ROWS.replace("55.6", "77.0"))
tab.reload_fanpilot_choices()
check("right now 77 °C" in tab.sensor_desc_label.text(), "description refreshes the live value (77 °C)")
# a renamed label reaches the dropdown text
write(rows=ROWS.replace("CPU (Tctl)", "Processor"))
tab.reload_fanpilot_choices()
check(combo.itemText(combo.findData("FP:t.k10temp/temp1")) == "FanPilot: Processor", "renamed label updates the item")
# kind change: bar capability follows the fresh read
write(rows=ROWS.replace("t.k10temp/temp1\ttemp", "t.k10temp/temp1\trpm"))
tab.reload_fanpilot_choices()
check("FP:t.k10temp/temp1" not in g.BAR_CAPABLE_SENSORS, "kind temp -> rpm: no longer bar-capable")
write()
tab.reload_fanpilot_choices()
# nan / inf / huge and over-long keys
write(rows="pct.n/pwm1\tpct\tN\tn\tnan\nrpm.h/pwm1\trpm\tH\th\t1e30\n" + "t." + "k" * 46 + "\ttemp\tL\tl\t30\n")
items = {it["key"]: it for it in g.fanpilot_sensors(SENS)}
check(items["FP:pct.n/pwm1"]["value"] is None and items["FP:rpm.h/pwm1"]["value"] is None,
      "nan / 1e30 -> no value (same as the LCD)")
check(not any(len(k) > 50 for k in items), "keys too long for the LCD program are not offered")
write()
tab.reload_fanpilot_choices()

# selection is kept when the list didn't change
combo.setCurrentIndex(i_temp)
write()
tab.reload_fanpilot_choices()
check(combo.currentData() == "FP:t.k10temp/temp1", "selection kept across a refresh")

# adding an element saves the FP: key exactly
tab.current_screen = "L2"
before = len(tab.config["L2"])
tab.on_add_element()
saved = open(g.CUSTOM_SCREENS_FILE).read()
check(len(tab.config["L2"]) == before + 1 and "sensor=FP:t.k10temp/temp1" in saved,
      "added element saved as sensor=FP:t.k10temp/temp1")
reloaded = g.load_custom_screens()
check(any(el.get("sensor") == "FP:t.k10temp/temp1" for el in reloaded["L2"]), "and loads back")

# the hint row can never be added
os.unlink(SENS)
tab.reload_fanpilot_choices()
combo.setCurrentIndex(combo.count() - 1)
n = len(tab.config["L2"])
tab.on_add_element()
check(len(tab.config["L2"]) == n, "greyed-out hint row is never added as an element")

# names for saved elements when FanPilot is gone
check(g.sensor_display_name("FP:t.k10temp/temp1") == "FanPilot: CPU (Tctl)", "known FP key keeps its name")
check("FanPilot not running" in g.sensor_display_name("FP:rpm.other/pwm9"), "unknown FP key explained")
check(g.sensor_display_name("CPU_TEMP") == "CPU Temp", "built-in names unchanged")

# stale / damaged files
write(stamp=time.time() - 120)
check(g.fanpilot_sensors(SENS) == [], "2-minute-old file (FanPilot stopped) -> ignored")
with open(SENS, "w") as f:
    f.write("# fanpilot-sensors 2 %d\n%s" % (time.time(), ROWS))
check(g.fanpilot_sensors(SENS) == [], "unknown format version -> ignored")
write(rows="bad line\nx\tvolts\tV\tv\t1\nt.ok/temp1\ttemp\tOK\tfine\tabc\n")
items = g.fanpilot_sensors(SENS)
check(len(items) == 1 and items[0]["value"] is None, "damaged rows skipped, non-number -> no value")

print(f"fanpilot GUI: {passes} passed, {fails} failed")
sys.exit(1 if fails else 0)
