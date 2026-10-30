#include <gtk/gtk.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int run_gtk_wayland_window_smoke(void);
static int external_button_clicked;
static int external_key_pressed;
static int external_popup_closed;
static int external_key_after_outside_dismissal;
static int external_key_after_popup_closed;
static unsigned int external_popup_mapped_count;
static unsigned int external_popup_closed_count;
static unsigned int external_popup_unmapped_count;
static int external_popup_recreated;
static int external_popup_is_mapped;
static GtkWidget *external_popover;
static int trace_gtk_type_registration;

static void trace_gtk_stage(const char *stage) {
    const char *enabled = getenv("AKAOS_GTK_TRACE");
    if (!trace_gtk_type_registration &&
        (!enabled || !enabled[0] || enabled[0] == '0'))
        return;
    printf("gtk-upstream-stage: %s\n", stage);
    fflush(stdout);
}

static void external_button_was_clicked(GtkButton *button, gpointer data) {
    (void)button;
    (void)data;
    external_button_clicked = 1;
    puts("upstream-gtk-wayland-button-clicked");
    fflush(stdout);
    if (external_popover)
        gtk_popover_popup(GTK_POPOVER(external_popover));
}

static gboolean external_key_was_pressed(GtkEventControllerKey *controller,
                                         guint keyval, guint keycode,
                                         GdkModifierType state,
                                         gpointer data) {
    (void)controller;
    (void)keycode;
    (void)state;
    (void)data;
    if (keyval == (guint)'a') {
        external_key_pressed = 1;
        puts("upstream-gtk-wayland-key-pressed");
        fflush(stdout);
        return TRUE;
    }
    if (keyval == (guint)'b' && external_popup_closed) {
        external_key_after_popup_closed = 1;
        puts("upstream-gtk-wayland-key-after-popup-dismissed");
        fflush(stdout);
        return TRUE;
    }
    if (keyval == (guint)'c' && external_popup_closed_count >= 2U) {
        external_key_after_outside_dismissal = 1;
        puts("upstream-gtk-wayland-key-after-outside-dismissal");
        fflush(stdout);
        return TRUE;
    }
    return FALSE;
}

static void external_popup_was_closed(GtkPopover *popover, gpointer data) {
    (void)popover;
    (void)data;
    external_popup_closed = 1;
    ++external_popup_closed_count;
    if (external_popup_closed_count == 1U)
        puts("upstream-gtk-wayland-popup-closed-after-escape");
    else if (external_popup_closed_count == 2U)
        puts("upstream-gtk-wayland-popup-closed-after-outside-click");
    fflush(stdout);
}

static GtkWidget *create_external_popover(GtkWidget *button) {
    GtkWidget *popover = gtk_popover_new();
    if (!popover) return NULL;
    gtk_popover_set_child(GTK_POPOVER(popover),
                          gtk_label_new("Upstream GTK popup"));
    g_signal_connect(popover, "closed",
                     G_CALLBACK(external_popup_was_closed), NULL);
    gtk_widget_set_parent(popover, button);
    return popover;
}

int akaos_gtk_set_wayland_environment(const char *display_name, int debug,
                                      int trace) {
    static const char *const names[] = {
        "HOME", "PATH", "USER", "LOGNAME", "SHELL", "TERM",
        "XDG_RUNTIME_DIR", "WAYLAND_DISPLAY", "GDK_BACKEND",
        "GSK_RENDERER", "GTK_A11Y",
    };
    const char *const values[] = {
        "/root", "/bin:/usr/bin", "root", "root", "/bin/akash", "linux",
        "/run/user/0", display_name, "wayland", "cairo", "none",
    };
    for (size_t index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (trace) {
            fprintf(stderr, "GTK_ENV setenv %s\n", names[index]);
            fflush(stderr);
        }
        if (setenv(names[index], values[index], 1) != 0) {
            fprintf(stderr, "GTK_ENV setenv %s failed: errno=%d\n",
                    names[index], errno);
            fflush(stderr);
            return 0;
        }
    }
    if (debug &&
        (setenv("GDK_DEBUG", "misc", 1) != 0 ||
         setenv("WAYLAND_DEBUG", "1", 1) != 0)) {
        fprintf(stderr, "GTK_ENV debug setup failed: errno=%d\n", errno);
        fflush(stderr);
        return 0;
    }
    return 1;
}

