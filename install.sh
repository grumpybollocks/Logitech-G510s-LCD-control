#!/bin/bash
# Full setup for the G510s LCD project. Run it from inside the project folder:
#
#   ./install.sh          normal install (asks you to replug the keyboard once)
#   ./install.sh --yes    same, without waiting for the replug prompt
#
# Supported: Arch family (Arch, Manjaro, EndeavourOS, Garuda, CachyOS...) and
# Debian/Ubuntu family (Debian, Ubuntu, Mint, Pop!_OS...). Fedora and openSUSE
# are attempted with the same steps but haven't been tested yet.
#
# Installs every dependency, puts the system files (udev rules, hwdb remap,
# systemd services) in place and compiles the C programs. Safe to re-run any
# time -- every step checks first and only adds what's missing.
#
# Where a distro doesn't package something this needs, it's built from source,
# from a pinned release whose checksum must match (the same checksums the Arch
# packages use): libg15 + libg15render 1.3.1 (the LCD library), and ydotool
# 1.0.4 when the distro's ydotool is too old to replay macros (e.g. Ubuntu
# 24.04 ships 0.1.8). Those go to /usr/local and never replace distro files.
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"
YES=0; [ "${1:-}" = "--yes" ] && YES=1
# Run as your normal user: it sets things up for YOU ($USER, your services, your
# 'input' group membership) and uses sudo only where needed. Under sudo, $USER
# and $HOME would be root's and the user services step can't work.
if [ "$(id -u)" -eq 0 ]; then
    echo "Please run this as your normal user, not with sudo:  ./install.sh"
    echo "(it asks for your sudo password itself, only for the steps that need it)"
    exit 1
fi
command -v sudo >/dev/null || { echo "This installer needs 'sudo' (for the system-wide steps). Install it, then re-run."; exit 1; }
SUDO=sudo
# The services are per-user (systemctl --user): without a user session bus that
# step would fail only AFTER all the sudo steps -- check first.
if ! systemctl --user show-environment >/dev/null 2>&1; then
    echo "No user session here (systemctl --user doesn't work) -- run this from a terminal"
    echo "in your normal desktop login, not via su or a bare ssh/sudo shell."
    exit 1
fi
# The checkout path is written into systemd units (quoted there); these two
# characters can't be quoted safely.
# ($ too: systemd expands $VAR / $$ even inside a quoted ExecStart word.)
case "$DIR" in *'"'*|*'\'*|*'$'*) echo "Please move the project to a folder whose path has no \", \\ or \$ in it."; exit 1 ;; esac

# ------------------------------------------------------------------ distro
[ -r /etc/os-release ] && . /etc/os-release
FAMILY=""
for id in ${ID:-} ${ID_LIKE:-}; do
    case "$id" in
        arch|archlinux) FAMILY=arch ;;
        debian|ubuntu)  FAMILY=debian ;;
        fedora|rhel|centos) FAMILY=fedora ;;
        suse|opensuse|opensuse-tumbleweed|opensuse-leap) FAMILY=suse ;;
    esac
    [ -n "$FAMILY" ] && break
done
if [ -z "$FAMILY" ]; then
    echo "Sorry: ${PRETTY_NAME:-this Linux} isn't one this installer knows (Arch, Debian/Ubuntu,"
    echo "Fedora, openSUSE families). The README lists what's needed to set it up by hand."
    exit 1
fi
echo "Detected: ${PRETTY_NAME:-unknown} -> $FAMILY family"
case "$FAMILY" in fedora|suse) echo "  NOTE: $FAMILY isn't tested yet -- please report how it goes." ;; esac

