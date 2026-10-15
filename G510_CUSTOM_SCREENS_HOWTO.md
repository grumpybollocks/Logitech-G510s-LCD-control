# Custom Screens — How to Use It

The "LCD Screens" tab lets you build your own little dashboards for
the L2-L5 buttons on the keyboard. Here's the whole thing, short version.

## The basics

1. **Pick a screen** — click L1, L2, L3, L4, or L5 at the top.
   - **L1 is a live preview, not editable.** It's the built-in clock —
     pressing the physical L1 button on the keyboard cycles it between
     the clock and the CPU/RAM/VRAM/TEMP stats screen, same as it
     always has. This tab just mirrors whichever of those two is
     currently showing so you can see it without reaching for the
     keyboard; the Add/Import controls are disabled here (hover them
     for a tooltip explaining why) since there's nothing to edit.
   - **L2-L5 are yours.** Add whatever you want to these. An empty
     screen just shows blank — nothing to clear out first.

2. **Add a sensor** — pick one (CPU %, RAM, GPU temp, whatever) from
   the first dropdown, pick **Number** (just shows the value) or
   **Bar** (shows a bar + the value), choose horizontal or vertical
   orientation for bars, then click **Add**. Vertical bars show a
   short label above the bar; the CPU cores use compact labels such as
   C1 through C8 by default.

3. **Add text** — click **Add Text...**, type whatever you want (a
   label, a name, anything static), and it lands on the preview like
   any other element. Up to 4 text fields per screen.

4. **Move it** — click and drag it around on the preview, right where
   you see it. Wherever you drop it is where it'll show up on the real
   keyboard screen.

5. **Put an image on it** — click **Import Image...**, pick a picture
   from your computer, and it gets shrunk down and converted
   automatically to fit the tiny black-and-white screen. Drag it around
   like anything else.

6. **Resize a bar or an image** — hover your mouse near a bar's right
   end, or an image's bottom-right corner, and a little blue square
   appears. Drag that to make it bigger or smaller.

7. **Resize a number or a text field** — these are just text, and the
   screen's tiny built-in fonts don't stretch smoothly, so there's no
   drag handle. Instead, pick a size (Small/Medium/Large/Huge) from
   the **Text Size** dropdown before adding one, or click the size
   button (e.g. "Small") next to an existing one in the list on the
   right to cycle through the sizes. Sensor labels and their values
   use the same selected size.

8. **Remove something** — click the ✕ next to it in the list on the
   right.

9. **Change an element's display name** — edit the text field next to
   a sensor or text element in **Elements on this screen**, then press
   Enter. The custom label is saved with the screen and appears in the
   live preview and on the keyboard. Temperature sensors default to
   compact names such as **CPU** and **GPU**, so they read e.g.
   `CPU 32°C` and `GPU 45°C`; these names can be changed independently.

10. **Choose date and time formats** — for a **Date** or **Time**
    element, use the compact format selector in its row. This replaces
    the LCD-label field because Date and Time are displayed without a
    label; the selector keeps the row the same height as the other
    elements. Only supported presets are offered, and the choice is
    saved with that screen. The preview updates immediately when you
    choose another format.

11. **Start from a dashboard preset** — choose **CPU / GPU
    performance**, **Temperature overview**, or **System overview** and
    click **Apply layout preset...**. The selected L2-L5 screen is
    replaced after confirmation; you can then move, rename, or remove
    the generated elements as usual.

That's it. **Nothing needs a Save button** — every change (move,
resize, add, remove) saves itself instantly.

## Things worth knowing

- **The screen is tiny** — 160×43 pixels, same as the real LCD. If you
  cram too much in, rows will overlap or run off the bottom. The
  preview shows you exactly what the real keyboard will show, so if it
  looks wrong in the app, it'll look wrong on the keyboard too — space
  things out.
- **Each kind of element has its own cap per screen**: 16 sensors, 2
  images, 4 text fields — not one shared total, so e.g. a screen can
  have 16 sensors AND 2 images AND 4 text fields at once. The app will
  tell you if you hit any of these limits.
- **"Bar" isn't available for every sensor** — only ones with an
  honest 0-100% or a sensible temperature range (the app will tell you
  if you pick one that can't; it just shows as a number instead).
- **Vertical bars** are useful for compact multi-core or GPU meters.
  Hover near the lower end of a vertical bar and drag its handle up or
  down to resize it.
- **The blue resize handle only shows up when you're near a bar or a
  resizable image.** It used to always show on every bar at once,
  which looked cluttered and confusing — now it only appears when
  you're actually about to use it.
- **Sensor picker shows what each one means** — the description under
  the dropdown updates as you change the selection, so you don't have
  to guess what "GPU Hotspot Temp" or "Max Temp Seen" actually is.

## Bringing it to the real keyboard

Once your custom screen looks right in the app, press the matching
physical L-button on the keyboard (L2, L3, L4, or L5) to see it live.
If the keyboard doesn't seem to reflect your latest changes, it might
need the LCD service restarted — ask for that if you're not sure how.

## Dashboard presets

Choose a preset and click **Apply layout preset...** to replace the
selected L2-L5 screen. The app asks before replacing it. Available
layouts are:

- **CPU / GPU performance** — eight per-logical-core utilization bars,
  GPU temperature/load, RAM usage, and network upload/download rates.
- **Temperature overview** — CPU temperature, GPU edge/hotspot/VRAM
  temperatures, and the CPU's maximum temperature since the service
  started.
- **System overview** — CPU, RAM, and GPU utilization bars alongside
  the current time and date.

CPU core bar heights show utilization (not temperature); Linux does not
expose per-core CPU temperatures on all hardware. You can move, rename,
resize, or remove the generated elements like any other custom screen.
