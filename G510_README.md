# G510s LCD Stats Screen + Buttons + Backlight

Full technical deep-dive for the G510s app: protocol details, bug
history, build gotchas. This predates the two-keyboard repo landing
page (`README.md`), so it refers to itself as "this project"
throughout. Start with **Current Status** below, then jump to a
specific section as needed.

## Current Status

Skip ahead to a specific section below if you need deep detail on something in particular.

**v1.0 TAGGED** (git tag `v1.0`, pushed). Everything in the v1.0 section below is DONE and confirmed working by the user — don't re-diagnose any of it, only read further sections if something specific is actually broken.

**v1.1 BUILT, NOT YET TAGGED** — code is committed on main, rebuilt binaries are live and running on the real hardware, and everything has been verified by the assistant (compiled clean, ran repeatedly with no crash, output visually inspected pixel-by-pixel). It has NOT yet been physically confirmed by the user on the actual keyboard LCD — don't tag v1.1 or claim it's "done" until that happens.

**Backlight+G-Keys canvas rearchitecture BUILT, NOT YET CONFIRMED** (2026-09-14, see `G510_CANVAS_PLAN.md` for the full writeup) — the Backlight and G-Keys tabs are now merged into one "Backlight + G-Keys" tab, with a real keyboard-shaped canvas (`src/g510_canvas.py`) replacing the old plain-grid G-Keys UI, mirroring the sibling G910 app's visual style (same dark stylesheet, same canvas+panel pattern) but independently built since the two keyboards' real capabilities are too different to share code. Self-verified thoroughly by the assistant (headless smoke test, real signal-wiring test, a REAL hardware Apply test confirming the physical LED color actually changes and the canvas reflects it, full-window screenshot QA) while the user was asleep and unable to look at it themselves — treat this the same as v1.1's Custom Screens tab: built and tested as far as possible without a human looking at the real result, not "done" until they actually click through it. A real, pre-existing (not introduced by this change) hardware quirk was found and documented at the time, not yet fixed: the brightness slider's intermediate values don't reliably read back from `/sys/class/leds/g15::kbd_backlight/brightness` the way `max_brightness=255` implies they should — see `G510_CANVAS_PLAN.md` for details. **Update 2026-09-15**: investigated properly and confirmed genuinely broken (every write reads back as 255, no exceptions) — the slider has been removed entirely rather than left as a non-functional control; see the BACKLIGHT section below.

**Custom Screens phase-1 polish BUILT, on branch `g510s-dev`, NOT on main, NOT confirmed** (2026-09-15) — PNG/image import (already on main) got a post-launch self-audit that found and fixed two real robustness gaps (unterminated buffer on the image path field; height wasn't constrained on import, which combined with the other fix would have made a too-tall image silently vanish at load with zero feedback), then a discoverability pass on the editor itself, prompted by the user's ask (relayed via the peer session) to make it "very very well and intuitive" as final polish, not a rebuild: two images imported back-to-back no longer land on the exact same spot (invisible stacking), disabled controls on L1 now explain why via tooltip, and the persistent drag hint now covers images. Full regression suite green, headless-verified, real `custom_screens.txt` diffed untouched throughout — but per the rule above, none of this gets called "done" or merged/tagged until physically clicked through on the real keyboard.

**Custom Screens text fields + L1 GUI fixes + analog clock BUILT, on branch `g510s-dev`, NOT on main, NOT confirmed** (2026-09-15) — L3-L5 gained user-addable text fields (up to 4/screen, drag-positioned like every other element, persisted as `TEXT content=... x=... y=...` lines in `custom_screens.txt`), the Custom Screens tab's L1 preview bug (intermittently flashing L2's content while typing, caused by three different callers racing on one shared `--preview` temp file) got a real fix (each caller now gets its own tagged temp file, plus atomic tmp+rename writes on the C side so no reader can ever see a half-written frame), and the L1 clock screen gained an analog clock face in its previously-empty right-hand space — see ANALOG CLOCK under Multi-Screen System below for the full build story. Full regression suite green (11 bugcheck scripts, hardware-touching ones included since they restore state), production binary rebuilt and the live `g510-lcd-stats.service` + GUI both restarted on this real machine — but per the rule above, none of this gets called "done" or merged/tagged until physically confirmed on the real keyboard.

