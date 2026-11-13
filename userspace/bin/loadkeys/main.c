
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xscore/keyboard.h>

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "-l") == 0 || strcmp(argv[1], "--list") == 0) {
        printf("Available layouts:\n");
        for (int i = 0; i < KEYMAP_MAX; ++i) {
            printf("  %d: %s\n", i, xscore_keyboard_layout_name(i));
        }
        return 0;
    }
    
    if (argc >= 2) {
        int layout = atoi(argv[1]);
        if (xscore_keyboard_set_layout(layout)) {
            printf("Layout set to: %s\n", xscore_keyboard_layout_name(layout));
            return 0;
        } else {
            fprintf(stderr, "Error: Invalid layout index.\n");
            return 1;
        }
    }
    return 0;
}
