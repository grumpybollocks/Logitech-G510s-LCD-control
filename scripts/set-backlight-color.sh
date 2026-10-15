#!/bin/bash
# Applies the chosen keyboard backlight color. Run automatically by
# 99-g510-lcd.rules whenever the LED device appears (boot or replug).
# Auto-updated by g510_app.py every time you click Apply or Set as Default.
echo 255 > /sys/class/leds/g15::kbd_backlight/brightness
echo "0 255 0" > /sys/class/leds/g15::kbd_backlight/multi_intensity
