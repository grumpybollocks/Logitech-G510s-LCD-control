#!/usr/bin/env python3
"""
G510s keyboard canvas -- real per-key geometry, QPainter rendering,
rect-based hit-testing. Sibling to the G910 project's g910_canvas.py
(same visual pattern, dark theme, per-cell layout model) but built as
an independent file, not a shared import -- the two keyboards' real
capabilities are too different to force through one abstraction:

  G910: per-key HID++ RGB, 9 G-keys (5 left column + 4 top row), a
        Logo key with its own LED, click-to-color on individual keys.
  G510s: ONE sysfs LED for the whole board (no per-key addressing at
        all -- confirmed in this project's own README), 18 G-keys in
        3 columns x 6 rows (grouped in 3 blocks of 2 rows, matching
        the real physical board -- confirmed from actual product
        photos, not guessed), no Logo key with its own light.

So here, only the G-keys and M1/M2/M3/MR are genuinely interactive
(clicking a G-key opens the existing macro-record dialog; clicking an
M-key switches the active profile). Everything else -- the main board,
nav cluster, numpad, and the LCD -- is drawn purely for visual
completeness/realism, tinted with whatever the CURRENT live backlight
color actually is (read from read_current_rgb(), not picked for
looks), since that's the only truthful thing to show for hardware that
has just one color for the entire board.

Main board / nav cluster / numpad position data below is ported
verbatim from the G910 project's own port of Solaar's
ui/perkey/layouts/_keyboard_base.py (itself ported from OpenRGB's
KeyboardLayoutManager.cpp, GPL-2.0-or-later) -- real column positions
and inter-group gaps for a standard ANSI layout, which the G510s also
uses (confirmed full-size ANSI + numpad from real product photos).
G-key/M-key/LCD placement is this project's own, derived from the
actual product photos the user provided (not the G910's, which don't
apply here at all).
"""
import sys
from dataclasses import dataclass
from PyQt5.QtCore import Qt, QRectF, QPointF, QSizeF, pyqtSignal
from PyQt5.QtGui import QPainter, QColor, QPainterPath, QFont, QPen
from PyQt5.QtWidgets import QWidget, QApplication

CELL_PX = 32
GUTTER_PX = 4
# 10 -> 16: was the only real lever for the LCD panel's own top gap
# ("add a bit more space on top and bottom so it's symmetrical") --
# the LCD is the topmost cell on the board, so its distance to the
# canvas edge is structurally always exactly PADDING_PX, whatever its
# own row value is. This is the canvas's outer margin on all four
# sides, not LCD-specific, but a uniform +6px is itself inherently
# symmetrical and a reasonable amount of extra breathing room overall.
PADDING_PX = 16


@dataclass
class Cell:
    key_name: str        # stable identifier; "G1".."G18", "_M1".."_MR", or a real key name for decorative cells
    label: str            # what's drawn on the key
    row: float
    col: float
    width: float = 1.0
    height: float = 1.0
    kind: str = "main"     # "main" / "gkey" / "mkey" / "lcd" -- decides both styling and click behavior


# --- G-keys: 3 columns x 6 rows, grouped in 3 blocks of 2 rows each
# with a gap between blocks -- matches the real board (confirmed from
# actual product photos: G1-G3/G4-G6, G7-G9/G10-G12, G13-G15/G16-G18,
# small gaps between blocks, far left edge spanning roughly the F-row
# down to the Ctrl row).
GKEY_COL0 = -3.3
_gkey_rows = [0, 1, 2.3, 3.3, 4.6, 5.6]
GKEY_CELLS = []
for block in range(3):
    for sub_row in range(2):
        row = _gkey_rows[block * 2 + sub_row]
        for col_i in range(3):
            n = block * 6 + sub_row * 3 + col_i + 1
            GKEY_CELLS.append(Cell(f"G{n}", f"G{n}", row, GKEY_COL0 + col_i, kind="gkey"))

# --- M1/M2/M3/MR: one row of 4, directly above the G-key columns
# (matches the real board -- top-left corner, above G1-G3).
MKEY_CELLS = [
    Cell("_M1", "M1", -1.3, GKEY_COL0, width=0.75, height=0.85, kind="mkey"),
    Cell("_M2", "M2", -1.3, GKEY_COL0 + 0.85, width=0.75, height=0.85, kind="mkey"),
    Cell("_M3", "M3", -1.3, GKEY_COL0 + 1.7, width=0.75, height=0.85, kind="mkey"),
    Cell("_MR", "MR", -1.3, GKEY_COL0 + 2.55, width=0.75, height=0.85, kind="mkey"),
]
MKEY_NAMES = {"_M1", "_M2", "_M3", "_MR"}