# Packages per family. Arch's list is exactly what it always was (build tools are
# added in step 5 only if gcc/pkg-config are missing); elsewhere the build tools
# are needed anyway for the source builds, and ydotool is handled in step 3.
case "$FAMILY" in
    arch)   PKGS=(python-pyqt5 python-pillow ydotool python-evdev freetype2 zenity playerctl)
            have() { pacman -Qi "$1" &>/dev/null; }
            inst() { $SUDO pacman -S --needed --noconfirm "$@"; } ;;
    debian) PKGS=(python3-pyqt5 python3-pil python3-evdev libfreetype-dev zenity playerctl pulseaudio-utils libusb-dev build-essential pkg-config cmake curl ca-certificates)
            # "install ok installed" only: `dpkg -s` also succeeds for removed-but-not-purged packages
            have() { dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q 'install ok installed'; }
            APT_UPDATED=0
            apt_update_once() { [ "$APT_UPDATED" = 1 ] || { $SUDO apt-get update -qq || true; APT_UPDATED=1; }; }
            inst() { apt_update_once && $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y "$@"; } ;;
    fedora) PKGS=(python3-qt5 python3-pillow python3-evdev freetype-devel zenity playerctl pulseaudio-utils libusb-compat-0.1-devel gcc make pkgconf-pkg-config cmake curl)
            have() { rpm -q "$1" &>/dev/null || rpm -q --whatprovides "$1" &>/dev/null; }
            inst() { $SUDO dnf install -y "$@"; } ;;
    suse)   PKGS=(python3-qt5 python3-Pillow python3-evdev freetype2-devel zenity playerctl pulseaudio-utils libusb-compat-devel gcc make pkg-config cmake curl)
            have() { rpm -q "$1" &>/dev/null || rpm -q --whatprovides "$1" &>/dev/null; }
            inst() { $SUDO zypper --non-interactive install "$@"; } ;;
esac

# One scratch dir for every source build, removed however the script ends.
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# Build-from-source helper: download, verify the pinned sha512, unpack into $2.
fetch_verified() {
    local url=$1 want=$2 into=$3 file
    mkdir -p "$into"
    file="$into/download.tar.gz"
    curl -sSfL -o "$file" "$url" || { echo "  [error] download failed: $url" >&2; return 1; }
    if [ "$(sha512sum "$file" | cut -d' ' -f1)" != "$want" ]; then
        echo "  [error] checksum mismatch for $url -- refusing to build it" >&2; return 1
    fi
    tar -xzf "$file" -C "$into"
}

echo "=== 1/8: Distro packages ==="
# Real per-package [ok]/[missing] reporting -- never a silent black box.
MISSING=()
for pkg in "${PKGS[@]}"; do
    if have "$pkg"; then echo "  [ok]      $pkg"; else echo "  [missing] $pkg"; MISSING+=("$pkg"); fi
