/* Small versioned provider used to exercise DT_VERSYM/DT_VERDEF lookup. */
int akaos_versioned_answer_v1(void) {
    return 11;
}

int akaos_versioned_answer_v2(void) {
    return 22;
}

__asm__(".symver akaos_versioned_answer_v1,akaos_versioned_answer@AKAOS_1.0");
__asm__(".symver akaos_versioned_answer_v2,akaos_versioned_answer@@AKAOS_2.0");
