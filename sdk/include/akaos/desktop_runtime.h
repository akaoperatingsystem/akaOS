#ifndef AKAOS_DESKTOP_RUNTIME_H
#define AKAOS_DESKTOP_RUNTIME_H

/*
 * Small, freestanding desktop-runtime ABI exported by akaOS' compatibility
 * shared objects. The akaos_ names are intentionally explicit: this bounded
 * ABI is not the complete upstream GLib/GTK/Qt ABI.
 */
#include <stddef.h>
#include <stdint.h>

#define AKAOS_DESKTOP_RUNTIME_ABI 2U

struct akaos_runtime_window {
    uint32_t id;
    uint32_t visible;
    int32_t width;
    int32_t height;
    char title[96];
};

/* A small, C-callable widget boundary used by the native GTK/Qt source-port
 * layer. The objects intentionally have no toolkit-specific layout ABI; a
 * port can build its normal widget tree and hand the resulting root to the
 * akaOS bridge. */
enum akaos_runtime_widget_kind {
    AKAOS_RUNTIME_WIDGET_CONTAINER = 1,
    AKAOS_RUNTIME_WIDGET_BOX = 2,
    AKAOS_RUNTIME_WIDGET_LABEL = 3,
    AKAOS_RUNTIME_WIDGET_BUTTON = 4,
    AKAOS_RUNTIME_WIDGET_ENTRY = 5,
    AKAOS_RUNTIME_WIDGET_CHECK_BUTTON = 6,
    AKAOS_RUNTIME_WIDGET_GRID = 7,
    AKAOS_RUNTIME_WIDGET_SCROLL = 8,
};

typedef unsigned (*akaos_runtime_source_fn)(void *data);
typedef void (*GDestroyNotify)(void *data);
typedef void (*akaos_qt_slot_fn)(void *instance, void *data);
typedef unsigned (*GUnixFDSourceFunc)(int fd, unsigned condition,
                                      void *data);
typedef unsigned (*GIOFunc)(void *channel, unsigned condition, void *data);

#define G_IO_IN     0x0001U
#define G_IO_OUT    0x0004U
#define G_IO_ERR    0x0008U
#define G_IO_HUP    0x0010U
#define G_IO_NVAL   0x0020U

/* GLib-shaped bounded memory helpers exported by the native runtime. */
void *g_malloc(size_t size);
void *g_malloc0(size_t size);
void *g_realloc(void *memory, size_t size);
void g_free(void *memory);
char *g_strdup(const char *text);
char *g_strndup(const char *text, size_t length);
void *g_memdup2(const void *memory, size_t size);

/* Bounded GLib collection-shaped containers.  The concrete layouts remain
 * private to the runtime DSO; callers use the normal opaque pointer API. */
typedef struct _GArray GArray;
typedef struct _GPtrArray GPtrArray;
typedef struct _GString GString;
typedef struct _GHashTable GHashTable;
typedef struct _GList GList;
typedef struct _GSList GSList;
typedef struct _GBytes GBytes;
typedef struct _GVariant GVariant;
typedef struct _GVariantBuilder GVariantBuilder;
typedef struct _GSettings GSettings;
typedef struct _GFile GFile;
typedef struct _GFileInfo GFileInfo;
typedef struct _GFileEnumerator GFileEnumerator;
typedef struct _GBinding GBinding;
typedef struct _GCancellable GCancellable;
typedef struct _GTask GTask;
typedef struct _GError GError;
typedef struct _GSimpleAction GSimpleAction;
typedef void (*GAsyncReadyCallback)(void *source_object, void *result,
                                    void *user_data);
/* Bounded, copy-in GValue representation used by the native GObject bridge.
 * The fixed storage keeps values safe across independently loaded DSOs. */
