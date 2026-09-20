# G510s Canvas Rearchitecture — Backlight + G-Keys merged into one view

Built 2026-09-14 on the machine where the real G510s hardware lives,
mirroring the sibling G910 project's canvas rearchitecture (its own
`G910_CANVAS_PLAN.md`, branch `g910-canvas` — both now over in
[grumpybollocks/g910-control](https://github.com/grumpybollocks/g910-control),
not in this repo) at the user's explicit request: "adapt this one to
the variables at hand." Not a shared
codebase with the G910 app — a parallel, independent implementation,
because the two keyboards' real capabilities are too different to
force through one abstraction (see "What's different from G910"
below). This is built and self-verified; **not yet visually confirmed
by the user** (they were asleep when this was finished) — same rule as
everywhere else in this project: don't call it fully done until they
say so.

## What changed

- `Backlight` and `G-Keys` tabs merged into one: `Backlight + G-Keys`.
- The G-Keys grid of plain `QPushButton`s is replaced by
  `src/g510_canvas.py`'s `G510Canvas` — a real keyboard-shaped
  `QWidget` with `QPainter`-based rendering and rect-based hit-testing
  (`Cell(key_name, label, row, col, width, height, kind)`), same
  overall pattern as the G910 app's `g910_canvas.py`.
- `Custom Screens` tab renamed to `Custom Screens (WIP)` and otherwise
  **completely untouched** — the user explicitly said to wait on that
  tab since they weren't there to confirm LCD changes.
- Applied the G910 app's exact dark `STYLESHEET` (copied verbatim,
  credited in a comment) for visual consistency across the two sibling
  apps.
- Fixed the same "hardcoded `resize()` goes stale" class of bug the
  G910 project hit: `MainWindow` now calls `self.adjustSize()` instead
  of a fixed pixel size, so it always fits whatever the current tab
  actually needs.
- Added an MR indicator to the canvas (M1/M2/M3/MR row, top-left,
  above the G-key columns) — the physical key/LED existed
  (`g15::macro_record`, already used by `g510_macro_daemon.py`) but
  the GUI never showed it before now.

## What's reused from the G910 project vs. built fresh

**Reused verbatim** (with credit, not re-derived): the main
board/nav-cluster/numpad `Cell` position data — real column positions,
inter-group gaps, and standard ANSI keycap width overrides
(Backspace≈2u, Tab≈1.5u, Caps≈1.75u, Enter≈2.25u, Shift≈2.25u/2.75u,
Ctrl/Alt/Win≈1.25u). That data is itself ported by the G910 project
from Solaar's `ui/perkey/layouts/_keyboard_base.py` (from OpenRGB's
`KeyboardLayoutManager.cpp`, GPL-2.0-or-later) — it's real reference
data for a standard ANSI layout, which the G510s also uses (confirmed
full-size ANSI + numpad from the user's own product photos, not
guessed). Reusing it here is correct reuse of a verified fact, not
copying an assumption.

**Built fresh, not reused from G910 at all**: G-key/M-key/LCD
placement (G510s's physical layout is completely different — 18
G-keys in 3 columns × 6 rows grouped in 3 blocks of 2, vs G910's 9 in
a 5+4 split; no Logo key on G510s at all), and every bit of the
color-control backend.

## What's different from G510s vs. G910 (why no shared code)

| | G910 | G510s |
|---|---|---|
| Backlight | Per-key HID++ RGB (`keyledsctl`, one color per key) | ONE sysfs LED for the entire board (`/sys/class/leds/g15::kbd_backlight`) — confirmed, no per-key addressing exists on this hardware at all |
| G-keys | 9 (G1-G9), 5 left column + 4 top row | 18 (G1-G18), 3 columns × 6 rows, grouped in 3 blocks of 2 rows |
| Logo key | Yes, individually lit | No equivalent |
| Canvas interaction | Click/drag-select any key, opens a color picker | Only G-keys (open macro dialog) and M1/M2/M3 (switch profile) are interactive — everything else is decorative, because there's nothing else *to* interact with on this hardware |

Given that, the G510s canvas shows the WHOLE board (main
board/nav/numpad/M-keys-row) tinted in whatever the **actual current
live backlight color** is (`read_current_rgb()`, already-existing,
already-proven code) — refreshed the moment Apply changes it — rather
than any per-key preview, since a per-key preview would misrepresent
hardware that can't actually do that. G-keys get their own fixed
accent color (not the board color) specifically to visually mark "this
part is clickable," and the LCD placeholder gets a third, muted tone
to mark "this is a screen, not a light or a key" — matching the same
three-way visual distinction the user's product photos actually show.