**Cross-repo bug audit, committed on main, NOT tagged/released** (2026-09-20, done from a separate machine with no G510s hardware attached — see the peer G910 session's own repo for context) — a full line-by-line read of `g510_lcd_stats.c` (all 1621 lines) plus `src/g510_app.py` and `src/g510_macro_daemon.py` found and fixed three real bugs, all confirmed via direct reproduction before AND after the fix, not just read from the code:
1. `RecorderThread.run()` (`g510_app.py`) opened the keyboard device with no guard — if it's not plugged in, the exception escaped `QThread.run()` and hard-aborted the whole process (SIGABRT), reproduced both by calling the thread directly and through the real Record-button UI path. Now wraps the open in try/except and emits `finished_recording("", False)` instead, matching the `ok: bool` convention `ImageResizeWorker` (added later in the same file) already uses. The dialog now shows "Keyboard not found -- is it plugged in?" instead of crashing.
2. `g510_macro_daemon.py`'s `main()` had the identical unguarded device open — confirmed this raises `FileNotFoundError`, which combined with the systemd unit's `Restart=on-failure`/`RestartSec=3` meant the whole process got killed and relaunched every 3 seconds, forever, until the keyboard was plugged in. New `_wait_for_device()` polls quietly instead of crashing.
3. `g510_lcd_stats.c`'s `read_cpu_totals()` and `get_ram_kb()` were the only two stat-readers in the whole file that didn't check `fopen()` for `NULL` before using it — every other one (`get_cpu_temp_c`, `get_vram_bytes`, `get_swap_percent`, `get_uptime_hours`, etc.) already does. Confirmed via an isolated repro that calling `fscanf`/`fgets` on a `NULL FILE*` is a real `SIGSEGV` (exit code 139), not just a theoretical gap -- and these two are on `draw_stats_screen()`'s hot path (the DEFAULT screen, called every ~2s), not an obscure corner case. Both now return zeroed output and bail out early on a failed `fopen`, same as the rest of the file. Verified by extracting the exact fixed function bodies into a standalone test compiled with `-Wall -Wextra` and run against the real `/proc/stat`/`/proc/meminfo` on this machine (unchanged, correct nonzero output).

**Update 2026-09-20 (confirmed on real hardware)**: full project rebuild done from the machine with the actual G510s attached — `-Wall -Wextra` clean, `--preview 0` (the default stats screen, `draw_stats_screen()`'s own hot path) rendered real non-zero CPU %/RAM values with no crash, full 19-script regression suite green, production binary redeployed, all 3 systemd `--user` services restarted and confirmed active, GUI relaunched. All three fixes in this audit (RecorderThread guard, macro daemon `_wait_for_device()`, the two `fopen()` NULL-guards) are now fully confirmed end-to-end, not just source-verified.

**Update 2026-09-20 (resolved)**: the previous repo's full git history carried real personal identifiers, and the current `PKGBUILD` had the same in its live content. Rather than rewriting history in place (`git-filter-repo`, a force-push, every existing clone invalidated), this repo was moved to a fresh, history-free copy at its current location/name -- `PKGBUILD`'s Maintainer line no longer carries an email, and the new repo's single initial commit carries no personal identity in its author field either.

## What v1.1 adds

A "Custom Screens" tab in `g510_app.py` — an AIDA64-style dashboard builder for the L2-L5 buttons. Pick a screen (L2-L5), add sensors with a display style (Number or Bar) and an X/Y position, see the result in a live preview pane, Remove any element — every change auto-saves immediately (no separate Save button). New sensors beyond the original CPU/RAM/VRAM/CPU-Temp: GPU %, GPU Edge/Hotspot/VRAM temps, Swap %, Disk % (root + an optional secondary drive), Uptime, Network up/down speed, and 6 genuinely-unlabeled motherboard temps (shown honestly as "MB Temp 1..6", not invented names). Layouts are stored in `custom_screens.txt` (plain SCREEN/ELEMENT text lines, no JSON lib needed in C) and rendered by `g510_lcd_stats.c`'s `draw_custom_screen()`.

**Preview mechanism**: `g510_lcd_stats` gained a `--preview <screen> <outfile>` one-shot mode that renders a single frame through the EXACT same drawing code as the live LCD, but writes a PPM image file instead of touching `/dev/g510-lcd`. `g510_app.py` runs this in the background and displays the result — what you see in the editor is guaranteed pixel-identical to the real screen, not a lookalike.

Two real bugs found and fixed while building this (both matter beyond v1.1, keep in mind for any future C changes to this file):

1. `g510_lcd_stats.c`'s compile command was missing FreeType/TTF flags that `libg15render.so` was actually built with. Without them, this program's view of the `g15canvas` struct is SMALLER than what the library writes into (it has extra `FT_Library`/`FT_Face` fields gated by `#ifdef TTF_SUPPORT`) — `g15r_initCanvas()` then writes past the end of the stack-allocated struct. This was a LATENT bug in v1.0 too (silently landed on stack padding, never tripped anything visible) until v1.1's extra locals shifted it onto the stack canary, causing "stack smashing detected" aborts. Confirmed via AddressSanitizer (no heap/logic bug, only leaked FreeType init allocations — harmless) plus a direct A/B compile with and without stack-protector. Fix: `#define TTF_SUPPORT` + FreeType headers before `#include <libg15render.h>`, and compile with `$(pkg-config --cflags freetype2) ... $(pkg-config --libs freetype2)`. This is now in `install.sh`, `scripts/rebuild.sh`, and the source file itself — if you ever hand-compile this file, use the same flags or it WILL crash intermittently.
2. `fonts/lcd-label-8.fnt` (the converted Eurostile Bold label font) had a corrupted 'S' glyph (rendered as something closer to a '6'). Never caught before because no existing label (CPU/RAM/VRAM/TEMP/MAX) contained the letter S — v1.1's "SWAP" and "DISK" labels hit it immediately. Fixed by reconverting from the original source font (`/usr/local/share/fonts/e/Eurostile_Bold.otf`) via `g15fontconvert -s 8 -i <otf> -o fonts/lcd-label-8.fnt`. Verified by rendering the full A-Z alphabet plus every actual label string used in the sensor table — all clean now, and the pre-existing CPU/RAM/VRAM/TEMP screen was re-verified pixel-for-pixel unchanged.
3. (2026-09-20 audit) Every `fopen()` in this file needs a `NULL` check before the returned `FILE*` is used — `read_cpu_totals()` and `get_ram_kb()` were the two places that didn't, and calling `fscanf`/`fgets` on a `NULL FILE*` is a real, confirmed `SIGSEGV`, not a theoretical concern. If you add a new stat reader, copy the `if (!f) { ...zero the outputs...; return; }` pattern every other function in this file already uses — don't assume `/proc`/`/sys` files always exist just because they always have so far.

**PNG/image placement is now built** (2026-09-14, see `G510_DRAW_ON_DISPLAY_PLAN.md`) — an "Import Image..." button in the Custom Screens panel converts and places a PNG (or other Pillow-readable image) on the current screen, draggable like a sensor element, using `src/png-to-lcd.py` and `g15r_drawXBM()` for real now, not just "ready for later."

## v1.0 section

Everything below in this section is DONE and confirmed working by the user:

- **PRIMARY UI is `g510_app.py`** (PyQt5, one window, QTabWidget).
  - **Backlight + G-Keys tab** (merged into one tab 2026-09-14, was two separate tabs before — see `G510_CANVAS_PLAN.md`): a real keyboard-shaped canvas (`g510_canvas.py`) on the left showing the whole board tinted in the actual live backlight color, G-keys in their own accent color (clickable, opens the macro dialog), M1/M2/M3/MR shown above the G-key columns; a control panel on the right with the color dropdown, Apply (live + persists as boot default), Set as Default (persists WITHOUT touching the live color), and a compact Service Control row (Start/Stop/Restart). Brightness slider removed 2026-09-15 — see the BACKLIGHT section. "Apply Defaults" was removed by request (redundant with Set as Default, and had a real bug: it read from a hardcoded constant instead of the actual persisted default — if you ever see a similar "the button that's supposed to remember what I set doesn't" complaint, check for this exact class of bug first: hardcoded fallback vs. actually reading the persisted file). GUI polls the daemon's live-profile file every 500ms so the canvas's highlighted M-key follows physical M-key presses too, not just its own clicks. Each G-key: Record a keystroke combo (QThread + python-evdev, grabs the device while recording) OR type a shell command instead — both confirmed working by the user, replayed via `g510_macro_daemon.py` (separate systemd --user service, watches `/dev/g510-keys`). M1/M2/M3 physically switching the live profile: CONFIRMED working (the keycodes were never actually wrong — see BUTTONS section, the daemon logic was already correct once tested). M1/M2/M3/MR hardware indicator LEDs: CONFIRMED working — the fix was switching from a declarative `MODE=`/`GROUP=` udev rule (never matched, root cause not fully understood beyond "these LEDs are seat-tagged and behave differently") to the same `ACTION=="add"` + `RUN+="chgrp/chmod"` pattern already proven for `kbd_backlight`. MR's LED state is now shown on the canvas too, but clicking it does nothing yet — see `G510_CANVAS_PLAN.md`.
- The old yad/bash backlight UI (`g510-backlight-control.sh` + `g510-backlight-apply.sh`) is NOT on this branch anymore — it lives on the `legacy-yad-backlight-script` branch as a standalone reference. `g510_app.py` is the only interface on main.

*(rest of this file: explains the WHY behind the non-obvious parts, for whoever/whatever needs to actually debug something)*

## What This Is

Turns the Logitech G510s keyboard's built-in LCD into a live CPU/RAM/VRAM/TEMP display, wires up the 5 buttons under the screen (L1-L5), and adds RGB backlight control. Everything runs as your normal user (no root needed at runtime), starts automatically at login, and survives reboots/replugs/kernel updates.

## Files

| File | Description |
| --- | --- |
| `g510_app.py` | PRIMARY UI (PyQt5). Backlight + G-Keys merged into one tab (canvas-based, see `g510_canvas.py`); Custom Screens is a separate tab, currently WIP, see STATUS block above. |
| `g510_canvas.py` | Real keyboard-shaped canvas for the Backlight + G-Keys tab -- `QPainter`-based rendering, per-key `Cell` geometry. See `G510_CANVAS_PLAN.md`. |
| `g510_lcd_stats.c` | draws the screen(s), the main program |
| `g510_lcd_buttons.c` | listens for L1-L5 presses |
| `png-to-lcd.py` | PNG -> raw 1bpp bitmap converter, called by the GUI's "Import Image..." button |
| `g510-lcd-stats.service` | systemd --user unit for the above |
| `g510-lcd-buttons.service` | systemd --user unit for the above |
| `99-g510-lcd.rules` | udev rules (see PERSISTENCE below) |
| `fonts/lcd-label-8.fnt` | the label font, converted, actually used (see FONTS below) |
| `fonts/source-ttf/FONT.otf` | the original label font `lcd-label-8.fnt` is converted FROM — committed to the repo. Its embedded metadata references an Adobe Typekit EULA rather than a free license; that's a knowingly accepted risk, not an oversight. install.sh/scripts/rebuild.sh convert it automatically now, no manual step needed. |
| `fonts/source-ttf/FALLBACK.ttf` | Liberation Sans Bold (SIL Open Font License, genuinely free) — automatically used instead if `FONT.otf` ever fails to load or fails a corruption sanity check. See `font_sanity.h`. |
| `set-backlight-color.sh` | applies the saved backlight color on boot (auto-rewritten every time you click Apply in the GUI — don't hand-edit and expect it to stick, the GUI owns this file now) |
| `rebuild.sh` | recompiles both programs, restarts services |
| `view-logs.sh` | tails both services' logs live |
| `start.sh` | one-click start (used by the Desktop icon) |

Desktop icons (in `~/Desktop`, named "G510 LCD - ..."):

| Icon | Description |
| --- | --- |
| App | PRIMARY: launches `g510_app.py` |
| Rebuild | run after editing the `.c` files |
| Start | restarts both services (works whether stopped, crashed, or just stuck/unresponsive) |
| View Logs | live log viewer |
| Project Folder | opens this folder in the file manager |

## How To Make A Code Change

1. Edit `g510_lcd_stats.c` or `g510_lcd_buttons.c`
2. Double-click "G510 LCD - Rebuild" on the Desktop (or run `./rebuild.sh`) — this recompiles both and restarts the services for you.
3. If you touched pixel-drawing code, check the screen for garbage — see "The Pixel Format" below before assuming it's a typo.

## The Pixel Format (the single most important thing to know)

The LCD is 160x43, 1 bit per pixel. libg15render's canvas buffer stores pixels ROW-MAJOR, MSB-first (`pixel_offset = y*160+x`). The physical LCD hardware wants pixels in a different, VERTICAL "page" format (8 pixels per byte, one byte per column, LSB = top pixel) — this is the classic SSD1306-style layout. `dump_to_lcd_format()` in `g510_lcd_stats.c` is a byte-for-byte port of libg15's own `dumpPixmapIntoLCDFormat()` that does this conversion. If you ever see garbled diagonal/streaky output instead of clean text, you skipped this conversion somewhere — do NOT just `memcpy` canvas->buffer to the device, it will look like static.

The final wire format is a 992-byte HID report: byte 0 = `0x03` (the report ID, discovered by reading libg15's source — it's not documented anywhere else), bytes 1-31 = padding/zero, bytes 32-991 = the converted 960 bytes of page-format pixel data.

## Why This Doesn't Use g15daemon / libg15's Own Device Code

g15daemon (the "standard" tool for this) grabs the keyboard's whole extra-keys USB interface via libusb, detaching it from the kernel's `hid_lg_g15` driver, then re-emits key events through its OWN decoder — which has the WRONG key table for a G510s specifically (a long-standing, never-fixed bug in that 15+ year old project). This broke media/volume keys HARD when tested live (confirmed: random garbage keystrokes).

Instead, this program writes directly to `/dev/g510-lcd`, a hidraw device node — hidraw lets you send/receive HID reports through a device the kernel driver already owns, without detaching anything. The kernel's normal key handling (`hid_lg_g15`) keeps working completely undisturbed. This was verified repeatedly with the LCD screen running continuously while actively using media keys — zero interference either way.

## Media Keys Fix (predates this project, but this is why it's safe to touch this keyboard at all — read before changing anything input-related)

Three separate, unrelated problems were found and fixed on this exact keyboard. None of them involve this LCD project's code, but breaking any of them again is an easy mistake to make while experimenting:

1. Play/Pause is PHYSICALLY DEAD (corrosion/worn contact, confirmed via raw HID capture — zero signal from that key even wet with contact cleaner, while every neighboring key on the same interface worked). Fixed with a udev hwdb remap: the Stop button's key sends the correct signal, so `/etc/udev/hwdb.d/91-g510-stop-to-playpause.hwdb` remaps its exact scan code (`0x000c00b7`, HID consumer-page "Stop") to `KEY_PLAYPAUSE` instead of `KEY_STOPCD`. You lose a hardware Stop button; you gain a working Play/Pause. If you ever want the real Stop function back, delete that hwdb file and re-run `sudo systemd-hwdb update`.
2. Brave's native media-key support and the "Plasma Integration" browser extension (id `cimiefiiaegbelhefglklhhakcgmhkai`) were BOTH registering as MPRIS players for the same tab, so KDE's media-key router got confused about which one to actually control. Fixed by disabling that extension's media-control feature (its `active_bit` should read `false` in Brave's Preferences JSON if you ever need to check).
3. The kernel driver, `hid_lg_g15`, is what actually reports all of this keyboard's keys correctly — do NOT blacklist it (an earlier attempt to "fix" flaky media keys by forcing hid-generic instead turned out to be unnecessary once #1 and #2 above were found; `hid_lg_g15` is also required for the Gaming Keys interface this LCD project's buttons depend on). Check `lsmod | grep hid_lg_g15` and `grep -rl lg_g15 /etc/modprobe.d/` (should be empty) if media keys or L1-L5 ever stop responding after a system change.
4. **Direct report: "only next and back media buttons don't work"** (Play/Pause, Volume, and Mute all still fine). Root-caused via hard evidence, not guessed: captured raw evdev events directly on the keyboard's Consumer Control interface while the user pressed the keys -- `KEY_PLAYPAUSE` and `KEY_PREVIOUSSONG` both fired correctly, so the hwdb remap/kernel driver were never the problem. `kglobalaccel`'s `nextmedia`/`previousmedia` shortcuts were also confirmed correctly bound (`qdbus --literal org.kde.kglobalaccel /component/mediacontrol org.kde.kglobalaccel.Component.allShortcutInfos`), and a direct `playerctl next` call genuinely skipped the track -- so MPRIS `Next`/`Previous` worked fine when invoked directly too. The actual fault: Brave's `plasma-browser-integration` MPRIS registration had gone stale (its D-Bus name intermittently returned `ServiceUnknown` on a direct `Properties.Get` query even though `playerctl -l` still listed it), which broke media-key *routing* specifically for Next/Previous while Play/Pause and volume kept working through other paths. **Fix: refresh the browser tab/page** -- re-registers a clean MPRIS session. Confirmed by the user immediately after refreshing. Nothing in this project's own code was at fault.

## Multi-Screen System

A single file, `$XDG_RUNTIME_DIR/g510lcd_screen`, holds one number:

- `0` = the stats screen (CPU/TEMP/VRAM/RAM, in that vertical order)
- `1` = a clock (digital time+date on the left, plus an analog clock face on the right — see ANALOG CLOCK below)
- `2-5` = "L2".."L5" test screens (see BUTTONS below)

`g510_lcd_stats.c` re-reads this file every loop iteration (~1-2s) and draws whichever screen it says. `g510_lcd_buttons.c` is the only thing that ever WRITES to this file. To add a new screen: write a new `draw_XXX_screen()` function, add a branch for its number in `main()`'s loop, and make some button set that number in `g510_lcd_buttons.c`.

### Analog clock (L1, right side)

Direct request: fill the empty space to the right of the digital time+date on the clock screen with an analog clock face. Built in three passes, each a direct refinement request:

1. First pass: plain square frame (`g15r_pixelBox`), no numerals, hands only — matched the display's blocky 1-bit aesthetic. Position wasn't guessed: rendered the real clock screen via `--preview` and scanned the actual pixel data for the rightmost lit (digital text) pixel — x=92, leaving a real measured gap, not an assumed one.
2. Refinement ("rounded corners... roman numerals at 12 3 6 9... small lines in between"): switched to `g15r_drawRoundBox` for rounded corners, grew the face from 36x36 to 40x40 (`(107,1)-(147,41)`) to fit roman numerals without crowding, added `XII`/`III`/`VI`/`IX` at 12/3/6/9, and 8 small tick marks at the remaining hours.
3. `G15_TEXT_SMALL` (the built-in bitmap font used for the numerals in pass 2) has no runtime width-query API — each numeral's real pixel width was measured directly (render + scan lit pixels, same technique as the space measurement above): `XII`=11px, `III`=11px, `VI`=7px, `IX`=7px, all 5px tall. Numeral positions were each string's own measured width/2 and height/2 subtracted from its point on a 12px-radius circle.
4. Further refinement ("JUST A BIT SMALLER"): `G15_TEXT_SMALL` is already the smallest built-in bitmap font this library ships (only SMALL/MED/LARGE/HUGE exist — checked the header, nothing smaller to ask for). Since roman numerals only ever need three shapes (I/V/X), each a trivial straight-line composition, they're now hand-drawn directly with `g15r_drawLine` (`draw_roman_glyph`/`draw_roman_numeral` in the source) at 4px tall instead of rendered from the bitmap font — genuinely smaller (XII: 11px→7px wide, III: 11px→5px, VI/IX: 7px→5px) AND crisper on a 1-bit display than shrinking a bitmap or antialiased glyph would be, since straight strokes don't have half-lit pixels to go muddy at tiny sizes. Centering math now computes the real composed width from the actual glyphs being drawn, not a separately-measured constant.

Tick marks sit at radius 15-18 (clear of the numeral zone, inside the rounded frame). Hand lengths (minute=9, hour=6) were kept under the numeral radius so neither hand ever visually overlaps a numeral, including at :15/:45 and 3:00/9:00. No second hand — deliberately simple at this size.

Same angle convention as everywhere else in this file: 0 = 12 o'clock, increasing clockwise, `dx = round(sin(angle)*len)`, `dy = -round(cos(angle)*len)` (LCD y-axis is down-positive).

Built and self-verified (compiled `-Wall -Wextra` clean, regression suite green, rendered via `--preview` and visually inspected zoomed-in) on branch `g510s-dev` — like everything else on this branch, not tagged/merged to `main` until physically confirmed on the real keyboard.

## Buttons (L1-L5)

Real, standard Linux keycodes — no HID remapping was needed, unlike the media keys (see MEDIA KEYS FIX above for that story). Confirmed by testing each button individually:

- L1 = 696 (`KEY_KBD_LCD_MENU1`) — cycles: stats -> clock -> stats... (if currently on an L2-L5 test screen, L1 returns to stats)
- L2 = 697 (`KEY_KBD_LCD_MENU2`) — shows "L2" on screen (test/placeholder)
- L3 = 698, L4 = 699, L5 = 700 — same pattern

These switches BOUNCE (one physical press can fire 2-4 raw events) — `g510_lcd_buttons.c` has a 400ms debounce per key, don't remove it. L2-L5 currently do nothing but confirm the press — to actually program them, edit the `else` branch in `g510_lcd_buttons.c`'s main loop (the one that currently just calls `write_screen()`+`log_button()`).

## Fonts

The library ships `default-00` through `default-39.fnt`, but they're all the SAME typeface at different sizes — not actually different fonts. For anything else, use `g15fontconvert -i font.ttf -o out.fnt -s N -g 1` (the AUR `libg15render` package; `-s` is NOT literal pixel height, it's closer to a point size — check the ACTUAL resulting height with:

```bash
python3 -c "import struct; print(struct.unpack('<H', open('FILE.fnt','rb').read()[4:6])[0])"
```

and adjust `-s` up/down until it matches what you want (our 10px row spacing needs a font around 8-9px tall).

IMPORTANT: thin/regular-weight fonts render GARBLED at this tiny size (confirmed multiple times) — there simply aren't enough pixels for fine strokes. Use Bold or Black weights only. Current setup: labels use the bundled `fonts/source-ttf/FONT.otf` (matches the G510s's original stock LCD font style), numbers use the library's own built-in `G15_TEXT_SMALL` font (also tried several converted fonts for numbers — all looked worse than the built-in one, which was purpose-built for this exact resolution).

