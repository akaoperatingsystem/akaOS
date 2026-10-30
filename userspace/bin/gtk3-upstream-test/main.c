#include <gtk/gtk.h>
#include <dirent.h>
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GtkWidget *window;
static GtkWidget *content_box;
static GtkWidget *button;
static GtkWidget *label;
static unsigned int layout_attempts;
static int layout_ready;
static int button_clicked;

static int report_loaded_module(struct dl_phdr_info *info, size_t size,
                                void *user_data) {
    (void)size;
    (void)user_data;
    const char *name = info->dlpi_name && info->dlpi_name[0]
        ? info->dlpi_name : "<main>";
    printf("gtk3-wayland-module=%s base=%p\n", name,
           (void *)(uintptr_t)info->dlpi_addr);
    return 0;
}

static void inspect_cursor_assets(void) {
    const char *cursor_directory = "/usr/share/icons/default/cursors";
    DIR *directory = opendir(cursor_directory);
    if (!directory) {
        puts("gtk3-wayland-cursor-directory-open-failed");
        return;
    }

    size_t cursor_entries = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        printf("gtk3-wayland-cursor-entry=%s type=%u\n",
               entry->d_name, entry->d_type);
        ++cursor_entries;
    }
    closedir(directory);
    printf("gtk3-wayland-cursor-entry-count=%zu\n", cursor_entries);

    FILE *cursor_file = fopen(
        "/usr/share/icons/default/cursors/nw-resize", "rb");
    if (!cursor_file) {
        puts("gtk3-wayland-cursor-open-failed");
        return;
    }
    unsigned char cursor_data[65536] = {0};
    size_t bytes_read = fread(cursor_data, 1, sizeof(cursor_data), cursor_file);
    int read_error = ferror(cursor_file);
    int toc_seek = fseek(cursor_file, 16L, SEEK_SET);
    unsigned char toc[12] = {0};
    size_t toc_read = toc_seek == 0 ? fread(toc, 1, sizeof(toc), cursor_file) : 0;
    int chunk_seek = -1;
    size_t chunk_read = 0;
    if (toc_read == sizeof(toc)) {
        uint32_t chunk_offset = (uint32_t)toc[8] |
                                ((uint32_t)toc[9] << 8U) |
                                ((uint32_t)toc[10] << 16U) |
                                ((uint32_t)toc[11] << 24U);
        chunk_seek = fseek(cursor_file, (long)chunk_offset, SEEK_SET);
        if (chunk_seek == 0) {
            unsigned char chunk_header[16];
            chunk_read = fread(chunk_header, 1, sizeof(chunk_header),
                               cursor_file);
        }
    }
    fclose(cursor_file);
    printf("gtk3-wayland-cursor-read=%zu error=%d toc=%d/%zu chunk=%d/%zu "
           "header=%02x%02x%02x%02x\n",
           bytes_read, read_error, toc_seek, toc_read, chunk_seek, chunk_read,
           cursor_data[0], cursor_data[1], cursor_data[2], cursor_data[3]);
}

