#include <cairo.h>
#if defined(AKAOS_CAIRO_WITH_GOBJECT)
#include <cairo-gobject.h>
#endif
#include <stdint.h>
#include <stdio.h>
#if defined(AKAOS_CAIRO_WITH_FREETYPE)
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cairo-ft.h>
#endif

int main(void) {
    if (cairo_version() != 11806) return 1;
#if defined(AKAOS_CAIRO_WITH_GOBJECT)
    if (!G_TYPE_IS_BOXED(CAIRO_GOBJECT_TYPE_MATRIX) ||
        !G_TYPE_IS_BOXED(CAIRO_GOBJECT_TYPE_RECTANGLE) ||
        !G_TYPE_IS_ENUM(CAIRO_GOBJECT_TYPE_OPERATOR))
        return 10;
#endif

    cairo_surface_t *surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64);
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
        return 2;

    cairo_t *context = cairo_create(surface);
    if (!context || cairo_status(context) != CAIRO_STATUS_SUCCESS) {
        if (context) cairo_destroy(context);
        cairo_surface_destroy(surface);
        return 3;
    }

    cairo_set_operator(context, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgb(context, 1.0, 0.0, 0.0);
    cairo_paint(context);
    cairo_set_operator(context, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(context, 0.0, 0.0, 1.0, 0.5);
    cairo_arc(context, 32.0, 32.0, 10.0, 0.0,
              6.283185307179586476925286766559005768);
    cairo_fill(context);

    cairo_pattern_t *gradient = cairo_pattern_create_linear(0.0, 0.0,
                                                            64.0, 0.0);
    if (!gradient || cairo_pattern_status(gradient) != CAIRO_STATUS_SUCCESS) {
        if (gradient) cairo_pattern_destroy(gradient);
        cairo_destroy(context);
        cairo_surface_destroy(surface);
        return 4;
    }
    cairo_pattern_add_color_stop_rgba(gradient, 0.0, 0.0, 1.0, 0.0, 1.0);
    cairo_pattern_add_color_stop_rgba(gradient, 1.0, 0.0, 0.0, 1.0, 1.0);
    cairo_rectangle(context, 0.0, 60.0, 64.0, 4.0);
    cairo_set_source(context, gradient);
    cairo_fill(context);

#if defined(AKAOS_CAIRO_WITH_FREETYPE)
    FT_Library ft_library = NULL;
    FT_Face ft_face = NULL;
    if (FT_Init_FreeType(&ft_library) != 0 ||
        FT_New_Face(ft_library, "/usr/share/fonts/truetype/akaos/default.ttf",
                    0, &ft_face) != 0 ||
        FT_Set_Pixel_Sizes(ft_face, 0, 16) != 0) {
        if (ft_face) FT_Done_Face(ft_face);
        if (ft_library) FT_Done_FreeType(ft_library);
        cairo_pattern_destroy(gradient);
        cairo_destroy(context);
        cairo_surface_destroy(surface);
        return 7;
    }
    cairo_font_face_t *font_face = cairo_ft_font_face_create_for_ft_face(
        ft_face, 0);
    if (!font_face ||
        cairo_font_face_status(font_face) != CAIRO_STATUS_SUCCESS) {
        if (font_face) cairo_font_face_destroy(font_face);
        FT_Done_Face(ft_face);
        FT_Done_FreeType(ft_library);
        cairo_pattern_destroy(gradient);
        cairo_destroy(context);
        cairo_surface_destroy(surface);
        return 8;
    }
    cairo_set_font_face(context, font_face);
    cairo_set_font_size(context, 16.0);
    cairo_set_source_rgb(context, 1.0, 1.0, 1.0);
    cairo_move_to(context, 2.0, 19.0);
    cairo_show_text(context, "akaOS");
    cairo_font_face_destroy(font_face);
    FT_Done_Face(ft_face);
    FT_Done_FreeType(ft_library);
#endif
    cairo_surface_flush(surface);

    int stride = cairo_image_surface_get_stride(surface);
    unsigned char *data = cairo_image_surface_get_data(surface);
    if (cairo_status(context) != CAIRO_STATUS_SUCCESS || stride < 64 * 4 ||
        !data)
        return 5;
    uint32_t untouched = *(uint32_t *)(void *)(data + 8 * stride + 8 * 4);
    uint32_t blended = *(uint32_t *)(void *)(data + 32 * stride + 32 * 4);
    uint32_t gradient_pixel =
        *(uint32_t *)(void *)(data + 61 * stride + 32 * 4);
    if (untouched != UINT32_C(0xffff0000) ||
        ((blended >> 16U) & 0xffU) < 100U ||
        (blended & 0xffU) < 100U || ((blended >> 8U) & 0xffU) != 0U ||
        (gradient_pixel >> 24U) != 0xffU)
        return 6;

#if defined(AKAOS_CAIRO_WITH_FREETYPE)
    unsigned glyph_pixels = 0;
    for (int y = 0; y < 22; ++y) {
        for (int x = 0; x < 64; ++x) {
            uint32_t pixel = *(uint32_t *)(void *)(data + y * stride + x * 4);
            if (((pixel >> 16U) & 0xffU) > 200U &&
                ((pixel >> 8U) & 0xffU) > 200U &&
                (pixel & 0xffU) > 200U)
                ++glyph_pixels;
        }
    }
    if (glyph_pixels < 8U) return 9;
#endif

    printf("Upstream Cairo 1.18.6 image surface and ARGB32 buffer verified\n");
    printf("Upstream Cairo vector, alpha, gradient, and Pixman path verified\n");
#if defined(AKAOS_CAIRO_WITH_FREETYPE)
    printf("Upstream FreeType glyph loaded and Cairo text rasterized\n");
#endif
#if defined(AKAOS_CAIRO_WITH_GOBJECT)
    printf("Upstream Cairo GObject boxed and enum types registered\n");
#endif
    printf("upstream-cairo-smoke-ok\n");

    cairo_pattern_destroy(gradient);
    cairo_destroy(context);
    cairo_surface_destroy(surface);
    return 0;
}
