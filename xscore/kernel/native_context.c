#include <xscore/serial.h>
#include <xscore/user.h>
#include <xscore/vmm.h>
#include <stdint.h>

/*
 * Native ARM/RISC-V ABI probe. This stays separate from the x86-only
 * comprehensive user self-test because the native ports intentionally expose
 * a smaller VMM API while their direct-kernel runners are brought up.
 */
int xscore_user_native_context_self_test(void) {
    uint64_t kernel_root = xscore_vmm_current_root();
    uint64_t first_root = xscore_vmm_address_space_create();
    uint64_t second_root = xscore_vmm_address_space_create();
    if (!kernel_root || !first_root || !second_root ||
        first_root == second_root) {
        if (first_root) xscore_vmm_address_space_destroy(first_root);
        if (second_root) xscore_vmm_address_space_destroy(second_root);
        xscore_serial_write(
            "[FAIL] Native context ABI probe could not create roots\n");
        return 0;
    }

    /*
     * Load two private roots and restore the kernel root. This validates the
     * architecture page-table activation primitive without migrating a user
     * task to a secondary CPU.
     */
    int roots_ok =
        xscore_vmm_activate(first_root) &&
        xscore_vmm_current_root() == first_root &&
        xscore_vmm_activate(second_root) &&
        xscore_vmm_current_root() == second_root &&
        xscore_vmm_activate(kernel_root) &&
        xscore_vmm_current_root() == kernel_root;
    if (!roots_ok) {
        xscore_serial_write(
            "[FAIL] Native address-space activation ABI probe failed\n");
        xscore_vmm_address_space_destroy(first_root);
        xscore_vmm_address_space_destroy(second_root);
        return 0;
    }
    xscore_vmm_address_space_destroy(first_root);
    xscore_vmm_address_space_destroy(second_root);
    xscore_serial_write(
        "[PASS] Native address-space activation ABI verified\n");
    return 1;
}