typedef struct _GValue {
    uint32_t initialized;
    char type_string[4];
    int64_t signed_value;
    uint64_t unsigned_value;
    char string_value[96];
} GValue;
typedef unsigned (*GHashFunc)(const void *key);
typedef int (*GEqualFunc)(const void *left, const void *right);
enum {
    G_FILE_TYPE_UNKNOWN = 0,
    G_FILE_TYPE_REGULAR = 1,
    G_FILE_TYPE_DIRECTORY = 2,
    G_FILE_TYPE_SYMBOLIC_LINK = 3,
    G_FILE_TYPE_SPECIAL = 4,
};
GArray *g_array_new(int zero_terminated, int clear, unsigned element_size);
GArray *g_array_append_vals(GArray *array, const void *values, size_t count);
GArray *g_array_set_size(GArray *array, size_t length);
size_t g_array_get_length(const GArray *array);
unsigned g_array_get_element_size(const GArray *array);
void *g_array_free(GArray *array, int free_segment);
GPtrArray *g_ptr_array_new(void);
int g_ptr_array_add(GPtrArray *array, void *value);
size_t g_ptr_array_get_length(const GPtrArray *array);
void *g_ptr_array_index(const GPtrArray *array, size_t index);
void **g_ptr_array_free(GPtrArray *array, int free_segment);
GString *g_string_new(const char *initial);
GString *g_string_append(GString *string, const char *text);
GString *g_string_append_len(GString *string, const char *text, size_t length);
GString *g_string_truncate(GString *string, size_t length);
char *g_string_free(GString *string, int free_segment);
GHashTable *g_hash_table_new(GHashFunc hash_func, GEqualFunc equal_func);
int g_hash_table_insert(GHashTable *table, void *key, void *value);
void *g_hash_table_lookup(const GHashTable *table, const void *key);
int g_hash_table_contains(const GHashTable *table, const void *key);
int g_hash_table_remove(GHashTable *table, const void *key);
size_t g_hash_table_size(const GHashTable *table);
void g_hash_table_destroy(GHashTable *table);
unsigned g_str_hash(const void *key);
int g_str_equal(const void *left, const void *right);
GList *g_list_append(GList *list, void *data);
GList *g_list_prepend(GList *list, void *data);
GList *g_list_remove(GList *list, const void *data);
size_t g_list_length(const GList *list);
void *g_list_nth_data(const GList *list, size_t index);
void g_list_free(GList *list);
GSList *g_slist_append(GSList *list, void *data);
GSList *g_slist_prepend(GSList *list, void *data);
GSList *g_slist_remove(GSList *list, const void *data);
size_t g_slist_length(const GSList *list);
void *g_slist_nth_data(const GSList *list, size_t index);
void g_slist_free(GSList *list);
GBytes *g_bytes_new(const void *data, size_t size);
GBytes *g_bytes_ref(GBytes *bytes);
void g_bytes_unref(GBytes *bytes);
const void *g_bytes_get_data(const GBytes *bytes, size_t *size);
size_t g_bytes_get_size(const GBytes *bytes);
int g_bytes_equal(const GBytes *left, const GBytes *right);
GVariant *g_variant_new_string(const char *value);
GVariant *g_variant_new_boolean(int value);
GVariant *g_variant_new_uint32(uint32_t value);
GVariant *g_variant_new_int32(int32_t value);
GVariant *g_variant_new_uint64(uint64_t value);
GVariant *g_variant_new_int64(int64_t value);
GVariant *g_variant_new(const char *format_string, ...);
GVariant *g_variant_new_tuple(const GVariant *const children[], size_t count);
GVariant *g_variant_new_array(const GVariant *const children[], size_t count);
GVariant *g_variant_new_dict_entry(const GVariant *key, const GVariant *value);
GVariant *g_variant_new_variant(const GVariant *value);
GVariant *g_variant_get_variant(const GVariant *value);
GVariant *g_variant_get_key(const GVariant *entry);
GVariant *g_variant_get_value(const GVariant *entry);
GVariant *g_variant_lookup_value(const GVariant *container,
                                 const char *key, const char *type_string);