static int run_environment_mutation_smoke(void) {
    if (setenv("XDG_RUNTIME_DIR", "/run/user/0", 1) != 0) {
        printf("GTK dynamic setenv first mutation failed: errno=%d\n", errno);
        return 1;
    }
    puts("gtk-dynamic-setenv-first-ok");
    const char *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!xdg_runtime_dir || strcmp(xdg_runtime_dir, "/run/user/0") != 0)
        return 2;
    if (setenv("WAYLAND_DISPLAY", "wayland-0", 1) != 0) {
        printf("GTK dynamic setenv second mutation failed: errno=%d\n", errno);
        return 3;
    }
    puts("gtk-dynamic-setenv-second-ok");
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    if (!wayland_display || strcmp(wayland_display, "wayland-0") != 0)
        return 4;
    int replace_result = setenv("XDG_RUNTIME_DIR", "/run/user/0/session", 1);
    xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (replace_result != 0 || !xdg_runtime_dir ||
        strcmp(xdg_runtime_dir, "/run/user/0/session") != 0 ||
        !wayland_display || strcmp(wayland_display, "wayland-0") != 0) {
        printf("GTK dynamic setenv replacement failed: result=%d errno=%d "
               "XDG=%s WAYLAND=%s\n", replace_result, errno,
               xdg_runtime_dir ? xdg_runtime_dir : "(null)",
               wayland_display ? wayland_display : "(null)");
        return 5;
    }
    puts("upstream-gtk-environment-mutation-ok");
    return 0;
}

