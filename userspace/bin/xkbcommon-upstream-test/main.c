#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>

static const char keymap_text[] =
    "xkb_keymap {\n"
    "  xkb_keycodes { minimum = 8; maximum = 255; <AC01> = 38; };\n"
    "  xkb_types {\n"
    "    type \"TWO_LEVEL\" {\n"
    "      modifiers = Shift;\n"
    "      map[None] = Level1;\n"
    "      map[Shift] = Level2;\n"
    "      level_name[Level1] = \"Base\";\n"
    "      level_name[Level2] = \"Shift\";\n"
    "    };\n"
    "  };\n"
    "  xkb_compatibility {\n"
    "    virtual_modifiers NumLock;\n"
    "    interpret.useModMapMods = AnyLevel;\n"
    "  };\n"
    "  xkb_symbols { key <AC01> { type = \"TWO_LEVEL\", [ a, A ] }; };\n"
    "};\n";

int main(void) {
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap = NULL;
    struct xkb_state *state = NULL;
    char text[8];
    xkb_keycode_t keycode;
    xkb_mod_index_t shift_index;
    int result = 1;

    if (!context) goto done;
    keymap = xkb_keymap_new_from_string(
        context, keymap_text, XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) goto done;
    keycode = xkb_keymap_key_by_name(keymap, "AC01");
    if (keycode == XKB_KEYCODE_INVALID) goto done;
    state = xkb_state_new(keymap);
    if (!state) goto done;

    if (xkb_state_key_get_utf8(state, keycode, text, sizeof(text)) != 1 ||
        strcmp(text, "a") != 0)
        goto done;

    shift_index = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
    if (shift_index == XKB_MOD_INVALID || shift_index >= 32U) goto done;
    (void)xkb_state_update_mask(state,
                                (xkb_mod_mask_t)1U << shift_index,
                                0U, 0U, 0U, 0U, 0U);
    if (xkb_state_key_get_utf8(state, keycode, text, sizeof(text)) != 1 ||
        strcmp(text, "A") != 0)
        goto done;

    puts("Upstream xkbcommon parsed a keymap and translated a/A");
    puts("Upstream XKB keymap compilation and modifier state verified");
    puts("upstream-xkbcommon-smoke-ok");
    result = 0;

done:
    if (state) xkb_state_unref(state);
    if (keymap) xkb_keymap_unref(keymap);
    if (context) xkb_context_unref(context);
    if (result != 0)
        fputs("Upstream xkbcommon keymap/state smoke failed\n", stderr);
    return result;
}