done
if [ ${#MISSING[@]} -gt 0 ]; then inst "${MISSING[@]}"; else echo "  All distro packages already installed."; fi
# the audio visualizer records with parec (from libpulse on Arch)
if [ "$FAMILY" = arch ] && ! command -v parec >/dev/null; then
    echo "  [missing] parec (libpulse) -- needed by the audio visualizer"
    inst libpulse
fi

# Arch: build tools are needed by the AUR build (step 2) and nm (TrueType check).
# Same rule as before -- only installed if gcc/pkg-config/nm are missing -- just earlier.
if [ "$FAMILY" = arch ] && { ! command -v gcc &>/dev/null || ! command -v pkg-config &>/dev/null || ! command -v nm &>/dev/null; }; then
    echo "  [missing] build tools (gcc/pkg-config/binutils)"
    inst base-devel pkgconf
fi

echo "=== 2/8: LCD library (libg15, libg15render) ==="
if [ "$FAMILY" = arch ]; then
    # Arch family: the AUR packages (needs an AUR helper).
    AUR_MISSING=()
    for pkg in libg15 libg15render; do
        if pacman -Qi "$pkg" &>/dev/null; then echo "  [ok]      $pkg"; else echo "  [missing] $pkg"; AUR_MISSING+=("$pkg"); fi
    done
    if [ ${#AUR_MISSING[@]} -gt 0 ]; then
        AUR=$(command -v yay || command -v paru || true)
        if [ -z "$AUR" ]; then
            echo "An AUR helper is needed for: ${AUR_MISSING[*]}"
            echo "Install yay (https://github.com/Jguer/yay) or paru first, then re-run this script."
            exit 1
        fi
        "$AUR" -S --needed --noconfirm "${AUR_MISSING[@]}"
    fi
elif [ -e /usr/local/lib/libg15render.so.1 ] && [ -e /usr/local/lib/libg15.so.1 ]; then
    echo "  [ok]      libg15 + libg15render (built earlier, /usr/local)"
else
    # Everywhere else: build the exact release the AUR uses, from source, with
    # TrueType support -- the LCD program must be built with the same TTF setting
    # as the library or their shared struct layout differs (see G510_README.md).
    echo "  building libg15 + libg15render 1.3.1 from source (checksum-pinned)..."
    fetch_verified https://github.com/vividnightmare/libg15/archive/refs/tags/1.3.1.tar.gz \
        7a76b4fd9358d65f82009e9b448e158f6ca4424256ecf24608f1a47d87a500952283e7c2f7fc6940d4fa550f52c03ee7cdda146b28da4c85580bca43fbaadee2 "$WORK/libg15"
    fetch_verified https://github.com/vividnightmare/libg15render/archive/refs/tags/1.3.1.tar.gz \
        c5bc04e281506415c4b559fde08f48e30b34cc6fdd47ca1765cefcf9eeb8aa82ad0902204d35358db36deebc491d8b8bdfb0d82741393f36dd7be5f80d12461c "$WORK/libg15render"
    for p in libg15 libg15render; do
        extra=""; [ "$p" = libg15render ] && extra="--enable-ttf"
        # GitHub's release tarballs drop the executable bit on ./configure: run it via sh.
        # Compiler output goes to a log (libg15render prints harmless warnings); shown on failure.
        ( cd "$WORK/$p/$p-1.3.1" && sh ./configure --prefix=/usr/local $extra && make -j"$(nproc)" && $SUDO make install ) >"$WORK/$p.log" 2>&1 \
            || { echo "  [error] building $p failed -- last lines of the build log:"; tail -15 "$WORK/$p.log"; exit 1; }
        echo "  [built]   $p"
    done
fi
if [ "$FAMILY" != arch ]; then
    # Make sure the loader searches /usr/local/lib (Debian/Ubuntu already do;
    # Fedora/openSUSE don't). Every run, so a half-finished earlier run recovers.
    if ! grep -qsx '/usr/local/lib' /etc/ld.so.conf /etc/ld.so.conf.d/*.conf; then
        echo /usr/local/lib | $SUDO tee /etc/ld.so.conf.d/g510-lcd.conf >/dev/null
    fi
    $SUDO ldconfig
fi
# Whichever library the loader will use must have TrueType support.
scripts/g15-flags.sh --check || exit 1
echo "  [ok]      $(scripts/g15-flags.sh --lib) has TrueType support"

echo "=== 3/8: ydotool (replays G-key macros) ==="
# Macros are stored as raw keycodes ("29:1 46:1 46:0 29:0"), which needs
# ydotool 1.x with its ydotoold daemon. 0.1.x understands neither.
# (Detected via ydotoold's own --help: "ydotool key --help" only answers when
# the daemon is already running, so it can't be used before setup.)
is_ydotoold_1x() { [ -n "$1" ] && [ -x "$1" ] && timeout 3 "$1" --help 2>&1 | grep -q -- '--socket-path'; }
ydotool_ok() { command -v ydotool >/dev/null && is_ydotoold_1x "$(command -v ydotoold)"; }
if ! ydotool_ok; then
    case "$FAMILY" in
        arch)
            # installed from the repos in step 1 -- never built here
            echo "  [warn] ydotool doesn't look like version 1.x -- if G-key macros don't replay: sudo pacman -Syu ydotool" ;;
        debian)
            # use the distro's only if it's already 1.x (Ubuntu 24.04 ships 0.1.8, which can't).
            # Fresh lists first; strip a Debian epoch ("1:0.1.8-3" is 0.1.8).
            apt_update_once
            cand=$(apt-cache policy ydotool 2>/dev/null | awk '/Candidate:/{print $2}')
            case "${cand#*:}" in
                [1-9]*) inst ydotool || echo "  [warn] apt couldn't install ydotool $cand (see above) -- building it from source instead" ;;
            esac ;;
        *)
            inst ydotool || echo "  [warn] the package manager couldn't install ydotool (see above) -- building it from source instead" ;;
    esac
    hash -r
fi
if ydotool_ok; then
    echo "  [ok]      ydotool 1.x ($(command -v ydotoold))"
elif [ "$FAMILY" = arch ]; then
    :   # warned above; Arch keeps its packaged ydotool
else
    echo "  this distro's ydotool is missing or too old -- building ydotool 1.0.4 from source (checksum-pinned)..."
    fetch_verified https://github.com/ReimuNotMoe/ydotool/archive/refs/tags/v1.0.4.tar.gz \
        bbf66d752aa1dce9173c930e3a71bc277b330763b1aa0e38f9fec1976c282c64330251ba5abe222a991f4bcafbabf1312a940eb4b40a34bf99f628c2a41bc4a2 "$WORK/ydotool"
    # build just the two programs (the man pages would need scdoc)
    ( cd "$WORK/ydotool/ydotool-1.0.4" && cmake -B build -DCMAKE_INSTALL_PREFIX=/usr/local && \
      cmake --build build --target ydotool ydotoold -j"$(nproc)" && \
      $SUDO install -m755 build/ydotool build/ydotoold /usr/local/bin/ ) >"$WORK/ydotool.log" 2>&1 \
      || { echo "  [error] building ydotool failed -- last lines of the build log:"; tail -15 "$WORK/ydotool.log"; exit 1; }
    hash -r
    ydotool_ok && echo "  [built]   ydotool 1.0.4 -> /usr/local/bin" || { echo "  [error] ydotool still not usable"; exit 1; }
fi
# ydotoold needs /dev/uinput (group input) and a per-user service; some
# distros package neither. Add exactly what Arch's ydotool package ships.
if ! grep -qsE 'KERNEL=="uinput".*GROUP="input"' /usr/lib/udev/rules.d/*.rules /etc/udev/rules.d/*.rules; then
    echo 'KERNEL=="uinput", GROUP="input", MODE="0660", OPTIONS+="static_node=uinput"' \
        | $SUDO tee /etc/udev/rules.d/80-g510-uinput.rules >/dev/null
    echo "  [added]   /dev/uinput access for the 'input' group"
fi
# That rule (static_node) is only applied when udev starts, i.e. at the next
# boot -- so also apply it NOW, or ydotoold can't open /dev/uinput until a
# reboot (a re-login isn't enough; found testing on Ubuntu 24.04).
if [ ! -e /dev/uinput ] || [ "$(stat -c %G /dev/uinput)" != input ]; then
    $SUDO modprobe uinput 2>/dev/null || true
    if [ -e /dev/uinput ]; then
        $SUDO chgrp input /dev/uinput && $SUDO chmod 0660 /dev/uinput && echo "  [ok]      /dev/uinput usable now (no reboot needed)"
    fi
fi
# The user service must start a 1.x ydotoold. If there's none, or the one that
# exists starts an old (0.1.x) daemon, write our own in ~/.config, which takes
# precedence over a packaged one.
unit_bin=$(systemctl --user show ydotool.service -p ExecStart --value 2>/dev/null | sed -nE 's/^\{ path=([^ ;]+).*/\1/p')
if [ "$(systemctl --user is-enabled ydotool.service 2>/dev/null)" = masked ]; then
    echo "  [warn]    ydotool.service is masked -- G-key macros won't replay until: systemctl --user unmask ydotool.service"
elif is_ydotoold_1x "$unit_bin"; then
    echo "  [ok]      ydotool user service ($unit_bin)"
elif [ "$FAMILY" = arch ] && ! ydotool_ok; then
    :   # don't override Arch's packaged unit with a daemon we couldn't verify
else
    mkdir -p ~/.config/systemd/user
    cat > ~/.config/systemd/user/ydotool.service <<EOF
[Unit]
Description=Starts ydotoold service (added by G510 install.sh)

[Service]
Type=simple
Restart=always
RestartSec=3
ExecStart=$(command -v ydotoold)
ExecReload=/usr/bin/kill -HUP \$MAINPID
KillMode=process
TimeoutSec=180

[Install]
WantedBy=default.target
EOF
    systemctl --user daemon-reload 2>/dev/null || true
    echo "  [added]   ydotool user service ($(command -v ydotoold))"
fi

echo "=== 4/8: Convert the label fonts ==="
# Both fonts ship in the repo (fonts/source-ttf/) and are converted every run
# (self-heals a corrupted .fnt -- see font_sanity.h).
convert_font() {
    # $1=source (.otf/.ttf)  $2=output (.fnt)  $3=description
    if [ ! -f "$1" ]; then
        echo "  [missing] $1 -- skipping conversion, $3 falls through to the next tier."
    elif ! command -v g15fontconvert &>/dev/null; then
        echo "  [missing] g15fontconvert (comes with libg15render, step 2) -- skipping conversion."
    else
        echo "  Converting $1 -> $2"
        g15fontconvert -s 8 -i "$1" -o "$2" || echo "  [warn]    converting $1 failed -- $3 falls through to the next tier."
    fi
}
convert_font "$DIR/fonts/source-ttf/FONT.otf" "$DIR/fonts/lcd-label-8.fnt" "the primary label font"
convert_font "$DIR/fonts/source-ttf/FALLBACK.ttf" "$DIR/fonts/lcd-label-8-fallback.fnt" "the fallback label font"

echo "=== 5/8: Compile the C programs ==="
# Same FreeType/TTF flags libg15render.so itself was built with -- without them
# g15canvas's struct layout mismatches and corrupts stack memory (bit us once,
# caught via AddressSanitizer). -DPROJECT_DIR bakes in THIS checkout's path.
# (Arch build tools were checked before step 2.)
# scripts/g15-flags.sh adds the /usr/local paths only where that library is the
# one the loader uses (the same flags rebuild.sh and the tests use).
gcc $(scripts/g15-flags.sh) $(pkg-config --cflags freetype2) -DPROJECT_DIR="\"$DIR\"" src/g510_lcd_stats.c -o src/g510_lcd_stats \
    -lg15render $(pkg-config --libs freetype2) -lm -ldl
gcc src/g510_lcd_buttons.c -o src/g510_lcd_buttons
echo "  [ok]      src/g510_lcd_stats, src/g510_lcd_buttons"

echo "=== 6/8: udev rules + hwdb (needs sudo) ==="
# The backlight-restore-on-hotplug script lives at a fixed system path udev can
# always find (same path the PKGBUILD uses), whatever the checkout location.
$SUDO install -Dm755 scripts/restore-backlight.sh /usr/lib/g510-lcd/restore-backlight.sh
$SUDO install -Dm644 udev/99-g510-lcd.rules /etc/udev/rules.d/99-g510-lcd.rules
$SUDO mkdir -p /etc/udev/hwdb.d
$SUDO cp udev/91-g510-stop-to-playpause.hwdb /etc/udev/hwdb.d/91-g510-stop-to-playpause.hwdb
$SUDO udevadm control --reload-rules
$SUDO systemd-hwdb update

# The LCD (hidraw), the G-keys (/dev/input/event*), the backlight LEDs and
# /dev/uinput (macros) are all group "input" -- a user outside it gets
# "Permission denied" on every one. Many distros don't add desktop users to it.
NEED_RELOGIN=0
if id -nG "$USER" | tr ' ' '\n' | grep -qx input; then
    echo "  [ok]      $USER is in the 'input' group"
else
    echo "  [adding]  $USER to the 'input' group (log out and back in afterwards)"
    echo "            Good to know: this group lets programs you run read keyboards and send"
    echo "            keystrokes -- that's what the G-keys and macros need."
    $SUDO usermod -aG input "$USER"
    NEED_RELOGIN=1
fi

# Seed a default backlight colour so the replug below shows something. Only if
# absent -- never clobbers a colour you picked in the GUI.
mkdir -p "$HOME/.local/share/g510-lcd"
if [ ! -f "$HOME/.local/share/g510-lcd/set-backlight-color.sh" ]; then
    cp scripts/set-backlight-color.sh "$HOME/.local/share/g510-lcd/set-backlight-color.sh"
    chmod 755 "$HOME/.local/share/g510-lcd/set-backlight-color.sh"
    echo "  Seeded a default backlight color -- change it anytime from the GUI."
fi

if [ "$YES" = 1 ] || [ ! -t 0 ]; then
    echo "NOTE: unplug and replug the keyboard once so the new rules apply."
else
    echo "NOTE: unplug and replug the keyboard now so these fully apply."
    read -r -p "Press Enter once you've replugged the keyboard..."
fi

echo "=== 7/8: systemd --user services ==="
mkdir -p ~/.config/systemd/user
# Fill in this checkout's path. Not sed: '&' and '|' in a path would be taken as
# sed syntax. The units quote the path (spaces are fine) and '%' is doubled
# (systemd would read it as a specifier).
render_unit() {
    local content esc
    content=$(<"$1")
    esc=${DIR//%/%%}
    shopt -u patsub_replacement 2>/dev/null || true   # bash 5.2+: keep '&' literal
    printf '%s\n' "${content//__PROJECT_DIR__/$esc}" > "$2"
}
for svc in g510-lcd-stats g510-lcd-buttons g510-macro-daemon g510-backlight-restore; do
    render_unit "services/$svc.service" ~/.config/systemd/user/"$svc.service"
done
systemctl --user daemon-reload
systemctl --user enable --now ydotool.service || true   # starts properly once you're in 'input' (re-login)
systemctl --user enable g510-lcd-stats.service g510-lcd-buttons.service g510-macro-daemon.service
# restart (not start): on a re-run the programs were just rebuilt and the units
# rewritten, so running ones must be restarted; stopped ones simply start.
systemctl --user restart g510-lcd-stats.service g510-lcd-buttons.service g510-macro-daemon.service
systemctl --user enable g510-backlight-restore.service
systemctl --user start g510-backlight-restore.service || true   # colour now; again at every login

echo "=== 8/8: Desktop shortcuts ==="
DESKTOP_DIR=$(xdg-user-dir DESKTOP 2>/dev/null || true)
{ [ -z "$DESKTOP_DIR" ] || [ "$DESKTOP_DIR" = "$HOME" ]; } && DESKTOP_DIR="$HOME/Desktop"
mkdir -p "$DESKTOP_DIR" "$HOME/.local/share/applications"

# Open a script in whatever terminal this desktop has (KDE, GNOME, Xfce, ...).
term_cmd() {
    local t
    for t in konsole gnome-terminal kgx ptyxis xfce4-terminal mate-terminal tilix kitty alacritty x-terminal-emulator xterm; do
        command -v "$t" >/dev/null || continue
        case "$t" in
            konsole)                    echo "konsole --noclose -e \"$1\"" ;;
            gnome-terminal|kgx|ptyxis)  echo "$t -- \"$1\"" ;;
            xfce4-terminal|mate-terminal) echo "$t -x \"$1\"" ;;
            tilix|alacritty|x-terminal-emulator|xterm) echo "$t -e \"$1\"" ;;
            kitty)                      echo "kitty \"$1\"" ;;
        esac
        return
    done
    echo "xdg-open \"$1\""
}

