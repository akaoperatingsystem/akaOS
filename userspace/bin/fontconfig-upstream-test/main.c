#include <fontconfig/fontconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    if (setenv("FONTCONFIG_FILE", "/etc/fonts/fonts.conf", 1) != 0 ||
        setenv("FONTCONFIG_PATH", "/etc/fonts", 1) != 0) {
        puts("Fontconfig environment setup failed");
        return 1;
    }

    puts("Fontconfig smoke: parsing configuration and scanning fonts");
    fflush(stdout);
    if (!FcInit()) {
        puts("FcInit failed");
        return 2;
    }
    FcConfig *config = FcConfigGetCurrent();
    fflush(stdout);
    if (!config) {
        puts("FcInitLoadConfigAndFonts failed");
        return 2;
    }
    puts("Fontconfig smoke: config loaded");
    fflush(stdout);

    FcPattern *pattern = FcNameParse((const FcChar8 *)"sans-serif");
    if (!pattern) {
        FcConfigDestroy(config);
        puts("Fontconfig FcNameParse failed");
        return 3;
    }
    puts("Fontconfig smoke: generic pattern parsed");
    fflush(stdout);
    if (!FcConfigSubstitute(config, pattern, FcMatchPattern)) {
        if (pattern) FcPatternDestroy(pattern);
        FcConfigDestroy(config);
        puts("Fontconfig could not substitute a generic sans pattern");
        return 3;
    }
    puts("Fontconfig smoke: configuration substitution complete");
    FcDefaultSubstitute(pattern);
    puts("Fontconfig smoke: configuration defaults applied; matching font");
    fflush(stdout);

    FcResult match_result = FcResultNoMatch;
    FcPattern *match = FcFontMatch(config, pattern, &match_result);
    FcPatternDestroy(pattern);
    if (!match || match_result != FcResultMatch) {
        if (match) FcPatternDestroy(match);
        FcConfigDestroy(config);
        printf("Fontconfig sans-serif match failed (result=%d)\n",
               (int)match_result);
        return 4;
    }

    FcChar8 *family = NULL;
    FcChar8 *file = NULL;
    FcResult family_result = FcPatternGetString(match, FC_FAMILY, 0, &family);
    FcResult file_result = FcPatternGetString(match, FC_FILE, 0, &file);
    int valid = family_result == FcResultMatch &&
                file_result == FcResultMatch && family && file &&
                strstr((const char *)family, "DejaVu Sans") != NULL &&
                strstr((const char *)file, "/default.ttf") != NULL;

    if (valid) {
        printf("Upstream Fontconfig %d matched %s at %s\n",
               FcGetVersion(),
               (const char *)family, (const char *)file);
        puts("Fontconfig configuration, Expat XML parsing, and font discovery verified");
        puts("upstream-fontconfig-smoke-ok");
    } else {
        printf("Fontconfig returned family=%s file=%s\n",
               family ? (const char *)family : "<missing>",
               file ? (const char *)file : "<missing>");
    }

    FcPatternDestroy(match);
    FcConfigDestroy(config);
    return valid ? 0 : 5;
}
