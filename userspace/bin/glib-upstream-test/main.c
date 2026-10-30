#include <gio/gio.h>
#include <stdio.h>
#include <string.h>

static GMainLoop *probe_loop;

static gboolean stop_loop(gpointer data) {
    unsigned *dispatch_count = data;
    ++*dispatch_count;
    g_main_loop_quit(probe_loop);
    return G_SOURCE_REMOVE;
}

static gpointer thread_result(gpointer data) {
    return GINT_TO_POINTER(GPOINTER_TO_INT(data) + 1);
}

int main(void) {
    const gchar sample[] = "akaOS UTF-8: \xe2\x9c\x93";
    if (!g_utf8_validate(sample, -1, NULL)) return 1;

    GPtrArray *items = g_ptr_array_new_with_free_func(g_free);
    if (!items) return 2;
    g_ptr_array_add(items, g_strdup("upstream-glib"));
    g_ptr_array_add(items, g_strdup("upstream-gio"));
    if (items->len != 2 ||
        strcmp(g_ptr_array_index(items, 0), "upstream-glib") != 0) {
        g_ptr_array_unref(items);
        return 3;
    }

    GObject *object = g_object_new(G_TYPE_OBJECT, NULL);
    if (!object || !G_IS_OBJECT(object)) {
        g_ptr_array_unref(items);
        return 4;
    }

    GVariant *tuple = g_variant_ref_sink(g_variant_new("(si)", "akaOS", 90));
    if (!tuple || !g_variant_is_of_type(tuple, G_VARIANT_TYPE("(si)"))) {
        if (tuple) g_variant_unref(tuple);
        g_object_unref(object);
        g_ptr_array_unref(items);
        return 5;
    }
    const gchar *variant_name = NULL;
    gint variant_version = 0;
    g_variant_get(tuple, "(&si)", &variant_name, &variant_version);
    if (!variant_name || strcmp(variant_name, "akaOS") != 0 ||
        variant_version != 90) {
        g_variant_unref(tuple);
        g_object_unref(object);
        g_ptr_array_unref(items);
        return 6;
    }

    GFile *file = g_file_new_for_path("/etc/passwd");
    gchar *basename = file ? g_file_get_basename(file) : NULL;
    if (!basename || strcmp(basename, "passwd") != 0) {
        g_free(basename);
        if (file) g_object_unref(file);
        g_variant_unref(tuple);
        g_object_unref(object);
        g_ptr_array_unref(items);
        return 7;
    }

    GSimpleAction *action = g_simple_action_new("upstream-probe", NULL);
    if (!action || strcmp(g_action_get_name(G_ACTION(action)),
                          "upstream-probe") != 0) {
        if (action) g_object_unref(action);
        g_free(basename);
        g_object_unref(file);
        g_variant_unref(tuple);
        g_object_unref(object);
        g_ptr_array_unref(items);
        return 8;
    }

    unsigned dispatch_count = 0;
    probe_loop = g_main_loop_new(NULL, FALSE);
    if (!probe_loop) return 9;
    g_idle_add(stop_loop, &dispatch_count);
    g_main_loop_run(probe_loop);
    g_main_loop_unref(probe_loop);
    probe_loop = NULL;
    if (dispatch_count != 1) return 10;

    GThread *thread = g_thread_new("glib-upstream-probe", thread_result,
                                   GINT_TO_POINTER(41));
    if (!thread || GPOINTER_TO_INT(g_thread_join(thread)) != 42) return 11;

    g_print("Upstream GLib 2.90 utility, object, variant, and main context verified\n");
    g_print("Upstream GIO file/action APIs verified\n");
    g_print("Upstream GLib thread API verified\n");
    g_print("upstream-glib-smoke-ok\n");

    g_object_unref(action);
    g_free(basename);
    g_object_unref(file);
    g_variant_unref(tuple);
    g_object_unref(object);
    g_ptr_array_unref(items);
    return 0;
}