static gboolean verify_layout(gpointer unused) {
    (void)unused;
    GtkAllocation window_allocation;
    GtkAllocation box_allocation;
    GtkAllocation button_allocation;
    GtkAllocation label_allocation;
    gint box_x = 0;
    gint box_y = 0;
    gint button_x = 0;
    gint button_y = 0;
    gint label_x = 0;
    gint label_y = 0;

    ++layout_attempts;
    if (!gtk_widget_get_mapped(window) ||
        !gtk_widget_get_mapped(content_box) ||
        !gtk_widget_get_mapped(button) || !gtk_widget_get_mapped(label))
        goto wait_or_fail;

    gtk_widget_get_allocation(window, &window_allocation);
    gtk_widget_get_allocation(content_box, &box_allocation);
    gtk_widget_get_allocation(button, &button_allocation);
    gtk_widget_get_allocation(label, &label_allocation);
    if (window_allocation.width <= 0 || window_allocation.height <= 0 ||
        box_allocation.width <= 0 || box_allocation.height <= 0 ||
        button_allocation.width <= 0 || button_allocation.height <= 0 ||
        label_allocation.width <= 0 || label_allocation.height <= 0 ||
        !gtk_widget_translate_coordinates(content_box, window, 0, 0,
                                          &box_x, &box_y) ||
        !gtk_widget_translate_coordinates(button, content_box, 0, 0,
                                          &button_x, &button_y) ||
        !gtk_widget_translate_coordinates(label, content_box, 0, 0,
                                          &label_x, &label_y))
        goto wait_or_fail;

    if (box_x < 0 || box_y < 0 ||
        box_x + box_allocation.width > window_allocation.width ||
        box_y + box_allocation.height > window_allocation.height ||
        button_x < 0 || button_y < 0 ||
        button_x + button_allocation.width > box_allocation.width ||
        button_y + button_allocation.height > box_allocation.height ||
        label_x < 0 || label_y < 0 ||
        label_x + label_allocation.width > box_allocation.width ||
        label_y + label_allocation.height > box_allocation.height) {
        fprintf(stderr,
                "GTK 3 Wayland layout escaped parent: "
                "window=%dx%d box=%d,%d %dx%d button=%d,%d %dx%d "
                "label=%d,%d %dx%d\n",
                window_allocation.width, window_allocation.height,
                box_x, box_y, box_allocation.width, box_allocation.height,
                button_x, button_y, button_allocation.width,
                button_allocation.height, label_x, label_y,
                label_allocation.width, label_allocation.height);
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }

    layout_ready = 1;
    printf("gtk3-wayland-widget-layout-verified "
           "window=%dx%d box=%dx%d button=%dx%d label=%dx%d\n",
           window_allocation.width, window_allocation.height,
           box_allocation.width, box_allocation.height,
           button_allocation.width, button_allocation.height,
           label_allocation.width, label_allocation.height);
    puts("gtk3-wayland-window-ready-for-input");
    fflush(stdout);
    return G_SOURCE_REMOVE;

wait_or_fail:
    if (layout_attempts >= 1200U) {
        puts("gtk3-wayland-widget-layout-timeout");
        fflush(stdout);
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void button_was_clicked(GtkButton *source, gpointer data) {
    (void)source;
    (void)data;
    button_clicked = 1;
    puts("gtk3-wayland-button-clicked");
    fflush(stdout);
    gtk_main_quit();
}

int main(int argc, char **argv) {
    puts("gtk3-wayland-main-enter");
    fflush(stdout);
    if (setenv("HOME", "/root", 1) != 0 ||
        setenv("USER", "root", 1) != 0 ||
        setenv("XDG_RUNTIME_DIR", "/run/user/0", 1) != 0 ||
        setenv("WAYLAND_DISPLAY", "wayland-0", 1) != 0 ||
        setenv("GDK_BACKEND", "wayland", 1) != 0 ||
        setenv("XCURSOR_PATH", "/usr/share/icons", 1) != 0 ||
        setenv("XCURSOR_THEME", "default", 1) != 0 ||
        setenv("AKAOS_XCURSOR_TRACE", "1", 1) != 0) {
        puts("gtk3-wayland-environment-setup-failed");
        return 1;
    }
    puts("gtk3-wayland-environment-ready");
    printf("gtk3-wayland-xcursor-path=%s theme=%s trace=%s\n",
           getenv("XCURSOR_PATH"), getenv("XCURSOR_THEME"),
           getenv("AKAOS_XCURSOR_TRACE"));
    fflush(stdout);
    inspect_cursor_assets();
    fflush(stdout);
    puts("gtk3-wayland-init-check-enter");
    fflush(stdout);
    if (!gtk_init_check(&argc, &argv)) {
        puts("gtk3-wayland-display-connect-failed");
        return 2;
    }
    puts("gtk3-wayland-init-check-returned");
    fflush(stdout);
    dl_iterate_phdr(report_loaded_module, NULL);
    fflush(stdout);
    GtkSettings *settings = gtk_settings_get_default();
    gchar *cursor_theme_name = NULL;
    if (settings) {
        g_object_get(settings, "gtk-cursor-theme-name", &cursor_theme_name,
                     NULL);
        printf("gtk3-wayland-cursor-theme-current=%s\n",
               cursor_theme_name ? cursor_theme_name : "(null)");
        fflush(stdout);
        g_free(cursor_theme_name);
    }

    puts("gtk3-wayland-window-create-enter");
    fflush(stdout);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    puts("gtk3-wayland-window-create-returned");
    fflush(stdout);
    content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    puts("gtk3-wayland-box-create-returned");
    fflush(stdout);
    button = gtk_button_new_with_label("Upstream GTK 3 through Lumen");
    puts("gtk3-wayland-button-create-returned");
    fflush(stdout);
    label = gtk_label_new("GTK 3.24 / akaOS Wayland");
    puts("gtk3-wayland-label-create-returned");
    fflush(stdout);
    printf("gtk3-wayland-private-pointers label=%p button=%p\n",
           (void *)GTK_LABEL(label)->priv,
           (void *)GTK_BUTTON(button)->priv);
    fflush(stdout);
    GtkWidget *button_child = gtk_bin_get_child(GTK_BIN(button));
    printf("gtk3-wayland-button-child-is-label=%d private=%p\n",
           GTK_IS_LABEL(button_child),
           GTK_IS_LABEL(button_child) ?
               (void *)GTK_LABEL(button_child)->priv : NULL);
    fflush(stdout);
    if (!window || !content_box || !button || !label) return 3;

    gtk_window_set_title(GTK_WINDOW(window), "akaOS GTK 3 Wayland test");
    gtk_window_set_default_size(GTK_WINDOW(window), 420, 240);
    gtk_widget_set_hexpand(content_box, TRUE);
    gtk_widget_set_vexpand(content_box, TRUE);
    gtk_widget_set_hexpand(button, TRUE);
    gtk_widget_set_vexpand(button, TRUE);
    gtk_box_pack_start(GTK_BOX(content_box), button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content_box), label, FALSE, FALSE, 8);
    gtk_container_add(GTK_CONTAINER(window), content_box);
    gtk_window_set_focus(GTK_WINDOW(window), button);
    g_signal_connect(button, "clicked", G_CALLBACK(button_was_clicked), NULL);

    puts("gtk3-wayland-widget-tree-ready");
    fflush(stdout);
    gtk_widget_show_all(window);
    puts("gtk3-wayland-show-all-returned");
    fflush(stdout);
    g_timeout_add(10U, verify_layout, NULL);
    puts("gtk3-wayland-main-loop-enter");
    fflush(stdout);
    gtk_main();

    gtk_widget_destroy(window);
    puts("gtk3-wayland-client-exit");
    fflush(stdout);
    if (!layout_ready) return 4;
    return button_clicked ? 0 : 5;
}
