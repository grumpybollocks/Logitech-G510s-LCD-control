#ifndef G510_FONT_SANITY_H
#define G510_FONT_SANITY_H
/* Requires TTF_SUPPORT + <libg15render.h> already included by the
   caller (same reason g510_lcd_stats.c defines TTF_SUPPORT before
   including libg15render.h itself). */

/* Every character that actually appears in a SENSORS[] label string in
   g510_lcd_stats.c (grep-derived from SENSORS[]'s .label fields --
   keep in sync by hand if that table changes). Checked instead of the
   full alphabet so a font that legitimately never converted an unused
   glyph doesn't trip a false positive. */
static const char LABEL_SANITY_CHARS[] = "ACDEGHIKLMNOPRSTUVWXZ2";

/* Returns 1 if `font` looks like a real, usable label font, 0 if it
   looks corrupted enough that it shouldn't be trusted.

   Checks glyph METRICS via the real g15font/g15glyph struct fields
   (plain public structs, verified directly against the installed
   /usr/include/libg15render.h -- no guessed API) -- catches a
   missing/inactive glyph, a zero-width glyph, a NULL glyph buffer, or
   implausible font-level metrics.

   What this can NOT catch: a glyph whose BITMAP DATA is scrambled but
   whose width/active/height still look plausible -- libg15render
   exposes no per-glyph checksum or rendering-comparison hook, only
   these metrics. A full fix would need an actual visual diff against
   known-good renders; out of scope for this lightweight runtime
   check. This is exactly the failure class documented in
   G510_README.md's "Gotcha We Actually Hit" section (a corrupted 'S'
   glyph once rendered as something closer to '6') -- this check
   guards against a font that's more badly broken than that (missing
   glyphs, implausible metrics), not necessarily that exact subtle
   case, which is why a second, genuinely-clean fallback font
   (FALLBACK.ttf) exists as well, not just this check alone. */
static int label_font_is_sane(g15font *font) {
    if (!font) return 0;
    if (font->font_height == 0 || font->font_height > 32) return 0;
    if (font->numchars == 0) return 0;
    for (size_t i = 0; i < sizeof(LABEL_SANITY_CHARS) - 1; i++) {
        unsigned char idx = (unsigned char)LABEL_SANITY_CHARS[i];
        if (!font->active[idx]) return 0;
        g15glyph *g = &font->glyph[idx];
        if (!g->buffer) return 0;
        if (g->width == 0) return 0;
        if (g->width > font->font_height * 4) return 0;
    }
    return 1;
}
#endif