GVariantBuilder *g_variant_builder_new(const char *type_string);
int g_variant_builder_add_value(GVariantBuilder *builder, GVariant *value);
int g_variant_builder_add(GVariantBuilder *builder, const char *format_string, ...);
GVariant *g_variant_builder_end(GVariantBuilder *builder);
void g_variant_builder_clear(GVariantBuilder *builder);
GVariant *g_variant_ref(GVariant *value);
void g_variant_unref(GVariant *value);
size_t g_variant_n_children(const GVariant *value);
GVariant *g_variant_get_child_value(const GVariant *value, size_t index);
const char *g_variant_get_string(const GVariant *value, size_t *length);
int g_variant_get_boolean(const GVariant *value);
uint32_t g_variant_get_uint32(const GVariant *value);
int32_t g_variant_get_int32(const GVariant *value);
uint64_t g_variant_get_uint64(const GVariant *value);
int64_t g_variant_get_int64(const GVariant *value);
const char *g_variant_get_type_string(const GVariant *value);
int g_variant_is_of_type(const GVariant *value, const char *type_string);
GSettings *g_settings_new(const char *schema_id);
GSettings *g_settings_ref(GSettings *settings);
void g_settings_unref(GSettings *settings);
GVariant *g_settings_get_value(const GSettings *settings, const char *key);
int g_settings_set_value(GSettings *settings, const char *key,
                         const GVariant *value);
char *g_settings_get_string(const GSettings *settings, const char *key);
int g_settings_set_string(GSettings *settings, const char *key,
                          const char *value);
int g_settings_get_boolean(const GSettings *settings, const char *key);
int g_settings_set_boolean(GSettings *settings, const char *key, int value);
void g_value_init(GValue *value, const char *type_string);
void g_value_unset(GValue *value);
void g_value_set_string(GValue *value, const char *text);
const char *g_value_get_string(const GValue *value);
void g_value_set_boolean(GValue *value, int state);
int g_value_get_boolean(const GValue *value);
void g_value_set_int(GValue *value, int value_int);
int g_value_get_int(const GValue *value);
void g_value_set_uint(GValue *value, unsigned value_uint);
unsigned g_value_get_uint(const GValue *value);
int g_object_set_property(void *object, const char *property_name,
                          const GValue *value);
int g_object_get_property(const void *object, const char *property_name,
                          GValue *value);
/* Common GLib convenience forms.  The bounded bridge accepts the scalar and
 * string properties listed by the native runtime; the final argument is a
 * NULL property name, matching the usual g_object_set/get call shape. */
void g_object_set(void *object, const char *first_property_name, ...);
void g_object_get(const void *object, const char *first_property_name, ...);
GBinding *g_object_bind_property(void *source, const char *source_property,
                                 void *target, const char *target_property,
                                 unsigned flags);
GBinding *g_binding_ref(GBinding *binding);
void g_binding_unbind(GBinding *binding);
GCancellable *g_cancellable_new(void);
GCancellable *g_cancellable_ref(GCancellable *cancellable);
void g_cancellable_unref(GCancellable *cancellable);
void g_cancellable_cancel(GCancellable *cancellable);
int g_cancellable_is_cancelled(const GCancellable *cancellable);
GError *g_error_new(uint32_t domain, int code, const char *message);
GError *g_error_copy(const GError *error);
void g_error_free(GError *error);
int g_error_matches(const GError *error, uint32_t domain, int code);
uint32_t g_error_get_domain(const GError *error);
int g_error_get_code(const GError *error);
const char *g_error_get_message(const GError *error);
GTask *g_task_new(void *source_object, GCancellable *cancellable,
                  GAsyncReadyCallback callback, void *user_data);
