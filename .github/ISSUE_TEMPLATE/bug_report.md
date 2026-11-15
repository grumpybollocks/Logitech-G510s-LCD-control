---
name: Bug report
about: Something on the LCD, backlight, G-keys or installer doesn't work
labels: bug
---

**What happened, and what did you expect?**


**Steps to reproduce** (what you pressed / clicked, headset plugged in or not):


**System**
- Distro and version (e.g. Ubuntu 24.04, Arch):
- Kernel (`uname -r`):
- Desktop (KDE, GNOME, ...):
- Installed with: `./install.sh` / the PKGBUILD
- Keyboard: G510 / G510s

**Output** — paste each into the blocks below. Glance over it for anything private first.

If the install failed, the installer's output:
```
```

`src/g510_lcd_stats --sensors` (run from the checkout; PKGBUILD install: `/usr/lib/g510-lcd/src/g510_lcd_stats --sensors`):
```
```

`journalctl --user -u g510-lcd-stats -u g510-lcd-buttons -u g510-macro-daemon -b --no-pager | tail -n 100`:
```
```

`lsusb | grep 046d`:
```
```
