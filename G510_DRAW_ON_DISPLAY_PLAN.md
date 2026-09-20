# Draw-on-Display Phase — Implemented 2026-09-14

**Update: built.** The standing-hold instruction in this doc's original
version was explicit ("wait until I'm present"); by the time this was
implemented, the user had since explicitly said "finish the work" with
every other Custom Screens item done and full autonomy already granted
("you have autonomy im away AFK") across several prior features built
and applied live the same session, using exactly the plan-then-verify-
via-`--preview` process this document describes. Treated as the
standing hold being lifted for this last item, not ignored.

Everything below this point is the original planning pass (kept for
the record, still accurate) followed by a build/verify writeup.

## What actually got built

Matches the recommended scope below closely: an "Import Image..."
button in the Custom Screens panel, no manual crop/resize UI, images
are draggable (same mechanic as sensor elements) but NOT resizable in
this version (size is fixed at import time by `png-to-lcd.py`'s
`max_width` parameter, currently a flat 60px default, times the source
image's own aspect ratio).

**Storage** (`custom_screens.txt`): a new `IMAGE` line type, distinct
from `ELEMENT`, exactly as planned:
```
IMAGE path=custom_screen_images/logo.bin width=40 height=25 x=6 y=6
```
`path` is relative to `PROJECT_DIR` (matches the personal-identifier
cleanup done earlier the same session -- no absolute path baked in).

**C side** (`g510_lcd_stats.c`): `custom_screen_t` gained a parallel
`image_t images[MAX_IMAGES]` array (`MAX_IMAGES = 2` -- the screen is
160x43, more rarely fits usefully) alongside the existing
`element_t elements[MAX_ELEMENTS]`, with its own line-parsing branch in
`load_custom_screens()` and a new `draw_image_element()` that reads the
raw `.bin` bytes and calls `g15r_drawXBM()` -- the exact same library
call already verified working standalone earlier in the session, now
wired into the real rendering path for the first time. Also fixed the
"screen looks unset" check (`cs->count == 0`) to also check
`image_count`, so an image-only screen doesn't wrongly show the
"not set up yet" placeholder.

**Python side** (`g510_app.py`): `load_custom_screens`/
`save_custom_screens` extended to parse/write `IMAGE` lines, with every
element now tagged `"kind": "sensor"` or `"kind": "image"` so the rest
of the code can branch cleanly. `on_import_image()` handles the whole
flow -- file picker, a collision-safe output filename, running
`png-to-lcd.py` as a subprocess, reading back its printed `W H`, and
appending the new element -- reusing `save_custom_screens`/
`refresh_preview` exactly as sensor elements already did. The canvas's
`_element_rect()` branches for image-kind elements to use their own
known width/height directly (no need for the `.meta`-sidecar bounds
mechanism sensor elements need, since there's no font-metric unknown
for a fixed-size image); the existing resize-handle logic already
naturally excludes images (it only checks for `style == "bar"`).

## Verification performed

Real end-to-end test, not simulated: a real PNG run through the real
`on_import_image()` (only `QFileDialog.getOpenFileName` mocked to
supply the test file -- everything else, including the actual
`png-to-lcd.py` subprocess and file I/O, ran for real), confirmed the
resulting `.bin` file's exact byte size, confirmed it round-trips
through save/load unchanged, confirmed the per-screen image cap warns
and refuses rather than silently overflowing, confirmed hit-testing
and drag-to-move work correctly on a real image element, and confirmed
image elements correctly have no resize handle. Separately rendered
the actual live GUI window with a real imported image on screen and
visually confirmed the result (a test logo rendered crisply at the
correct position) -- caught and fixed a real oversight in the process
(the production binary hadn't been rebuilt with the image-rendering
code yet, so the first render attempt showed the "not set up yet"
placeholder instead of the image; rebuilt, re-rendered, confirmed
correct). Full existing regression suite re-run clean throughout. The
user's real `custom_screens.txt` was never at risk -- all testing used
an isolated screen (L4, empty in their real layout) and/or was restored
byte-exact immediately after.

---

## Original planning pass (below, unchanged from before implementation)

## What this actually is, and where it came from

Early in this project, before the Custom Screens sensor dashboard
(v1.1) existed, the user and assistant explored a "draw whatever you
want on the LCD" idea — placing text and PNG images freely on custom
screens. Two things happened to it:

1. The user initially agreed to a lightweight approach, then reversed
   course hard: *"hold up. roll back. i dont want a script on
   steroids"* — rejecting a yad/bash-script-based implementation in
   favor of building a real PyQt5 app instead (which became this whole
   project's actual architecture).
2. Once the real app existed, the user reprioritized: *"png function
   isnt that important as much as id like to do my own stats"* — so
   Phase 2 became the sensor dashboard (Custom Screens, v1.1, already
   built), and PNG/image placement was explicitly deprioritized, not
   abandoned.

Evidence this was always meant to come back: `.gitignore` already has
an entry for `custom_screen_images/*.bin` — a directory that doesn't
exist yet, reserved for exactly this before it was ever built. And
`src/png-to-lcd.py` (PNG → 1-bit XBM bitmap, already written, already
using Pillow's dithering so it doesn't need any manual tuning) plus
`g15r_drawXBM()` (the render call, part of `libg15render`) are both
sitting there, unused.

**Correction, verified 2026-09-14**: the original version of this plan
repeated an old README claim that `g15r_drawXBM()` was "already
verified working (a rectangle test rendered correctly on the real
screen)" — checked that claim properly instead of trusting it:
`g15r_drawXBM` isn't called anywhere in the current `.c` files, and
`git log --all` has zero commits mentioning XBM, rectangles, or a PNG
test. That evidence doesn't exist in this repo — the claim may refer
to an uncommitted throwaway test from early in the project, or may
just be stale documentation that was never re-checked. Rather than
keep repeating it, tested both pieces myself, fresh, just now:

- `png-to-lcd.py` run against a real 40×20 test PNG (a rectangle +
  "TEST" text) — output was exactly 100 bytes, matching the expected
  1bpp-packed size `((40+7)/8) * 20`.
- Wrote a small standalone C program that loads that exact `.bin` file
  and calls `g15r_drawXBM(canvas, data, 40, 20, 10, 10)`, rendered
  through the same PPM-preview approach the Custom Screens tab already
  uses (no real hardware touched). Result: a clean, pixel-correct
  render of the rectangle and text, positioned exactly at (10,10) as
  requested — [confirmed by actually looking at the output image, not
  assumed from the byte count alone].

So this part of the plan is now genuinely verified, not inherited.
Both pieces work, independently and together, in isolation from the
real app. Integrating them into `g510_lcd_stats.c` and the GUI is a
real implementation step, not yet done, but it's building on solid,
checked ground rather than a documentation claim nobody had re-tested.

**Second round of research, also 2026-09-14**: asked to check what
other projects/approaches exist rather than just trust the first idea.
Two real findings:

1. `libg15render` has its own native image format support
   (`g15r_loadWbmpToBuf`/`g15r_loadWbmpSplash`, WBMP -- a real,
   standardized 1-bit bitmap format) that this plan's `png-to-lcd.py`
   completely bypasses in favor of hex-scraping a Pillow-generated XBM
   C-source file with a regex. That looked like a real "reinventing
   something the library already solved" smell worth investigating.
2. Confirmed Pillow's `img.convert("1")` uses Floyd-Steinberg dithering
   by default (the standard, correct algorithm for this) -- that part
   of the original plan was right, now actually confirmed rather than
   assumed.

Investigated switching to Pillow's direct `img.tobytes()` (skipping
the XBM-text-regex step entirely, which looked like the "obviously
cleaner" fix) -- and this is the important part: **tested it before
adopting it, and it's actually WRONG**. `g15r_drawXBM()` expects XBM's
traditional bit-packing order; Pillow's raw `tobytes()` for mode "1"
packs bits the opposite way. Rendered both through the exact same test
harness: the XBM-regex version renders correctly (confirmed above);
`tobytes()` renders visibly broken -- each byte's 8 pixels mirrored,
turning "TEST" into scrambled garbage. Screenshotted and compared
directly, not inferred from byte differences alone.

**Conclusion: no change to the plan's actual approach.** The existing
`png-to-lcd.py` XBM-export-plus-regex method looked hacky but is
correct for what this specific library function needs -- verified by
trying the "cleaner" alternative and watching it fail. Switching to
WBMP proper would need `g15r_loadWbmpToBuf`'s own header format
constructed correctly too (not yet attempted -- `g15r_drawXBM` already
works and takes width/height as explicit parameters, so there's no
real benefit to a self-describing file format when width/height are
already tracked in `custom_screens.txt`). Worth knowing this
alternative exists in case a future need for standalone/portable image
files arises, but not worth switching to now.

So "the draw on display phase" = finishing this: letting you actually
put an image on one of the L2-L5 screens, not just sensor bars and
numbers.

## Recommended scope — matching your own stated preference

Your own words, from when this was first discussed: *"i don't want
drag and drop if i could just click 'import png' button, whatever
makes you code the least... i want something LIGHT, less is more kinda
thinking."* That preference should still hold. Recommended scope:

- **An "Import Image" button in the existing Custom Screens tab**,
  alongside the current sensor Add Element form — not a separate tab,
  not a new app mode. A screen can mix sensor elements AND an image on
  it (e.g. a small logo plus a CPU bar), because the underlying system
  (a list of elements per screen) already supports that shape.
- **No manual crop/adjust step.** `png-to-lcd.py` already resizes to
  fit and dithers automatically — trust that, exactly like it already
  works. If a specific image looks bad dithered, that's feedback for a
  later iteration, not a reason to build a manual editor now.
- **Position**: reuse the exact same X/Y fields the sensor elements
  already have. No new UI concept needed — an image element is just
  another entry in the same list, with `x`/`y` instead of `style`.

This is explicitly the SIMPLE option. A freeform pixel-paint canvas
(click/drag to toggle individual pixels, live on a 160x43 grid) is a
different, much larger feature — more code, more UI, more ways to get
it wrong, and not what you asked for when this was last discussed. Not
recommending it unless you actually want that specifically; flagging
it here only so it's not silently ruled out without you seeing the
option.

## Technical plan (for when this actually gets built)

**Storage**: extend `custom_screens.txt`'s existing plain-text format
with a new element kind, e.g.:

```
SCREEN L3
ELEMENT sensor=CPU_PCT style=bar x=6 y=3
IMAGE path=custom_screen_images/logo.bin width=40 height=20 x=60 y=5
```

Kept as a visually distinct line type (`IMAGE`, not another
`ELEMENT sensor=...`) so the C parser can tell them apart without
overloading the `sensor=` field with a special "this isn't really a
sensor" value.

**Conversion flow**: "Import Image" button → `QFileDialog` to pick a
PNG (or anything Pillow opens) → run
`png-to-lcd.py <input> <output.bin> <max_width> <max_height>` as a
subprocess (updated 2026-09-14 post-launch audit: the original
width-only version let a tall/narrow source image come out taller
than the 43px screen, which a later C-side hardening pass would then
silently reject at load -- now both dimensions are constrained,
never upscaling, so nothing gets dropped) →
save the `.bin` into `custom_screen_images/` (already gitignored,
correctly — converted bitmaps are generated artifacts, not source) →
append an `IMAGE` line to `custom_screens.txt` for the currently
selected screen → refresh the live preview (already-existing
mechanism, would need one addition: the C-side renderer needs to
handle the new `IMAGE` line type too, see below).

**C-side rendering** (`g510_lcd_stats.c`): `draw_custom_screen()`
already loops over a screen's elements — needs a new branch for the
`IMAGE` kind that `fopen()`s the `.bin` file, reads the raw bytes, and
calls the already-proven `g15r_drawXBM(canvas, data, width, height,
x, y)`. The `custom_screens.txt` parser needs a matching addition to
recognize `IMAGE` lines (distinct from `ELEMENT` lines) and store
width/height/path per image element, alongside the existing
`element_t` array (or a small parallel one — genuinely minor either
way, a real implementation-time decision, not a planning-blocking one).

**Preview-first, always**: every change here gets checked through the
EXISTING `--preview <screen> <outfile>` mechanism before ever touching
`/dev/g510-lcd` — this was built specifically so screen changes can be
verified without needing to watch the real hardware for every single
iteration. The final "does it actually look right on the keyboard"
check still needs you there in person, same as always — the preview
gets us to that point without guessing along the way.

## Open questions for you to decide (not blocking planning, but blocking implementation)

1. **Multiple images per screen, or just one?** One is simpler and
   matches "minimum necessary" — but the data format above supports
   either with zero extra design work, so this is really just "do you
   want the GUI to allow adding a second one," not a technical
   question.
2. **What happens to an image if the screen is later deleted/resized?**
   Probably nothing special needed (the `.bin` file just becomes
   unreferenced, matching how removing a sensor element already works)
   — flagging only so it's a conscious non-decision, not an oversight.
3. **Any interest in the freeform pixel-paint idea at all**, even as a
   possible LATER phase after image import works? Not recommending it
   now, but your call.

## What happens next

Nothing, until you say so. When you're ready: confirm the scope above
(or correct it), and this becomes a normal build-and-verify cycle like
everything else in this project — implement, test through `--preview`
extensively, then confirm together on the real LCD before calling it
done.
