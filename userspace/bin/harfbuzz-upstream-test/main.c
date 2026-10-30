#include <ft2build.h>
#include FT_FREETYPE_H
#include <math.h>
#include <hb.h>
#include <hb-ft.h>
#include <hb-subset.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int shape_text(hb_font_t *font, const char *text, int require_rtl,
                      unsigned *glyph_count_out) {
    hb_buffer_t *buffer = hb_buffer_create();
    if (!buffer || !hb_buffer_allocation_successful(buffer)) {
        if (buffer) hb_buffer_destroy(buffer);
        return 1;
    }
    hb_buffer_add_utf8(buffer, text, -1, 0, -1);
    hb_buffer_guess_segment_properties(buffer);
    if (require_rtl && hb_buffer_get_direction(buffer) != HB_DIRECTION_RTL) {
        hb_buffer_destroy(buffer);
        return 2;
    }
    hb_shape(font, buffer, NULL, 0);
    unsigned count = 0;
    hb_glyph_info_t *glyphs = hb_buffer_get_glyph_infos(buffer, &count);
    hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer,
                                                                    NULL);
    if (!glyphs || !positions || count == 0) {
        hb_buffer_destroy(buffer);
        return 3;
    }
    int64_t advance = 0;
    for (unsigned i = 0; i < count; ++i) advance += positions[i].x_advance;
    if (advance <= 0) {
        hb_buffer_destroy(buffer);
        return 4;
    }
    if (glyph_count_out) *glyph_count_out = count;
    hb_buffer_destroy(buffer);
    return 0;
}

int main(void) {
    if (round(0.5) != 1.0 || round(-0.5) != -1.0 ||
        roundf(0.5f) != 1.0f || roundf(-0.5f) != -1.0f ||
        trunc(1.75) != 1.0 || trunc(-1.75) != -1.0 ||
        truncf(1.75f) != 1.0f || truncf(-1.75f) != -1.0f ||
        !signbit(trunc(-0.25)) || roundl(-2.5L) != -3.0L ||
        truncl(-2.75L) != -2.0L) {
        puts("akaOS POSIX round/trunc math verification failed");
        return 1;
    }
    puts("akaOS POSIX round and trunc double/float/long-double verified");

    FT_Library library = NULL;
    FT_Face face = NULL;
    if (FT_Init_FreeType(&library) != 0 ||
        FT_New_Face(library, "/usr/share/fonts/truetype/akaos/default.ttf",
                    0, &face) != 0 ||
        FT_Set_Pixel_Sizes(face, 0, 18) != 0) {
        if (face) FT_Done_Face(face);
        if (library) FT_Done_FreeType(library);
        return 1;
    }

    hb_font_t *font = hb_ft_font_create_referenced(face);
    if (!font || !hb_font_get_face(font)) {
        if (font) hb_font_destroy(font);
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return 2;
    }

    hb_font_set_scale(font, 18 * 64, 18 * 64);
    unsigned latin_glyphs = 0;
    unsigned arabic_glyphs = 0;
    if (shape_text(font, "office", 0, &latin_glyphs) != 0 ||
        latin_glyphs >= 6U ||
        shape_text(font, "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85", 1,
                   &arabic_glyphs) != 0 || arabic_glyphs >= 4U) {
        hb_font_destroy(font);
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return 3;
    }

    hb_face_t *source_face = hb_font_get_face(font);
    hb_subset_input_t *subset_input = hb_subset_input_create_or_fail();
    if (!source_face || !subset_input) {
        if (subset_input) hb_subset_input_destroy(subset_input);
        hb_font_destroy(font);
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return 4;
    }
    hb_set_t *subset_unicodes = hb_subset_input_unicode_set(subset_input);
    hb_set_add(subset_unicodes, 'A');
    hb_set_add(subset_unicodes, 'B');
    hb_set_add(subset_unicodes, ' ');
    hb_face_t *subset_face = hb_subset_or_fail(source_face, subset_input);
    hb_subset_input_destroy(subset_input);
    if (!subset_face || hb_face_get_glyph_count(subset_face) < 2U) {
        if (subset_face) hb_face_destroy(subset_face);
        hb_font_destroy(font);
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return 5;
    }
    hb_blob_t *subset_blob = hb_face_reference_blob(subset_face);
    unsigned subset_length = 0;
    const char *subset_data = hb_blob_get_data(subset_blob, &subset_length);
    if (!subset_data || subset_length < 12U) {
        hb_blob_destroy(subset_blob);
        hb_face_destroy(subset_face);
        hb_font_destroy(font);
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return 6;
    }

    printf("Upstream HarfBuzz 14.5 shaped Latin ligature text (%u glyphs)\n",
           latin_glyphs);
    printf("Upstream HarfBuzz shaped Arabic RTL text (%u glyphs)\n",
           arabic_glyphs);
    printf("Upstream HarfBuzz subset generated a valid font (%u bytes)\n",
           subset_length);
    printf("upstream-harfbuzz-smoke-ok\n");

    hb_blob_destroy(subset_blob);
    hb_face_destroy(subset_face);
    hb_font_destroy(font);
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return 0;
}
