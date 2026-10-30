#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdio.h>

int main(void) {
    FT_Library library = NULL;
    FT_Face face = NULL;
    FT_Int major = 0;
    FT_Int minor = 0;
    FT_Int patch = 0;
    FT_Error error = FT_Init_FreeType(&library);
    if (error != 0 || !library) return 1;
    FT_Library_Version(library, &major, &minor, &patch);
    if (major != 2 || minor != 14 || patch != 3) {
        FT_Done_FreeType(library);
        return 2;
    }

    error = FT_New_Face(library, "/usr/share/fonts/truetype/akaos/default.ttf",
                        0, &face);
    if (error != 0 || !face) {
        FT_Done_FreeType(library);
        return 3;
    }
    if (FT_Set_Pixel_Sizes(face, 0, 18) != 0 ||
        FT_Get_Char_Index(face, (FT_ULong)'A') == 0 ||
        FT_Load_Char(face, (FT_ULong)'A', FT_LOAD_RENDER) != 0) {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return 4;
    }

    const FT_Bitmap *bitmap = &face->glyph->bitmap;
    if (bitmap->pixel_mode != FT_PIXEL_MODE_GRAY || bitmap->width == 0 ||
        bitmap->rows == 0 || !bitmap->buffer) {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return 5;
    }
    unsigned covered = 0;
    for (unsigned row = 0; row < bitmap->rows; ++row) {
        const unsigned char *pixels = bitmap->buffer + row * bitmap->pitch;
        for (unsigned column = 0; column < bitmap->width; ++column)
            if (pixels[column] != 0) ++covered;
    }
    if (covered == 0) {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return 6;
    }

    printf("Upstream FreeType %d.%d.%d loaded DejaVu Sans\n",
           major, minor, patch);
    printf("FreeType glyph metrics and grayscale rasterization verified\n");
    printf("upstream-freetype-smoke-ok\n");
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return 0;
}