static int run_gtk_external_wayland_smoke(void) {
    trace_gtk_stage("external-smoke-enter");
    if (!akaos_gtk_set_wayland_environment("wayland-0", 0, 0)) return 1;
    trace_gtk_stage("environment-ready");
    trace_gtk_stage("gtk-init-check-enter");
    int initialized = gtk_init_check();
    trace_gtk_stage("gtk-init-check-returned");
    if (!initialized) {
        puts("GTK could not open the running Lumen Wayland bridge");
        return 1;
    }
    trace_gtk_stage("gtk-initialized");
    trace_gtk_stage("generic-object-instance-enter");
    GObject *object_probe = g_object_new(G_TYPE_OBJECT, NULL);
    trace_gtk_stage("generic-object-instance-returned");
    if (!object_probe) return 2;
    g_object_unref(object_probe);
    trace_gtk_stage("label-type-query-enter");
    GType probe_label_type = gtk_label_get_type();
    trace_gtk_stage("label-type-query-returned");
    trace_gtk_stage("label-class-init-enter");
    GTypeClass *probe_label_class = g_type_class_get(probe_label_type);
    trace_gtk_stage("label-class-init-returned");
    if (!probe_label_class) return 2;
    trace_gtk_stage("label-instance-enter");
    GtkWidget *label_probe = g_object_new(probe_label_type, NULL);
    trace_gtk_stage("label-instance-returned");
    if (!label_probe) return 2;
    g_object_ref_sink(label_probe);
    g_object_unref(label_probe);
    trace_gtk_stage("window-type-query-enter");
    GType window_type = gtk_window_get_type();
    trace_gtk_stage("window-type-query-returned");
    trace_gtk_stage("window-class-init-enter");
    GTypeClass *window_class = g_type_class_get(window_type);
    trace_gtk_stage("window-class-init-returned");
    if (!window_class) return 2;
    trace_gtk_stage("window-instance-init-enter");
    GtkWidget *window = g_object_new(window_type, NULL);
    trace_gtk_stage("window-instance-init-returned");
    if (!window) return 2;
    trace_gtk_stage("window-created");
    external_button_clicked = 0;
    external_key_pressed = 0;
    external_popup_closed = 0;
    external_key_after_outside_dismissal = 0;
    external_key_after_popup_closed = 0;
    external_popup_mapped_count = 0U;
    external_popup_closed_count = 0U;
    external_popup_unmapped_count = 0U;
    external_popup_recreated = 0;
    external_popup_is_mapped = 0;
    external_popover = NULL;
    gtk_window_set_title(GTK_WINDOW(window), "akaOS GTK Wayland client");
    gtk_window_set_default_size(GTK_WINDOW(window), 420, 240);
    GtkWidget *button = gtk_button_new_with_label(
        "Upstream GTK rendered by Lumen");
    g_signal_connect(button, "clicked",
                     G_CALLBACK(external_button_was_clicked), NULL);
    GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *layout_label = gtk_label_new("GtkBox child layout verified");
    if (!content_box || !layout_label) {
        gtk_window_destroy(GTK_WINDOW(window));
        return 2;
    }
    gtk_widget_set_hexpand(content_box, TRUE);
    gtk_widget_set_vexpand(content_box, TRUE);
    gtk_widget_set_hexpand(button, TRUE);
    gtk_widget_set_vexpand(button, TRUE);
    gtk_box_append(GTK_BOX(content_box), button);
    gtk_box_append(GTK_BOX(content_box), layout_label);
    gtk_window_set_child(GTK_WINDOW(window), content_box);
    trace_gtk_stage("button-attached");
    external_popover = create_external_popover(button);
    if (!external_popover) {
        gtk_window_destroy(GTK_WINDOW(window));
        return 2;
    }
    GtkEventController *key_controller = gtk_event_controller_key_new();
    if (!key_controller) {
        gtk_window_destroy(GTK_WINDOW(window));
        return 2;
    }
    gtk_event_controller_set_propagation_phase(key_controller,
                                                GTK_PHASE_CAPTURE);
    g_signal_connect(key_controller, "key-pressed",
                     G_CALLBACK(external_key_was_pressed), NULL);
    gtk_widget_add_controller(window, key_controller);
    gtk_window_set_focus(GTK_WINDOW(window), button);
    trace_gtk_stage("window-ready-to-present");
    gtk_window_present(GTK_WINDOW(window));
    trace_gtk_stage("window-present-returned");

    for (unsigned attempt = 0U; attempt < 1200U; ++attempt) {
        while (g_main_context_iteration(NULL, FALSE)) { }
        if (gtk_widget_get_mapped(window) &&
            gtk_widget_get_mapped(content_box) &&
            gtk_widget_get_mapped(button) &&
            gtk_widget_get_mapped(layout_label) &&
            gtk_widget_get_width(window) > 0 &&
            gtk_widget_get_height(window) > 0 &&
            gtk_widget_get_width(content_box) > 0 &&
            gtk_widget_get_height(content_box) > 0 &&
            gtk_widget_get_width(button) > 0 &&
            gtk_widget_get_height(button) > 0 &&
            gtk_widget_get_width(layout_label) > 0 &&
            gtk_widget_get_height(layout_label) > 0)
            break;
        (void)usleep(10000U);
    }
    if (!gtk_widget_get_mapped(window)) {
        gtk_window_destroy(GTK_WINDOW(window));
        puts("GTK Wayland surface was not mapped by Lumen");
        return 3;
    }
    int window_width = gtk_widget_get_width(window);
    int window_height = gtk_widget_get_height(window);
    int box_width = gtk_widget_get_width(content_box);
    int box_height = gtk_widget_get_height(content_box);
    int button_width = gtk_widget_get_width(button);
    int button_height = gtk_widget_get_height(button);
    int label_width = gtk_widget_get_width(layout_label);
    int label_height = gtk_widget_get_height(layout_label);
    graphene_rect_t box_bounds;
    graphene_rect_t button_bounds;
    graphene_rect_t label_bounds;
    if (!gtk_widget_get_mapped(content_box) ||
        !gtk_widget_get_mapped(button) ||
        !gtk_widget_get_mapped(layout_label) || window_width <= 0 ||
        window_height <= 0 || box_width <= 0 || box_height <= 0 ||
        button_width <= 0 || button_height <= 0 || label_width <= 0 ||
        label_height <= 0 ||
        !gtk_widget_compute_bounds(content_box, window, &box_bounds) ||
        !gtk_widget_compute_bounds(button, window, &button_bounds) ||
        !gtk_widget_compute_bounds(layout_label, content_box, &label_bounds) ||
        box_bounds.origin.x < 0.0f || box_bounds.origin.y < 0.0f ||
        box_bounds.origin.x + box_bounds.size.width > (float)window_width ||
        box_bounds.origin.y + box_bounds.size.height > (float)window_height ||
        button_bounds.origin.x < box_bounds.origin.x ||
        button_bounds.origin.y < box_bounds.origin.y ||
        button_bounds.origin.x + button_bounds.size.width >
            box_bounds.origin.x + box_bounds.size.width ||
        button_bounds.origin.y + button_bounds.size.height >
            box_bounds.origin.y + box_bounds.size.height ||
        label_bounds.origin.x < 0.0f || label_bounds.origin.y < 0.0f ||
        label_bounds.origin.x + label_bounds.size.width > (float)box_width ||
        label_bounds.origin.y + label_bounds.size.height > (float)box_height) {
        fprintf(stderr,
                "GTK Wayland widget layout invalid: window=%dx%d "
                "box=%dx%d button=%dx%d label=%dx%d\n",
                window_width, window_height, box_width, box_height,
                button_width, button_height, label_width, label_height);
        if (external_popover && gtk_widget_get_parent(external_popover))
            gtk_widget_unparent(external_popover);
        external_popover = NULL;
        gtk_window_destroy(GTK_WINDOW(window));
        return 12;
    }
    printf("upstream-gtk-wayland-widget-layout-verified "
           "window=%dx%d box=%dx%d button=%dx%d label=%dx%d\n",
           window_width, window_height, box_width, box_height,
           button_width, button_height, label_width, label_height);
    fflush(stdout);
    puts("upstream-gtk-wayland-window-ready-for-input");
    fflush(stdout);

    /* Keep the process alive until compositor-routed keyboard and pointer
     * events reach GTK through the advertised Wayland seat and its popup
     * surface receives a compositor configure. */
    for (unsigned attempt = 0U; attempt < 3000U &&
         (!external_key_pressed || !external_button_clicked ||
          external_popup_mapped_count != 2U ||
          external_popup_closed_count != 2U ||
          external_popup_unmapped_count != 2U ||
          !external_key_after_popup_closed ||
          !external_key_after_outside_dismissal); ++attempt) {
        while (g_main_context_iteration(NULL, FALSE)) { }
        int popover_mapped = external_popover &&
                             gtk_widget_get_mapped(external_popover);
        if (popover_mapped && !external_popup_is_mapped) {
            external_popup_is_mapped = 1;
            ++external_popup_mapped_count;
            if (external_popup_mapped_count == 1U)
                puts("upstream-gtk-wayland-popup-mapped");
            else if (external_popup_mapped_count == 2U)
                puts("upstream-gtk-wayland-popup-reopened");
            fflush(stdout);
        } else if (!popover_mapped) {
            external_popup_is_mapped = 0;
            if (external_popup_unmapped_count <
                external_popup_closed_count) {
                external_popup_unmapped_count =
                    external_popup_closed_count;
                if (external_popup_unmapped_count == 1U)
                    puts("upstream-gtk-wayland-popup-unmapped-after-escape");
                else if (external_popup_unmapped_count == 2U)
                    puts("upstream-gtk-wayland-popup-unmapped-after-outside-click");
                fflush(stdout);
                if (external_popup_unmapped_count == 1U &&
                    !external_popup_recreated) {
                    GtkWidget *old_popover = external_popover;
                    external_popover = NULL;
                    if (old_popover && gtk_widget_get_parent(old_popover))
                        gtk_widget_unparent(old_popover);
                    external_popover = create_external_popover(button);
                    external_popup_recreated = external_popover != NULL;
                    puts(external_popup_recreated
                             ? "upstream-gtk-wayland-popup-ready-for-reopen"
                             : "upstream-gtk-wayland-popup-reopen-setup-failed");
                    fflush(stdout);
                }
            }
        }
        (void)usleep(10000U);
    }
    int clicked = external_button_clicked;
    int key_pressed = external_key_pressed;
    int popup_closed = external_popup_closed;
    unsigned int popup_mapped_count = external_popup_mapped_count;
    unsigned int popup_closed_count = external_popup_closed_count;
    unsigned int popup_unmapped_count = external_popup_unmapped_count;
    int key_after_popup_closed = external_key_after_popup_closed;
    int key_after_outside_dismissal = external_key_after_outside_dismissal;
    if (gtk_widget_get_parent(external_popover))
        gtk_widget_unparent(external_popover);
    gtk_window_destroy(GTK_WINDOW(window));
    while (g_main_context_iteration(NULL, FALSE)) { }
    puts("upstream-gtk-lumen-client-exit");
    if (!key_pressed) return 5;
    if (!clicked) return 4;
    if (popup_mapped_count != 2U) return 6;
    if (popup_closed_count != 2U) return 7;
    if (popup_unmapped_count != 2U) return 8;
    if (!popup_closed) return 9;
    if (!key_after_popup_closed) return 10;
    return key_after_outside_dismissal ? 0 : 11;
}

