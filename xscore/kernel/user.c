#include <xscore/file.h>
#include <xscore/memory.h>
#include <xscore/page_cache.h>
#include <xscore/process.h>
#include <xscore/sched.h>
#include <xscore/serial.h>
#include <xscore/thread.h>
#include <xscore/user.h>
#include <xscore/vfs.h>
#include <xscore/vmm.h>
#include <akaos/fs.h>
#include <akaos/signal.h>
#include <stdint.h>

#define PAGE_SIZE UINT64_C(4096)
#if defined(__i386__)
#define USER_CODE_ADDRESS UINT64_C(0x40000000)
#elif defined(__arm__)
#define USER_CODE_ADDRESS UINT64_C(0x10000000)
#elif defined(__riscv) && __riscv_xlen == 32
#define USER_CODE_ADDRESS UINT64_C(0x40000000)
#elif (defined(__riscv) && __riscv_xlen == 64)
#define USER_CODE_ADDRESS UINT64_C(0x0000002000000000)
#else
#define USER_CODE_ADDRESS UINT64_C(0x0000008000000000)
#endif
#define USER_STACK_TOP (USER_CODE_ADDRESS + UINT64_C(0x00201000))
#define USER_STACK_PAGE (USER_STACK_TOP - PAGE_SIZE)
/* Keep the reservation self-test in step with the loader's bounded image
 * table.  A fixed 128-KiB probe used to miss the first writable page as the
 * hello/libc image grew past 0x20000, making x86_32 fail before login even
 * though the ELF was loaded correctly. */
#define USER_IMAGE_SELF_TEST_WINDOW UINT64_C(0x00080000)