`.otb`/`.pcf` bitmap fonts (like Terminus) do NOT work with `g15fontconvert` — it silently produces an empty/broken `.fnt` (`font_height` stuck at one value regardless of `-s`, near-zero file size). Only scalable TTF/OTF outline fonts convert correctly.

**Licensing note (recorded decision, not an oversight):** `FONT.otf`'s embedded metadata (checked directly with `strings fonts/source-ttf/FONT.otf`) contains an Adobe Typekit EULA reference (`typekit.com/eulas/...`), not a free-license one — meaning this specific file is very likely not something its author intended to be freely redistributed. It's bundled here anyway, a knowingly accepted risk. As a safety net, not a licensing fix, `fonts/source-ttf/FALLBACK.ttf` (Liberation Sans Bold, genuinely SIL Open Font License, license text at `fonts/source-ttf/FALLBACK.LICENSE.txt`) is also bundled and converted — `g510_lcd_stats` automatically switches to it if `FONT.otf` ever fails to load or fails a glyph sanity check (see `font_sanity.h` and the "Gotcha We Actually Hit" section above about the corrupted-'S'-glyph bug this guards against).

install.sh and scripts/rebuild.sh now convert both fonts automatically (`g15fontconvert`, part of the AUR `libg15render` package) — no manual step needed on a fresh install.