int main(int argc, char **argv) {
    trace_gtk_type_registration = argc == 2 &&
        strcmp(argv[1], "--types-only") == 0;
    trace_gtk_stage("main-enter");
    if (argc == 2 && strcmp(argv[1], "--environment-test") == 0)
        return run_environment_mutation_smoke();
    int external_wayland = argc == 2 &&
        strcmp(argv[1], "--external-wayland-smoke") == 0;
    int types_only = trace_gtk_type_registration;
    if (argc > 1 && !external_wayland && !types_only) {
        puts("Usage: gtk-upstream-test [--types-only|--environment-test|--external-wayland-smoke]");
        return 2;
    }
    trace_gtk_stage("gtk-version-query-enter");
    unsigned int major = gtk_get_major_version();
    unsigned int minor = gtk_get_minor_version();
    unsigned int micro = gtk_get_micro_version();
    trace_gtk_stage("gtk-version-query-returned");
    if (major != 4U) {
        printf("Unexpected GTK major version: %u\n", major);
        return 1;
    }

    trace_gtk_stage("widget-type-registration-enter");
    GType widget_type = gtk_widget_get_type();
    trace_gtk_stage("widget-type-registration-returned");
    GType label_type = gtk_label_get_type();
    trace_gtk_stage("label-type-registration-returned");
    GType button_type = gtk_button_get_type();
    trace_gtk_stage("button-type-registration-returned");
    const char *widget_name = g_type_name(widget_type);
    const char *label_name = g_type_name(label_type);
    const char *button_name = g_type_name(button_type);
    if (!widget_name || !label_name || !button_name ||
        strcmp(widget_name, "GtkWidget") != 0 ||
        strcmp(label_name, "GtkLabel") != 0 ||
        strcmp(button_name, "GtkButton") != 0 ||
        !g_type_is_a(label_type, widget_type) ||
        !g_type_is_a(button_type, widget_type)) {
        puts("GTK GObject widget registration or inheritance failed");
        return 2;
    }

    printf("Upstream GTK %u.%u.%u registered %s, %s, and %s\n",
           major, minor, micro, widget_name, label_name, button_name);
    puts("GTK GObject widget type registration verified");
    trace_gtk_stage("gobject-checks-passed");
    if (types_only) {
        puts("upstream-gtk-types-only-ok");
        return 0;
    }
    if (external_wayland) return run_gtk_external_wayland_smoke();
    if (run_gtk_wayland_window_smoke() != 0) return 3;
    puts("upstream-gtk-smoke-ok");
    return 0;
}