write_shortcut() {
    local entry="[Desktop Entry]
Type=Application
Name=$1
Comment=$3
Exec=$4
Icon=$5
Terminal=false
Categories=Utility;"
    echo "$entry" > "$DESKTOP_DIR/$1.desktop"
    chmod +x "$DESKTOP_DIR/$1.desktop"
    command -v gio &>/dev/null && gio set "$DESKTOP_DIR/$1.desktop" metadata::trusted true 2>/dev/null || true

    echo "$entry" > "$HOME/.local/share/applications/$2.desktop"
    chmod +x "$HOME/.local/share/applications/$2.desktop"
}

write_shortcut "G510 LCD - App" "g510-lcd-app" \
    "G510 keyboard LCD control app (backlight, service control)" \
    "python3 \"$DIR/src/g510_app.py\"" "preferences-desktop-color"

write_shortcut "G510 LCD - Rebuild" "g510-lcd-rebuild" \
    "Recompile the keyboard LCD program after editing and restart it" \
    "$(term_cmd "$DIR/scripts/rebuild.sh")" "utilities-terminal"

write_shortcut "G510 LCD - Start" "g510-lcd-start" \
    "One-click start for the keyboard LCD screen and button services" \
    "\"$DIR/scripts/start.sh\"" "media-playback-start"

write_shortcut "G510 LCD - View Logs" "g510-lcd-view-logs" \
    "Watch live logs from the keyboard LCD screen and button services" \
    "$(term_cmd "$DIR/scripts/view-logs.sh")" "utilities-terminal"

write_shortcut "G510 LCD - Project Folder" "g510-lcd-project-folder" \
    "Open the keyboard LCD project source and files" \
    "xdg-open \"$DIR\"" "folder"

echo ""
echo "=== Done ==="
echo "One thing that can't be scripted: Brave's 'Plasma Integration' extension"
echo "media-control feature needs to be manually disabled if you use"
echo "Brave + media keys together (see G510_README.md, MEDIA KEYS FIX #2)."
echo ""
if [ "$NEED_RELOGIN" = 1 ]; then
    echo "*** You were just added to the 'input' group: log out and back in"
    echo "*** (or reboot) before the LCD, G-keys, macros and backlight will work."
    echo ""
fi
echo "Sensors this PC's LCD will show (N/A = not found -- see README 'Sensors'):"
"$DIR/src/g510_lcd_stats" --sensors | sed -n '3,/^$/p'
echo ""
systemctl --user status g510-lcd-stats.service g510-lcd-buttons.service g510-macro-daemon.service --no-pager -l | head -30
