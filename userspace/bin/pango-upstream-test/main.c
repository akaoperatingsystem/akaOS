#include <cairo.h>
#include <hb.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    FILE *lock_probe = tmpfile();
    if (!lock_probe) {
        puts("Pango smoke could not create a stdio lock probe");
        return 1;
    }
    flockfile(lock_probe);
    flockfile(lock_probe);
    int lock_write_ok = fputs("stdio-lock-ok", lock_probe) >= 0;
    rewind(lock_probe);
    int lock_read_ok = getc_unlocked(lock_probe) == 's';
    funlockfile(lock_probe);
    funlockfile(lock_probe);
    if (fclose(lock_probe) != 0 || !lock_write_ok || !lock_read_ok) {
        puts("POSIX recursive stream locking/unlocked input failed");
        return 1;
    }

    puts("Pango probe: creating Cairo font map");
    fflush(stdout);
    PangoFontMap *font_map = pango_cairo_font_map_new();
    if (!font_map) {
        puts("Pango Cairo font map creation failed");
        return 2;
    }
    puts("Pango probe: Cairo font map created");
    fflush(stdout);
    pango_cairo_font_map_set_resolution(PANGO_CAIRO_FONT_MAP(font_map), 96.0);

    puts("Pango probe: creating context");
    fflush(stdout);
    PangoContext *context = pango_font_map_create_context(font_map);
    if (!context) {
        g_object_unref(font_map);
        puts("Pango context creation failed");
        return 3;
    }
    puts("Pango probe: context created");
    fflush(stdout);
    PangoFontDescription *description =
        pango_font_description_from_string("DejaVu Sans 18");
    if (!description) {
        g_object_unref(context);
        g_object_unref(font_map);
        puts("Pango font description creation failed");
        return 3;
    }
    puts("Pango probe: loading DejaVu Sans");
    fflush(stdout);
    PangoFont *font = pango_context_load_font(context, description);
    if (!font) {
        pango_font_description_free(description);
        g_object_unref(context);
        g_object_unref(font_map);
        puts("Pango could not load DejaVu Sans through Fontconfig/FreeType");
        return 3;
    }
    puts("Pango probe: font loaded");
    printf("Pango probe: glyph coverage A=%d Arabic=%d digit=%d\n",
           pango_font_has_char(font, 'A'),
           pango_font_has_char(font, 0x0627),
           pango_font_has_char(font, '1'));
    hb_font_t *pango_hb_font = pango_font_get_hb_font(font);
    hb_codepoint_t pango_glyph = 0U;
    int pango_hb_has_a = pango_hb_font &&
        hb_font_get_nominal_glyph(pango_hb_font, 'A', &pango_glyph);
    printf("Pango probe: its HarfBuzz font has A=%d glyph=%u\n",
           pango_hb_has_a, (unsigned int)pango_glyph);
    fflush(stdout);

    PangoLayout *layout = pango_layout_new(context);
    if (!layout) {
        puts("Pango layout creation failed");
        return 4;
    }
    puts("Pango probe: layout created");
    fflush(stdout);
    pango_layout_set_font_description(layout, description);
    pango_layout_set_width(layout, 420 * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    const char *text =
        "Pango shapes ffi with Arabic \xd8\xa7\xd9\x84\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\xd8\xa9 "
        "and Latin text, then wraps paragraphs for the akaOS desktop.";
    pango_layout_set_text(layout, text, -1);
    puts("Pango probe: mixed-script text assigned");
    fflush(stdout);

    PangoRectangle ink = {0};
    PangoRectangle logical = {0};
    puts("Pango probe: measuring layout");
    fflush(stdout);
    pango_layout_get_extents(layout, &ink, &logical);
    puts("Pango probe: layout measured");
    fflush(stdout);
    int line_count = pango_layout_get_line_count(layout);
    puts("Pango probe: line count obtained");
    fflush(stdout);
    int unknown_glyphs = pango_layout_get_unknown_glyphs_count(layout);
    if (line_count < 2 || logical.width <= 0 || logical.height <= 0 ||
        unknown_glyphs != 0) {
        printf("Pango layout failed: lines=%d size=%dx%d unknown-glyphs=%d\n",
               line_count, logical.width, logical.height, unknown_glyphs);
        g_object_unref(layout);
        g_object_unref(font);
        pango_font_description_free(description);
        g_object_unref(context);
        g_object_unref(font_map);
        return 4;
    }

    cairo_surface_t *surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 512, 160);
    cairo_t *cr = cairo_create(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_move_to(cr, 8.0, 8.0);
    pango_cairo_update_layout(cr, layout);
    pango_cairo_show_layout(cr, layout);
    cairo_surface_flush(surface);

    int rendered = 0;
    unsigned char *pixels = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < 160 && !rendered; ++y) {
        for (int x = 0; x < 512; ++x) {
            if (pixels[y * stride + x * 4 + 3] != 0) {
                rendered = 1;
                break;
            }
        }
    }

    int cairo_ok = cairo_status(cr) == CAIRO_STATUS_SUCCESS &&
                   cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS;
    if (!rendered || !cairo_ok) {
        puts("Pango Cairo produced no valid rendered glyph pixels");
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        g_object_unref(layout);
        g_object_unref(font);
        pango_font_description_free(description);
        g_object_unref(context);
        g_object_unref(font_map);
        return 5;
    }

    printf("Upstream Pango %s laid out %d lines with DejaVu Sans\n",
           pango_version_string(), line_count);
    puts("Pango font loading, Arabic shaping, line wrapping, and Cairo rendering verified");
    puts("upstream-pango-smoke-ok");

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(layout);
    g_object_unref(font);
    pango_font_description_free(description);
    g_object_unref(context);
    g_object_unref(font_map);
    return 0;
}