GTask *g_task_ref(GTask *task);
void g_task_unref(GTask *task);
void *g_task_get_source_object(const GTask *task);
int g_task_get_completed(const GTask *task);
void g_task_return_boolean(GTask *task, int result);
void g_task_return_error(GTask *task, GError *error);
int g_task_return_error_if_cancelled(GTask *task);
int g_task_propagate_boolean(const GTask *task, void **error);
GFile *g_file_new_for_path(const char *path);
GFile *g_file_new_for_uri(const char *uri);
GFile *g_file_ref(GFile *file);
void g_file_unref(GFile *file);
char *g_file_get_path(const GFile *file);
char *g_file_get_uri(const GFile *file);
char *g_file_get_basename(const GFile *file);
GFile *g_file_get_parent(const GFile *file);
int g_file_equal(const GFile *left, const GFile *right);
int g_file_has_prefix(const GFile *file, const GFile *prefix);
GFileEnumerator *g_file_enumerate_children(
    GFile *file, const char *attributes, int flags, GCancellable *cancellable,
    GError **error);
GFileEnumerator *g_file_enumerator_ref(GFileEnumerator *enumerator);
void g_file_enumerator_unref(GFileEnumerator *enumerator);
GFile *g_file_enumerator_get_container(const GFileEnumerator *enumerator);
GFileInfo *g_file_enumerator_next_file(GFileEnumerator *enumerator,
                                       GCancellable *cancellable,
                                       GError **error);
int g_file_enumerator_close(GFileEnumerator *enumerator,
                            GCancellable *cancellable, GError **error);
GFileInfo *g_file_info_ref(GFileInfo *info);
void g_file_info_unref(GFileInfo *info);
const char *g_file_info_get_name(const GFileInfo *info);
const char *g_file_info_get_display_name(const GFileInfo *info);
int g_file_info_get_file_type(const GFileInfo *info);
uint64_t g_file_info_get_size(const GFileInfo *info);
uint64_t g_file_info_get_attribute_uint64(const GFileInfo *info,
                                          const char *attribute);
const char *g_file_info_get_attribute_string(const GFileInfo *info,
                                             const char *attribute);
int g_file_get_contents(const char *filename, char **contents, size_t *length,
                        GError **error);
int g_file_set_contents(const char *filename, const char *contents, long length,
                        GError **error);
int g_file_query_exists(const GFile *file, GCancellable *cancellable);
void g_file_load_contents_async(GFile *file, GCancellable *cancellable,
                                GAsyncReadyCallback callback, void *user_data);
int g_file_load_contents_finish(GFile *file, GTask *result, char **contents,
                                size_t *length, char **etag_out,
                                GError **error);

void *akaos_runtime_loop_new(void);
void *akaos_runtime_loop_ref(void *loop);
void akaos_runtime_loop_unref(void *loop);
void akaos_runtime_loop_run(void *loop);
void akaos_runtime_loop_quit(void *loop);
int akaos_runtime_loop_running(void *loop);
unsigned akaos_runtime_timeout_add(unsigned interval_ms,
                                   akaos_runtime_source_fn callback,
                                   void *data);
int akaos_runtime_source_remove(unsigned source_id);

void *akaos_runtime_window_new(void);
int akaos_runtime_window_set_title(void *window, const char *title);
int akaos_runtime_window_set_size(void *window, int width, int height);
int akaos_runtime_window_show(void *window);
int akaos_runtime_window_hide(void *window);
int akaos_runtime_window_destroy(void *window);
void *gtk_window_new(int type);
void gtk_window_set_title(void *window, const char *title);
void gtk_window_set_default_size(void *window, int width, int height);
void gtk_window_set_resizable(void *window, int resizable);
int gtk_window_get_resizable(const void *window);
void gtk_window_maximize(void *window);
void gtk_window_unmaximize(void *window);
int gtk_window_is_maximized(const void *window);
void gtk_window_minimize(void *window);
void gtk_window_unminimize(void *window);
int gtk_window_is_minimized(const void *window);
void gtk_window_close(void *window);
void gtk_window_present(void *window);