## Backlight (RGB Keyboard Glow)

This is a REAL Linux kernel LED device, nothing hidraw/USB-custom about it: `/sys/class/leds/g15::kbd_backlight/` — `brightness` (0-255) and `multi_intensity` ("R G B" space-separated, 0-255 each). KDE's own "Keyboard Colour: Follow accent colour" toggle fights over this same file — turn that OFF in System Settings > Brightness & Color if you want manual control to actually stick.

Applied automatically on boot/replug by `set-backlight-color.sh` via the udev rule. Use the "Backlight + G-Keys" tab in `g510_app.py` to change it — a color preset dropdown, Apply / Set as Default, no sudo prompt. Whenever you click Apply, `apply_backlight()` in `g510_app.py` REWRITES `set-backlight-color.sh` with your new choice — so whatever you pick becomes the new permanent boot default automatically, not just a one-time live change, and the canvas's board color updates to match immediately.

**Brightness slider removed 2026-09-15, confirmed genuinely broken, not just unreliable**: the 2026-09-14 caveat below (`G510_CANVAS_PLAN.md`) suspected the brightness write/read-back was flaky; direct testing this pass settled it — writing 50, 128, 200, 255, and 100 to `/sys/class/leds/g15::kbd_backlight/brightness` all read back as 255 every time, immediately, with no variation. This is a real kernel/driver-level limitation on this hardware (`max_brightness=255` implies a working 0-255 range that doesn't actually exist in practice), not a timing/latency issue. The slider controlled nothing real, so it's gone — the app now always applies `MAX_BRIGHTNESS = 255` (the only value that's ever actually taken effect). Color (`multi_intensity`) writes remain fully reliable, untouched by this.

Uses a preset-color dropdown (`COLOR_RGB` dict) rather than a live color picker — a Qt color picker would work fine here (this isn't the yad CLR+SCL crash from the old script, see the `legacy-yad-backlight-script` branch for that story), just wasn't built yet. Add more presets by editing the `COLOR_RGB` dict near the top of `g510_app.py`.

## Persistence (why this survives reboots/updates)

`99-g510-lcd.rules` (installed at `/etc/udev/rules.d/`) does three things:

1. Creates `/dev/g510-lcd`, a stable symlink to whatever hidraw number the LCD's USB interface gets THIS boot (it changes across reboots/replugs, don't hardcode a hidrawN number anywhere).
2. Creates `/dev/g510-keys` the same way, for the Gaming Keys input device.
3. On the backlight LED appearing, chmod/chgrp's it to group "input" (so no sudo is needed at runtime) and runs `set-backlight-color.sh`.

The two systemd --user services are `enable`d, so they autostart at every login. `hid_lg_g15` (the kernel driver all of this depends on) is a mainline upstream driver, ships in every Manjaro kernel package — nothing here is tied to today's specific kernel version.

**"Save All Settings Now" button** — direct request: "make me a save button/feature o dont trust my settings will survive reboots". Every real edit in this app (dragging/resizing a Custom Screens element, clicking Apply or Set as Default, recording a macro) already writes straight to `DATA_DIR` immediately — there was no missing autosave path to add. What was missing was visible proof of that. The button (top toolbar, visible from either tab) re-saves the current Custom Screens config and backlight boot-default, then reads each one straight back off disk and compares it against what's supposed to be there — a real round-trip verification, not just "the write call didn't throw" — and reports exactly what it confirmed (or, honestly, what failed) in the status bar.

## Media Info + Audio Visualizer (L2-L5, draggable like any other element)

Three new sensor keys (`MEDIA_TITLE`, `MEDIA_ARTIST`, `MEDIA_ELAPSED`) and a new "visualizer" element kind, addable via the Custom Screens editor same as any sensor/text/image.

**Media info** — `update_media_info()` in `src/g510_lcd_stats.c` shells out to `playerctl` (real Arch `extra` package, in `install.sh`'s dependency list) once per poll cycle, over MPRIS. Two things that would be easy to get wrong by guessing instead of testing:
- Real title text can contain a literal `|` character (confirmed: a real YouTube video title was `"Müneccim | YouTube Music"`) — the combined `--format` query uses the ASCII Unit Separator (`\x1f`) as its field delimiter, not `|`, specifically because of this.
- `playerctl`'s `{{ position }}`/`{{ mpris:length }}` format-string fields are in **microseconds** — the separate `playerctl position` subcommand returns **seconds**. Confirmed by testing both directly; don't assume they match.
- **Player selection matters, and matters DIFFERENTLY for different fields**: querying with no `-p` flag lets `playerctl` pick "the first available player" by its own priority order. On a real KDE desktop with Brave playing YouTube Music, that picked Brave's own raw MPRIS export — which reported the generic tab title ("YouTube Music", no song name) and a blank artist — over `plasma-browser-integration` (KDE's own browser-media bridge), which reported the real title, artist, AND album for the exact same content.
  - But direct live testing found the opposite is true for `position`/`mpris:length`: "the timer never resets when a new song plays. it continues where it left off, total time also gets just added time." Confirmed by polling `playerctl -a` (all players at once) side by side while a YouTube Music playlist auto-advanced through several tracks: `plasma-browser-integration`'s own `position` climbed straight past 10+ minutes and `mpris:length` kept growing every poll — it does not cleanly reset those two fields between tracks in an autoplay playlist/mix. Brave's own raw MPRIS export (tied directly to the real `<video>` element's `currentTime`/`duration`, not a value derived by the browser-integration bridge) stayed correctly bounded to the real per-track duration over the same window.
  - Fix: a single `playerctl -a metadata` query (one subprocess, not two) returns every running player's metadata on its own line, each prefixed with `{{ playerName }}`. `update_media_info()` now picks title/artist from the `plasma-browser-integration` line when present (best metadata, as above) but picks position/length from a *different*, non-bridge line when one exists (correct, non-accumulating timing) — falling back to the same single source for both fields when there's only one real player anyway (Spotify, VLC, and other non-browser cases — unaffected, still covered by `bugcheck_media_sensors.py`'s real end-to-end test).

**Visualizer** — a real-time audio bar equalizer, capped at 1 per screen (`MAX_VISUALIZERS` in the C source) since there's exactly one shared audio-capture stream process-wide.

- **Player-agnostic by construction, not by per-app integration**: captures the PipeWire/PulseAudio default sink's *monitor* (system audio OUTPUT) via `parec`, not any one application's stream — it hears whatever's actually coming out of the speakers, so it works identically with Brave, Spotify, VLC, anything, with zero player-specific code.
- **A real, non-obvious bug was found and fixed in the capture mechanism itself**: the first version had `parec` write to stdout, read back through a `popen()` pipe. Confirmed by direct, repeated comparison — an anonymous pipe AND a named FIFO both silently deliver all-zero bytes forever, even though `read()`/`fread()` report success with the correct byte count. The exact same command redirected to a plain file, read back with `fopen`/`fread`, reliably returns real audio every time. Root cause: `parec`/PipeWire-pulse negotiates a much larger flush buffer when its output is detected as a pipe-like fd versus a file — not fixable from the reading side. **Current design**: `parec` is launched via real `fork()`/`execlp()` (a tracked child PID — this project has a standing rule against fragile `pkill` pattern-matching after being bitten by it before) writing continuously to a file under `$XDG_RUNTIME_DIR`, restarted every 30s (`VIZ_RESTART_SEC`) to bound disk use. A `SIGTERM`/`SIGINT` handler cleans up the child on daemon shutdown — without it, every service restart orphaned a `parec` process forever (found and fixed during testing).
- Bar/segment count are computed **dynamically** from the element's real width/height at a small fixed per-bar/per-segment pixel size (not a fixed count that just gets fatter when resized) — dragging the box bigger genuinely shows more frequency detail, not just bigger blocks.
- A segmented "zero line" baseline (matching real bar spacing) always spans the full 160px display width, regardless of the element's own configured width/position, so the widget always reads as "present, currently at zero" rather than looking identical to nothing being there.
- Uses a small hand-written single-bin DFT (`viz_dft_bin_magnitude()`, Goertzel-style — evaluates only the ~20 specific frequency bins the bars need) instead of a general FFT library — FFTW is available on this system but is a heavyweight dependency for something this small.
- `VIZ_SCALE` (raw DFT magnitude → visual bar height) is a calibrated constant, not derived — real songs vary in loudness/mastering more than one fixed constant can perfectly cover. If a future song looks too flat or pinned at max, that's this same real tradeoff, not a new bug; an adaptive/auto-gain scale would fix it properly but is a bigger change.
- The GUI editor's one-shot `--preview` renders read the *same* live capture file the persistent daemon maintains, so the Custom Screens tab shows a real live snapshot of whatever's actually playing — no separate live-preview mechanism was needed.
- Bars are drawn 4px wide (`bar_w`, was 2px — a deliberate "double line" request after live testing looked too thin/sparse) with a full-width segmented baseline drawn first, so every bar column shows a zero-position tick across the entire 160px display even for the columns that aren't currently lit.
- Tuned live against real currently-playing music (not synthetic test tones): DFT bin count raised (`VIZ_NUM_BARS`), smoothing changed to weight the new sample more heavily (`0.85` new / `0.15` old, was `0.7`/`0.3`) so bars visibly rise and fall per-beat ("Winamp style") instead of crawling. This value is a best-effort tuning pass, not a derived constant — if it still looks off on a different song, it's the same real tradeoff as `VIZ_SCALE` above, not a new bug.
- **Response-latency bug found and fixed** — direct request: "i need the visualizer respons faster". Measured the real cause directly by polling the capture file's size every 50ms: it was growing in ~65KB (~0.7s of audio) bursts roughly every 0.6-0.7s, not continuously — every bar drawn was reading audio up to ~0.7s stale, far more lag than the draw loop itself. `parec`'s `--latency-msec=50` flag (confirmed via the same polling test against a throwaway capture file before touching the real one) fixed it: writes now land in a few KB almost every poll cycle instead. The custom-screen draw loop's visualizer-only redraw rate was also doubled (100ms → 50ms, i.e. ~10fps → ~20fps in `g510_lcd_stats.c`'s main loop) since it became the next-largest lag source once capture itself was fast.
- Live element sizes are user data (`~/.local/share/g510-lcd/custom_screens.txt`, never committed) — the shipped default screens just show a wider box (116×15 vs the original 84×13) as a starting point; drag-resize still works exactly as before.

**Text overflow / truncation (a real bug, not just cosmetic)** — the G15's built-in bitmap fonts (`G15_TEXT_SMALL/MED/LARGE/HUGE`) have no width-query API at all (confirmed via `libg15render.h`, not guessed), so sensor VALUE text (long song titles, artist names) and freeform TEXT elements could render straight past the 160px display edge with nothing to stop them. Fixed with two small helpers in `src/g510_lcd_stats.c`:
- `measure_builtin_text_width()` — renders the string to a scratch off-screen canvas and scans for the rightmost lit pixel (the same technique already used elsewhere in this project for font metrics, since there's no other way to ask these bitmap fonts their width).
- `truncate_builtin_text()` — shrinks the string one character at a time, appending a single `.` marker, until `measure_builtin_text_width()` reports it fits the available space. Wired into both value-text draw paths (number-style and bar-style sensors) and freeform TEXT elements.
- Stress-tested directly (every font size, adversarial/unicode strings, an `avail` sweep from negative to the full display width) — the truncation math itself never once measured a pixel past its budget. Even so, an exact-to-the-edge truncation reads as "still touching/going off the screen" on a real physical LCD, so a small `TEXT_EDGE_MARGIN` (3px) is now subtracted from every available-width budget, giving truncated text visible breathing room from the display edge.

**Hit-box accuracy (`value_x2`)** — the `.meta` sidecar that tells the Python GUI where each element's clickable/draggable box is used a fixed `+30px` guess for a sensor's value-text width (or, for bar-style sensors, used the bar's own right edge and didn't account for the trailing value text at all). Any sensor with a value wider than that guess — media titles being the obvious case, but really any sensor — got an undersized or flat-out wrong hit-box, so part of the visible text sat outside the draggable area. Fixed by having the C side write a real measured `value_x2` (using `measure_builtin_text_width()` above) into the `.meta` file, and having `_element_rect()` in `src/g510_app.py` read that real field instead of guessing.

**Missing remove (✕) button regression** — adding the "Other elements:" button-grouping separator and longer sensor labels (e.g. `"Media: Song Title – <value>"`) pushed some rows past the fixed 230px side panel width (`CUSTOM_SCREENS_PANEL_WIDTH` in `src/g510_app.py`, now a single named constant instead of a magic number), clipping the ✕ button off the visible edge — it was still there in the widget tree, just unreachable. Fixed with `QFontMetrics(...).elidedText(...)`, computing the real available width for the label (panel width minus the ✕ button, any size-badge button, and fixed margins) and eliding with `...` rather than letting the row overflow; the full untruncated text is preserved as a tooltip.

**Auto-centered title/artist** — direct request: "id like the song name and artist always auto centre, text lenghts differ from song to song". `MEDIA_TITLE`/`MEDIA_ARTIST` are flagged `auto_center` in `SENSORS[]`; `draw_element()` re-truncates against a budget centered on the *whole* display (not just the space right of the configured x) and recomputes `x = (G15_LCD_WIDTH - value_w) / 2` every draw, since a different track's title/artist is a different width every time — the element's y (row) still comes from wherever it's dragged, only x is forced to center. Stress-tested from a single character up to a maximally-truncated ~97-character string: never centers past either display edge.

## Gotcha We Actually Hit (keep this in mind if things break weirdly)

A test script once ran `fopen("/dev/hidrawN", "wb")` at the exact moment the real device briefly didn't exist during re-enumeration — this silently created a REGULAR FILE named `hidrawN` in `/dev` instead of erroring, which then permanently blocked the kernel from recreating the real character device at that path. Symptom: permissions look right but writes still fail, or udev rules seem to apply then "un-apply". Fix: `stat /dev/hidrawN` — if it says "regular file" instead of "character special file", `sudo rm` it and replug the keyboard.

## Udev Rule Gotcha (if you ever add another rule)

Don't mix `ATTRS{}` from two different ancestor devices in one rule line (e.g. `ATTRS{idVendor}` lives on the USB device, `ATTRS{bInterfaceNumber}` lives on the USB interface, a different level) — once udev matches one `ATTRS{}` against a device, it locks onto that SAME device for the rest of the rule's `ATTRS{}` checks, silently failing to match. Use `ENV{}` for properties instead where possible (`ENV{ID_VENDOR_ID}`, `ENV{ID_MODEL_ID}`, `ENV{ID_USB_INTERFACE_NUM}` are all already resolved onto the device you actually want to match, no ancestor-walking involved) — this is what the hidraw rule does and it's reliable.
