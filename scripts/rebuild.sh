#!/bin/bash
# Rebuilds both G510 LCD programs and restarts their services.
# Run this after editing g510_lcd_stats.c or g510_lcd_buttons.c.
set -e
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_DIR/src"

echo "Converting label fonts..."
# Same automatic conversion install.sh does -- keeps the .fnt files in
# sync with fonts/source-ttf/*.{otf,ttf} on every rebuild, and self-heals
# if a corrupted .fnt ever triggered the runtime sanity-check fallback
# in g510_lcd_stats.c (see font_sanity.h).
convert_font() {
    if [ ! -f "$1" ]; then
        echo "  [missing] $1 -- skipping conversion."
    elif ! command -v g15fontconvert &>/dev/null; then
        echo "  [missing] g15fontconvert -- skipping conversion."
    else
        g15fontconvert -s 8 -i "$1" -o "$2"
    fi
}
convert_font "$PROJECT_DIR/fonts/source-ttf/FONT.otf" "$PROJECT_DIR/fonts/lcd-label-8.fnt"
convert_font "$PROJECT_DIR/fonts/source-ttf/FALLBACK.ttf" "$PROJECT_DIR/fonts/lcd-label-8-fallback.fnt"

echo "Building g510_lcd_stats..."
# Needs the same FreeType/TTF flags libg15render.so was built with, or
# g15canvas's struct layout mismatches between this program and the
# library and corrupts stack memory (bit us once already -- see README).
# -DPROJECT_DIR bakes in this checkout's own path (font/config file
# locations) without any file in the repo hardcoding a username.
gcc $(pkg-config --cflags freetype2) -DPROJECT_DIR="\"$PROJECT_DIR\"" g510_lcd_stats.c -o g510_lcd_stats -lg15render $(pkg-config --libs freetype2) -lm

echo "Building g510_lcd_buttons..."
gcc g510_lcd_buttons.c -o g510_lcd_buttons

echo "Restarting services..."
systemctl --user restart g510-lcd-stats.service
systemctl --user restart g510-lcd-buttons.service

echo ""
echo "Done. Status:"
systemctl --user status g510-lcd-stats.service --no-pager -l | head -5
systemctl --user status g510-lcd-buttons.service --no-pager -l | head -5

read -p "Press Enter to close..."