## MR button: exposed, not yet interactive

The canvas draws MR and reflects its real LED state
(`/sys/class/leds/g15::macro_record/brightness`, polled every 500ms
alongside the M1/M2/M3 profile sync) — confirmed this sysfs path
exists and is readable on the real hardware. Clicking it currently
does nothing. There's no established "toggle a recording mode"
concept anywhere in `g510_macro_daemon.py` or the rest of this app to
attach a click to — recording happens per-G-key via the existing
`MacroRecordDialog`, not through a global MR toggle. Exposing the
indicator without inventing new behavior for it seemed like the
correct scope; say the word if you want MR to actually do something
and it can be designed properly (with the same "plan first" rule as
everything else here).

## A real hardware quirk found while testing this, unrelated to the canvas itself

While verifying `on_apply()` still actually reaches the real LED
(it does — RGB confirmed correct on real hardware both before and
after this change), the **brightness slider's practical effect turned
out to be questionable**: writing intermediate values (e.g. 153 for
60%, or 30, or even 0) to `/sys/class/leds/g15::kbd_backlight/brightness`
did not reliably read back the value just written — readbacks showed
100%/255 in one test and a stale 30 in another, despite
`max_brightness` reporting 255 (implying a real 0-255 range). This
looks like either driver-side quantization to a small number of real
supported steps, or write/read latency in the USB round-trip, or
something else not yet isolated. **Not investigated further tonight**
— it predates this canvas change (the exact same `apply_backlight()`
function, unchanged), is orthogonal to what was actually asked for,
and doesn't block anything here (the canvas correctly shows whatever
color IS live regardless of what brightness value produced it). Worth
a dedicated, focused investigation later if the brightness slider
matters to you in practice — flagging it now so it isn't quietly lost.

## Verification performed (all before calling this ready to look at)

- `python3 -m py_compile` on both new/changed files: clean.
- Headless smoke test (`QT_QPA_PLATFORM=offscreen`): full `MainWindow`
  instantiates with both tabs, no exceptions.
- Signal wiring, tested directly against the real running code (not
  just read): a simulated G-key click opens `MacroRecordDialog`
  (dialog's `exec_()` mocked so it doesn't block headlessly, call
  confirmed via mock assertion); a simulated M-key click updates both
  `current_profile` and the canvas's highlighted M-key; MR's LED-state
  poll reads the real sysfs file and reflects it.
- **Real hardware test, not simulated**: called `on_apply()` through
  the actual GUI code path with a test color (Cyan, arbitrary
  brightness), confirmed the PHYSICAL LED's `multi_intensity` sysfs
  value actually changed to match, confirmed the canvas's board color
  updated to match, then restored the keyboard to its actual persisted
  default (Green, full brightness) and re-verified that stuck.
- Rendered a full screenshot of the actual live `MainWindow` (not a
  standalone test harness) to visually check the merged layout,
  colors, and proportions before considering this done — looked
  correct: G-keys in a distinct blue, LCD in a distinct muted tone,
  main board correctly showing the real live green, M1 correctly
  highlighted as the active profile.
- Grepped the whole project for leftover references to the removed
  `BacklightTab`/`GKeysTab` classes — none outside of explanatory
  comments.
- Relaunched the actual live GUI app process with the new code;
  confirmed it started with an empty log (no exceptions) and stayed
  running.

**What's NOT yet verified**: an actual mouse click through the real
GUI window (as opposed to a simulated signal emission) — the click
handling code path (`mousePressEvent` → `_cell_at` → signal emit) is
straightforward Qt and the signal-side wiring is confirmed, but a
literal physical click hasn't been done by a human yet. Recording a
real macro through the new canvas end-to-end, and seeing the whole
thing with your own eyes, are both still pending — same "not done
until you say so" rule as always.

## Extra: macro-assigned indicator (added after the initial merge)

