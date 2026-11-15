#!/bin/bash
# Removes what ./install.sh put in place. Run it from inside the project folder,
# as your normal user (like install.sh, it asks for sudo only for system files):
#
#   ./uninstall.sh            remove the services, udev/hwdb files and shortcuts
#   ./uninstall.sh --deps     also remove what install.sh BUILT into /usr/local
#                             (libg15 + libg15render 1.3.1, ydotool 1.0.4)
#   ./uninstall.sh --purge    also delete your settings (custom screens, images,
#                             macros, saved backlight colour)
#   ./uninstall.sh --yes      don't ask before starting (combine freely)
#
# What it never touches:
#   - distro packages installed in step 1 (PyQt5, Pillow, evdev, playerctl, ...)
#     and Arch/AUR packages (libg15, libg15render, ydotool): remove those with
#     your package manager if nothing else needs them;
#   - the g510-lcd pacman package (packaging/), which lives in /usr/lib/g510-lcd
#     and /usr/lib/systemd/user -- remove it with: sudo pacman -R g510-lcd
#   - your membership of the 'input' group (other programs may rely on it):
#     the exact command to leave it is printed at the end;
#   - this checkout folder itself: delete it by hand afterwards.
#
# Why --deps is opt-in: the /usr/local builds only happen where the distro
# doesn't package a usable version, and other programs (or a later install.sh
# run) may use them. They never replaced distro files, so leaving them is safe.
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
DEPS=0; PURGE=0; YES=0
for arg in "$@"; do
    case "$arg" in
        --deps)  DEPS=1 ;;
        --purge) PURGE=1 ;;
        --yes)   YES=1 ;;
        -h|--help) sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $arg (see ./uninstall.sh --help)"; exit 1 ;;
    esac
done
# Same rule as install.sh: the user services, shortcuts and settings are YOURS,
# so this must run as you; under sudo, $USER and $HOME would be root's.
if [ "$(id -u)" -eq 0 ]; then
    echo "Please run this as your normal user, not with sudo:  ./uninstall.sh"
    echo "(it asks for your sudo password itself, only for the system files)"
    exit 1
fi
command -v sudo >/dev/null || { echo "This needs 'sudo' (for the system files)."; exit 1; }
SUDO=sudo
if ! systemctl --user show-environment >/dev/null 2>&1; then
    echo "No user session here (systemctl --user doesn't work) -- run this from a terminal"
    echo "in your normal desktop login, not via su or a bare ssh/sudo shell."
    exit 1
fi

UNITS=(g510-lcd-stats g510-lcd-buttons g510-macro-daemon g510-backlight-restore)
UNIT_DIR="$HOME/.config/systemd/user"
YDO_UNIT="$UNIT_DIR/ydotool.service"
# install.sh's own ydotool unit carries this marker in its Description
YDO_OURS=0; grep -qs 'added by G510 install.sh' "$YDO_UNIT" && YDO_OURS=1
# Same Desktop folder install.sh used
DESKTOP_DIR=$(xdg-user-dir DESKTOP 2>/dev/null || true)
{ [ -z "$DESKTOP_DIR" ] || [ "$DESKTOP_DIR" = "$HOME" ]; } && DESKTOP_DIR="$HOME/Desktop"
SHORTCUTS=("G510 LCD - App:g510-lcd-app" "G510 LCD - Rebuild:g510-lcd-rebuild" "G510 LCD - Start:g510-lcd-start"
           "G510 LCD - View Logs:g510-lcd-view-logs" "G510 LCD - Project Folder:g510-lcd-project-folder")
LIBDIR=/usr/local/lib/g510-lcd
DATA_DIR="$HOME/.local/share/g510-lcd"

echo "This will remove:"
echo "  - user services: ${UNITS[*]} (stopped, disabled, unit files in $UNIT_DIR)"
if [ "$YDO_OURS" = 1 ]; then
    echo "  - ydotool.service (stopped, disabled, and the unit install.sh wrote in $UNIT_DIR)"
else
    echo "  - ydotool.service: stopped and disabled (install.sh enabled it; the unit file itself is your distro's)"
fi
echo "  - /etc/udev/rules.d/99-g510-lcd.rules, /etc/udev/hwdb.d/91-g510-stop-to-playpause.hwdb (Stop key comes back)"
echo "  - $LIBDIR (the backlight-restore script udev ran)"
echo "  - the 5 'G510 LCD - ...' shortcuts in $DESKTOP_DIR and ~/.local/share/applications"
if [ "$DEPS" = 1 ]; then
    echo "  - --deps: libg15/libg15render 1.3.1 and ydotool 1.0.4 files in /usr/local, /etc/udev/rules.d/80-g510-uinput.rules,"
    echo "            /etc/ld.so.conf.d/g510-lcd.conf (each only if present)"