# --- LCD: top-center above the WHOLE board. Real, visible mistake in
# the previous pass: centered against MAIN_CELLS alone (col range
# 0..15, center 7.5) instead of the true full render including G-keys
# and the nav/numpad cluster (col range -3.3..23.5, center 10.1) --
# looked centered against the F-row/alphanumeric block in isolation
# but visibly left-shifted against the actual whole-keyboard image, a
# real screenshot caught it directly. col computed as true_center -
# width/2 = 10.1 - 5.0 = 5.1.
#
# On its own row (-2.95, above the M-key row at -1.3 rather than
# sharing it) so it has real vertical room to grow -- the first
# "bigger" attempt (height=1.2) was still constrained by the M-key
# row's slot and came out *smaller* than the real LCD's native 160x43
# once scaled into that box. This size renders at ~2.2x native scale.
#
# height=2.77 (not a round number, deliberately) -- at width=10 the
# cell's own pixel aspect ratio was 356/93.2=3.82, slightly wider than
# the real LCD's 160/43=3.72, so KeepAspectRatio letterboxed a few
# pixels on the sides even though the content filled the full height
# ("make it a few more pixels tall so it fills the whole space" --
# taller, not wider, was the right fix specifically because height
# was already the constraining/fully-filled dimension). Solved
# directly for the height that makes the cell's own ratio match
# 160/43 exactly at this width: (356 / (160/43) + 4) / 36 = 2.7688,
# rounded to 2.77 -- not eyeballed.
#
# row: the top gap to the canvas edge is always exactly PADDING_PX
# regardless of this cell's own row (see PADDING_PX's comment above),
# so this row value only ever controls the BOTTOM gap (distance to the
# main board below). Was -3.10 (measured top=16.00px, bottom=15.88px,
# a genuinely symmetrical pair) -- moved further up to -3.60 on direct
# request for visibly more separation from the keyboard specifically,
# no longer aiming for top==bottom equality this time. Adds ~18px more
# bottom gap (0.5 row units * 36px/unit); top gap is unaffected by
# construction, still exactly PADDING_PX.
LCD_CELLS = [
    Cell("_LCD", "LCD", -3.60, 5.1, width=10.0, height=2.77, kind="lcd"),
]

# --- Main board, nav cluster, numpad: ported verbatim from the G910
# project's own Solaar/OpenRGB-derived position data (see module
# docstring) -- same standard ANSI layout, decorative here (no per-key
# color on this hardware).
MAIN_CELLS = [
    Cell("ESC", "Esc", 0, 0), Cell("F1", "F1", 0, 2), Cell("F2", "F2", 0, 3),
    Cell("F3", "F3", 0, 4), Cell("F4", "F4", 0, 5), Cell("F5", "F5", 0, 6),
    Cell("F6", "F6", 0, 7), Cell("F7", "F7", 0, 8), Cell("F8", "F8", 0, 9),
    Cell("F9", "F9", 0, 10), Cell("F10", "F10", 0, 11), Cell("F11", "F11", 0, 12),
    Cell("F12", "F12", 0, 13),
    Cell("GRAVE", "`", 1, 0), Cell("1", "1", 1, 1), Cell("2", "2", 1, 2),
    Cell("3", "3", 1, 3), Cell("4", "4", 1, 4), Cell("5", "5", 1, 5),
    Cell("6", "6", 1, 6), Cell("7", "7", 1, 7), Cell("8", "8", 1, 8),
    Cell("9", "9", 1, 9), Cell("0", "0", 1, 10), Cell("MINUS", "-", 1, 11),
    Cell("EQUAL", "=", 1, 12), Cell("BACKSPACE", "Bksp", 1, 13, width=2.0),
    Cell("TAB", "Tab", 2, 0, width=1.5), Cell("Q", "Q", 2, 1.5), Cell("W", "W", 2, 2.5),
    Cell("E", "E", 2, 3.5), Cell("R", "R", 2, 4.5), Cell("T", "T", 2, 5.5),
    Cell("Y", "Y", 2, 6.5), Cell("U", "U", 2, 7.5), Cell("I", "I", 2, 8.5),
    Cell("O", "O", 2, 9.5), Cell("P", "P", 2, 10.5), Cell("LBRACE", "[", 2, 11.5),
    Cell("RBRACE", "]", 2, 12.5), Cell("BACKSLASH", "\\", 2, 13.5, width=1.5),
    Cell("CAPSLOCK", "Caps", 3, 0, width=1.75), Cell("A", "A", 3, 1.75),
    Cell("S", "S", 3, 2.75), Cell("D", "D", 3, 3.75), Cell("F", "F", 3, 4.75),
    Cell("G", "G", 3, 5.75), Cell("H", "H", 3, 6.75), Cell("J", "J", 3, 7.75),
    Cell("K", "K", 3, 8.75), Cell("L", "L", 3, 9.75), Cell("SEMICOLON", ";", 3, 10.75),
    Cell("APOSTROPHE", "'", 3, 11.75), Cell("ENTER", "Enter", 3, 12.75, width=2.25),
    Cell("LSHIFT", "Shift", 4, 0, width=2.25), Cell("Z", "Z", 4, 2.25),
    Cell("X", "X", 4, 3.25), Cell("C", "C", 4, 4.25), Cell("V", "V", 4, 5.25),
    Cell("B", "B", 4, 6.25), Cell("N", "N", 4, 7.25), Cell("M", "M", 4, 8.25),
    Cell("COMMA", ",", 4, 9.25), Cell("DOT", ".", 4, 10.25), Cell("SLASH", "/", 4, 11.25),
    Cell("RSHIFT", "Shift", 4, 12.25, width=2.75),
    Cell("LCTRL", "Ctrl", 5, 0, width=1.25),
    Cell("LWIN", "Win", 5, 1.25, width=1.25),
    Cell("LALT", "Alt", 5, 2.5, width=1.25),
    Cell("SPACE", "Space", 5, 3.75, width=6.25),
    Cell("ALTGR", "AltGr", 5, 10.0, width=1.25),
    Cell("RWIN", "Win", 5, 11.25, width=1.25),
    Cell("MENU", "Menu", 5, 12.5, width=1.25),
    Cell("RCTRL", "Ctrl", 5, 13.75, width=1.25),
]

