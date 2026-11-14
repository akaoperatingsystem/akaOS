/*
 * NVRAM Variable Access (/dev/efivar & ProcFS) for XSCore
 */

#include <xscore/firmware.h>
#include <xscore/vfs.h>
#include <xscore/serial.h>
#include <stddef.h>
#include <string.h>

static size_t str_append_at(char *buf, size_t max, size_t off, const char *s) {
    if (off >= max) return off;
    while (*s && off + 1 < max) buf[off++] = *s++;
    buf[off] = '\0';
    return off;
}

static size_t num_append_at(char *buf, size_t max, size_t off, uint64_t val) {
    char tmp[32];
    int pos = 0;
    if (val == 0) tmp[pos++] = '0';
    else {
        while (val > 0) {
            tmp[pos++] = (char)('0' + (val % 10));
            val /= 10;
        }
    }
    while (pos > 0 && off + 1 < max) buf[off++] = tmp[--pos];
    buf[off] = '\0';
    return off;
}

static size_t hex_append_at(char *buf, size_t max, size_t off, uint64_t val, int digits) {
    static const char hex[] = "0123456789abcdef";
    off = str_append_at(buf, max, off, "0x");
    for (int i = (digits - 1) * 4; i >= 0; i -= 4) {
        if (off + 1 < max) {
            buf[off++] = hex[(val >> i) & 0xf];
        }
    }
    buf[off] = '\0';
    return off;
}

static size_t guid_append_at(char *buf, size_t max, size_t off, const struct efi_guid *g) {
    static const char hex[] = "0123456789ABCDEF";
    /* Format: 8-4-4-4-12 */
    for (int i = 7; i >= 0; i--) {
        if (off + 1 < max) buf[off++] = hex[(g->data1 >> (i * 4)) & 0xf];
    }
    if (off + 1 < max) buf[off++] = '-';
    for (int i = 3; i >= 0; i--) {
        if (off + 1 < max) buf[off++] = hex[(g->data2 >> (i * 4)) & 0xf];
    }
    if (off + 1 < max) buf[off++] = '-';
    for (int i = 3; i >= 0; i--) {
        if (off + 1 < max) buf[off++] = hex[(g->data3 >> (i * 4)) & 0xf];
    }
    if (off + 1 < max) buf[off++] = '-';
    for (int i = 0; i < 2; i++) {
        if (off + 1 < max) buf[off++] = hex[(g->data4[i] >> 4) & 0xf];
        if (off + 1 < max) buf[off++] = hex[g->data4[i] & 0xf];
    }
    if (off + 1 < max) buf[off++] = '-';
    for (int i = 2; i < 8; i++) {
        if (off + 1 < max) buf[off++] = hex[(g->data4[i] >> 4) & 0xf];
        if (off + 1 < max) buf[off++] = hex[g->data4[i] & 0xf];
    }
    buf[off] = '\0';
    return off;
}

size_t xscore_efivar_format_proc_list(char *buf, size_t max_size) {
    if (!buf || max_size == 0) return 0;
    buf[0] = '\0';
    size_t off = 0;

    off = str_append_at(buf, max_size, off, "=== akaOS UEFI NVRAM Variable Store ===\n");
    off = str_append_at(buf, max_size, off, "Firmware Mode : ");
    off = str_append_at(buf, max_size, off, xscore_firmware_is_uefi() ? "Native UEFI 2.x\n" : "Legacy BIOS Compatibility\n");
    off = str_append_at(buf, max_size, off, "Secure Boot   : ");
    off = str_append_at(buf, max_size, off, xscore_uefi_secure_boot_enabled() ? "Enabled (SetupMode=0)\n" : "Disabled\n");
    off = str_append_at(buf, max_size, off, "\nRegistered Variables:\n");

    char name[EFIVAR_MAX_NAME_LEN];
    name[0] = '\0';
    struct efi_guid guid;
    uint64_t nsz = sizeof(name);

    int count = 0;
    while (xscore_uefi_get_next_variable_name(&nsz, name, &guid) == 0) {
        count++;
        off = str_append_at(buf, max_size, off, "  [");
        off = num_append_at(buf, max_size, off, count);
        off = str_append_at(buf, max_size, off, "] ");
        off = str_append_at(buf, max_size, off, name);
        off = str_append_at(buf, max_size, off, " {");
        off = guid_append_at(buf, max_size, off, &guid);
        off = str_append_at(buf, max_size, off, "}\n");

        uint32_t attr = 0;
        uint64_t dsz = 0;
        (void)xscore_uefi_get_variable(name, &guid, &attr, &dsz, NULL);
        off = str_append_at(buf, max_size, off, "      Attr: ");
        off = hex_append_at(buf, max_size, off, attr, 8);
        off = str_append_at(buf, max_size, off, ", Size: ");
        off = num_append_at(buf, max_size, off, dsz);
        off = str_append_at(buf, max_size, off, " bytes\n");

        nsz = sizeof(name);
    }

    return off;
}

int xscore_efivar_init(void) {
    xscore_serial_write("[EFIVAR] Initialized /dev/efivar interface\n");
    return 0;
}
