# Ready for anyone

This isn't a personal script that happens to work on one machine — it's
a finished app anyone can pick up, clone, and run on their own G510s,
with nothing tying it back to how it was originally built. This doc
records what makes that true, and what's left before it gets there
fully.

## The short version

The app doesn't hardcode a username, hostname, or checkout location
anywhere in the actual source/config that ships. `install.sh` resolves
its own real location at run time (`DIR="$(cd "$(dirname "$0")" &&
pwd)"`) and either substitutes that into a checked-in
`__PROJECT_DIR__` placeholder (systemd service files, the udev rule)
or passes it to the C compiler as `-DPROJECT_DIR` (the LCD program,
with a compile-time `#error` if that flag is ever missing — no silent
wrong-path fallback). The install script also generates its own
desktop launchers fresh from the real resolved path, rather than
assuming one already exists. Clone it anywhere, run the installer, it
works — that's the bar.

## What the install script actually does

`install.sh`:
1. Resolves its own real location (`$DIR`), independent of where the
   repo was cloned.
2. Checks each dependency's install status first and reports it
   (`[ok]`/`[missing]`) before touching the system — never a silent
   `pacman -S` black box.
3. Installs only what's actually missing.
4. Prompts/exits with a clear message instead of guessing when
   something genuinely can't be auto-determined — see the script's
   own comments for the specific cases.
5. Substitutes `__PROJECT_DIR__`/passes `-DPROJECT_DIR` into every
   checked-in service file, udev rule, and C source that needs to know
   where it's actually installed.
6. Generates a desktop launcher fresh, written to BOTH `~/Desktop` (a
   literal desktop icon) and `~/.local/share/applications` (the
   standard XDG location every desktop environment's actual app menu
   reads — several DEs, GNOME notably, don't show desktop icons at
   all by default, so relying on `~/Desktop` alone would make the app
   undiscoverable there regardless of path-correctness).

## Status

**Done and verified this pass**, on the machine with the actual G510s
hardware — `install.sh` generates all 5 desktop launchers fresh from
the resolved `$DIR`. Tested by running just the shortcut-generation
logic in isolation (not the full installer, which would trigger
unrelated sudo/pacman/udev/systemd changes), diffing the regenerated
files against the real working originals — only difference was added
`Exec=` path quoting (a real robustness improvement), confirmed the
app still launches cleanly via the quoted command, confirmed
`dolphin`/`konsole`/`gio` are all present on this system. `zenity`
added to the dependency list. Shortcuts now also land in
`~/.local/share/applications`.

## Researched against a real reference, not guessed

Checked `pacman -Ql solaar` — a real, actively-maintained, officially
Arch-packaged Logitech device manager already installed on this
machine, same problem domain (Logitech HID++ device control, PyQt-
based GUI, needs a udev rule). Its actual installed layout — `/usr/bin/
solaar`, `/usr/lib/udev/rules.d/`, `/usr/share/applications/
solaar.desktop`, Python source under `/usr/lib/python3.*/
site-packages/` — confirmed the standard, professional pattern: a real
system package installs everything to fixed locations at build time,
so there's no per-user path to discover at runtime at all. The
`__PROJECT_DIR__`-templating this repo does today is the right answer
for a "git clone + run install.sh" install; a real AUR package
(PKGBUILD per app, `/usr/lib/systemd/user/` for the service units,
`/usr/lib/udev/rules.d/` for the udev rule, pacman's own `depends=`
handling dependency resolution instead of a hand-rolled script) is the
next step under consideration for going all the way from "anyone can
clone and run this" to "anyone can just `yay -S` it" (a real
`packaging/g510-lcd/` PKGBUILD already exists on `g510s-dev`, see the
table in the main README) — not yet started here on `main`.