NAV_COL0 = 15.5
NAV_CELLS = [
    Cell("SYSRQ", "PrtSc", 0, NAV_COL0), Cell("SCROLLLOCK", "ScrLk", 0, NAV_COL0 + 1),
    Cell("PAUSE", "Pause", 0, NAV_COL0 + 2),
    Cell("INSERT", "Ins", 1, NAV_COL0), Cell("HOME", "Home", 1, NAV_COL0 + 1),
    Cell("PAGEUP", "PgUp", 1, NAV_COL0 + 2),
    Cell("DELETE", "Del", 2, NAV_COL0), Cell("END", "End", 2, NAV_COL0 + 1),
    Cell("PAGEDOWN", "PgDn", 2, NAV_COL0 + 2),
    Cell("UP", "^", 4, NAV_COL0 + 1),
    Cell("LEFT", "<", 5, NAV_COL0), Cell("DOWN", "v", 5, NAV_COL0 + 1),
    Cell("RIGHT", ">", 5, NAV_COL0 + 2),
]

NUMPAD_COL0 = NAV_COL0 + 4
NUMPAD_CELLS = [
    Cell("NUMLOCK", "Num", 1, NUMPAD_COL0), Cell("KPSLASH", "/", 1, NUMPAD_COL0 + 1),
    Cell("KPASTERISK", "*", 1, NUMPAD_COL0 + 2), Cell("KPMINUS", "-", 1, NUMPAD_COL0 + 3),
    Cell("KP7", "7", 2, NUMPAD_COL0), Cell("KP8", "8", 2, NUMPAD_COL0 + 1),
    Cell("KP9", "9", 2, NUMPAD_COL0 + 2), Cell("KPPLUS", "+", 2, NUMPAD_COL0 + 3, height=2.0),
    Cell("KP4", "4", 3, NUMPAD_COL0), Cell("KP5", "5", 3, NUMPAD_COL0 + 1),
    Cell("KP6", "6", 3, NUMPAD_COL0 + 2),
    Cell("KP1", "1", 4, NUMPAD_COL0), Cell("KP2", "2", 4, NUMPAD_COL0 + 1),
    Cell("KP3", "3", 4, NUMPAD_COL0 + 2), Cell("KPENTER", "Enter", 4, NUMPAD_COL0 + 3, height=2.0),
    Cell("KP0", "0", 5, NUMPAD_COL0, width=2.0), Cell("KPDOT", ".", 5, NUMPAD_COL0 + 2),
]

ALL_CELLS = MKEY_CELLS + LCD_CELLS + GKEY_CELLS + MAIN_CELLS + NAV_CELLS + NUMPAD_CELLS