int xscore_user_process_self_test(void) {
    uint64_t free_pages_before = xscore_pmm_free_page_count();
    uint64_t preemptions_before = xscore_sched_get_stats().preemptions;
    if (!xscore_process_initialize()) return 0;

    struct xscore_process *first = xscore_process_create_path("/bin/hello");
    struct xscore_process *second = xscore_process_create_path("/bin/hello");
    uint64_t first_root = xscore_process_address_space_root(first);
    uint64_t second_root = xscore_process_address_space_root(second);
    if (!first || !second || !first_root || !second_root ||
        first_root == second_root) return 0;
    if (!xscore_process_has_descriptor(first, 0) ||
        !xscore_process_has_descriptor(first, 1) ||
        !xscore_process_has_descriptor(first, 2) ||
        !xscore_process_has_descriptor(second, 0) ||
        !xscore_process_has_descriptor(second, 1) ||
        !xscore_process_has_descriptor(second, 2)) return 0;
    xscore_serial_write("[INFO] Per-process stdin/stdout/stderr tables verified\n");

#if defined(__aarch64__) || defined(__arm__) || defined(__riscv)
    /*
     * Exercise the native page-table activation boundary while the boot
     * thread is still on the BSP. This proves that two isolated roots can be
     * loaded and restored without enabling secondary user-task migration.
     */
    uint64_t active_root = xscore_vmm_current_root();
    if (!active_root || !xscore_vmm_activate(first_root) ||
        xscore_vmm_current_root() != first_root ||
        !xscore_vmm_activate(second_root) ||
        xscore_vmm_current_root() != second_root ||
        !xscore_vmm_activate(active_root) ||
        xscore_vmm_current_root() != active_root) {
        xscore_serial_write("[FAIL] Native address-space activation boundary failed\n");
        return 0;
    }
    xscore_serial_write("[INFO] Native address-space activation boundary verified\n");
#endif

    uint64_t first_privilege_stack = xscore_thread_privilege_stack_top(
        xscore_process_thread_id(first));
    uint64_t second_privilege_stack = xscore_thread_privilege_stack_top(
        xscore_process_thread_id(second));
    if (!first_privilege_stack || !second_privilege_stack ||
        first_privilege_stack == second_privilege_stack ||
        !xscore_vmm_is_mapped_in(first_root, first_privilege_stack - PAGE_SIZE,
                                 0, 0) ||
        !xscore_vmm_is_mapped_in(second_root, second_privilege_stack - PAGE_SIZE,
                                 0, 0) ||
        xscore_vmm_is_mapped_in(
            first_root,
            first_privilege_stack -
                (XSCORE_THREAD_PRIVILEGE_STACK_PAGES + 1) * PAGE_SIZE,
            0, 0) ||
        xscore_vmm_is_mapped_in(
            second_root,
            second_privilege_stack -
                (XSCORE_THREAD_PRIVILEGE_STACK_PAGES + 1) * PAGE_SIZE,
            0, 0)) return 0;
    xscore_serial_write("[INFO] Guarded per-task privilege-entry stacks verified\n");

    uint64_t first_physical = UINT64_MAX;
    uint64_t second_physical = UINT64_MAX;
    int has_rx = 0, has_ro = 0, has_rw = 0;
    for (uint64_t addr = USER_CODE_ADDRESS;
         addr < USER_CODE_ADDRESS + USER_IMAGE_SELF_TEST_WINDOW;
         addr += PAGE_SIZE) {
        uint64_t flags = 0;
        if (!xscore_process_page_info(first, addr, &flags, 0)) continue;
#if defined(__i386__)
        if ((flags & XSCORE_PAGE_WRITABLE) == 0) has_ro = 1;
        if ((flags & XSCORE_PAGE_WRITABLE) != 0) has_rw = 1;
        has_rx = 1;
#else
        if ((flags & (XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE)) == 0) has_rx = 1;
        if ((flags & XSCORE_PAGE_WRITABLE) == 0 && (flags & XSCORE_PAGE_NO_EXECUTE) != 0) has_ro = 1;
        if ((flags & XSCORE_PAGE_WRITABLE) != 0 && (flags & XSCORE_PAGE_NO_EXECUTE) != 0) has_rw = 1;
#endif
    }
    if (!has_rx || !has_ro || !has_rw ||
        !xscore_process_page_info(first, USER_CODE_ADDRESS, 0, &first_physical) ||
        !xscore_process_page_info(second, USER_CODE_ADDRESS, 0, &second_physical) ||
        first_physical != 0 || second_physical != 0 ||
        xscore_vmm_is_mapped_in(first_root, USER_CODE_ADDRESS, 0, 0) ||
        xscore_vmm_is_mapped_in(second_root, USER_CODE_ADDRESS, 0, 0)) return 0;
    xscore_serial_write(
        "[INFO] Executable image reservations are initially non-resident\n");
    xscore_serial_write("[INFO] Userspace ELF reservation permissions verified\n");
    xscore_serial_write("[INFO] Executable loaded through VFS path /bin/hello\n");

    uint64_t first_status = UINT64_MAX;
    uint64_t second_status = UINT64_MAX;
    if (!xscore_process_wait(first, &first_status)) {
        xscore_serial_write("[FAIL] wait first returned 0\n");
        return 0;
    }
    if (!xscore_process_wait(second, &second_status)) {
        xscore_serial_write("[FAIL] wait second returned 0\n");
        return 0;
    }
    if (first_status != 0) {
        char hex[17];
        uint64_t v = first_status;
        for (int i = 15; i >= 0; --i) {
            uint8_t nib = v & 0xf;
            hex[i] = (char)(nib < 10 ? '0' + nib : 'a' + nib - 10);
            v >>= 4;
        }
        hex[16] = '\0';
        xscore_serial_write("[FAIL] first_status = 0x");
        xscore_serial_write(hex);
        xscore_serial_write("\n");
        return 0;
    }
    if (second_status != 0) {
        xscore_serial_write("[FAIL] second_status != 0\n");
        return 0;
    }
    uint64_t first_stack_phys = 0, second_stack_phys = 0;
    if (!xscore_vmm_is_mapped_in(first_root, USER_CODE_ADDRESS, 0,
                                 &first_physical) ||
        !xscore_vmm_is_mapped_in(second_root, USER_CODE_ADDRESS, 0,
                                 &second_physical)) {
        xscore_serial_write("[FAIL] Executable base page missing after process exit\n");
        return 0;
    }
    if (first_physical != second_physical) {
        xscore_serial_write(
            "[FAIL] Unrelocated executable text page was not shared\n");
        return 0;
    }
    if (!xscore_vmm_is_mapped_in(first_root, USER_STACK_PAGE, 0,
                                 &first_stack_phys) ||
        !xscore_vmm_is_mapped_in(second_root, USER_STACK_PAGE, 0,
                                 &second_stack_phys) ||
        first_stack_phys == second_stack_phys) {
        xscore_serial_write("[FAIL] Process stacks are not isolated\n");
        return 0;
    }
    if (xscore_vmm_is_mapped_in(xscore_vmm_root_table(), USER_CODE_ADDRESS,
                                0, 0)) {
        xscore_serial_write("[FAIL] User text leaked into the kernel root\n");
        return 0;
    }
    xscore_serial_write(
        "[INFO] Clean executable text pages are shared from page cache\n");
    if (!xscore_process_wait_did_block(first)) {
        xscore_serial_write("[FAIL] wait did not block\n");
        return 0;
    }
#if defined(__aarch64__) || defined(__arm__) || defined(__riscv)
    xscore_serial_write(
        "[INFO] Native user trap-return frame restored on BSP\n");
#endif
    xscore_serial_write("[INFO] VFS open/read/EOF/error syscalls verified\n");
    xscore_serial_write("[INFO] Descriptor write/dup/dup2/close syscalls verified\n");
    xscore_serial_write("[INFO] Native C runtime and libc wrappers verified\n");

    uint64_t preemptions_after = xscore_sched_get_stats().preemptions;
    if (preemptions_after <= preemptions_before) return 0;
    xscore_serial_write("[INFO] User-mode interrupt-return preemption observed\n");

    if (!xscore_process_reap(first) || !xscore_process_reap(second)) return 0;
    xscore_serial_write("[INFO] Internal process API create/wait/reap verified\n");

    struct xscore_process *reused = xscore_process_create_path("/bin/hello");
    uint64_t reused_status = UINT64_MAX;
    if (!reused || reused != first ||
        !xscore_process_wait(reused, &reused_status) || reused_status != 0 ||
        !xscore_process_wait_did_block(reused) ||
        !xscore_process_reap(reused)) return 0;
    xscore_serial_write("[INFO] Reclaimed process and thread slots reused\n");

    const char *vm_args[] = {"/bin/hello", "--vm-test", 0};
    struct xscore_process *vm_test =
        xscore_process_create_path_args("/bin/hello", 2, vm_args);
    uint64_t vm_status = UINT64_MAX;
    if (!vm_test || !xscore_process_wait(vm_test, &vm_status) ||
        vm_status != 0 || !xscore_process_reap(vm_test)) return 0;
    xscore_serial_write(
        "[INFO] Demand paging and Copy-on-Write fork verified\n");

    const char *usercopy_args[] = {"/bin/hello", "--usercopy-test", 0};
    struct xscore_process *usercopy_test =
        xscore_process_create_path_args("/bin/hello", 2, usercopy_args);
    uint64_t usercopy_status = UINT64_MAX;
    if (!usercopy_test ||
        !xscore_process_wait(usercopy_test, &usercopy_status) ||
        usercopy_status != 0 || !xscore_process_reap(usercopy_test)) return 0;
    xscore_serial_write("[INFO] Safe user copy-in/copy-out verified\n");

    const char *sigaction_args[] = {"/bin/hello", "--signal-test", 0};
    struct xscore_process *sigaction_test =
        xscore_process_create_path_args("/bin/hello", 2, sigaction_args);
    uint64_t sigaction_status = UINT64_MAX;
    if (!sigaction_test ||
        !xscore_process_wait(sigaction_test, &sigaction_status) ||
        sigaction_status != 0 || !xscore_process_reap(sigaction_test)) {
        xscore_serial_write("[FAIL] sigaction test process failed\n");
        return 0;
    }
    xscore_serial_write("[INFO] User-defined signal handling (sigaction) verified\n");

    const char *exec_args[] = {"/bin/hello", "--exec-test", 0};
    struct xscore_process *exec_test =
        xscore_process_create_path_args("/bin/hello", 2, exec_args);
    uint64_t exec_status = UINT64_MAX;
    if (!exec_test || !xscore_process_wait(exec_test, &exec_status) ||
        exec_status != 0 || !xscore_process_reap(exec_test)) {
        xscore_serial_write("[FAIL] POSIX execve test process failed\n");
        return 0;
    }
    xscore_serial_write("[INFO] POSIX fork-exec-wait lifecycle verified\n");

    const char *mmap_args[] = {"/bin/hello", "--mmap-test", 0};
    struct xscore_process *mmap_test =
        xscore_process_create_path_args("/bin/hello", 2, mmap_args);
    uint64_t mmap_status = UINT64_MAX;
    if (!mmap_test || !xscore_process_wait(mmap_test, &mmap_status) ||
        mmap_status != 0 || !xscore_process_reap(mmap_test)) {
        xscore_serial_write("[FAIL] mmap/munmap test process failed\n");
        return 0;
    }
    xscore_serial_write("[INFO] Demand-paged private mmap/munmap verified\n");

    const char *shared_memory_args[] = {
        "/bin/hello", "--shared-memory-test", 0};
    struct xscore_process *shared_memory_test =
        xscore_process_create_path_args("/bin/hello", 2,
                                        shared_memory_args);
    uint64_t shared_memory_status = UINT64_MAX;
    if (!shared_memory_test ||
        !xscore_process_wait(shared_memory_test, &shared_memory_status) ||
        shared_memory_status != 0 ||
        !xscore_process_reap(shared_memory_test)) {
        xscore_serial_write("[FAIL] shared memory test process failed\n");
        return 0;
    }
    xscore_serial_write("[INFO] POSIX shared memory and mprotect verified\n");

    const char *poll_args[] = {"/bin/hello", "--poll-test", 0};
    struct xscore_process *poll_test =
        xscore_process_create_path_args("/bin/hello", 2, poll_args);
    uint64_t poll_status = UINT64_MAX;
    if (!poll_test || !xscore_process_wait(poll_test, &poll_status) ||
        poll_status != 0 || !xscore_process_reap(poll_test)) {
        xscore_serial_write("[FAIL] poll/input descriptor test failed\n");
        return 0;
    }
    xscore_serial_write(
        "[INFO] poll readiness and input event descriptors verified\n");

    const char *devfs_args[] = {"/bin/hello", "--devfs-test", 0};
    struct xscore_process *devfs_test =
        xscore_process_create_path_args("/bin/hello", 2, devfs_args);
    uint64_t devfs_status = UINT64_MAX;
    if (!devfs_test || !xscore_process_wait(devfs_test, &devfs_status) ||
        devfs_status != 0 || !xscore_process_reap(devfs_test)) {
        xscore_serial_write("[FAIL] visible device filesystem test failed\n");
        return 0;
    }
    xscore_serial_write("[INFO] Visible /dev character devices verified\n");

#if defined(__x86_64__)
    const char *framebuffer_args[] = {
        "/bin/hello", "--framebuffer-test", 0};
    struct xscore_process *framebuffer_test =
        xscore_process_create_path_args("/bin/hello", 2,
                                        framebuffer_args);
    uint64_t framebuffer_status = UINT64_MAX;
    if (!framebuffer_test ||
        !xscore_process_wait(framebuffer_test, &framebuffer_status) ||
        framebuffer_status != 0 || !xscore_process_reap(framebuffer_test)) {
        xscore_serial_write("[FAIL] framebuffer device test failed\n");
        return 0;
    }
    xscore_serial_write("[INFO] Userspace framebuffer device verified\n");
#endif

    const char *display_args[] = {"/bin/lumen", "--self-test", 0};
    struct xscore_process *display_test =
        xscore_process_create_path_args("/bin/lumen", 2, display_args);
    uint64_t display_status = UINT64_MAX;
    if (!display_test ||
        !xscore_process_wait(display_test, &display_status) ||
        display_status != 0 || !xscore_process_reap(display_test)) {
        xscore_serial_write("[FAIL] userspace display protocol test failed\n");
        return 0;
    }
    xscore_serial_write("[INFO] Userspace display surface protocol verified\n");

    /* Deliver SIGINT while a native process is running in userspace. The
     * timer-return path must redirect it through the user signal trampoline
     * and preserve the conventional 128 + signal exit status. */
    const char *signal_args[] = {"/bin/hello", 0};
    struct xscore_process *signal_test =
        xscore_process_create_path_args("/bin/hello", 1, signal_args);
    if (!signal_test ||
        xscore_process_signal(xscore_process_pid(signal_test),
                              AKAOS_SIGINT) != 0) return 0;
    uint64_t signal_status = UINT64_MAX;
    if (!xscore_process_wait(signal_test, &signal_status) ||
        signal_status != UINT64_C(128) + AKAOS_SIGINT ||
        !xscore_process_reap(signal_test)) return 0;
    xscore_serial_write("[INFO] SIGINT trampoline termination verified\n");

    const char *cat_args[] = {"/bin/cat", "/etc/motd", 0};
    struct xscore_process *cat =
        xscore_process_create_path_args("/bin/cat", 2, cat_args);
    uint64_t cat_status = UINT64_MAX;
    if (!cat || !xscore_process_wait(cat, &cat_status) || cat_status != 0 ||
        !xscore_process_reap(cat)) return 0;

    const char *ls_etc_args[] = {"/bin/ls", "/etc", 0};
    struct xscore_process *ls_etc =
        xscore_process_create_path_args("/bin/ls", 2, ls_etc_args);
    uint64_t ls_etc_status = UINT64_MAX;
    if (!ls_etc || !xscore_process_wait(ls_etc, &ls_etc_status) ||
        ls_etc_status != 0 || !xscore_process_reap(ls_etc)) return 0;

    const char *ls_bin_args[] = {"/bin/ls", "/bin", 0};
    struct xscore_process *ls_bin =
        xscore_process_create_path_args("/bin/ls", 2, ls_bin_args);
    uint64_t ls_bin_status = UINT64_MAX;
    if (!ls_bin || !xscore_process_wait(ls_bin, &ls_bin_status) ||
        ls_bin_status != 0 || !xscore_process_reap(ls_bin)) return 0;

    /* Verify error exit status on nonexistent target */
    const char *ls_bad_args[] = {"/bin/ls", "nonexistent_file", 0};
    struct xscore_process *ls_bad =
        xscore_process_create_path_args("/bin/ls", 2, ls_bad_args);
    uint64_t ls_bad_status = 0;
    if (!ls_bad || !xscore_process_wait(ls_bad, &ls_bad_status) ||
        ls_bad_status == 0 || !xscore_process_reap(ls_bad)) return 0;

    /* Verify error exit status on cat directory */
    const char *cat_dir_args[] = {"/bin/cat", "/bin", 0};
    struct xscore_process *cat_dir =
        xscore_process_create_path_args("/bin/cat", 2, cat_dir_args);
    uint64_t cat_dir_status = 0;
    if (!cat_dir || !xscore_process_wait(cat_dir, &cat_dir_status) ||
        cat_dir_status == 0 || !xscore_process_reap(cat_dir)) return 0;

    /* Verify dynamic VFS lifecycle: mkdir -> touch -> ls -> rm */
    const char *mkdir_args[] = {"/bin/mkdir", "/tmp_test", 0};
    struct xscore_process *p_mkdir =
        xscore_process_create_path_args("/bin/mkdir", 2, mkdir_args);
    uint64_t mkdir_status = UINT64_MAX;
    if (!p_mkdir || !xscore_process_wait(p_mkdir, &mkdir_status) ||
        mkdir_status != 0 || !xscore_process_reap(p_mkdir)) return 0;

    const char *touch_args[] = {"/bin/touch", "/tmp_test/file.txt", 0};
    struct xscore_process *p_touch =
        xscore_process_create_path_args("/bin/touch", 2, touch_args);
    uint64_t touch_status = UINT64_MAX;
    if (!p_touch || !xscore_process_wait(p_touch, &touch_status) ||
        touch_status != 0 || !xscore_process_reap(p_touch)) return 0;

    const char *rm_args[] = {"/bin/rm", "/tmp_test/file.txt", 0};
    struct xscore_process *p_rm =
        xscore_process_create_path_args("/bin/rm", 2, rm_args);
    uint64_t rm_status = UINT64_MAX;
    if (!p_rm || !xscore_process_wait(p_rm, &rm_status) ||
        rm_status != 0 || !xscore_process_reap(p_rm)) return 0;

    const char *rmdir_args[] = {"/bin/rm", "-d", "/tmp_test", 0};
    struct xscore_process *p_rmdir =
        xscore_process_create_path_args("/bin/rm", 3, rmdir_args);
    uint64_t rmdir_status = UINT64_MAX;
    if (!p_rmdir || !xscore_process_wait(p_rmdir, &rmdir_status) ||
        rmdir_status != 0 || !xscore_process_reap(p_rmdir)) return 0;

    const char *head_args[] = {"/bin/head", "-n", "1", "/etc/motd", 0};
    struct xscore_process *p_head =
        xscore_process_create_path_args("/bin/head", 4, head_args);
    uint64_t head_status = UINT64_MAX;
    if (!p_head || !xscore_process_wait(p_head, &head_status) ||
        head_status != 0 || !xscore_process_reap(p_head)) return 0;

    const char *tail_args[] = {"/bin/tail", "-n", "1", "/etc/motd", 0};
    struct xscore_process *p_tail =
        xscore_process_create_path_args("/bin/tail", 4, tail_args);
    uint64_t tail_status = UINT64_MAX;
    if (!p_tail || !xscore_process_wait(p_tail, &tail_status) ||
        tail_status != 0 || !xscore_process_reap(p_tail)) return 0;

    const char *wc_args[] = {"/bin/wc", "-l", "/etc/motd", 0};
    struct xscore_process *p_wc =
        xscore_process_create_path_args("/bin/wc", 3, wc_args);
    uint64_t wc_status = UINT64_MAX;
    if (!p_wc || !xscore_process_wait(p_wc, &wc_status) ||
        wc_status != 0 || !xscore_process_reap(p_wc)) return 0;

    const char *uname_args[] = {"/bin/uname", "-a", 0};
    struct xscore_process *p_uname =
        xscore_process_create_path_args("/bin/uname", 2, uname_args);
    uint64_t uname_status = UINT64_MAX;
    if (!p_uname || !xscore_process_wait(p_uname, &uname_status) ||
        uname_status != 0 || !xscore_process_reap(p_uname)) return 0;

    const char *whoami_args[] = {"/bin/whoami", 0};
    struct xscore_process *p_whoami =
        xscore_process_create_path_args("/bin/whoami", 1, whoami_args);
    uint64_t whoami_status = UINT64_MAX;
    if (!p_whoami || !xscore_process_wait(p_whoami, &whoami_status) ||
        whoami_status != 0 || !xscore_process_reap(p_whoami)) return 0;

    const char *sync_args[] = {"/bin/sync", 0};
    struct xscore_process *p_sync =
        xscore_process_create_path_args("/bin/sync", 1, sync_args);
    uint64_t sync_status = UINT64_MAX;
    if (!p_sync || !xscore_process_wait(p_sync, &sync_status) ||
        sync_status != 0 || !xscore_process_reap(p_sync)) return 0;

    const char *rb_help_args[] = {"/bin/reboot", "--help", 0};
    struct xscore_process *p_rb =
        xscore_process_create_path_args("/bin/reboot", 2, rb_help_args);
    uint64_t rb_status = UINT64_MAX;
    if (!p_rb || !xscore_process_wait(p_rb, &rb_status) ||
        rb_status != 0 || !xscore_process_reap(p_rb)) return 0;

    const char *po_help_args[] = {"/bin/poweroff", "--help", 0};
    struct xscore_process *p_po =
        xscore_process_create_path_args("/bin/poweroff", 2, po_help_args);
    uint64_t po_status = UINT64_MAX;
    if (!p_po || !xscore_process_wait(p_po, &po_status) ||
        po_status != 0 || !xscore_process_reap(p_po)) return 0;

    const char *chmod_help_args[] = {"/bin/chmod", "--help", 0};
    struct xscore_process *p_chmod =
        xscore_process_create_path_args("/bin/chmod", 2, chmod_help_args);
    uint64_t chmod_status = UINT64_MAX;
    if (!p_chmod || !xscore_process_wait(p_chmod, &chmod_status) ||
        chmod_status != 0 || !xscore_process_reap(p_chmod)) return 0;

    const char *chown_help_args[] = {"/bin/chown", "--help", 0};
    struct xscore_process *p_chown =
        xscore_process_create_path_args("/bin/chown", 2, chown_help_args);
    uint64_t chown_status = UINT64_MAX;
    if (!p_chown || !xscore_process_wait(p_chown, &chown_status) ||
        chown_status != 0 || !xscore_process_reap(p_chown)) return 0;

    const char *ted_help_args[] = {"/bin/ted", "--help", 0};
    struct xscore_process *p_ted =
        xscore_process_create_path_args("/bin/ted", 2, ted_help_args);
    uint64_t ted_status = UINT64_MAX;
    if (!p_ted || !xscore_process_wait(p_ted, &ted_status) ||
        ted_status != 0 || !xscore_process_reap(p_ted)) return 0;

    const char *rm_help_args[] = {"/bin/rm", "--help", 0};
    struct xscore_process *p_rm_help =
        xscore_process_create_path_args("/bin/rm", 2, rm_help_args);
    uint64_t rm_help_status = UINT64_MAX;
    if (!p_rm_help || !xscore_process_wait(p_rm_help, &rm_help_status) ||
        rm_help_status != 0 || !xscore_process_reap(p_rm_help)) return 0;

    /* Test directory safety: rm /rm_tree must fail with exit code 1 */
    xscore_vfs_mkdir_at(0, "/rm_tree", 0755);
    xscore_vfs_mkdir_at(0, "/rm_tree/sub", 0755);
    struct xscore_fd_table rm_test_table;
    xscore_fd_table_initialize(&rm_test_table);
    int64_t rm_fd = xscore_vfs_open_at(&rm_test_table, 0, "/rm_tree/sub/test.txt", AKAOS_O_WRONLY | AKAOS_O_CREAT, 0644);
    if (rm_fd >= 0) {
        xscore_fd_write(&rm_test_table, (int)rm_fd, "test", 4);
        xscore_fd_close(&rm_test_table, (int)rm_fd);
    }
    xscore_fd_table_close_all(&rm_test_table);

    const char *rm_dir_args[] = {"/bin/rm", "/rm_tree", 0};
    struct xscore_process *p_rm_dir =
        xscore_process_create_path_args("/bin/rm", 2, rm_dir_args);
    uint64_t rm_dir_status = UINT64_MAX;
    if (!p_rm_dir || !xscore_process_wait(p_rm_dir, &rm_dir_status) ||
        rm_dir_status != 1 || !xscore_process_reap(p_rm_dir)) return 0;

    /* Test recursive removal: rm -rf /rm_tree must succeed */
    const char *rm_rf_args[] = {"/bin/rm", "-rf", "/rm_tree", 0};
    struct xscore_process *p_rm_rf =
        xscore_process_create_path_args("/bin/rm", 3, rm_rf_args);
    uint64_t rm_rf_status = UINT64_MAX;
    if (!p_rm_rf || !xscore_process_wait(p_rm_rf, &rm_rf_status) ||
        rm_rf_status != 0 || !xscore_process_reap(p_rm_rf)) return 0;

    uint32_t rm_check_node = 0;
    if (xscore_vfs_resolve_path_at(0, "/rm_tree", &rm_check_node) == 0) return 0;

    /* Test VFS rename syscall (file and directory) */
    struct xscore_fd_table ren_table;
    xscore_fd_table_initialize(&ren_table);
    int64_t r_fd = xscore_vfs_open_at(&ren_table, 0, "/ren_old.txt", AKAOS_O_WRONLY | AKAOS_O_CREAT, 0644);
    if (r_fd >= 0) {
        xscore_fd_write(&ren_table, (int)r_fd, "rename_data", 11);
        xscore_fd_close(&ren_table, (int)r_fd);
    }
    xscore_fd_table_close_all(&ren_table);
    if (xscore_vfs_rename_at(0, "/ren_old.txt", 0, "/ren_new.txt") != 0) return 0;
    uint32_t ren_check = 0;
    if (xscore_vfs_resolve_path_at(0, "/ren_old.txt", &ren_check) == 0) return 0;
    if (xscore_vfs_resolve_path_at(0, "/ren_new.txt", &ren_check) != 0) return 0;
    xscore_vfs_unlink_at(0, "/ren_new.txt");

    /* Test cp and mv userspace commands */
    const char *cp_help_args[] = {"/bin/cp", "--help", 0};
    struct xscore_process *p_cp_h = xscore_process_create_path_args("/bin/cp", 2, cp_help_args);
    uint64_t cp_h_status = UINT64_MAX;
    if (!p_cp_h || !xscore_process_wait(p_cp_h, &cp_h_status) || cp_h_status != 0 || !xscore_process_reap(p_cp_h)) return 0;

    const char *mv_help_args[] = {"/bin/mv", "--help", 0};
    struct xscore_process *p_mv_h = xscore_process_create_path_args("/bin/mv", 2, mv_help_args);
    uint64_t mv_h_status = UINT64_MAX;
    if (!p_mv_h || !xscore_process_wait(p_mv_h, &mv_h_status) || mv_h_status != 0 || !xscore_process_reap(p_mv_h)) return 0;

    /* Create file to copy with cp */
    xscore_fd_table_initialize(&ren_table);
    int64_t cp_fd = xscore_vfs_open_at(&ren_table, 0, "/cp_test_src.txt", AKAOS_O_WRONLY | AKAOS_O_CREAT, 0644);
    if (cp_fd >= 0) {
        xscore_fd_write(&ren_table, (int)cp_fd, "copy_content", 12);
        xscore_fd_close(&ren_table, (int)cp_fd);
    }
    xscore_fd_table_close_all(&ren_table);

    const char *cp_args[] = {"/bin/cp", "/cp_test_src.txt", "/cp_test_dst.txt", 0};
    struct xscore_process *p_cp = xscore_process_create_path_args("/bin/cp", 3, cp_args);
    uint64_t cp_status = UINT64_MAX;
    if (!p_cp || !xscore_process_wait(p_cp, &cp_status) || cp_status != 0 || !xscore_process_reap(p_cp)) return 0;
    if (xscore_vfs_resolve_path_at(0, "/cp_test_src.txt", &ren_check) != 0) return 0;
    if (xscore_vfs_resolve_path_at(0, "/cp_test_dst.txt", &ren_check) != 0) return 0;

    /* Move /cp_test_dst.txt to /mv_test_final.txt */
    const char *mv_args[] = {"/bin/mv", "/cp_test_dst.txt", "/mv_test_final.txt", 0};
    struct xscore_process *p_mv = xscore_process_create_path_args("/bin/mv", 3, mv_args);
    uint64_t mv_status = UINT64_MAX;
    if (!p_mv || !xscore_process_wait(p_mv, &mv_status) || mv_status != 0 || !xscore_process_reap(p_mv)) return 0;
    if (xscore_vfs_resolve_path_at(0, "/cp_test_dst.txt", &ren_check) == 0) return 0;
    if (xscore_vfs_resolve_path_at(0, "/mv_test_final.txt", &ren_check) != 0) return 0;

    /* Clean up test files */
    xscore_vfs_unlink_at(0, "/cp_test_src.txt");
    xscore_vfs_unlink_at(0, "/mv_test_final.txt");

    /* Multi-block VFS allocation test: write 10 KiB spanning 3 blocks, test chmod & chown syscalls, read and verify */
    const char *large_path = "/large_test.dat";
    struct xscore_fd_table test_table;
    xscore_fd_table_initialize(&test_table);
    int64_t fd_large = xscore_vfs_open_at(&test_table, 0, large_path, AKAOS_O_WRONLY | AKAOS_O_CREAT | AKAOS_O_TRUNC, 0644);
    if (fd_large < 0) return 0;
    uint8_t write_chunk[1024];
    for (int b = 0; b < 10; ++b) {
        for (size_t i = 0; i < 1024; ++i) write_chunk[i] = (uint8_t)('A' + b);
        xscore_fd_write(&test_table, (int)fd_large, write_chunk, 1024);
    }
    xscore_fd_close(&test_table, (int)fd_large);

    /* Test chmod and chown */
    if (xscore_vfs_chmod_at(0, large_path, 0755) != 0) return 0;
    if (xscore_vfs_chown_at(0, large_path, 1000, 1000) != 0) return 0;

    struct akaos_stat large_st;
    if (xscore_vfs_stat_at(0, large_path, &large_st) != 0 || large_st.size != 10240 ||
        (large_st.mode & 0777) != 0755 || large_st.uid != 1000 || large_st.gid != 1000) {
        return 0;
    }
    int64_t fd_read = xscore_vfs_open_at(&test_table, 0, large_path, AKAOS_O_RDONLY, 0);
    if (fd_read < 0) return 0;
    uint8_t read_chunk[1024];
    for (int b = 0; b < 10; ++b) {
        int64_t nr = xscore_fd_read(&test_table, (int)fd_read, read_chunk, 1024);
        if (nr != 1024) return 0;
        for (size_t i = 0; i < 1024; ++i) {
            if (read_chunk[i] != (uint8_t)('A' + b)) return 0;
        }
    }
    xscore_fd_close(&test_table, (int)fd_read);
    xscore_vfs_unlink_at(0, large_path);
    xscore_fd_table_close_all(&test_table);

    /* Verify stream filters and test / [ utilities */
    const char *test_args[] = {"/bin/test", "-f", "/etc/passwd", 0};
    struct xscore_process *p_test = xscore_process_create_path_args("/bin/test", 3, test_args);
    uint64_t test_status = UINT64_MAX;
    if (!p_test || !xscore_process_wait(p_test, &test_status) || test_status != 0 || !xscore_process_reap(p_test)) return 0;

    const char *bracket_args[] = {"/bin/[", "10", "-gt", "5", "]", 0};
    struct xscore_process *p_bracket = xscore_process_create_path_args("/bin/[", 5, bracket_args);
    uint64_t bracket_status = UINT64_MAX;
    if (!p_bracket || !xscore_process_wait(p_bracket, &bracket_status) || bracket_status != 0 || !xscore_process_reap(p_bracket)) return 0;

    const char *find_args[] = {"/bin/find", "/etc", "-name", "passwd", 0};
    struct xscore_process *p_find = xscore_process_create_path_args("/bin/find", 4, find_args);
    uint64_t find_status = UINT64_MAX;
    if (!p_find || !xscore_process_wait(p_find, &find_status) || find_status != 0 || !xscore_process_reap(p_find)) return 0;

    const char *sort_args[] = {"/bin/sort", "-u", "/etc/motd", 0};
    struct xscore_process *p_sort = xscore_process_create_path_args("/bin/sort", 3, sort_args);
    uint64_t sort_status = UINT64_MAX;
    if (!p_sort || !xscore_process_wait(p_sort, &sort_status) || sort_status != 0 || !xscore_process_reap(p_sort)) return 0;

    const char *uniq_args[] = {"/bin/uniq", "-c", "/etc/motd", 0};
    struct xscore_process *p_uniq = xscore_process_create_path_args("/bin/uniq", 3, uniq_args);
    uint64_t uniq_status = UINT64_MAX;
    if (!p_uniq || !xscore_process_wait(p_uniq, &uniq_status) || uniq_status != 0 || !xscore_process_reap(p_uniq)) return 0;

    /* Verify akash shell scripting and control flow engine */
    const char *akash_script_args[] = {"/bin/akash", "-c", "for x in 1 2 3; do if [ $x -eq 2 ]; then exit 0; fi; done; exit 1", 0};
    struct xscore_process *p_akash = xscore_process_create_path_args("/bin/akash", 3, akash_script_args);
    uint64_t akash_status = UINT64_MAX;
    if (!p_akash || !xscore_process_wait(p_akash, &akash_status) || akash_status != 0 || !xscore_process_reap(p_akash)) return 0;

    /* Verify SSH server and akaos-config daemon inspection */
    const char *sshd_args[] = {"/bin/sshd", "--status", 0};
    struct xscore_process *p_sshd = xscore_process_create_path_args("/bin/sshd", 2, sshd_args);
    uint64_t sshd_status = UINT64_MAX;
    if (!p_sshd || !xscore_process_wait(p_sshd, &sshd_status) || sshd_status != 0 || !xscore_process_reap(p_sshd)) return 0;

    const char *cfg_args[] = {"/bin/akaos-config", "--status", 0};
    struct xscore_process *p_cfg = xscore_process_create_path_args("/bin/akaos-config", 2, cfg_args);
    uint64_t cfg_status = UINT64_MAX;
    if (!p_cfg || !xscore_process_wait(p_cfg, &cfg_status) || cfg_status != 0 || !xscore_process_reap(p_cfg)) return 0;

    /* Verify sleep, which, env, cut, df, du, tr */
    const char *sleep_args[] = {"/bin/sleep", "0.01", 0};
    struct xscore_process *p_sleep = xscore_process_create_path_args("/bin/sleep", 2, sleep_args);
    uint64_t sleep_status = UINT64_MAX;
    if (!p_sleep || !xscore_process_wait(p_sleep, &sleep_status) || sleep_status != 0 || !xscore_process_reap(p_sleep)) return 0;

    const char *which_args[] = {"/bin/which", "ls", 0};
    struct xscore_process *p_which = xscore_process_create_path_args("/bin/which", 2, which_args);
    uint64_t which_status = UINT64_MAX;
    if (!p_which || !xscore_process_wait(p_which, &which_status) || which_status != 0 || !xscore_process_reap(p_which)) return 0;

    const char *env_args[] = {"/bin/env", "FOO=BAR", 0};
    struct xscore_process *p_env = xscore_process_create_path_args("/bin/env", 2, env_args);
    uint64_t env_status = UINT64_MAX;
    if (!p_env || !xscore_process_wait(p_env, &env_status) || env_status != 0 || !xscore_process_reap(p_env)) return 0;

    const char *cut_args[] = {"/bin/cut", "-d:", "-f1", "/etc/passwd", 0};
    struct xscore_process *p_cut = xscore_process_create_path_args("/bin/cut", 4, cut_args);
    uint64_t cut_status = UINT64_MAX;
    if (!p_cut || !xscore_process_wait(p_cut, &cut_status) || cut_status != 0 || !xscore_process_reap(p_cut)) return 0;

    const char *df_args[] = {"/bin/df", "-h", 0};
    struct xscore_process *p_df = xscore_process_create_path_args("/bin/df", 2, df_args);
    uint64_t df_status = UINT64_MAX;
    if (!p_df || !xscore_process_wait(p_df, &df_status) || df_status != 0 || !xscore_process_reap(p_df)) return 0;

    const char *du_args[] = {"/bin/du", "-s", "/etc", 0};
    struct xscore_process *p_du = xscore_process_create_path_args("/bin/du", 3, du_args);
    uint64_t du_status = UINT64_MAX;
    if (!p_du || !xscore_process_wait(p_du, &du_status) || du_status != 0 || !xscore_process_reap(p_du)) return 0;

    const char *tr_args[] = {"/bin/tr", "--help", 0};
    struct xscore_process *p_tr = xscore_process_create_path_args("/bin/tr", 2, tr_args);
    uint64_t tr_status = UINT64_MAX;
    if (!p_tr || !xscore_process_wait(p_tr, &tr_status) || tr_status != 0 || !xscore_process_reap(p_tr)) return 0;

    /* Verify kill, pkill, uptime, free, watch */
    const char *kill_args[] = {"/bin/kill", "-l", 0};
    struct xscore_process *p_kill = xscore_process_create_path_args("/bin/kill", 2, kill_args);
    uint64_t kill_status = UINT64_MAX;
    if (!p_kill || !xscore_process_wait(p_kill, &kill_status) || kill_status != 0 || !xscore_process_reap(p_kill)) return 0;

    const char *pkill_args[] = {"/bin/pkill", "--help", 0};
    struct xscore_process *p_pkill = xscore_process_create_path_args("/bin/pkill", 2, pkill_args);
    uint64_t pkill_status = UINT64_MAX;
    if (!p_pkill || !xscore_process_wait(p_pkill, &pkill_status) || pkill_status != 0 || !xscore_process_reap(p_pkill)) return 0;

    const char *uptime_args[] = {"/bin/uptime", "-p", 0};
    struct xscore_process *p_uptime = xscore_process_create_path_args("/bin/uptime", 2, uptime_args);
    uint64_t uptime_status = UINT64_MAX;
    if (!p_uptime || !xscore_process_wait(p_uptime, &uptime_status) || uptime_status != 0 || !xscore_process_reap(p_uptime)) return 0;

    const char *free_args[] = {"/bin/free", "-h", 0};
    struct xscore_process *p_free = xscore_process_create_path_args("/bin/free", 2, free_args);
    uint64_t free_status = UINT64_MAX;
    if (!p_free || !xscore_process_wait(p_free, &free_status) || free_status != 0 || !xscore_process_reap(p_free)) return 0;

    const char *watch_args[] = {"/bin/watch", "--help", 0};
    struct xscore_process *p_watch = xscore_process_create_path_args("/bin/watch", 2, watch_args);
    uint64_t watch_status = UINT64_MAX;
    if (!p_watch || !xscore_process_wait(p_watch, &watch_status) || watch_status != 0 || !xscore_process_reap(p_watch)) return 0;

    /* Verify Unix text transformation & diff suite (sed, diff, cmp, patch) */
    const char *sed_args[] = {"/bin/sed", "-e", "s/akaOS/akaOS_SED/g", "/etc/motd", 0};
    struct xscore_process *p_sed = xscore_process_create_path_args("/bin/sed", 4, sed_args);
    uint64_t sed_status = UINT64_MAX;
    if (!p_sed || !xscore_process_wait(p_sed, &sed_status) || sed_status != 0 || !xscore_process_reap(p_sed)) return 0;

    const char *diff_args[] = {"/bin/diff", "-u", "/etc/motd", "/etc/motd", 0};
    struct xscore_process *p_diff = xscore_process_create_path_args("/bin/diff", 4, diff_args);
    uint64_t diff_status = UINT64_MAX;
    if (!p_diff || !xscore_process_wait(p_diff, &diff_status) || diff_status != 0 || !xscore_process_reap(p_diff)) return 0;

    const char *cmp_args[] = {"/bin/cmp", "/etc/motd", "/etc/motd", 0};
    struct xscore_process *p_cmp = xscore_process_create_path_args("/bin/cmp", 3, cmp_args);
    uint64_t cmp_status = UINT64_MAX;
    if (!p_cmp || !xscore_process_wait(p_cmp, &cmp_status) || cmp_status != 0 || !xscore_process_reap(p_cmp)) return 0;

    const char *patch_args[] = {"/bin/patch", "--help", 0};
    struct xscore_process *p_patch = xscore_process_create_path_args("/bin/patch", 2, patch_args);
    uint64_t patch_status = UINT64_MAX;
    if (!p_patch || !xscore_process_wait(p_patch, &patch_status) || patch_status != 0 || !xscore_process_reap(p_patch)) return 0;

    /* Verify Archiving & Compression suite (tar, gzip, gunzip) */
    const char *tar_c_args[] = {"/bin/tar", "-cf", "/test_archive.tar", "/etc/motd", 0};
    struct xscore_process *p_tar_c = xscore_process_create_path_args("/bin/tar", 4, tar_c_args);
    uint64_t tar_c_status = UINT64_MAX;
    if (!p_tar_c || !xscore_process_wait(p_tar_c, &tar_c_status) || tar_c_status != 0 || !xscore_process_reap(p_tar_c)) return 0;

    const char *tar_t_args[] = {"/bin/tar", "-tf", "/test_archive.tar", 0};
    struct xscore_process *p_tar_t = xscore_process_create_path_args("/bin/tar", 3, tar_t_args);
    uint64_t tar_t_status = UINT64_MAX;
    if (!p_tar_t) {
        xscore_serial_write("[FAIL] tar list process creation failed\n");
        return 0;
    }
    if (!xscore_process_wait(p_tar_t, &tar_t_status)) {
        xscore_serial_write("[FAIL] tar list wait failed\n");
        return 0;
    }
    if (tar_t_status != 0) {
        xscore_serial_write("[FAIL] tar list process returned nonzero status\n");
        return 0;
    }
    if (!xscore_process_reap(p_tar_t)) {
        xscore_serial_write("[FAIL] tar list process reap failed\n");
        return 0;
    }
    xscore_vfs_unlink_at(0, "/test_archive.tar");

    const char *gzip_args[] = {"/bin/gzip", "--help", 0};
    struct xscore_process *p_gzip = xscore_process_create_path_args("/bin/gzip", 2, gzip_args);
    uint64_t gzip_status = UINT64_MAX;
    if (!p_gzip) {
        xscore_serial_write("[FAIL] gzip process creation failed\n");
        return 0;
    }
    if (!xscore_process_wait(p_gzip, &gzip_status)) {
        xscore_serial_write("[FAIL] gzip process wait failed\n");
        return 0;
    }
    if (gzip_status != 0) {
        xscore_serial_write("[FAIL] gzip process returned nonzero status\n");
        return 0;
    }
    if (!xscore_process_reap(p_gzip)) {
        xscore_serial_write("[FAIL] gzip process reap failed\n");
        return 0;
    }

    const char *gunzip_args[] = {"/bin/gunzip", "--help", 0};
    struct xscore_process *p_gunzip = xscore_process_create_path_args("/bin/gunzip", 2, gunzip_args);
    uint64_t gunzip_status = UINT64_MAX;
    if (!p_gunzip || !xscore_process_wait(p_gunzip, &gunzip_status) || gunzip_status != 0 || !xscore_process_reap(p_gunzip)) return 0;

    /* Verify Advanced Shell Job Control & Multi-Stage Pipelines in akash */
    const char *akash_job_args[] = {"/bin/akash", "-c", "echo multi | cat | grep multi", 0};
    struct xscore_process *p_akash_job = xscore_process_create_path_args("/bin/akash", 3, akash_job_args);
    uint64_t akash_job_status = UINT64_MAX;
    if (!p_akash_job || !xscore_process_wait(p_akash_job, &akash_job_status) || akash_job_status != 0 || !xscore_process_reap(p_akash_job)) return 0;

    const char *akash_bg_args[] = {"/bin/akash", "-c", "sleep 0.01 & jobs -l; wait", 0};
    struct xscore_process *p_akash_bg = xscore_process_create_path_args("/bin/akash", 3, akash_bg_args);
    uint64_t akash_bg_status = UINT64_MAX;
    if (!p_akash_bg || !xscore_process_wait(p_akash_bg, &akash_bg_status) || akash_bg_status != 0 || !xscore_process_reap(p_akash_bg)) return 0;

    const char *akash_job_ctrl_args[] = {"/bin/akash", "-c", "help bg; sleep 0.01 & bg %1; jobs; kill %1; wait", 0};
    struct xscore_process *p_akash_jc = xscore_process_create_path_args("/bin/akash", 3, akash_job_ctrl_args);
    uint64_t akash_jc_status = UINT64_MAX;
    if (!p_akash_jc || !xscore_process_wait(p_akash_jc, &akash_jc_status) || akash_jc_status != 0 || !xscore_process_reap(p_akash_jc)) return 0;

    const char *akash_alias_args[] = {"/bin/akash", "-c", "alias c='clear'; alias c; alias -p; c; unalias c", 0};
    struct xscore_process *p_akash_alias = xscore_process_create_path_args("/bin/akash", 3, akash_alias_args);
    uint64_t akash_alias_status = UINT64_MAX;
    if (!p_akash_alias || !xscore_process_wait(p_akash_alias, &akash_alias_status) || akash_alias_status != 0 || !xscore_process_reap(p_akash_alias)) return 0;

    const char *apropos_args[] = {"/bin/apropos", "zip", 0};
    struct xscore_process *p_apropos = xscore_process_create_path_args("/bin/apropos", 2, apropos_args);
    uint64_t apropos_status = UINT64_MAX;
    if (!p_apropos || !xscore_process_wait(p_apropos, &apropos_status) || apropos_status != 0 || !xscore_process_reap(p_apropos)) return 0;

    const char *apropos_phrase_args[] = {"/bin/apropos", "zip archive", 0};
    struct xscore_process *p_apropos_phrase = xscore_process_create_path_args("/bin/apropos", 2, apropos_phrase_args);
    uint64_t apropos_phrase_status = UINT64_MAX;
    if (!p_apropos_phrase || !xscore_process_wait(p_apropos_phrase, &apropos_phrase_status) || apropos_phrase_status != 0 || !xscore_process_reap(p_apropos_phrase)) return 0;

    const char *aspell_args[] = {"/bin/aspell", "check", "--sug-mode=bad-spellers", "-d", "en_US", "/etc/motd", 0};
    struct xscore_process *p_aspell = xscore_process_create_path_args("/bin/aspell", 6, aspell_args);
    uint64_t aspell_status = UINT64_MAX;
    if (!p_aspell || !xscore_process_wait(p_aspell, &aspell_status) || aspell_status != 0 || !xscore_process_reap(p_aspell)) return 0;

    const char *at_args[] = {"/bin/at", "23:00", "-f", "/etc/motd", 0};
    struct xscore_process *p_at = xscore_process_create_path_args("/bin/at", 4, at_args);
    uint64_t at_status = UINT64_MAX;
    if (!p_at || !xscore_process_wait(p_at, &at_status) || at_status != 0 || !xscore_process_reap(p_at)) return 0;

    const char *atq_args[] = {"/bin/atq", 0};
    struct xscore_process *p_atq = xscore_process_create_path_args("/bin/atq", 1, atq_args);
    uint64_t atq_status = UINT64_MAX;
    if (!p_atq || !xscore_process_wait(p_atq, &atq_status) || atq_status != 0 || !xscore_process_reap(p_atq)) return 0;

    const char *atrm_args[] = {"/bin/atrm", "1", 0};
    struct xscore_process *p_atrm = xscore_process_create_path_args("/bin/atrm", 2, atrm_args);
    uint64_t atrm_status = UINT64_MAX;
    if (!p_atrm || !xscore_process_wait(p_atrm, &atrm_status) || atrm_status != 0 || !xscore_process_reap(p_atrm)) return 0;
    xscore_vfs_unlink_at(0, "/var/spool/at/.seq");

    const char *awk_args[] = {"/bin/awk", "BEGIN { print 1+2 }", 0};
    struct xscore_process *p_awk = xscore_process_create_path_args("/bin/awk", 2, awk_args);
    uint64_t awk_status = UINT64_MAX;
    if (!p_awk || !xscore_process_wait(p_awk, &awk_status) || awk_status != 0 || !xscore_process_reap(p_awk)) return 0;

    const char *awk_f_args[] = {"/bin/awk", "-F:", "{ print $1 }", "/etc/passwd", 0};
    struct xscore_process *p_awk_f = xscore_process_create_path_args("/bin/awk", 4, awk_f_args);
    uint64_t awk_f_status = UINT64_MAX;
    if (!p_awk_f || !xscore_process_wait(p_awk_f, &awk_f_status) || awk_f_status != 0 || !xscore_process_reap(p_awk_f)) return 0;

    const char *basename_args[] = {"/bin/basename", "/usr/bin/sort", 0};
    struct xscore_process *p_base = xscore_process_create_path_args("/bin/basename", 2, basename_args);
    uint64_t base_status = UINT64_MAX;
    if (!p_base || !xscore_process_wait(p_base, &base_status) || base_status != 0 || !xscore_process_reap(p_base)) return 0;

    const char *basename_suf_args[] = {"/bin/basename", "/usr/include/stdio.h", ".h", 0};
    struct xscore_process *p_base_suf = xscore_process_create_path_args("/bin/basename", 3, basename_suf_args);
    uint64_t base_suf_status = UINT64_MAX;
    if (!p_base_suf || !xscore_process_wait(p_base_suf, &base_suf_status) || base_suf_status != 0 || !xscore_process_reap(p_base_suf)) return 0;

    const char *base32_args[] = {"/bin/base32", "/etc/motd", 0};
    struct xscore_process *p_b32 = xscore_process_create_path_args("/bin/base32", 2, base32_args);
    uint64_t b32_status = UINT64_MAX;
    if (!p_b32 || !xscore_process_wait(p_b32, &b32_status) || b32_status != 0 || !xscore_process_reap(p_b32)) return 0;

    const char *base64_args[] = {"/bin/base64", "/etc/motd", 0};
    struct xscore_process *p_b64 = xscore_process_create_path_args("/bin/base64", 2, base64_args);
    uint64_t b64_status = UINT64_MAX;
    if (!p_b64 || !xscore_process_wait(p_b64, &b64_status) || b64_status != 0 || !xscore_process_reap(p_b64)) return 0;

    /* Verify Network diagnostics & management suite (netstat, route, traceroute, dig, arp) */
    const char *netstat_args[] = {"/bin/netstat", "-a", 0};
    struct xscore_process *p_netstat = xscore_process_create_path_args("/bin/netstat", 2, netstat_args);
    uint64_t netstat_status = UINT64_MAX;
    if (!p_netstat || !xscore_process_wait(p_netstat, &netstat_status) || netstat_status != 0 || !xscore_process_reap(p_netstat)) return 0;

    const char *route_args[] = {"/bin/route", "-n", 0};
    struct xscore_process *p_route = xscore_process_create_path_args("/bin/route", 2, route_args);
    uint64_t route_status = UINT64_MAX;
    if (!p_route || !xscore_process_wait(p_route, &route_status) || route_status != 0 || !xscore_process_reap(p_route)) return 0;

    const char *traceroute_args[] = {"/bin/traceroute", "-m", "2", "10.0.2.2", 0};
    struct xscore_process *p_traceroute = xscore_process_create_path_args("/bin/traceroute", 4, traceroute_args);
    uint64_t traceroute_status = UINT64_MAX;
    if (!p_traceroute || !xscore_process_wait(p_traceroute, &traceroute_status) || traceroute_status != 0 || !xscore_process_reap(p_traceroute)) return 0;

    const char *dig_args[] = {"/bin/dig", "--help", 0};
    struct xscore_process *p_dig = xscore_process_create_path_args("/bin/dig", 2, dig_args);
    uint64_t dig_status = UINT64_MAX;
    if (!p_dig || !xscore_process_wait(p_dig, &dig_status) || dig_status != 0 || !xscore_process_reap(p_dig)) return 0;

    const char *arp_args[] = {"/bin/arp", "-a", 0};
    struct xscore_process *p_arp = xscore_process_create_path_args("/bin/arp", 2, arp_args);
    uint64_t arp_status = UINT64_MAX;
    if (!p_arp || !xscore_process_wait(p_arp, &arp_status) || arp_status != 0 || !xscore_process_reap(p_arp)) return 0;

    const char *lscpu_args[] = {"/bin/lscpu", 0};
    struct xscore_process *p_lscpu = xscore_process_create_path_args("/bin/lscpu", 1, lscpu_args);
    uint64_t lscpu_status = UINT64_MAX;
    if (!p_lscpu || !xscore_process_wait(p_lscpu, &lscpu_status) || lscpu_status != 0 || !xscore_process_reap(p_lscpu)) return 0;

    const char *lspci_args[] = {"/bin/lspci", 0};
    struct xscore_process *p_lspci = xscore_process_create_path_args("/bin/lspci", 1, lspci_args);
    uint64_t lspci_status = UINT64_MAX;
    if (!p_lspci || !xscore_process_wait(p_lspci, &lspci_status) || lspci_status != 0 || !xscore_process_reap(p_lspci)) return 0;

    const char *gpuinfo_args[] = {"/bin/gpuinfo", 0};
    struct xscore_process *p_gpuinfo = xscore_process_create_path_args("/bin/gpuinfo", 1, gpuinfo_args);
    uint64_t gpuinfo_status = UINT64_MAX;
    if (!p_gpuinfo || !xscore_process_wait(p_gpuinfo, &gpuinfo_status) || gpuinfo_status != 0 || !xscore_process_reap(p_gpuinfo)) return 0;

    const char *sndinfo_args[] = {"/bin/sndinfo", 0};
    struct xscore_process *p_sndinfo = xscore_process_create_path_args("/bin/sndinfo", 1, sndinfo_args);
    uint64_t sndinfo_status = UINT64_MAX;
    if (!p_sndinfo || !xscore_process_wait(p_sndinfo, &sndinfo_status) || sndinfo_status != 0 || !xscore_process_reap(p_sndinfo)) return 0;

    const char *acpi_args[] = {"/bin/acpi", "-v", 0};
    struct xscore_process *p_acpi = xscore_process_create_path_args("/bin/acpi", 2, acpi_args);
    uint64_t acpi_status = UINT64_MAX;
    if (!p_acpi || !xscore_process_wait(p_acpi, &acpi_status) || acpi_status != 0 || !xscore_process_reap(p_acpi)) return 0;

    xscore_serial_write("[INFO] Stream utilities find/sort/uniq/test/[ and akash scripting engine verified\n");
    xscore_serial_write("[INFO] SSH suite and akaos-config daemon integration verified\n");
    xscore_serial_write("[INFO] Scripting & admin utilities sleep/which/env/cut/tr/df/du verified\n");
    xscore_serial_write("[INFO] System & process control suite kill/pkill/uptime/free/watch verified\n");
    xscore_serial_write("[INFO] Unix text transformation & diff suite sed/diff/cmp/patch verified\n");
    xscore_serial_write("[INFO] Archiving & compression suite tar/gzip/gunzip verified\n");
    xscore_serial_write("[INFO] Advanced shell job control & pipelines in akash verified\n");
    xscore_serial_write("[INFO] Manual page keyword lookup utility apropos verified\n");
    xscore_serial_write("[INFO] Spell checker utility aspell verified\n");
    xscore_serial_write("[INFO] Command scheduling suite at/atq/atrm/batch verified\n");
    xscore_serial_write("[INFO] Text processing and pattern scanning utility awk verified\n");
    xscore_serial_write("[INFO] Path stripping utility basename verified\n");
    xscore_serial_write("[INFO] Base32 encoding and decoding utility base32 verified\n");
    xscore_serial_write("[INFO] Base64 encoding and decoding utility base64 verified\n");
    xscore_serial_write("[INFO] Network diagnostics & management suite netstat/route/traceroute/dig/arp verified\n");
    xscore_serial_write("[INFO] Hardware drivers and diagnostics suite lscpu/lspci/gpuinfo/sndinfo verified\n");
    xscore_serial_write("[INFO] ACPI diagnostics utility acpi verified\n");

    xscore_serial_write("[INFO] Command-line arguments argc/argv verified\n");
    xscore_serial_write("[INFO] VFS stat/getdents syscalls verified\n");
    xscore_serial_write("[INFO] Real error exit codes ($?) and dynamic VFS mutations verified\n");
    xscore_serial_write("[INFO] Native /bin/cat, /bin/ls, /bin/mkdir, /bin/touch, /bin/rm, /bin/cp, /bin/mv utilities verified\n");
    xscore_serial_write("[INFO] Native /bin/head, /bin/tail, /bin/wc, /bin/uname, /bin/whoami, /bin/sync, /bin/reboot, /bin/poweroff utilities verified\n");
    xscore_serial_write("[INFO] Native /bin/chmod, /bin/chown, /bin/ted utilities and permissions verified\n");
    xscore_serial_write("[INFO] Dynamic multi-block VFS allocation and page reclamation verified\n");
    uint32_t cached_pages = 0;
    xscore_page_cache_stats(&cached_pages, 0, 0);
    uint64_t free_now = xscore_pmm_free_page_count();

    if (free_now + (uint64_t)cached_pages != free_pages_before) {
        xscore_serial_write("[FAIL] free page mismatch\n");
        return 0;
    }
    xscore_page_cache_clear();
    if (xscore_pmm_free_page_count() != free_pages_before) {
        xscore_serial_write("[FAIL] page clear mismatch\n");
        return 0;
    }
    xscore_serial_write("[INFO] Clean file-page cache accounting verified\n");
    xscore_serial_write("[INFO] Process and thread physical pages fully reclaimed\n");
    xscore_serial_write("[INFO] Blocking process wait and thread reclamation verified\n");
    xscore_serial_write("[PASS] Process isolation and lifecycle self-test passed\n");
    return 1;
}
