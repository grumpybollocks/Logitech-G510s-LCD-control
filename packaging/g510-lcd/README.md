# g510-lcd packaging

A real Arch/pacman PKGBUILD for the G510s app, alongside the existing
`install.sh` git-clone install method (both are valid; this one
follows the "package installs everything to fixed `/usr/...`
locations, no per-user path discovery needed" pattern -- see
`PORTABILITY.md`), mirroring `packaging/g910-control/` on the G910
side, built the same night for direct comparison.

## Status

Built, installed for real on the real machine (`sudo pacman -U`, run
by the user directly after this session's own permission system
blocked doing it automatically), and both gaps originally left open
below are now closed with real fixes, not workarounds. **Not
submitted to the AUR** -- that's still deliberately not done (see
"Before real AUR submission" below). **G510s as a whole is still work
in progress** -- this packaging pass being solid doesn't mean the app
itself is "finished"; Custom Screens is still explicitly WIP, and
nothing here changes the project's own standing rule that nothing
gets tagged or called fully done until physically confirmed end to
end on the real keyboard.

## Dependencies

`depends=('python' 'python-pyqt5' 'python-pillow' 'python-evdev'
'ydotool' 'freetype2' 'libg15render')`. Cross-checked against actual
source, not copied from install.sh's list unexamined -- `yad` is in
install.sh's pacman line but a real grep of every `.py`/`.c` file
found zero uses of it anywhere in current code (leftover from the
retired yad-based backlight script, see `BRANCHES.md`'s
`legacy-yad-backlight-script` entry); correctly left out here.
`python-pillow` is real and required (`png-to-lcd.py`'s Floyd-Steinberg
dithering for the Custom Screens image-import feature).

## Layout -- deliberately NOT flat, unlike g910-control

`packaging/g910-control`'s PKGBUILD installs its `.py` files flat
under `/usr/lib/g910-control/` (no `src/` subdirectory). This package
keeps the `src/` subdirectory:

- `/usr/bin/g510-lcd` -- thin wrapper, `exec`s the real script
- `/usr/lib/g510-lcd/src/*.py`, `*.c`-compiled binaries -- same shape
  as the git-clone checkout, not flattened
- `/usr/lib/systemd/user/g510-lcd-stats.service`,
  `g510-lcd-buttons.service`, `g510-macro-daemon.service`
- `/usr/lib/udev/rules.d/99-g510-lcd.rules` -- installed byte-identical
  to the checked-in source, nothing stripped (see the resolved gap
  below for why that's now possible)
- `/usr/lib/g510-lcd/restore-backlight.sh` -- the udev-triggered
  backlight-restore wrapper (see below)
- `/usr/share/applications/g510-lcd.desktop`
- `/usr/share/licenses/g510-lcd/LICENSE`

This isn't a style choice -- `g510_app.py` computes its own
`PROJECT_DIR` as `Path(__file__).resolve().parent.parent`, and several
of its own path constants (`STATS_BINARY`, `DEFAULTS_SCRIPT`, the
`png-to-lcd.py` subprocess call) are built as `PROJECT_DIR / "src" /
...` or `PROJECT_DIR / "scripts" / ...`. Flattening the layout the way
`g910-control` does would silently break every one of those
(`PROJECT_DIR` would resolve one directory too high, e.g. `/usr/lib`
instead of `/usr/lib/g510-lcd`) without a real source change this pass
didn't make. Keeping the checkout shape under a fixed root means the
existing, already-tested path logic (the same code exercised by
tonight's full regression suite) is correct here with zero changes,
rather than a new, unverified code path.

## Resolved: the udev rule's backlight-restore line

`99-g510-lcd.rules` has four jobs: create `/dev/g510-lcd` and
`/dev/g510-keys` symlinks (both genuinely load-bearing --
`g510_lcd_stats.c`/`g510_lcd_buttons.c` hard-`fopen()` those exact
paths and fail outright without them), fix LED sysfs permissions, and
restore the last-applied backlight color on every hotplug.

That last job used to shell out to a `__PROJECT_DIR__`-substituted
script, which couldn't work for a package (udev fires as root with no
per-user `$HOME` to resolve). Fixed properly, not dropped:
`scripts/restore-backlight.sh` is a small, install-mode-agnostic
wrapper -- at hotplug time it asks `loginctl` which user actually has
the active `seat0` session (this is genuinely single-seat desktop
hardware; broader multi-user handling is out of scope), resolves
their `$HOME` via `getent passwd`, and execs their own
`~/.local/share/g510-lcd/set-backlight-color.sh`. Zero `PROJECT_DIR`
dependency, so both `install.sh` and this PKGBUILD install the exact
same file to the exact same fixed path
(`/usr/lib/g510-lcd/restore-backlight.sh`) -- the udev rule itself
needed no substitution or stripping either way anymore, installed
byte-identical to the checked-in source.

Verified for real, not just read: ran the script directly on the real
machine (`bash -x`, full trace) -- correctly resolved the real
logged-in user, found their real saved color script, executed it, and
the LED sysfs state matched afterward. This is the same mechanism a
real hotplug event would trigger, just invoked manually instead of
waiting for a physical replug.

One minor, honest wrinkle: on a machine with both install methods
present (like this one, right now), `/usr/lib/g510-lcd/` is a path
`pacman` considers package-owned once this PKGBUILD is installed --
`install.sh`'s own `sudo cp` into that same path writes outside
`pacman`'s bookkeeping. Harmless in practice (both methods copy the
exact same file content), but worth knowing before treating
`pacman -Qkk g510-lcd` as gospel on a machine that's also run
`install.sh`.

