#!/bin/bash
# Extra compiler flags for building against libg15render -- ONE place, used by
# install.sh, scripts/rebuild.sh and tests/run-tests.sh.
#
# Prints "-I/usr/local/include -L/usr/local/lib" only when the libg15render the
# runtime loader will actually use IS the one in /usr/local (install.sh builds it
# there on non-Arch distros). Otherwise prints nothing, so on Arch a stray
# hand-built copy in /usr/local can't be compiled against while /usr/lib's is
# loaded at runtime -- that header/library mismatch is the struct-layout memory
# corruption described in G510_README.md.
#
#   scripts/g15-flags.sh           -> the flags (possibly empty)
#   scripts/g15-flags.sh --lib     -> path of the libg15render the loader uses
#   scripts/g15-flags.sh --check   -> exit 1 (with a message) unless that library
#                                     has TrueType support (g510_lcd_stats needs it)
lib=$(PATH="$PATH:/sbin:/usr/sbin" ldconfig -p 2>/dev/null | awk '/libg15render\.so\.1 /{print $NF; exit}')
case "${1:-}" in
    --lib) echo "$lib" ;;
    --check)
        if [ -z "$lib" ]; then echo "libg15render.so.1 not found by the loader" >&2; exit 1; fi
        if ! nm -D "$lib" 2>/dev/null | grep -q ' T g15r_ttfLoad$'; then
            echo "$lib was built WITHOUT TrueType support -- g510_lcd_stats needs it (rebuild it with --enable-ttf)" >&2
            exit 1
        fi ;;
    *) case "$lib" in /usr/local/*) echo "-I/usr/local/include -L/usr/local/lib" ;; esac ;;
esac
exit 0
