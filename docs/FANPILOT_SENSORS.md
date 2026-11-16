# FanPilot sensors on the G510 LCD

Fan speeds, fan duty and friendly-named temperatures on your keyboard's LCD,
supplied by [FanPilot](https://github.com/grumpybollocks/fanpilot), a
separate fan-control app for Linux. **This is optional.** Without FanPilot the
G510 app works exactly as before. FanPilot elements just show `N/A`.

- [What you get](#what-you-get)
- [Using them](#using-them)
- [Which PCs](#which-pcs)
- [Sensor keys](#sensor-keys)
- [Short labels on the LCD](#short-labels-on-the-lcd)
- [How it works](#how-it-works)
- [The file format (feed it from your own program)](#the-file-format)
- [Limits](#limits)
- [Troubleshooting](#troubleshooting)
- [Not available (yet)](#not-available-yet)

## What you get

While FanPilot is running, the **Custom Screens** sensor dropdown gets an
extra section at the end, one entry per thing FanPilot knows, under
FanPilot's own names:

| Kind | Examples | Number | Bar |
| --- | --- | --- | --- |
| Temperature (`temp`) | CPU (Tctl), CPU die (CCD1), GPU edge / hotspot / memory, VRM, chipset, NVMe, motherboard, RAM, drives | `56°C` | yes, 0–90 °C (full at 90 °C, like the built-in temperatures) |
| Fan speed (`rpm`) | CPU fan, System fan 1, Pump, Graphics card fan | `1180` | no (RPM has no honest 0–100 scale) |
| Fan duty (`pct`) | the same fans, as the % FanPilot drives them at | `45%` | yes |

The entries read **"FanPilot: CPU fan (CPU_FAN) speed"** and so on in the
dropdown. In the list of elements on a screen they appear as **"FP CPU fan…"**
(the row is narrow), with the full name in the tooltip.

These are things the LCD program can't see by itself: fan RPM, the duty
FanPilot chose, and names that match what is actually on your board instead
of `MB_TEMP3`.

## Using them

1. Install and start FanPilot (its own README covers that). A packaged
   install, or its `install.sh`, both work.
2. Open the G510 app and go to **Custom Screens**, then pick L2–L5.
3. Open the sensor dropdown and scroll to the end. Pick a **FanPilot: …**
   entry, choose **Number** or **Bar**, then click **Add**. Drag it into
   place on the preview.

If FanPilot is installed but stopped, the dropdown shows a greyed-out
*"FanPilot sensors: start FanPilot to use fan speeds and its sensors"* line
instead. When you start FanPilot, the entries appear the next time you
switch to the Custom Screens tab (click another tab and back).

`custom_screens.txt` stores them like any other element, with an `FP:`
prefix on the key:

```
SCREEN L3
ELEMENT sensor=FP:rpm.it8688/pwm1 style=number x=0 y=0 width=40 font=0
ELEMENT sensor=FP:pct.it8688/pwm1 style=bar x=0 y=11 width=60 font=0
ELEMENT sensor=FP:t.k10temp/temp1 style=bar x=0 y=22 width=60 font=0
```

## Which PCs

Anything FanPilot supports, which means the G510 side has nothing
board-specific. FanPilot finds the sensors and fans, and the G510 app shows
whatever FanPilot lists:

- **Motherboards:** the Super I/O chips FanPilot drives (ITE `it87`,
  Nuvoton `nct6775` family, Fintek, Winbond…). Read-only chips still give
  temperatures.
- **CPUs:** AMD `k10temp` (Tctl plus one entry per CCD) or `zenpower`, Intel `coretemp`.
- **Graphics cards:** AMD `amdgpu` (several cards are fine, each gets its own
  key) and NVIDIA temperatures through `nvidia-smi`.
- **Drives and RAM:** NVMe, SATA through `drivetemp`, DDR4 `jc42`, DDR5
  `spd5118`, when the kernel exposes them.

Checked on real hardware so far: Gigabyte B550M (ITE IT8688E) + Ryzen +
Radeon RX 9060 XT + NVMe: 19 FanPilot sensors, all drawn on the LCD. Other
id shapes (two identical chips, a second AMD card, NVIDIA bus ids, hundreds
of drive/RAM rows) are covered by the tests in `tests/test_fanpilot.c` and
`tests/test_fanpilot_gui.py`.

## Sensor keys

Keys come from FanPilot and stay the same across reboots. Use them after
`FP:` in `custom_screens.txt`.

| Key | Meaning | Example |
| --- | --- | --- |
| `t.<chip>/temp<N>` | a temperature | `t.k10temp/temp1`, `t.it8688/temp5` |
| `rpm.<fan>` | speed of a fan FanPilot controls | `rpm.it8688/pwm2` |
| `pct.<fan>` | its duty in % | `pct.it8688/pwm2` |

`<chip>` is the kernel's hwmon name (`it8688`, `nct6798`, `k10temp`,
`amdgpu`, `nvme`…). When two chips have the **same** name, FanPilot adds the
device they belong to, so the key doesn't depend on boot order:

| Situation | Key |
| --- | --- |
| two NVMe drives | `t.nvme@nvme0/temp1`, `t.nvme@nvme1/temp1` |
| two AMD cards | `rpm.amdgpu@0000:03:00.0/fan1` |
| NVIDIA card | `t.nvidia@0000:01:00.0/temp1` |
| the only AMD card | `rpm.amdgpu/fan1` (no `@…`) |

> Adding a second identical device (another NVMe, another GPU) renames the
> existing one's key from `nvme/…` to `nvme@nvme0/…`. An element using the
> old key then shows `N/A`. Pick it again from the dropdown.

`src/g510_lcd_stats --sensors` lists the keys your PC has right now.
(`fanpilotctl sensors` shows FanPilot's names and values, but not the keys.)

## Short labels on the LCD

On the LCD, an element shows a label of at most 4 characters in front of the
value. FanPilot picks it:

| Label | Means |
| --- | --- |
| `CPU`, `CCD1`, `CCD2` | CPU control temperature, per-die temperatures |
| `GPU`, `GHOT`, `GMEM` | AMD GPU edge / hotspot / memory |
| `GPUT` | NVIDIA GPU temperature |
| `NVME` | NVMe drive |
| `SYS`, `SOC`, `SOCK`, `VRM`, `PCH` | board: system, SoC VRM, CPU socket, VRM, chipset |
| `CPUF` / `CPF%` | CPU fan speed / duty |
| `SYS1` / `SY1%` | System fan 1 speed / duty (and 2, 3…) |
| `GPUF` / `GPF%` | graphics card fan speed / duty |
| `FAN5` / `FN5%` | any other header, by its number |

Anything else gets the first 4 letters of its name. If FanPilot has no
reading at the moment (for example, a fan the BIOS is driving has no duty),
the element shows `N/A` and keeps its place.

## How it works

```
fanpilotd (root service)                      G510 app + LCD program (your user)
  every tick (1 s by default, max 5 s)  ──►  /run/fanpilot/sensors.tsv  ──►  read at most once a second
  atomic rewrite, mode 644, tmpfs (RAM)                                     stale (> 15 s) or missing → N/A
```

- The file lives in RAM (`/run` is tmpfs), so nothing is written to your disk.
- FanPilot writes a new file and renames it into place, so a reader never sees
  a half-written file.
- When FanPilot stops, it deletes the file. If it crashes, the file goes stale
  after 15 seconds. Either way, every FP element shows `N/A` and nothing hangs
  or crashes.
- The LCD program (`g510_lcd_stats.c`, `fanpilot_load()`) and the app
  (`g510_app.py`, `fanpilot_sensors()`) use the **same** parsing rules and
  limits, so the app never offers a sensor the LCD can't draw.
- Nothing runs as root on the G510 side, and FanPilot is never contacted
  directly. The file is the whole interface.

## The file format

Any program can feed the LCD this way, not only FanPilot:

```
# fanpilot-sensors 1 1794839056
t.k10temp/temp1	temp	CPU	CPU (Tctl)	56.4
rpm.it8688/pwm2	rpm	SYS1	System fan 1 (SYS_FAN1) speed	1188
pct.it8688/pwm2	pct	SY1%	System fan 1 (SYS_FAN1) duty	100
```

- **Header:** `# fanpilot-sensors 1 <unix time>`. Version `1` is the only one
  accepted. The time must be within 15 s of now, or the whole file is ignored.
- **Rows:** exactly five **tab**-separated fields: `key`, `kind` (`temp` |
  `rpm` | `pct`), a short label (≤ 4 characters), a long label, and the value
  (a number, or empty when unknown).
- Rows with a different number of fields, an unknown kind, an empty key or a
  non-numeric value are skipped (or shown as `N/A`). The rest of the file
  still counts. CRLF line endings and a missing final newline are fine.
- **Rewrite it at least every 15 seconds**, atomically (write a temp file,
  then `rename`), and readable by your user.

The path is fixed at `/run/fanpilot/sensors.tsv`, so a different program
needs write access there (FanPilot's service owns the directory).

## Limits

The limits are deliberately generous, so even a large workstation keeps every
fan:

| Limit | Value | Why |
| --- | --- | --- |
| rows | 256 | FanPilot lists temperatures first and fans last. A PC with many drives, DIMMs and GPUs must never lose its fan rows. |
| line length | 510 bytes plus the newline (CRLF: the `\r` counts) | room for long custom names you gave fans in FanPilot |
| key length | 47 bytes | longer keys are skipped, never cut short (a cut key could match the wrong sensor) |
| short label | 4 characters | longer ones are cut to 4 |
| staleness | 15 s | FanPilot's slowest setting is a 5 s tick |

Before 2026-11-16 the limits were 64 rows and 255-byte lines.

## Troubleshooting

| You see | Check |
| --- | --- |
| No FanPilot entries in the dropdown | `systemctl status fanpilotd`; `ls -l /run/fanpilot/sensors.tsv` (it should be a few seconds old, mode `-rw-r--r--`). |
| Greyed-out "start FanPilot" line | FanPilot is installed but not running: `sudo systemctl start fanpilotd`. |
| An element shows `N/A` | `src/g510_lcd_stats --sensors` lists the current keys. A key can change when a second identical device is added (see [Sensor keys](#sensor-keys)). |
| A fan has RPM but no duty | the fan is under BIOS/automatic control, so FanPilot sets no duty for it |
| The value is up to a second behind FanPilot's window | expected: both refresh once a second |

`src/g510_lcd_stats --sensors` ends with a **FanPilot** block: the number of
sensors read, then one line per key with its short label, name and value. It
says `not running` if the file is missing or stale.

## Not available (yet)

Only listed here so nobody hunts for them. All of these are FanPilot-side:

- **Fans FanPilot can only read** (no PWM control): RPM-only headers on
  read-only chips, and **NVIDIA fan %** (from `nvidia-smi`). FanPilot shows
  them in its own window but doesn't write them to `sensors.tsv`. When it
  does, they will appear here with no G510 change: any `rpm` or `pct` row
  works.
- **Voltages, power, clocks:** FanPilot's Sensors view has them, but the
  file format has no kind for them (`temp` / `rpm` / `pct` only).
