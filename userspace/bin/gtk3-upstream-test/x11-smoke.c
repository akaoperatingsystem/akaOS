/* Guest probe for upstream GTK 3 on the akaOS X11 display server.
 *
 * The probe connects to the native X11 server with the unmodified upstream
 * GTK 3 library, builds a window with a button and a label, verifies that the
 * layout stays inside the window and then waits for a pointer and a keyboard
 * event.  Every stage prints a marker so the QEMU harness can verify that the
 * whole GTK path works.
 */
#include <gtk/gtk.h>
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
    if (!gtk_widget_translate_coordinates(content_box, window, 0, 0,
                                          &box_x, &box_y) ||
        !gtk_widget_translate_coordinates(button, content_box, 0, 0,
                                          &button_x, &button_y) ||
        !gtk_widget_translate_coordinates(label, content_box, 0, 0,
                                          &label_x, &label_y))
        goto wait_or_fail;

    if (window_allocation.width <= 0 || window_allocation.height <= 0 ||
        box_allocation.width <= 0 || box_allocation.height <= 0 ||
        button_allocation.width <= 0 || button_allocation.height <= 0 ||
        label_allocation.width <= 0 || label_allocation.height <= 0) {
        goto wait_or_fail;
    }

    layout_ready = 1;
    printf("gtk3-x11-widget-layout-verified window=%dx%d box=%dx%d "
           "button=%dx%d label=%dx%d\n",
           window_allocation.width, window_allocation.height,
           box_allocation.width, box_allocation.height,
           button_allocation.width, button_allocation.height,
           label_allocation.width, label_allocation.height);
    printf("gtk3-x11-window-ready-for-input\n");
    fflush(stdout);
    return G_SOURCE_REMOVE;

wait_or_fail:
    if (layout_attempts >= 1200U) {
        puts("gtk3-x11-widget-layout-timeout");
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
    puts("gtk3-x11-button-clicked");
    fflush(stdout);
    gtk_main_quit();
}

int main(int argc, char **argv) {
    puts("gtk3-x11-main-enter");
    fflush(stdout);
    if (setenv("HOME", "/root", 1) != 0 ||
        setenv("USER", "root", 1) != 0 ||
        setenv("XDG_RUNTIME_DIR", "/run/user/0", 1) != 0 ||
        setenv("DISPLAY", ":0", 1) != 0 ||
        setenv("GDK_BACKEND", "x11", 1) != 0 ||
        setenv("NO_AT_BRIDGE", "1", 1) != 0) {
        puts("gtk3-x11-environment-setup-failed");
        return 1;
    }
    puts("gtk3-x11-environment-ready");
    fflush(stdout);

    if (!gtk_init_check(&argc, &argv)) {
        puts("gtk3-x11-display-connect-failed");
        fflush(stdout);
        return 2;
    }
    puts("gtk3-x11-init-check-returned");
    fflush(stdout);

    GdkDisplay *display = gdk_display_get_default();
    if (display) {
        printf("gtk3-x11-display-name=%s\n", gdk_display_get_name(display));
        fflush(stdout);
    }

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    button = gtk_button_new_with_label("Upstream GTK 3 on akaOS X11");
    label = gtk_label_new("GTK 3.24.52 / akaOS X11 server");
    if (!window || !content_box || !button || !label) {
        puts("gtk3-x11-widget-create-failed");
        return 3;
    }

    gtk_window_set_title(GTK_WINDOW(window), "akaOS GTK 3 X11 test");
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

    puts("gtk3-x11-widget-tree-ready");
    fflush(stdout);
    gtk_widget_show_all(window);
    puts("gtk3-x11-show-all-returned");
    fflush(stdout);
    g_timeout_add(10U, verify_layout, NULL);
    puts("gtk3-x11-main-loop-enter");
    fflush(stdout);
    gtk_main();

    gtk_widget_destroy(window);
    puts("gtk3-x11-client-exit");
    fflush(stdout);
    if (!layout_ready) return 4;
    return button_clicked ? 0 : 5;
}
