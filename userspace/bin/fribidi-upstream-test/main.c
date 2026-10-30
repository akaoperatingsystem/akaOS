#include <fribidi/fribidi.h>
#include <stdio.h>

int main(void) {
    const FriBidiChar logical[] = {
        'A', 'B', 'C', ' ', 0x05d0, 0x05d1, 0x05d2, ' ', '1', '2', '3'
    };
    const FriBidiChar expected_visual[] = {
        'A', 'B', 'C', ' ', '1', '2', '3', ' ', 0x05d2, 0x05d1, 0x05d0
    };
    const FriBidiStrIndex length = (FriBidiStrIndex)
        (sizeof(logical) / sizeof(logical[0]));
    FriBidiChar visual[sizeof(logical) / sizeof(logical[0])];
    FriBidiStrIndex logical_to_visual[sizeof(logical) / sizeof(logical[0])];
    FriBidiStrIndex visual_to_logical[sizeof(logical) / sizeof(logical[0])];
    FriBidiLevel embedding_levels[sizeof(logical) / sizeof(logical[0])];
    FriBidiParType base_direction = FRIBIDI_PAR_LTR;

    FriBidiLevel max_level = fribidi_log2vis(
        logical, length, &base_direction, visual,
        logical_to_visual, visual_to_logical, embedding_levels);
    if (max_level <= 1 || base_direction != FRIBIDI_PAR_LTR) {
        puts("FriBidi did not resolve the mixed paragraph direction/levels");
        return 1;
    }

    for (FriBidiStrIndex i = 0; i < length; ++i) {
        if (visual[i] != expected_visual[i]) {
            printf("FriBidi visual-order mismatch at %d (U+%04x, expected U+%04x)\n",
                   i, (unsigned int)visual[i],
                   (unsigned int)expected_visual[i]);
            return 2;
        }
        if (logical_to_visual[visual_to_logical[i]] != i ||
            visual_to_logical[logical_to_visual[i]] != i) {
            puts("FriBidi logical/visual index maps are not inverses");
            return 3;
        }
    }
    if (!(embedding_levels[4] & 1) || !(embedding_levels[5] & 1) ||
        !(embedding_levels[6] & 1) || (embedding_levels[0] & 1)) {
        puts("FriBidi returned unexpected mixed-script embedding levels");
        return 4;
    }

    printf("Upstream FriBidi %s reordered mixed-script text and index maps\n",
           fribidi_version_info);
    puts("Unicode bidi ordering, script levels, and index maps verified");
    puts("upstream-fribidi-smoke-ok");
    return 0;
}