# --- Colors: dark theme matching the G910 app's palette for visual
# consistency across the two sibling apps.
BG_COLOR = QColor(0x17, 0x17, 0x1a)
BOARD_UNSET_COLOR = QColor(42, 42, 46)     # shown before the first live sync
KEY_BORDER_COLOR = QColor(10, 10, 12)
GKEY_COLOR = QColor(58, 90, 130)            # distinct accent -- signals "clickable"
GKEY_BORDER_COLOR = QColor(80, 130, 190)
LCD_COLOR = QColor(70, 90, 60)               # muted screen-like tone -- visually distinct as "not a key"
LCD_BORDER_COLOR = QColor(100, 104, 110)     # cool steel-gray bezel, distinct from every key's near-black border
MKEY_UNSET_COLOR = QColor(50, 50, 55)
ACTIVE_MKEY_COLOR = QColor(58, 108, 196)     # matches G910's active-profile accent
MR_ACTIVE_COLOR = QColor(196, 70, 58)
ASSIGNED_BORDER_COLOR = QColor(230, 175, 60)  # warm gold -- "this G-key has a macro"


class G510Canvas(QWidget):
    """Real keyboard-shaped canvas for the G510s. Click a G-key to open
    its macro dialog; click M1/M2/M3 to switch the active profile. MR
    is drawn to reflect the real macro-record LED state (this project
    never exposed it in the GUI before) but isn't interactive yet --
    there's no established "toggle recording mode" concept in the
    daemon to attach a click to."""
    gkey_clicked = pyqtSignal(str)   # "G1".."G18"
    mkey_clicked = pyqtSignal(str)   # "M1"/"M2"/"M3" (MR excluded -- see above)

    def __init__(self):
        super().__init__()
        self._board_color = BOARD_UNSET_COLOR
        self._active_mkey = "M1"
        self._mr_active = False
        self._assigned = set()  # G-key names with a macro in the current profile
        self._lcd_pixmap = None  # live mirror of the real LCD, set by set_lcd_pixmap()
        self.setMouseTracking(True)  # needed to get hover moves without a button held
        self._compute_size()

    def set_lcd_pixmap(self, pixmap):
        """A live-rendered thumbnail of whatever's actually on the
        physical LCD right now (see KeyboardTab's refresh_lcd_mirror()
        in g510_app.py, which decides WHICH screen to render and calls
        this on a timer). None falls back to the plain placeholder
        fill+label, e.g. before the first render completes."""
        self._lcd_pixmap = pixmap
        self.update()

    def set_board_color(self, qcolor):
        self._board_color = qcolor
        self.update()

    def set_active_mkey(self, name):
        self._active_mkey = name
        self.update()

    def set_mr_active(self, on):
        self._mr_active = on
        self.update()

    def set_assigned_keys(self, key_names):
        """Which G-keys have a macro assigned in the CURRENT profile --
        drawn with a distinct border so you can see at a glance what's
        already programmed without opening every key's dialog."""
        self._assigned = set(key_names)
        self.update()

    def _compute_size(self):
        min_row = min(c.row for c in ALL_CELLS)
        min_col = min(c.col for c in ALL_CELLS)
        max_row_bottom = max(c.row + c.height for c in ALL_CELLS)
        max_col_right = max(c.col + c.width for c in ALL_CELLS)
        rows = max_row_bottom - min_row
        cols = max_col_right - min_col
        w = int(PADDING_PX * 2 + cols * (CELL_PX + GUTTER_PX))
        h = int(PADDING_PX * 2 + rows * (CELL_PX + GUTTER_PX))
        self._row_offset = -min_row
        self._col_offset = -min_col
        self.setMinimumSize(w, h)

    def _cell_rect(self, cell):
        x = PADDING_PX + (cell.col + self._col_offset) * (CELL_PX + GUTTER_PX)
        y = PADDING_PX + (cell.row + self._row_offset) * (CELL_PX + GUTTER_PX)
        w = cell.width * CELL_PX + max(0.0, cell.width - 1.0) * GUTTER_PX
        h = cell.height * CELL_PX + max(0.0, cell.height - 1.0) * GUTTER_PX
        return QRectF(x, y, w, h)

    def _cell_at(self, point):
        for cell in ALL_CELLS:
            if self._cell_rect(cell).contains(point):
                return cell
        return None

    def _label_color(self, fill):
        lum = 0.299 * fill.redF() + 0.587 * fill.greenF() + 0.114 * fill.blueF()
        return QColor(0, 0, 0) if lum > 0.55 else QColor(230, 230, 230)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.fillRect(self.rect(), BG_COLOR)
        font = QFont()
        font.setPointSize(8)
        mkey_font = QFont()
        mkey_font.setPointSize(7)
        painter.setFont(font)

        for cell in ALL_CELLS:
            rect = self._cell_rect(cell)
            path = QPainterPath()
            path.addRoundedRect(rect, 4, 4)

            if cell.key_name == f"_{self._active_mkey}":
                fill = ACTIVE_MKEY_COLOR
            elif cell.key_name == "_MR" and self._mr_active:
                fill = MR_ACTIVE_COLOR
            elif cell.kind == "mkey":
                fill = MKEY_UNSET_COLOR
            elif cell.kind == "gkey":
                fill = GKEY_COLOR
            elif cell.kind == "lcd":
                fill = LCD_COLOR
            else:
                fill = self._board_color

            painter.fillPath(path, fill)
            if cell.kind == "gkey" and cell.key_name in self._assigned:
                border, pen_width = ASSIGNED_BORDER_COLOR, 2
            elif cell.kind == "gkey":
                # 2px, same weight as the LCD's own bezel ("add some
                # of the same around the G keys too") -- keeps the
                # existing blue accent color rather than switching to
                # the LCD's steel-gray, since that blue is what
                # actually signals "this is clickable" and isn't
                # meaningful to change just for a matching outline.
                border, pen_width = GKEY_BORDER_COLOR, 2
            elif cell.kind == "lcd":
                border, pen_width = LCD_BORDER_COLOR, 2
            else:
                border, pen_width = KEY_BORDER_COLOR, 1
            painter.setPen(QPen(border, pen_width))
            painter.drawPath(path)

            if cell.kind == "lcd" and self._lcd_pixmap is not None:
                # Real LCD is 160x43 (3.72:1) -- KeepAspectRatio never
                # upscales past the cell's own bounds, so a mismatched
                # cell aspect ratio just letterboxes instead of
                # stretching/distorting the mirrored content.
                # FastTransformation (nearest-neighbor), not Smooth --
                # this is a 1-bit monochrome pixel display being
                # mirrored at ~2.2x; bilinear smoothing blurs its sharp
                # pixel edges into soft gray gradients (direct report:
                # "looks blurry now"), where nearest-neighbor keeps it
                # crisp and blocky, honestly representing what the real
                # hardware actually looks like instead of prettifying it.
                # A small inset so the steel-gray border reads as a
                # real bezel framing the screen, not just an outline
                # sitting flush against the content's own edge pixels.
                bezel = 3
                inner_size = (rect.size() - QSizeF(bezel * 2, bezel * 2)).toSize()
                scaled = self._lcd_pixmap.scaled(
                    inner_size, Qt.KeepAspectRatio, Qt.FastTransformation
                )
                px = rect.x() + (rect.width() - scaled.width()) / 2
                py = rect.y() + (rect.height() - scaled.height()) / 2
                painter.drawPixmap(int(px), int(py), scaled)
                # Redrawn on top -- the pixmap above would otherwise
                # sit over the outer edge of the border stroke just
                # painted, dulling it right where it matters most.
                painter.setPen(QPen(border, pen_width))
                painter.drawPath(path)
            else:
                painter.setPen(self._label_color(fill))
                painter.setFont(mkey_font if cell.kind == "mkey" else font)
                painter.drawText(rect, Qt.AlignCenter, cell.label)

    def _is_clickable(self, cell):
        return cell is not None and (cell.kind == "gkey" or cell.key_name in ("_M1", "_M2", "_M3"))

    def mouseMoveEvent(self, event):
        # Pointing-hand cursor over anything clickable -- the only hint
        # this project gives that a cell does something, since nothing
        # else on the canvas visually says "button" the way a QPushButton
        # would have.
        cell = self._cell_at(QPointF(event.pos()))
        self.setCursor(Qt.PointingHandCursor if self._is_clickable(cell) else Qt.ArrowCursor)

    def mousePressEvent(self, event):
        if event.button() != Qt.LeftButton:
            return
        cell = self._cell_at(QPointF(event.pos()))
        if cell is None:
            return
        if cell.kind == "gkey":
            self.gkey_clicked.emit(cell.key_name)
        elif cell.key_name in ("_M1", "_M2", "_M3"):
            self.mkey_clicked.emit(cell.label)
        # MR and every decorative cell: no action, by design (see class docstring)


if __name__ == "__main__":
    app = QApplication(sys.argv)
    win = G510Canvas()
    win.setWindowTitle("G510s Canvas -- standalone test")
    win.set_board_color(QColor(110, 0, 255))
    win.show()
    sys.exit(app.exec_())