fi
[ "$PURGE" = 1 ] && echo "  - --purge: your settings in $DATA_DIR and ~/.local/share/g510lcd-buttons.log"
echo "It keeps: distro/AUR packages, your 'input' group membership$([ "$PURGE" = 1 ] || echo ", your settings in $DATA_DIR")."
if [ "$YES" != 1 ]; then
    [ -t 0 ] || { echo "Not a terminal: re-run with --yes to confirm. Nothing changed."; exit 1; }
    read -r -p "Continue? [y/N] " ans
    case "$ans" in y|Y|yes|YES) ;; *) echo "Nothing changed."; exit 0 ;; esac
fi

echo "=== 1/4: user services (as $USER) ==="
for svc in "${UNITS[@]}"; do
    systemctl --user disable --now "$svc.service" 2>/dev/null || true
    if [ -f "$UNIT_DIR/$svc.service" ]; then
        rm -f "$UNIT_DIR/$svc.service"; echo "  [removed] $UNIT_DIR/$svc.service"
    fi
done
systemctl --user disable --now ydotool.service 2>/dev/null || true
echo "  [ok]      ydotool.service stopped and disabled (re-enable: systemctl --user enable --now ydotool)"
if [ "$YDO_OURS" = 1 ]; then
    rm -f "$YDO_UNIT"; echo "  [removed] $YDO_UNIT"
fi
systemctl --user daemon-reload
systemctl --user reset-failed "${UNITS[@]/%/.service}" ydotool.service 2>/dev/null || true

echo "=== 2/4: desktop shortcuts ==="
for s in "${SHORTCUTS[@]}"; do
    name=${s%%:*}; id=${s#*:}
    for f in "$DESKTOP_DIR/$name.desktop" "$HOME/.local/share/applications/$id.desktop"; do
        if [ -f "$f" ]; then rm -f "$f"; echo "  [removed] $f"; fi
    done
done

echo "=== 3/4: udev rules + hwdb (needs sudo) ==="
for f in /etc/udev/rules.d/99-g510-lcd.rules /etc/udev/hwdb.d/91-g510-stop-to-playpause.hwdb "$LIBDIR/restore-backlight.sh"; do
    if [ -e "$f" ]; then $SUDO rm -f "$f"; echo "  [removed] $f"; fi
done
[ -d "$LIBDIR" ] && $SUDO rmdir "$LIBDIR" 2>/dev/null || true

if [ "$DEPS" = 1 ]; then
    # Exactly what `make install` of libg15/libg15render 1.3.1 (--prefix=/usr/local)
    # and install.sh's ydotool step put there. Only /usr/local: package managers
    # never install there, so nothing a package owns can be hit.
    L=/usr/local
    DEP_FILES=(
        "$L"/lib/libg15.so "$L"/lib/libg15.so.* "$L"/lib/libg15.la "$L"/lib/libg15.a "$L"/include/libg15.h
        "$L"/lib/libg15render.so "$L"/lib/libg15render.so.* "$L"/lib/libg15render.la "$L"/lib/libg15render.a
        "$L"/include/libg15render.h "$L"/bin/g15fontconvert "$L"/share/man/man3/libg15render.3
        "$L"/share/g15tools/fonts/G15/default-*.fnt "$L"/share/doc/libg15render-1.3/*
        "$L"/bin/ydotool "$L"/bin/ydotoold
        /etc/udev/rules.d/80-g510-uinput.rules /etc/ld.so.conf.d/g510-lcd.conf
    )
    for f in "${DEP_FILES[@]}"; do
        if [ -e "$f" ] || [ -L "$f" ]; then $SUDO rm -f "$f"; echo "  [removed] $f"; fi
    done
    for d in "$L"/share/g15tools/fonts/G15 "$L"/share/g15tools/fonts "$L"/share/g15tools "$L"/share/doc/libg15render-1.3; do
        [ -d "$d" ] && $SUDO rmdir "$d" 2>/dev/null || true
    done
    $SUDO ldconfig
    hash -r
fi
$SUDO udevadm control --reload-rules
$SUDO systemd-hwdb update
$SUDO udevadm trigger --action=change --subsystem-match=input --subsystem-match=hidraw || true

echo "=== 4/4: your settings ==="
if [ "$PURGE" = 1 ]; then
    rm -rf "$DATA_DIR" && echo "  [removed] $DATA_DIR"
    rm -f "$HOME/.local/share/g510lcd-buttons.log"
else
    [ -d "$DATA_DIR" ] && echo "  [kept]    $DATA_DIR (custom screens, images, macros, colour) -- --purge deletes it"
fi

echo ""
echo "=== Done ==="
echo "Unplug and replug the keyboard once so the old rules and the Stop-key remap let go."
echo "The project folder ($DIR) is still there: delete it by hand if you're done with it."
if id -nG "$USER" | tr ' ' '\n' | grep -qx input; then
    echo ""
    echo "You're still in the 'input' group (install.sh may have added you). Other programs"
    echo "may need it, so it isn't removed automatically. To leave it, run this, then log out and back in:"
    echo "    sudo gpasswd -d \"\$USER\" input"
fi