## Resolved: the label font

Same restriction `install.sh` already documents -- Eurostile Bold is
commercially licensed and can't be redistributed in the package, so
`g510_lcd_stats` still refuses to start without a converted copy
somewhere. What's fixed: it now checks
`~/.local/share/g510-lcd/fonts/lcd-label-8.fnt` (always user-writable,
regardless of install mode) *before* falling back to the old
`PROJECT_DIR`-relative path -- so the manual conversion step for a
packaged install no longer needs `sudo`, matching the git-clone flow's
convenience exactly.

Verified for real, in stages: rebuilt the binary with the fix,
confirmed all three isolated scenarios (font only in the old location,
font only in the new location, font in neither) behave exactly as
designed -- including the failure case still producing the same clear
error message, not some new unexpected one. Separately, against the
*actual installed package* on the real machine (installed before this
fix existed): it failed with the documented message, exactly as
predicted, since its compiled binary still only knew the old
`PROJECT_DIR`-only lookup. Copied the real converted font into
`~/.local/share/g510-lcd/fonts/` for real (plain user-level file copy,
no sudo) -- ready for whenever the package is rebuilt with this fix
and reinstalled, which hadn't happened yet as of this note. The fixed
package itself was rebuilt and content-verified the same way as the
rest of this file describes, but **reinstalling it on the real machine
to pick up this exact fix is still a pending step**, not done as part
of this pass.

## What's actually been verified this pass

1. `makepkg` against a real tarball -- both a manually-assembled one
   from the live working tree (for fast iteration while fixing a real
   bug below) and a genuine `git archive` of the `g510s-dev` branch
   HEAD (matching how `g910-control`'s README describes its own
   process, since neither app has a real public tag yet this can
   build from a real URL). Builds clean, no warnings.
2. Extracted the built `.pkg.tar.zst` and checked every substituted
   file by hand: the `/usr/bin/g510-lcd` wrapper, all three systemd
   service `ExecStart=` lines, and the udev rule (now installed
   byte-identical to source, confirmed via `diff`, not just "should
   match").
3. Confirmed the compiled `g510_lcd_stats` binary really has
   `/usr/lib/g510-lcd` baked in (`strings` on the binary).
4. Grepped every file in the built package (not just the source tree)
   for personal identifiers -- clean, except `.BUILDINFO`'s
   `builddir`/`startdir` fields, which record wherever a build
   happened to run (this test ran from a scratch directory under the
   user's home). Not a PKGBUILD defect -- a real AUR build (or even
   just building from `/tmp`) wouldn't show this; noted for honesty,
   not left silently unmentioned.
5. **Real install, done for real**: `sudo pacman -U`, run by the user
   directly. Confirmed afterward: `systemctl --user show
   g510-lcd-stats.service -p FragmentPath` still resolves to
   `~/.config/systemd/user/g510-lcd-stats.service` (the git-clone
   version), and its `MainPID` was unchanged before and after the
   install -- the packaged service files, udev rule, and everything
   else installed alongside without disrupting anything live, exactly
   as designed. This package has no `.install` hook (`pacman -Qi
   g510-lcd` shows `Install Script: No`), so `pacman -U` never touched
   systemd enablement at all -- the packaged services were neither
   started nor enabled by the install itself, and actually starting
   them (which would require temporarily removing the `~/.config`
   override so the packaged unit wins) to prove the packaged binaries
   work end-to-end wasn't done this pass.

## What's still genuinely pending (honest, not glossed over)

- **The font and backlight-restore fixes above exist in a rebuilt,
  isolated-tested package** -- but the package actually installed on
  the real machine right now predates both fixes. Reinstalling
  (`sudo pacman -U` again, on the newly rebuilt `.pkg.tar.zst`) is
  needed to bring the real install up to date -- not done as part of
  this exact pass, since it's the same class of action that needed the
  user directly the first time.
- **Starting the packaged services for a true end-to-end run** (LCD
  screen, buttons, macros actually working from `/usr/lib/g510-lcd/`,
  not just the binary answering to `--preview`) -- not attempted, to
  avoid any chance of disrupting the live dev-checkout services while
  testing.
- **`namcap`** -- not installed on this machine, lint step skipped
  entirely rather than assumed clean.
- **`sha256sums=SKIP`** -- same private-repo/no-public-tag situation
  documented in `G510_PLAN_A.md`'s Phase 4 section. Must become a real
  sum before any AUR submission.

## Before real AUR submission (not done, needs the user's go-ahead)

- Reinstall with the font/backlight fixes and do the true end-to-end
  service run above.
- Make the source repo public, compute a real `sha256sum`.
- Generate `.SRCINFO` (`makepkg --printsrcinfo > .SRCINFO`).
- Push to a dedicated `aur.archlinux.org` git remote.