void *akaos_runtime_widget_new(unsigned kind);
int akaos_runtime_widget_add(void *parent, void *child);
int akaos_runtime_widget_remove(void *parent, void *child);
int akaos_runtime_widget_set_text(void *widget, const char *text);
int akaos_runtime_widget_set_size_request(void *widget, int width, int height);
int akaos_runtime_widget_get_size(void *widget, int *width, int *height);
int akaos_runtime_widget_get_visible(void *widget);
void *akaos_runtime_widget_get_parent(void *widget);
int akaos_runtime_widget_set_name(void *widget, const char *name);
const char *akaos_runtime_widget_get_name(void *widget);

/* Common GTK-shaped constructors/tree operations. */
void *gtk_box_new(int orientation, int spacing);
void *gtk_application_new(const char *application_id, unsigned flags);
int g_application_register(void *application, void *cancellable, void **error);
void g_application_hold(void *application);
void g_application_release(void *application);
int g_application_get_is_registered(const void *application);
const char *g_application_get_application_id(const void *application);
void g_application_activate(void *application);
int g_application_run(void *application, int argc, char **argv);
void g_application_quit(void *application);
void *gtk_application_window_new(void *application);
GSimpleAction *g_simple_action_new(const char *name, const char *parameter_type);
GSimpleAction *g_simple_action_ref(GSimpleAction *action);
void g_simple_action_unref(GSimpleAction *action);
const char *g_action_get_name(const GSimpleAction *action);
void g_simple_action_set_enabled(GSimpleAction *action, int enabled);
int g_action_get_enabled(const GSimpleAction *action);
void g_action_activate(GSimpleAction *action, void *parameter);
int g_action_map_add_action(void *action_map, GSimpleAction *action);
int g_action_map_remove_action(void *action_map, const char *name);
GSimpleAction *g_action_map_lookup_action(const void *action_map,
                                           const char *name);
void *g_main_loop_new(void *context, int running);
void g_main_loop_run(void *loop);
void g_main_loop_quit(void *loop);
void *g_main_loop_ref(void *loop);
void g_main_loop_unref(void *loop);
int g_main_loop_is_running(void *loop);
unsigned g_timeout_add(unsigned interval, unsigned (*callback)(void *),
                       void *data);
unsigned g_idle_add(unsigned (*callback)(void *), void *data);
unsigned g_timeout_add_full(int priority, unsigned interval,
                            unsigned (*callback)(void *), void *data,
                            GDestroyNotify destroy_notify);
unsigned g_idle_add_full(int priority, unsigned (*callback)(void *),
                         void *data, GDestroyNotify destroy_notify);
unsigned g_unix_fd_add(int fd, unsigned condition, GUnixFDSourceFunc callback,
                       void *data);
unsigned g_unix_fd_add_full(int priority, int fd, unsigned condition,
                            GUnixFDSourceFunc callback, void *data,
                            GDestroyNotify destroy_notify);
void *g_io_channel_unix_new(int fd);
void *g_io_channel_ref(void *channel);
void g_io_channel_unref(void *channel);
int g_io_channel_get_fd(void *channel);
unsigned g_io_add_watch(void *channel, unsigned condition, GIOFunc callback,
                        void *data);
unsigned g_io_add_watch_full(int priority, void *channel, unsigned condition,
                             GIOFunc callback, void *data,
                             GDestroyNotify destroy_notify);
