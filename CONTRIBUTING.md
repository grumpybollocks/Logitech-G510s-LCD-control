# Contributing

Bug reports, hardware reports and patches are all welcome.

## Reporting a bug

Use the [bug report template](https://github.com/grumpybollocks/Logitech-G510s-LCD-control/issues/new?template=bug_report.md);
the README's [Reporting bugs](README.md#reporting-bugs) section lists what helps.
A `src/g510_lcd_stats --sensors` from hardware nobody has tested yet
(Intel, NVIDIA, other motherboards) is useful on its own.

## Running the tests

```
tests/run-tests.sh
```

It builds the C tests into a scratch folder and runs the app's Python
tests offscreen: it never touches your keyboard, the running services
or your saved settings. It needs what
`./install.sh` installs (gcc, pkg-config, FreeType, libg15render, PyQt5,
evdev). It ends with `ALL TESTS PASSED` and exits 0, or names the
failing checks.

## Changing the code

- After editing `src/g510_lcd_stats.c` or `src/g510_lcd_buttons.c`,
  `scripts/rebuild.sh` (the *G510 LCD - Rebuild* shortcut) recompiles
  them and restarts the services.
- `src/g510_lcd_stats --preview <screen> [out.ppm]` renders one screen
  to an image through the same drawing code as the LCD; handy for checking a rendering
  change without the keyboard.
- Keep `install.sh` and `uninstall.sh` in step: anything the installer
  starts putting in place, the uninstaller should know how to remove.
- Send changes as a pull request against `main`, with
  `tests/run-tests.sh` passing.