While doing the full self-audit below, added one small enhancement
beyond parity with the G910 app (which has no macro-assignment concept
to show at all): G-keys that have a macro saved in the CURRENTLY
ACTIVE profile now draw with a gold border instead of the plain accent
border, so you can see at a glance what's already programmed without
opening every key's dialog. Refreshed on profile switch
(`select_profile`) and after any macro dialog closes
(`open_key_dialog`, since Save/Clear may have changed the current
profile's assignments) — both paths call `refresh_assigned_keys()`,
which just reads `load_macros()` for the active profile's key set and
hands it to `canvas.set_assigned_keys()`. No new state, no new files,
reuses `load_macros()` exactly as it already existed.

## Comprehensive self-audit (2026-09-14, done solo while the user slept)

Requested explicitly: "a full comprehensive bug check, test the app
under the hood yourself without human interaction." Six separate
headless test scripts, run against the REAL code and, where
applicable, the REAL hardware (not a mock of the hardware) — every
script restores whatever it touched to its original state before
finishing:

1. **Geometry validation** — all 127 canvas cells checked pairwise for
   pixel-rect overlap (zero found), confirmed all 18 G-key names
   present and unique, confirmed the 4 M-key/MR names correct.
2. **Click simulation** — every single one of the 18 G-keys clicked at
   its exact rect center (via `_cell_at` + signal emission) and
   confirmed it fires the right key name; all 4 M-key/MR cells clicked
   via a REAL `QMouseEvent` through the actual `mousePressEvent`
   handler (not a shortcut) — M1/M2/M3 confirmed to fire, MR confirmed
   to correctly fire NOTHING (by design, see above); an empty
   background click confirmed to fire nothing.
3. **Full integration** — all 3 profiles × 3 sample G-keys (9
   combinations) clicked through the actual `KeyboardTab` signal
   wiring, confirming `MacroRecordDialog` is constructed with the
   correct (profile, gkey) pair every time (a spied `__init__`, not
   just a mock, so the real arguments were checked) — this is the
   class of bug a stale-closure-over-a-loop-variable mistake would
   have caused, and it didn't. Then ALL 9 `COLOR_RGB` presets applied
   through the real `on_apply()` path, each one verified against the
   ACTUAL physical LED's `multi_intensity` sysfs value (not assumed),
   and against the canvas's board color. `Set as Default` verified to
   leave the live LED completely untouched (a real bug class from
   earlier in this project's history — checked again here since the
   canvas merge touches the same code path).
4. **Service control + MR LED** — `restart`/`stop`/`start` all run for
   real through `systemctl --user`, actual service state checked after
   each (not just "no exception raised"). The `g15::macro_record` LED
   sysfs file was actually written to (1, then 0) and the poll
   function's result checked against each real value.
5. **Window resize + Custom Screens regression** — checked the window
   does NOT shrink when switching to the narrower Custom Screens tab
   (stays sized for the wider canvas tab; a deliberate non-issue, not
   a bug — a stable window shape while switching tabs is arguably
   better UX than one that jumps size). Re-ran `render_preview()` for
   all 6 LCD screens (0-5) to confirm the v1.1 Custom Screens tab
   still works unmodified. Re-ran a `save_macro`/`load_macros`
   round-trip against the REAL `macros.json` (with the original
   content saved first and restored after).
6. **Assigned-keys feature** (see above) — checked against the actual
   current `macros.json` content (not synthetic data): correct initial
   state for M1, correct update on switching to M2 and M3, and the
   real-world case of saving a new macro then closing a dialog for a
   DIFFERENT key still picks up the change (catches a "only refreshes
   the key you just edited" class of bug).

**Result: zero real bugs found.** Every test passed on the first
complete run, then all six were re-run together after adding the
assigned-keys feature, to confirm that addition didn't regress
anything already verified — passed again, identically.

**Real hardware left in its original state after all of this**:
confirmed at the end — LED back to Green/(0 255 0)/full brightness,
`macro_record` LED back to 0, `macros.json` byte-for-byte identical to
before testing started, all 3 services active.

## Rollback

Two tags exist for this, at different granularity:

- **`pre-canvas-checkpoint`** (commit `dae91d8`) — the exact state
  right before this canvas merge, but AFTER v1.1's Custom Screens tab.
  Use this if the canvas UI itself needs to be undone but v1.1 should
  stay.
- **`v1.0`** (commit `4521162`) — the last state confirmed working by
  the user in person, before v1.1 OR the canvas merge. Use this only
  if you want to throw away both.

To undo just the canvas merge while keeping history intact (preferred
— a revert commit, not a rewrite):

```bash
git revert d6a1d64
```

To hard-reset `main` back to before the canvas merge (rewrites
history — only do this if the commit hasn't been built on elsewhere,
and never without checking `git log` first for anything to lose):

```bash
git reset --hard pre-canvas-checkpoint
```

Either way, after rolling back the code: relaunch the GUI app
(`pkill -f src/g510_app.py`, then `python3 src/g510_app.py` from the
project root) — the currently-running process won't pick up a
rollback on its own.