int g_source_remove(unsigned source_id);
int g_main_context_iteration(void *context, int may_block);
int g_main_context_pending(void *context);
void *g_main_context_default(void);
void *g_main_context_new(void);
void *g_main_context_ref(void *context);
void g_main_context_unref(void *context);
void g_main_context_push_thread_default(void *context);
void g_main_context_pop_thread_default(void *context);
void gtk_main(void);
void gtk_main_quit(void);
void gtk_init(int *argc, char ***argv);
int gtk_init_check(int *argc, char ***argv);
void gtk_widget_show(void *widget);
void gtk_widget_show_all(void *widget);
void gtk_widget_hide(void *widget);
void gtk_widget_destroy(void *widget);
void *gtk_label_new(const char *text);
void *gtk_button_new_with_label(const char *text);
void *gtk_entry_new(void);
const char *gtk_entry_get_text(void *entry);
void gtk_entry_set_text(void *entry, const char *text);
void *gtk_check_button_new_with_label(const char *text);
void gtk_toggle_button_set_active(void *button, int active);
int gtk_toggle_button_get_active(void *button);
void *gtk_grid_new(void);
void gtk_grid_attach(void *grid, void *child, int column, int row,
                     int width, int height);
void gtk_container_add(void *parent, void *child);
void gtk_container_remove(void *parent, void *child);
void gtk_box_pack_start(void *box, void *child, int expand, int fill,
                        unsigned padding);
void gtk_widget_set_size_request(void *widget, int width, int height);
int gtk_widget_get_visible(void *widget);
void gtk_widget_set_visible(void *widget, int visible);
void gtk_widget_set_sensitive(void *widget, int sensitive);
int gtk_widget_get_sensitive(void *widget);
void *gtk_widget_get_parent(void *widget);
void gtk_widget_set_name(void *widget, const char *name);
const char *gtk_widget_get_name(void *widget);

/* A fixed callback ABI for source ports that only need activation signals.
 * It is deliberately explicit instead of pretending to implement GLib's
 * variadic marshaller ABI. */
unsigned g_signal_connect(void *instance, const char *detailed_signal,
                          void (*callback)(void *instance, void *data),
                          void *data);
int g_signal_emit_by_name(void *instance, const char *detailed_signal);
int g_signal_handler_disconnect(void *instance, unsigned handler_id);
unsigned g_signal_handlers_disconnect_by_func(
    void *instance, void (*callback)(void *instance, void *data), void *data);

void *g_object_ref(void *object);
void g_object_unref(void *object);
void *g_object_ref_sink(void *object);
int g_object_is_floating(void *object);
void g_object_set_data(void *object, const char *key, void *data);
void *g_object_get_data(const void *object, const char *key);
void *g_object_steal_data(void *object, const char *key);

/* C-facing Qt-shaped core/widget bridge. These entry points keep Qt-style
 * ports independent of C++ name mangling while preserving the bounded
 * runtime object and callback ownership rules. */
void *akaos_qt_application_new(void);
int akaos_qt_application_exec(void *application);
void akaos_qt_application_quit(void *application);
int akaos_qt_application_process_events(void *application, int iterations);
void *akaos_qt_object_new(void *parent);
int akaos_qt_object_set_property(void *object, const char *name,
                                 const char *value);
const char *akaos_qt_object_get_property(const void *object,
                                         const char *name);
unsigned akaos_qt_signal_connect(void *instance, const char *signal,
                                 akaos_qt_slot_fn callback, void *data);
int akaos_qt_signal_emit(void *instance, const char *signal);
int akaos_qt_signal_disconnect(void *instance, unsigned handler_id);
void *akaos_qt_widget_new(void);
int akaos_qt_widget_set_geometry(void *widget, int x, int y,
                                 int width, int height);
int akaos_qt_widget_get_geometry(const void *widget, int *x, int *y,
                                 int *width, int *height);
void akaos_qt_widget_set_title(void *widget, const char *title);
void akaos_qt_widget_show(void *widget);
void akaos_qt_widget_hide(void *widget);
int akaos_qt_widget_is_visible(const void *widget);
void akaos_qt_widget_set_enabled(void *widget, int enabled);
int akaos_qt_widget_is_enabled(const void *widget);
void akaos_qt_widget_destroy(void *widget);

#endif
