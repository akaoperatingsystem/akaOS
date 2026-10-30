#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <akaos/fs.h>

/* Keep the shared object freestanding: the builder intentionally compiles
 * this file without an SDK include path. These values mirror the public
 * akaos/desktop_runtime.h widget-kind enum. */
enum runtime_widget_kind {
    AKAOS_RUNTIME_WIDGET_CONTAINER = 1,
    AKAOS_RUNTIME_WIDGET_BOX = 2,
    AKAOS_RUNTIME_WIDGET_LABEL = 3,
    AKAOS_RUNTIME_WIDGET_BUTTON = 4,
    AKAOS_RUNTIME_WIDGET_ENTRY = 5,
    AKAOS_RUNTIME_WIDGET_CHECK_BUTTON = 6,
    AKAOS_RUNTIME_WIDGET_GRID = 7,
    AKAOS_RUNTIME_WIDGET_SCROLL = 8,
};

/*
 * Small, dependency-free ABI marker and desktop runtime used by akaOS native
 * profile sessions. The common GLib/GTK-shaped entry points below are a
 * practical source-port foundation (event-loop scheduling, object lifetime,
 * and window lifecycle); they are deliberately not advertised as the full
 * upstream GLib/GTK/Qt ABI.
 */
#ifndef AKAOS_RUNTIME_ID
#define AKAOS_RUNTIME_ID 0U
#endif

uint32_t akaos_runtime_abi(void) {
    return 2U;
}

uint32_t akaos_runtime_family(void) {
    return AKAOS_RUNTIME_ID;
}

#define RUNTIME_LOOP_CAPACITY 4U
#define RUNTIME_CONTEXT_CAPACITY 2U
#define RUNTIME_SOURCE_CAPACITY 16U
#define RUNTIME_IO_CHANNEL_CAPACITY 8U
#define RUNTIME_IO_WATCH_CAPACITY 16U
#define RUNTIME_WINDOW_CAPACITY 16U
#define RUNTIME_WIDGET_CHILD_CAPACITY 8U
#define RUNTIME_SIGNAL_CAPACITY 4U
#define RUNTIME_GLOBAL_SIGNAL_CAPACITY 16U
#define RUNTIME_DATA_CAPACITY 8U
#define RUNTIME_PROPERTY_CAPACITY 24U
#define RUNTIME_BINDING_CAPACITY 16U
#define RUNTIME_HASH_CAPACITY 32U
#define RUNTIME_ACTION_CAPACITY 16U
#define RUNTIME_ACTION_MAP_CAPACITY 32U
#define RUNTIME_LIST_WALK_LIMIT 256U

#define RUNTIME_SOURCE_TIMER 0U
#define RUNTIME_SOURCE_FD 1U
#define RUNTIME_POLLIN  0x0001
#define RUNTIME_POLLOUT 0x0004
#define RUNTIME_POLLERR 0x0008
#define RUNTIME_POLLHUP 0x0010
#define RUNTIME_POLLNVAL 0x0020
#define RUNTIME_FD_POLL_SLICE_MS 100

struct runtime_pollfd {
    int32_t fd;
    int16_t events;
    int16_t revents;
};

extern long akaos_desktop_poll(struct runtime_pollfd *descriptors,
                               size_t count, int timeout_ms);

struct runtime_signal {
    uint32_t used;
    uint32_t id;
    void *instance;
    char name[32];
    void (*callback)(void *instance, void *data);
    void *data;
};

struct runtime_context {
    uint32_t used;
    uint32_t references;
};

struct runtime_io_channel {
    uint32_t used;
    uint32_t references;
    int32_t fd;
};

struct runtime_io_watch {
    uint32_t used;
    uint32_t source_id;
    struct runtime_io_channel *channel;
    unsigned (*callback)(void *channel, unsigned condition, void *data);
    void *data;
    void (*destroy_notify)(void *data);
};

struct runtime_loop {
    uint32_t used;
    uint32_t running;
    uint32_t references;
    struct runtime_context *context;
};

struct runtime_source {
    uint32_t used;
    uint32_t id;
    uint32_t kind;
    uint32_t interval_ms;
    uint32_t due_ms;
    uint32_t dispatching;
    uint32_t removed;
    uint32_t dispatch_epoch;
    struct runtime_context *context;
    unsigned (*callback)(void *data);
    unsigned (*io_callback)(int fd, unsigned condition, void *data);
    int32_t fd;
    int16_t events;
    int16_t revents;
    void *data;
    void (*destroy_notify)(void *data);
};

struct runtime_data {
    uint32_t used;
    void *instance;
    char key[32];
    void *value;
};

/* GValue is intentionally a fixed-layout, copy-in value.  Keeping the
 * storage in the value itself avoids allocator ownership crossing between
 * the independently loaded GLib/GObject/GTK compatibility DSOs. */
struct runtime_gvalue {
    uint32_t initialized;
    char type_string[4];
    int64_t signed_value;
    uint64_t unsigned_value;
    char string_value[96];
};

struct runtime_property {
    uint32_t used;
    void *instance;
    char name[48];
    struct runtime_gvalue value;
};

struct runtime_binding {
    uint32_t used;
    uint32_t references;
    uint32_t flags;
    uint32_t updating;
    void *source;
    void *target;
    char source_property[48];
    char target_property[48];
};

struct runtime_window {
    uint32_t used;
    uint32_t visible;
    uint32_t references;
    uint32_t kind;
    uint32_t sensitive;
    uint32_t active;
    uint32_t resizable;
    uint32_t maximized;
    uint32_t minimized;
    uint32_t closed;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    char title[96];
    char text[96];
    char name[64];
    uint32_t is_application;
    uint32_t application_registered;
    uint32_t application_holds;
    uint32_t application_quit_requested;
    char application_id[96];
    struct runtime_window *parent;
    struct runtime_window *children[RUNTIME_WIDGET_CHILD_CAPACITY];
    uint32_t child_count;
    struct runtime_signal signals[RUNTIME_SIGNAL_CAPACITY];
    struct runtime_data data[RUNTIME_DATA_CAPACITY];
};

struct runtime_action {
    uint32_t used;
    uint32_t references;
    uint32_t enabled;
    char name[48];
    char parameter_type[8];
};
typedef struct runtime_action GSimpleAction;

struct runtime_action_map_entry {
    uint32_t used;
    void *action_map;
    struct runtime_action *action;
    char name[48];
};

static struct runtime_loop loops[RUNTIME_LOOP_CAPACITY];
static struct runtime_context contexts[RUNTIME_CONTEXT_CAPACITY];
static struct runtime_source sources[RUNTIME_SOURCE_CAPACITY];
static struct runtime_io_channel io_channels[RUNTIME_IO_CHANNEL_CAPACITY];
static struct runtime_io_watch io_watches[RUNTIME_IO_WATCH_CAPACITY];
static struct runtime_window windows[RUNTIME_WINDOW_CAPACITY];
/* Signals are process-wide rather than embedded only in a widget DSO's local
 * object table.  libglib/libgobject and libgtk are packaged as separate
 * sonames, so a callback connected through one handle must still be able to
 * target a widget created through another. */
static struct runtime_signal global_signals[RUNTIME_GLOBAL_SIGNAL_CAPACITY];
static struct runtime_data global_object_data[RUNTIME_GLOBAL_SIGNAL_CAPACITY];
static struct runtime_property global_object_properties[RUNTIME_PROPERTY_CAPACITY];
static struct runtime_binding global_bindings[RUNTIME_BINDING_CAPACITY];
static struct runtime_action actions[RUNTIME_ACTION_CAPACITY];
static struct runtime_action_map_entry action_map_entries[RUNTIME_ACTION_MAP_CAPACITY];
static uint32_t next_source_id = 1U;
static uint32_t runtime_clock_ms;
static uint32_t runtime_source_epoch;
static uint32_t next_signal_id = 1U;
static const char *application_name;
static struct runtime_context *runtime_thread_default_context;
/* g_application_run()/quit() are exported from the GLib-shaped DSO.  Keep
 * the active loop in that DSO so an application object created by libgtk can
 * still control the same dispatch context across the ABI boundary. */
static struct runtime_loop *g_application_loop;

/* Keep one process-wide default context, matching GLib's contract that
 * g_main_context_default() is stable and never returns NULL.  The runtime is
 * intentionally bounded; the fixed table leaves room for future explicit
 * contexts while current source dispatch remains deterministic and
 * allocation-free. */
static struct runtime_context *runtime_context_default(void) {
    if (!contexts[0].used) {
        contexts[0].used = 1U;
        contexts[0].references = 1U;
    }
    return &contexts[0];
}

static void runtime_context_ref_internal(struct runtime_context *context) {
    if (context && context->used && context->references != UINT32_MAX)
        ++context->references;
}

static void runtime_context_unref_internal(struct runtime_context *context) {
    if (!context || !context->used || context == runtime_context_default())
        return;
    if (context->references > 1U) {
        --context->references;
        return;
    }
    context->references = 0U;
    context->used = 0U;
    if (runtime_thread_default_context == context)
        runtime_thread_default_context = NULL;
}

static struct runtime_context *runtime_context_current(void) {
    if (runtime_thread_default_context &&
        runtime_thread_default_context->used)
        return runtime_thread_default_context;
    return runtime_context_default();
}

static struct runtime_context *find_context(void *value) {
    if (!value) return runtime_context_default();
    for (size_t index = 0; index < RUNTIME_CONTEXT_CAPACITY; ++index) {
        if (value == &contexts[index] && contexts[index].used)
            return &contexts[index];
    }
    /* Callers may pass a context handle obtained from another compatibility
     * DSO.  Source queues are process-wide in this freestanding runtime; use
     * the local default context instead of dereferencing an unknown pointer. */
    return runtime_context_default();
}

static struct runtime_io_channel *find_io_channel(void *value) {
    for (size_t index = 0; index < RUNTIME_IO_CHANNEL_CAPACITY; ++index) {
        if (value == &io_channels[index] && io_channels[index].used)
            return &io_channels[index];
    }
    return NULL;
}

static void runtime_io_channel_unref_internal(
    struct runtime_io_channel *channel) {
    if (!channel || !channel->used) return;
    if (channel->references > 1U) {
        --channel->references;
        return;
    }
    channel->references = 0U;
    channel->fd = -1;
    channel->used = 0U;
}

static void runtime_io_watch_destroy(void *data) {
    struct runtime_io_watch *watch = data;
    if (!watch || !watch->used) return;
    void (*destroy_notify)(void *) = watch->destroy_notify;
    void *user_data = watch->data;
    struct runtime_io_channel *channel = watch->channel;
    watch->used = 0U;
    watch->source_id = 0U;
    watch->channel = NULL;
    watch->callback = NULL;
    watch->data = NULL;
    watch->destroy_notify = NULL;
    if (destroy_notify) destroy_notify(user_data);
    runtime_io_channel_unref_internal(channel);
}

static unsigned runtime_io_watch_dispatch(int fd, unsigned condition,
                                           void *data) {
    (void)fd;
    struct runtime_io_watch *watch = data;
    if (!watch || !watch->used || !watch->callback) return 0U;
    return watch->callback(watch->channel, condition, watch->data);
}

static struct runtime_action *find_action(const void *value) {
    if (!value) return NULL;
    for (size_t index = 0; index < RUNTIME_ACTION_CAPACITY; ++index)
        if (actions[index].used && &actions[index] == value) return &actions[index];
    return NULL;
}

/* A small deterministic arena is enough for startup metadata and keeps the
 * freestanding shared objects independent of host malloc. Allocations are
 * tracked so realloc preserves contents; g_free releases the record and
 * leaves the arena monotonic, avoiding a fragile free-list ABI in each DSO. */
#if UINTPTR_MAX <= UINT32_MAX
/* i386 has a 128-page per-process mmap budget.  Keep enough arena for normal
 * toolkit startup while leaving one page of headroom for three resident DSOs. */
#define RUNTIME_HEAP_CAPACITY (52U * 1024U)
#else
#define RUNTIME_HEAP_CAPACITY (64U * 1024U)
#endif
#define RUNTIME_ALLOCATION_CAPACITY 64U
struct runtime_allocation {
    void *pointer;
    size_t size;
    uint32_t used;
};
static uint8_t runtime_heap[RUNTIME_HEAP_CAPACITY];
static size_t runtime_heap_offset;
static struct runtime_allocation runtime_allocations[RUNTIME_ALLOCATION_CAPACITY];

static void copy_bytes(void *destination, const void *source, size_t size) {
    if (!destination || !source) return;
    uint8_t *out = destination;
    const uint8_t *in = source;
    for (size_t index = 0; index < size; ++index) out[index] = in[index];
}

static void *runtime_allocate(size_t size, int clear) {
    if (size == 0U) size = 1U;
    size_t aligned = (size + 7U) & ~(size_t)7U;
    if (aligned < size || aligned > RUNTIME_HEAP_CAPACITY - runtime_heap_offset)
        return NULL;
    struct runtime_allocation *slot = NULL;
    for (size_t index = 0; index < RUNTIME_ALLOCATION_CAPACITY; ++index) {
        if (!runtime_allocations[index].used) {
            slot = &runtime_allocations[index];
            break;
        }
    }
    if (!slot) return NULL;
    void *pointer = &runtime_heap[runtime_heap_offset];
    runtime_heap_offset += aligned;
    slot->pointer = pointer;
    slot->size = size;
    slot->used = 1U;
    if (clear) {
        uint8_t *bytes = pointer;
        for (size_t index = 0; index < size; ++index) bytes[index] = 0;
    }
    return pointer;
}

void *g_malloc(size_t size) { return runtime_allocate(size, 0); }
void *g_malloc0(size_t size) { return runtime_allocate(size, 1); }

void g_free(void *memory) {
    if (!memory) return;
    for (size_t index = 0; index < RUNTIME_ALLOCATION_CAPACITY; ++index) {
        if (runtime_allocations[index].used &&
            runtime_allocations[index].pointer == memory) {
            runtime_allocations[index].used = 0U;
            runtime_allocations[index].pointer = NULL;
            runtime_allocations[index].size = 0U;
            return;
        }
    }
}

void *g_realloc(void *memory, size_t size) {
    if (!memory) return g_malloc(size);
    if (size == 0U) {
        g_free(memory);
        return NULL;
    }
    size_t old_size = 0U;
    for (size_t index = 0; index < RUNTIME_ALLOCATION_CAPACITY; ++index) {
        if (runtime_allocations[index].used &&
            runtime_allocations[index].pointer == memory) {
            old_size = runtime_allocations[index].size;
            break;
        }
    }
    if (old_size == 0U) return NULL;
    void *replacement = g_malloc(size);
    if (!replacement) return NULL;
    copy_bytes(replacement, memory, old_size < size ? old_size : size);
    g_free(memory);
    return replacement;
}

char *g_strdup(const char *text) {
    if (!text) return NULL;
    size_t length = 0U;
    while (text[length] != '\0') ++length;
    char *copy = g_malloc(length + 1U);
    if (!copy) return NULL;
    copy_bytes(copy, text, length);
    copy[length] = '\0';
    return copy;
}

char *g_strndup(const char *text, size_t length) {
    if (!text) return NULL;
    size_t actual = 0U;
    while (actual < length && text[actual] != '\0') ++actual;
    char *copy = g_malloc(actual + 1U);
    if (!copy) return NULL;
    copy_bytes(copy, text, actual);
    copy[actual] = '\0';
    return copy;
}

void *g_memdup2(const void *memory, size_t size) {
    if (!memory && size != 0U) return NULL;
    void *copy = g_malloc(size);
    if (copy && size) copy_bytes(copy, memory, size);
    return copy;
}

/* GLib-shaped collections -------------------------------------------------
 *
 * These containers intentionally keep the public ABI opaque while providing
 * the ordinary ownership and growth semantics needed by GLib/GIO source
 * ports.  They are bounded by the shared 64 KiB runtime arena; an allocation
 * failure is reported as NULL rather than allowing a desktop service to
 * corrupt memory.  The implementation is freestanding and does not depend
 * on a host libc.
 */
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
typedef unsigned (*GHashFunc)(const void *key);
typedef int (*GEqualFunc)(const void *left, const void *right);

/* The runtime DSO cannot rely on the process libc.  desktop_io.c provides
 * the small native syscall bridge used by the GIO-shaped file APIs below. */
extern long akaos_desktop_open(const char *path, size_t length, int flags);
extern long akaos_desktop_read(int descriptor, void *buffer, size_t length);
extern long akaos_desktop_getdents(int descriptor, void *entries,
                                   size_t capacity);
extern long akaos_desktop_stat(const char *path, size_t length, void *status);
extern long akaos_desktop_write(int descriptor, const void *buffer,
                                size_t length);
extern long akaos_desktop_close(int descriptor);

struct _GArray {
    void *data;
    size_t length;
    size_t capacity;
    unsigned element_size;
    uint32_t zero_terminated;
    uint32_t clear;
};

struct _GPtrArray {
    void **pdata;
    size_t length;
    size_t capacity;
};

struct _GString {
    char *str;
    size_t length;
    size_t capacity;
};

static int runtime_size_mul(size_t left, size_t right, size_t *result) {
    if (!result || (right != 0U && left > (size_t)-1 / right)) return 0;
    *result = left * right;
    return 1;
}

static int runtime_size_add(size_t left, size_t right, size_t *result) {
    if (!result || left > (size_t)-1 - right) return 0;
    *result = left + right;
    return 1;
}

static void zero_bytes(void *destination, size_t size) {
    if (!destination) return;
    uint8_t *bytes = destination;
    for (size_t index = 0; index < size; ++index) bytes[index] = 0U;
}

static int g_array_reserve(GArray *array, size_t needed) {
    if (!array || array->element_size == 0U) return 0;
    if (needed <= array->capacity) return 1;
    size_t capacity = array->capacity ? array->capacity : 4U;
    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2U) {
            capacity = needed;
            break;
        }
        capacity *= 2U;
    }
    size_t elements = capacity;
    if (array->zero_terminated && !runtime_size_add(elements, 1U, &elements))
        return 0;
    size_t bytes = 0U;
    if (!runtime_size_mul(elements, (size_t)array->element_size, &bytes))
        return 0;
    void *replacement = g_realloc(array->data, bytes);
    if (!replacement) return 0;
    array->data = replacement;
    array->capacity = capacity;
    return 1;
}

GArray *g_array_new(int zero_terminated, int clear, unsigned element_size) {
    if (element_size == 0U) return NULL;
    GArray *array = g_malloc0(sizeof(*array));
    if (!array) return NULL;
    array->element_size = element_size;
    array->zero_terminated = zero_terminated != 0;
    array->clear = clear != 0;
    return array;
}

GArray *g_array_set_size(GArray *array, size_t length) {
    if (!array || array->element_size == 0U) return NULL;
    if (!g_array_reserve(array, length)) return NULL;
    if (length > array->length) {
        size_t start = 0U;
        size_t count = 0U;
        if (!runtime_size_mul(array->length, (size_t)array->element_size, &start) ||
            !runtime_size_mul(length - array->length,
                              (size_t)array->element_size, &count)) return NULL;
        if (array->clear && count) zero_bytes((uint8_t *)array->data + start, count);
    }
    array->length = length;
    if (array->zero_terminated) {
        size_t terminator = 0U;
        if (!runtime_size_mul(length, (size_t)array->element_size, &terminator))
            return NULL;
        zero_bytes((uint8_t *)array->data + terminator, array->element_size);
    }
    return array;
}

GArray *g_array_append_vals(GArray *array, const void *values, size_t count) {
    if (!array || (count != 0U && !values)) return NULL;
    size_t new_length = 0U;
    if (!runtime_size_add(array->length, count, &new_length)) return NULL;
    size_t old_length = array->length;
    if (!g_array_set_size(array, new_length)) return NULL;
    size_t offset = 0U;
    size_t bytes = 0U;
    if (!runtime_size_mul(old_length, (size_t)array->element_size, &offset) ||
        !runtime_size_mul(count, (size_t)array->element_size, &bytes)) return NULL;
    if (bytes) copy_bytes((uint8_t *)array->data + offset, values, bytes);
    if (array->zero_terminated) {
        size_t terminator = 0U;
        if (runtime_size_mul(array->length, (size_t)array->element_size, &terminator))
            zero_bytes((uint8_t *)array->data + terminator, array->element_size);
    }
    return array;
}

size_t g_array_get_length(const GArray *array) {
    return array ? array->length : 0U;
}

unsigned g_array_get_element_size(const GArray *array) {
    return array ? array->element_size : 0U;
}

void *g_array_free(GArray *array, int free_segment) {
    if (!array) return NULL;
    void *segment = array->data;
    if (!free_segment) {
        g_free(segment);
        segment = NULL;
    }
    g_free(array);
    return segment;
}

static int g_ptr_array_reserve(GPtrArray *array, size_t needed) {
    if (!array || needed <= array->capacity) return array != NULL;
    size_t capacity = array->capacity ? array->capacity : 4U;
    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2U) {
            capacity = needed;
            break;
        }
        capacity *= 2U;
    }
    size_t bytes = 0U;
    if (!runtime_size_mul(capacity, sizeof(void *), &bytes)) return 0;
    void **replacement = g_realloc(array->pdata, bytes);
    if (!replacement) return 0;
    array->pdata = replacement;
    array->capacity = capacity;
    return 1;
}

GPtrArray *g_ptr_array_new(void) {
    return g_malloc0(sizeof(GPtrArray));
}

int g_ptr_array_add(GPtrArray *array, void *value) {
    if (!array || !runtime_size_add(array->length, 1U, &array->length)) return 0;
    size_t index = array->length - 1U;
    if (!g_ptr_array_reserve(array, array->length)) {
        array->length = index;
        return 0;
    }
    array->pdata[index] = value;
    return 1;
}

size_t g_ptr_array_get_length(const GPtrArray *array) {
    return array ? array->length : 0U;
}

void *g_ptr_array_index(const GPtrArray *array, size_t index) {
    if (!array || index >= array->length) return NULL;
    return array->pdata[index];
}

void **g_ptr_array_free(GPtrArray *array, int free_segment) {
    if (!array) return NULL;
    void **segment = array->pdata;
    if (!free_segment) {
        g_free(segment);
        segment = NULL;
    }
    g_free(array);
    return segment;
}

static int g_string_reserve(GString *string, size_t needed) {
    if (!string) return 0;
    size_t required = 0U;
    if (!runtime_size_add(needed, 1U, &required)) return 0;
    if (required <= string->capacity) return 1;
    size_t capacity = string->capacity ? string->capacity : 16U;
    while (capacity < required) {
        if (capacity > (size_t)-1 / 2U) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }
    char *replacement = g_realloc(string->str, capacity);
    if (!replacement) return 0;
    string->str = replacement;
    string->capacity = capacity;
    return 1;
}

GString *g_string_new(const char *initial) {
    if (!initial) initial = "";
    size_t length = 0U;
    while (initial[length] != '\0') ++length;
    GString *string = g_malloc0(sizeof(*string));
    if (!string || !g_string_reserve(string, length)) {
        g_free(string);
        return NULL;
    }
    if (length) copy_bytes(string->str, initial, length);
    string->length = length;
    string->str[length] = '\0';
    return string;
}

GString *g_string_append_len(GString *string, const char *text, size_t length) {
    if (!string || (length != 0U && !text)) return NULL;
    size_t new_length = 0U;
    if (!runtime_size_add(string->length, length, &new_length) ||
        !g_string_reserve(string, new_length)) return NULL;
    if (length) copy_bytes(string->str + string->length, text, length);
    string->length = new_length;
    string->str[string->length] = '\0';
    return string;
}

GString *g_string_append(GString *string, const char *text) {
    if (!text) return string;
    size_t length = 0U;
    while (text[length] != '\0') ++length;
    return g_string_append_len(string, text, length);
}

GString *g_string_truncate(GString *string, size_t length) {
    if (!string) return NULL;
    if (length > string->length) length = string->length;
    string->length = length;
    if (string->str) string->str[length] = '\0';
    return string;
}

char *g_string_free(GString *string, int free_segment) {
    if (!string) return NULL;
    char *segment = string->str;
    if (!free_segment) {
        g_free(segment);
        segment = NULL;
    }
    g_free(string);
    return segment;
}

struct runtime_hash_entry {
    void *key;
    void *value;
    unsigned hash;
    uint32_t state;
};

struct _GHashTable {
    GHashFunc hash_func;
    GEqualFunc equal_func;
    size_t size;
    struct runtime_hash_entry entries[RUNTIME_HASH_CAPACITY];
};

static unsigned runtime_pointer_hash(const void *key) {
    uintptr_t value = (uintptr_t)key;
    value ^= value >> 17;
    value ^= value >> 9;
    return (unsigned)value;
}

unsigned g_str_hash(const void *key) {
    if (!key) return 0U;
    const uint8_t *text = key;
    unsigned hash = 2166136261U;
    while (*text) {
        hash ^= *text++;
        hash *= 16777619U;
    }
    return hash;
}

int g_str_equal(const void *left, const void *right) {
    if (left == right) return 1;
    if (!left || !right) return 0;
    const uint8_t *a = left;
    const uint8_t *b = right;
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int runtime_pointer_equal(const void *left, const void *right) {
    return left == right;
}

static int g_hash_find_slot(const GHashTable *table, const void *key,
                            unsigned hash, int for_insert) {
    if (!table) return -1;
    size_t start = (size_t)hash % RUNTIME_HASH_CAPACITY;
    int tombstone = -1;
    for (size_t step = 0; step < RUNTIME_HASH_CAPACITY; ++step) {
        size_t index = (start + step) % RUNTIME_HASH_CAPACITY;
        const struct runtime_hash_entry *entry = &table->entries[index];
        if (entry->state == 0U) {
            if (for_insert) return tombstone >= 0 ? tombstone : (int)index;
            return -1;
        }
        if (entry->state == 2U) {
            if (tombstone < 0) tombstone = (int)index;
            continue;
        }
        GEqualFunc equal = table->equal_func ? table->equal_func : runtime_pointer_equal;
        if (entry->hash == hash && equal(entry->key, key)) return (int)index;
    }
    return for_insert ? tombstone : -1;
}

GHashTable *g_hash_table_new(GHashFunc hash_func, GEqualFunc equal_func) {
    GHashTable *table = g_malloc0(sizeof(*table));
    if (!table) return NULL;
    table->hash_func = hash_func ? hash_func : runtime_pointer_hash;
    table->equal_func = equal_func ? equal_func : runtime_pointer_equal;
    return table;
}

int g_hash_table_insert(GHashTable *table, void *key, void *value) {
    if (!table) return 0;
    unsigned hash = table->hash_func ? table->hash_func(key) : runtime_pointer_hash(key);
    int index = g_hash_find_slot(table, key, hash, 1);
    if (index < 0) return 0;
    struct runtime_hash_entry *entry = &table->entries[index];
    if (entry->state != 1U) {
        entry->state = 1U;
        ++table->size;
    }
    entry->key = key;
    entry->value = value;
    entry->hash = hash;
    return 1;
}

void *g_hash_table_lookup(const GHashTable *table, const void *key) {
    if (!table) return NULL;
    unsigned hash = table->hash_func ? table->hash_func(key) : runtime_pointer_hash(key);
    int index = g_hash_find_slot(table, key, hash, 0);
    return index >= 0 ? table->entries[index].value : NULL;
}

int g_hash_table_contains(const GHashTable *table, const void *key) {
    if (!table) return 0;
    unsigned hash = table->hash_func ? table->hash_func(key) : runtime_pointer_hash(key);
    return g_hash_find_slot(table, key, hash, 0) >= 0;
}

int g_hash_table_remove(GHashTable *table, const void *key) {
    if (!table) return 0;
    unsigned hash = table->hash_func ? table->hash_func(key) : runtime_pointer_hash(key);
    int index = g_hash_find_slot(table, key, hash, 0);
    if (index < 0) return 0;
    struct runtime_hash_entry *entry = &table->entries[index];
    entry->key = NULL;
    entry->value = NULL;
    entry->hash = 0U;
    entry->state = 2U;
    if (table->size) --table->size;
    return 1;
}

size_t g_hash_table_size(const GHashTable *table) {
    return table ? table->size : 0U;
}

void g_hash_table_destroy(GHashTable *table) {
    g_free(table);
}

struct _GList {
    void *data;
    GList *next;
    GList *prev;
};

struct _GSList {
    void *data;
    GSList *next;
};

GList *g_list_prepend(GList *list, void *data) {
    GList *node = g_malloc0(sizeof(*node));
    if (!node) return list;
    node->data = data;
    node->next = list;
    if (list) list->prev = node;
    return node;
}

GList *g_list_append(GList *list, void *data) {
    if (!list) return g_list_prepend(NULL, data);
    GList *tail = list;
    for (unsigned step = 0; step < RUNTIME_LIST_WALK_LIMIT && tail->next; ++step)
        tail = tail->next;
    if (tail->next) return list;
    GList *node = g_malloc0(sizeof(*node));
    if (!node) return list;
    node->data = data;
    node->prev = tail;
    tail->next = node;
    return list;
}

GList *g_list_remove(GList *list, const void *data) {
    GList *node = list;
    for (unsigned step = 0; node && step < RUNTIME_LIST_WALK_LIMIT; ++step,
                         node = node->next) {
        if (node->data != data) continue;
        GList *next = node->next;
        if (node->prev) node->prev->next = next;
        else list = next;
        if (next) next->prev = node->prev;
        g_free(node);
        return list;
    }
    return list;
}

size_t g_list_length(const GList *list) {
    size_t length = 0U;
    for (const GList *node = list;
         node && length < RUNTIME_LIST_WALK_LIMIT;
         node = node->next) ++length;
    return length;
}

void *g_list_nth_data(const GList *list, size_t index) {
    const GList *node = list;
    for (unsigned step = 0; node && step < RUNTIME_LIST_WALK_LIMIT; ++step,
                         node = node->next) {
        if (index == 0U) return node->data;
        --index;
    }
    return NULL;
}

void g_list_free(GList *list) {
    GList *node = list;
    for (unsigned step = 0; node && step < RUNTIME_LIST_WALK_LIMIT; ++step) {
        GList *next = node->next;
        g_free(node);
        node = next;
    }
}

GSList *g_slist_prepend(GSList *list, void *data) {
    GSList *node = g_malloc0(sizeof(*node));
    if (!node) return list;
    node->data = data;
    node->next = list;
    return node;
}

GSList *g_slist_append(GSList *list, void *data) {
    if (!list) return g_slist_prepend(NULL, data);
    GSList *tail = list;
    for (unsigned step = 0; step < RUNTIME_LIST_WALK_LIMIT && tail->next; ++step)
        tail = tail->next;
    if (tail->next) return list;
    GSList *node = g_malloc0(sizeof(*node));
    if (!node) return list;
    node->data = data;
    tail->next = node;
    return list;
}

GSList *g_slist_remove(GSList *list, const void *data) {
    GSList *previous = NULL;
    GSList *node = list;
    for (unsigned step = 0; node && step < RUNTIME_LIST_WALK_LIMIT; ++step) {
        if (node->data == data) {
            if (previous) previous->next = node->next;
            else list = node->next;
            g_free(node);
            return list;
        }
        previous = node;
        node = node->next;
    }
    return list;
}

size_t g_slist_length(const GSList *list) {
    size_t length = 0U;
    for (const GSList *node = list;
         node && length < RUNTIME_LIST_WALK_LIMIT;
         node = node->next) ++length;
    return length;
}

void *g_slist_nth_data(const GSList *list, size_t index) {
    const GSList *node = list;
    for (unsigned step = 0; node && step < RUNTIME_LIST_WALK_LIMIT; ++step,
                         node = node->next) {
        if (index == 0U) return node->data;
        --index;
    }
    return NULL;
}

void g_slist_free(GSList *list) {
    GSList *node = list;
    for (unsigned step = 0; node && step < RUNTIME_LIST_WALK_LIMIT; ++step) {
        GSList *next = node->next;
        g_free(node);
        node = next;
    }
}

struct _GBytes {
    void *data;
    size_t size;
    uint32_t references;
};

GBytes *g_bytes_new(const void *data, size_t size) {
    if (!data && size != 0U) return NULL;
    GBytes *bytes = g_malloc0(sizeof(*bytes));
    if (!bytes) return NULL;
    bytes->data = g_malloc(size);
    if (!bytes->data) {
        g_free(bytes);
        return NULL;
    }
    if (size) copy_bytes(bytes->data, data, size);
    bytes->size = size;
    bytes->references = 1U;
    return bytes;
}

GBytes *g_bytes_ref(GBytes *bytes) {
    if (!bytes || bytes->references == 0U || bytes->references == (uint32_t)-1)
        return NULL;
    ++bytes->references;
    return bytes;
}

void g_bytes_unref(GBytes *bytes) {
    if (!bytes || bytes->references == 0U) return;
    if (bytes->references > 1U) {
        --bytes->references;
        return;
    }
    bytes->references = 0U;
    g_free(bytes->data);
    g_free(bytes);
}

const void *g_bytes_get_data(const GBytes *bytes, size_t *size) {
    if (size) *size = bytes ? bytes->size : 0U;
    return bytes ? bytes->data : NULL;
}

size_t g_bytes_get_size(const GBytes *bytes) {
    return bytes ? bytes->size : 0U;
}

int g_bytes_equal(const GBytes *left, const GBytes *right) {
    if (!left || !right || left->size != right->size) return 0;
    const uint8_t *a = left->data;
    const uint8_t *b = right->data;
    for (size_t index = 0; index < left->size; ++index) {
        if (a[index] != b[index]) return 0;
    }
    return 1;
}

enum runtime_variant_kind {
    RUNTIME_VARIANT_STRING = 1U,
    RUNTIME_VARIANT_BOOLEAN = 2U,
    RUNTIME_VARIANT_UINT32 = 3U,
    RUNTIME_VARIANT_INT32 = 4U,
    RUNTIME_VARIANT_UINT64 = 5U,
    RUNTIME_VARIANT_INT64 = 6U,
    RUNTIME_VARIANT_TUPLE = 7U,
    RUNTIME_VARIANT_ARRAY = 8U,
    RUNTIME_VARIANT_DICT_ENTRY = 9U,
    RUNTIME_VARIANT_VARIANT = 10U,
};

struct _GVariant {
    uint32_t kind;
    uint32_t references;
    uint64_t unsigned_value;
    int64_t signed_value;
    char *string_value;
    GVariant **children;
    size_t child_count;
};

struct _GVariantBuilder {
    uint32_t kind;
    uint32_t used;
    char type_string[16];
    GVariant *children[8];
    size_t child_count;
};

#define RUNTIME_SETTINGS_CAPACITY 16U
struct runtime_settings_value {
    uint32_t used;
    uint32_t kind;
    char key[48];
    uint64_t unsigned_value;
    int64_t signed_value;
    char string_value[96];
};

struct _GSettings {
    uint32_t references;
    char schema_id[64];
    struct runtime_settings_value values[RUNTIME_SETTINGS_CAPACITY];
};

struct _GFile {
    uint32_t references;
    char path[192];
};

#define RUNTIME_DIRECTORY_BATCH 8U
#define RUNTIME_DIRECTORY_NAME_MAX AKAOS_NAME_MAX
#define RUNTIME_FILE_TYPE_UNKNOWN 0
#define RUNTIME_FILE_TYPE_REGULAR 1
#define RUNTIME_FILE_TYPE_DIRECTORY 2
#define RUNTIME_FILE_TYPE_SYMBOLIC_LINK 3
#define RUNTIME_FILE_TYPE_SPECIAL 4

/* This is the stable on-wire shape returned by the native getdents syscall.
 * Keep it local so the freestanding DSO does not depend on a host dirent ABI. */
struct runtime_dirent {
    uint64_t inode;
    uint32_t type;
    char name[RUNTIME_DIRECTORY_NAME_MAX + 1U];
};

struct runtime_stat {
    uint64_t inode;
    uint64_t size;
    uint32_t mode;
    uint32_t type;
    uint32_t uid;
    uint32_t gid;
};

struct _GFileInfo {
    uint32_t references;
    uint32_t file_type;
    uint64_t inode;
    uint64_t size;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    char name[RUNTIME_DIRECTORY_NAME_MAX + 1U];
    char display_name[RUNTIME_DIRECTORY_NAME_MAX + 1U];
};

struct _GFileEnumerator {
    uint32_t references;
    int descriptor;
    uint32_t closed;
    GFile *container;
    struct runtime_dirent entries[RUNTIME_DIRECTORY_BATCH];
    size_t entry_count;
    size_t entry_index;
};

struct _GCancellable {
    uint32_t references;
    uint32_t cancelled;
};

struct _GError {
    uint32_t domain;
    int32_t code;
    char message[128];
};

struct _GTask {
    uint32_t references;
    uint32_t completed;
    uint32_t result;
    void *source_object;
    GCancellable *cancellable;
    GError *error;
    char *contents;
    size_t contents_length;
    void (*callback)(void *source_object, void *result, void *user_data);
    void *user_data;
};

static void copy_text(char *destination, size_t capacity, const char *source);
static int strcmp_local(const char *left, const char *right);
static void runtime_binding_clear(struct runtime_binding *binding);
static void runtime_binding_propagate(void *instance, const char *property_name,
                                      const struct runtime_gvalue *value);
const char *g_variant_get_string(const GVariant *value, size_t *length);
int g_variant_is_of_type(const GVariant *value, const char *type_string);
GVariant *g_variant_ref(GVariant *value);
void g_variant_unref(GVariant *value);
GVariant *g_variant_new_tuple(const GVariant *const children[], size_t count);
GVariant *g_variant_new_array(const GVariant *const children[], size_t count);
GVariant *g_variant_new_dict_entry(const GVariant *key, const GVariant *value);
GVariant *g_variant_new_variant(const GVariant *value);

static GVariant *runtime_variant_new(uint32_t kind) {
    GVariant *value = g_malloc0(sizeof(*value));
    if (!value) return NULL;
    value->kind = kind;
    value->references = 1U;
    return value;
}

GVariant *g_variant_new_string(const char *value) {
    GVariant *variant = runtime_variant_new(RUNTIME_VARIANT_STRING);
    if (!variant) return NULL;
    variant->string_value = g_strdup(value ? value : "");
    if (!variant->string_value) {
        g_free(variant);
        return NULL;
    }
    return variant;
}

GVariant *g_variant_new_boolean(int value) {
    GVariant *variant = runtime_variant_new(RUNTIME_VARIANT_BOOLEAN);
    if (variant) variant->unsigned_value = value != 0;
    return variant;
}

GVariant *g_variant_new_uint32(uint32_t value) {
    GVariant *variant = runtime_variant_new(RUNTIME_VARIANT_UINT32);
    if (variant) variant->unsigned_value = value;
    return variant;
}

GVariant *g_variant_new_int32(int32_t value) {
    GVariant *variant = runtime_variant_new(RUNTIME_VARIANT_INT32);
    if (variant) variant->signed_value = value;
    return variant;
}

GVariant *g_variant_new_uint64(uint64_t value) {
    GVariant *variant = runtime_variant_new(RUNTIME_VARIANT_UINT64);
    if (variant) variant->unsigned_value = value;
    return variant;
}

GVariant *g_variant_new_int64(int64_t value) {
    GVariant *variant = runtime_variant_new(RUNTIME_VARIANT_INT64);
    if (variant) variant->signed_value = value;
    return variant;
}

static GVariant *runtime_variant_parse_format(const char **cursor,
                                              va_list *arguments);

static GVariant *runtime_variant_parse_tuple(const char **cursor,
                                             va_list *arguments) {
    GVariant *children[8];
    size_t count = 0U;
    while (*cursor && **cursor != ')' && count < 8U) {
        GVariant *child = runtime_variant_parse_format(cursor, arguments);
        if (!child) {
            for (size_t index = 0; index < count; ++index)
                g_variant_unref(children[index]);
            return NULL;
        }
        children[count++] = child;
    }
    if (!*cursor || **cursor != ')') {
        for (size_t index = 0; index < count; ++index)
            g_variant_unref(children[index]);
        return NULL;
    }
    ++*cursor;
    GVariant *tuple = g_variant_new_tuple((const GVariant *const *)children, count);
    for (size_t index = 0; index < count; ++index) g_variant_unref(children[index]);
    return tuple;
}

static GVariant *runtime_variant_parse_dict(const char **cursor,
                                            va_list *arguments) {
    GVariant *key = runtime_variant_parse_format(cursor, arguments);
    GVariant *value = key ? runtime_variant_parse_format(cursor, arguments) : NULL;
    if (!key || !value || !*cursor || **cursor != '}') {
        if (key) g_variant_unref(key);
        if (value) g_variant_unref(value);
        return NULL;
    }
    ++*cursor;
    GVariant *entry = g_variant_new_dict_entry(key, value);
    g_variant_unref(key);
    g_variant_unref(value);
    return entry;
}

static GVariant *runtime_variant_parse_format(const char **cursor,
                                              va_list *arguments) {
    if (!cursor || !*cursor || !arguments || **cursor == '\0') return NULL;
    char type = **cursor;
    ++*cursor;
    if (type == '(') return runtime_variant_parse_tuple(cursor, arguments);
    if (type == '{') return runtime_variant_parse_dict(cursor, arguments);
    if (type == '@') return g_variant_ref(va_arg(*arguments, GVariant *));
    switch (type) {
    case 's': return g_variant_new_string(va_arg(*arguments, const char *));
    case 'b': return g_variant_new_boolean(va_arg(*arguments, int));
    case 'u': return g_variant_new_uint32(va_arg(*arguments, unsigned));
    case 'i': return g_variant_new_int32(va_arg(*arguments, int));
    case 't': return g_variant_new_uint64(va_arg(*arguments, uint64_t));
    case 'x': return g_variant_new_int64(va_arg(*arguments, int64_t));
    case 'v': return g_variant_new_variant(va_arg(*arguments, GVariant *));
    default: return NULL;
    }
}

GVariant *g_variant_new(const char *format_string, ...) {
    if (!format_string || format_string[0] == '\0') return NULL;
    const char *cursor = format_string;
    va_list arguments;
    va_start(arguments, format_string);
    GVariant *value = runtime_variant_parse_format(&cursor, &arguments);
    va_end(arguments);
    if (!value || *cursor != '\0') {
        if (value) g_variant_unref(value);
        return NULL;
    }
    return value;
}

static GVariant *runtime_variant_new_children(uint32_t kind,
                                              const GVariant *const children[],
                                              size_t count) {
    if (count != 0U && !children) return NULL;
    GVariant *variant = runtime_variant_new(kind);
    if (!variant) return NULL;
    size_t bytes = 0U;
    if (!runtime_size_mul(count, sizeof(GVariant *), &bytes)) {
        g_free(variant);
        return NULL;
    }
    variant->children = count ? g_malloc0(bytes) : NULL;
    if (count && !variant->children) {
        g_free(variant);
        return NULL;
    }
    for (size_t index = 0; index < count; ++index) {
        if (!children[index] || !g_variant_ref((GVariant *)children[index])) {
            for (size_t rollback = 0; rollback < index; ++rollback)
                g_variant_unref(variant->children[rollback]);
            g_free(variant->children);
            g_free(variant);
            return NULL;
        }
        variant->children[index] = (GVariant *)children[index];
    }
    variant->child_count = count;
    return variant;
}

GVariant *g_variant_new_tuple(const GVariant *const children[], size_t count) {
    return runtime_variant_new_children(RUNTIME_VARIANT_TUPLE, children, count);
}

GVariant *g_variant_new_array(const GVariant *const children[], size_t count) {
    return runtime_variant_new_children(RUNTIME_VARIANT_ARRAY, children, count);
}

GVariant *g_variant_new_dict_entry(const GVariant *key, const GVariant *value) {
    const GVariant *children[2];
    if (!key || !value) return NULL;
    children[0] = key;
    children[1] = value;
    return runtime_variant_new_children(RUNTIME_VARIANT_DICT_ENTRY, children, 2U);
}

GVariant *g_variant_new_variant(const GVariant *value) {
    const GVariant *children[1];
    if (!value) return NULL;
    children[0] = value;
    return runtime_variant_new_children(RUNTIME_VARIANT_VARIANT, children, 1U);
}

GVariant *g_variant_get_variant(const GVariant *value) {
    if (!value || value->kind != RUNTIME_VARIANT_VARIANT ||
        !value->children || value->child_count != 1U) return NULL;
    return g_variant_ref(value->children[0]);
}

GVariant *g_variant_get_key(const GVariant *entry) {
    if (!entry || entry->kind != RUNTIME_VARIANT_DICT_ENTRY ||
        !entry->children || entry->child_count != 2U) return NULL;
    return g_variant_ref(entry->children[0]);
}

GVariant *g_variant_get_value(const GVariant *entry) {
    if (!entry || entry->kind != RUNTIME_VARIANT_DICT_ENTRY ||
        !entry->children || entry->child_count != 2U) return NULL;
    return g_variant_ref(entry->children[1]);
}

GVariant *g_variant_lookup_value(const GVariant *container,
                                 const char *key, const char *type_string) {
    if (!container || container->kind != RUNTIME_VARIANT_ARRAY || !key) return NULL;
    for (size_t index = 0; index < container->child_count; ++index) {
        GVariant *entry = container->children[index];
        if (!entry || entry->kind != RUNTIME_VARIANT_DICT_ENTRY ||
            entry->child_count != 2U) continue;
        const char *entry_key = g_variant_get_string(entry->children[0], NULL);
        if (!entry_key || strcmp_local(entry_key, key) != 0) continue;
        if (type_string && !g_variant_is_of_type(entry->children[1], type_string))
            return NULL;
        return g_variant_ref(entry->children[1]);
    }
    return NULL;
}

static int runtime_variant_builder_type(const char *type_string,
                                        uint32_t *kind_out) {
    if (!type_string || !kind_out || type_string[0] != 'a' ||
        type_string[1] == '\0') return 0;
    if (type_string[1] == '{') {
        size_t length = 0U;
        while (type_string[length] && length < 15U) ++length;
        if (length < 4U || type_string[length - 1U] != '}' ||
            type_string[2] == '\0' || type_string[3] == '\0') return 0;
    } else if (type_string[1] != '*' && type_string[2] != '\0') {
        return 0;
    }
    *kind_out = RUNTIME_VARIANT_ARRAY;
    return 1;
}

GVariantBuilder *g_variant_builder_new(const char *type_string) {
    uint32_t kind = 0U;
    if (!runtime_variant_builder_type(type_string, &kind)) return NULL;
    GVariantBuilder *builder = g_malloc0(sizeof(*builder));
    if (!builder) return NULL;
    builder->kind = kind;
    builder->used = 1U;
    copy_text(builder->type_string, sizeof(builder->type_string), type_string);
    return builder;
}

int g_variant_builder_add_value(GVariantBuilder *builder, GVariant *value) {
    if (!builder || !builder->used || !value ||
        builder->child_count >= 8U || !g_variant_ref(value)) return 0;
    builder->children[builder->child_count++] = value;
    return 1;
}

int g_variant_builder_add(GVariantBuilder *builder, const char *format_string, ...) {
    if (!builder || !builder->used || !format_string) return 0;
    const char *cursor = format_string;
    va_list arguments;
    va_start(arguments, format_string);
    GVariant *value = runtime_variant_parse_format(&cursor, &arguments);
    va_end(arguments);
    if (!value || *cursor != '\0') {
        if (value) g_variant_unref(value);
        return 0;
    }
    int result = g_variant_builder_add_value(builder, value);
    g_variant_unref(value);
    return result;
}

GVariant *g_variant_builder_end(GVariantBuilder *builder) {
    if (!builder || !builder->used) return NULL;
    GVariant *value = g_variant_new_array(
        (const GVariant *const *)builder->children, builder->child_count);
    for (size_t index = 0; index < builder->child_count; ++index)
        g_variant_unref(builder->children[index]);
    builder->child_count = 0U;
    builder->used = 0U;
    g_free(builder);
    return value;
}

void g_variant_builder_clear(GVariantBuilder *builder) {
    if (!builder || !builder->used) return;
    for (size_t index = 0; index < builder->child_count; ++index)
        g_variant_unref(builder->children[index]);
    builder->child_count = 0U;
    builder->used = 0U;
    g_free(builder);
}

GVariant *g_variant_ref(GVariant *value) {
    if (!value || value->references == 0U || value->references == (uint32_t)-1)
        return NULL;
    ++value->references;
    return value;
}

void g_variant_unref(GVariant *value) {
    if (!value || value->references == 0U) return;
    if (value->references > 1U) {
        --value->references;
        return;
    }
    value->references = 0U;
    g_free(value->string_value);
    if (value->children) {
        for (size_t index = 0; index < value->child_count; ++index)
            g_variant_unref(value->children[index]);
        g_free(value->children);
    }
    g_free(value);
}

size_t g_variant_n_children(const GVariant *value) {
    return value && (value->kind == RUNTIME_VARIANT_TUPLE ||
                     value->kind == RUNTIME_VARIANT_ARRAY ||
                     value->kind == RUNTIME_VARIANT_DICT_ENTRY)
        ? value->child_count : 0U;
}

GVariant *g_variant_get_child_value(const GVariant *value, size_t index) {
    if (!value || index >= value->child_count || !value->children) return NULL;
    return g_variant_ref(value->children[index]);
}

const char *g_variant_get_string(const GVariant *value, size_t *length) {
    if (!value || value->kind != RUNTIME_VARIANT_STRING) {
        if (length) *length = 0U;
        return NULL;
    }
    size_t actual = 0U;
    while (value->string_value && value->string_value[actual] != '\0') ++actual;
    if (length) *length = actual;
    return value->string_value;
}

int g_variant_get_boolean(const GVariant *value) {
    return value && value->kind == RUNTIME_VARIANT_BOOLEAN &&
        value->unsigned_value != 0U;
}

uint32_t g_variant_get_uint32(const GVariant *value) {
    return value && value->kind == RUNTIME_VARIANT_UINT32
        ? (uint32_t)value->unsigned_value : 0U;
}

int32_t g_variant_get_int32(const GVariant *value) {
    return value && value->kind == RUNTIME_VARIANT_INT32
        ? (int32_t)value->signed_value : 0;
}

uint64_t g_variant_get_uint64(const GVariant *value) {
    return value && value->kind == RUNTIME_VARIANT_UINT64
        ? value->unsigned_value : 0U;
}

int64_t g_variant_get_int64(const GVariant *value) {
    return value && value->kind == RUNTIME_VARIANT_INT64
        ? value->signed_value : 0;
}

const char *g_variant_get_type_string(const GVariant *value) {
    if (!value) return NULL;
    switch (value->kind) {
    case RUNTIME_VARIANT_STRING: return "s";
    case RUNTIME_VARIANT_BOOLEAN: return "b";
    case RUNTIME_VARIANT_UINT32: return "u";
    case RUNTIME_VARIANT_INT32: return "i";
    case RUNTIME_VARIANT_UINT64: return "t";
    case RUNTIME_VARIANT_INT64: return "x";
    case RUNTIME_VARIANT_TUPLE: return "(*)";
    case RUNTIME_VARIANT_ARRAY: return "a*";
    case RUNTIME_VARIANT_DICT_ENTRY: return "{**}";
    case RUNTIME_VARIANT_VARIANT: return "v";
    default: return NULL;
    }
}

int g_variant_is_of_type(const GVariant *value, const char *type_string) {
    const char *actual = g_variant_get_type_string(value);
    return actual && type_string && g_str_equal(actual, type_string);
}

static struct runtime_settings_value *runtime_settings_find(
    GSettings *settings, const char *key, int create) {
    if (!settings || !key || !key[0]) return NULL;
    struct runtime_settings_value *free_slot = NULL;
    for (size_t index = 0; index < RUNTIME_SETTINGS_CAPACITY; ++index) {
        struct runtime_settings_value *entry = &settings->values[index];
        if (!entry->used) {
            if (!free_slot) free_slot = entry;
            continue;
        }
        if (strcmp_local(entry->key, key) == 0) return entry;
    }
    if (!create || !free_slot) return NULL;
    free_slot->used = 1U;
    free_slot->kind = 0U;
    copy_text(free_slot->key, sizeof(free_slot->key), key);
    free_slot->string_value[0] = 0;
    return free_slot;
}

GSettings *g_settings_new(const char *schema_id) {
    if (!schema_id || !schema_id[0]) return NULL;
    GSettings *settings = g_malloc0(sizeof(*settings));
    if (!settings) return NULL;
    settings->references = 1U;
    copy_text(settings->schema_id, sizeof(settings->schema_id), schema_id);
    return settings;
}

GSettings *g_settings_ref(GSettings *settings) {
    if (!settings || settings->references == 0U ||
        settings->references == (uint32_t)-1) return NULL;
    ++settings->references;
    return settings;
}

void g_settings_unref(GSettings *settings) {
    if (!settings || settings->references == 0U) return;
    if (settings->references > 1U) {
        --settings->references;
        return;
    }
    settings->references = 0U;
    g_free(settings);
}

int g_settings_set_value(GSettings *settings, const char *key,
                         const GVariant *value) {
    if (!settings || !value) return 0;
    struct runtime_settings_value *entry =
        runtime_settings_find(settings, key, 1);
    if (!entry) return 0;
    entry->kind = value->kind;
    entry->unsigned_value = value->unsigned_value;
    entry->signed_value = value->signed_value;
    if (value->kind == RUNTIME_VARIANT_STRING) {
        if (!value->string_value) return 0;
        copy_text(entry->string_value, sizeof(entry->string_value),
                  value->string_value);
    } else if (value->kind != RUNTIME_VARIANT_BOOLEAN &&
               value->kind != RUNTIME_VARIANT_UINT32 &&
               value->kind != RUNTIME_VARIANT_INT32 &&
               value->kind != RUNTIME_VARIANT_UINT64 &&
               value->kind != RUNTIME_VARIANT_INT64) {
        entry->kind = 0U;
        return 0;
    }
    return 1;
}

GVariant *g_settings_get_value(const GSettings *settings, const char *key) {
    struct runtime_settings_value *entry = runtime_settings_find(
        (GSettings *)settings, key, 0);
    if (!entry) return NULL;
    switch (entry->kind) {
    case RUNTIME_VARIANT_STRING:
        return g_variant_new_string(entry->string_value);
    case RUNTIME_VARIANT_BOOLEAN:
        return g_variant_new_boolean((int)entry->unsigned_value);
    case RUNTIME_VARIANT_UINT32:
        return g_variant_new_uint32((uint32_t)entry->unsigned_value);
    case RUNTIME_VARIANT_INT32:
        return g_variant_new_int32((int32_t)entry->signed_value);
    case RUNTIME_VARIANT_UINT64:
        return g_variant_new_uint64(entry->unsigned_value);
    case RUNTIME_VARIANT_INT64:
        return g_variant_new_int64(entry->signed_value);
    default:
        return NULL;
    }
}

char *g_settings_get_string(const GSettings *settings, const char *key) {
    struct runtime_settings_value *entry = runtime_settings_find(
        (GSettings *)settings, key, 0);
    return entry && entry->kind == RUNTIME_VARIANT_STRING
        ? g_strdup(entry->string_value) : NULL;
}

int g_settings_set_string(GSettings *settings, const char *key,
                          const char *value) {
    if (!value) return 0;
    struct runtime_settings_value *entry =
        runtime_settings_find(settings, key, 1);
    if (!entry) return 0;
    entry->kind = RUNTIME_VARIANT_STRING;
    copy_text(entry->string_value, sizeof(entry->string_value), value);
    return 1;
}

int g_settings_get_boolean(const GSettings *settings, const char *key) {
    struct runtime_settings_value *entry = runtime_settings_find(
        (GSettings *)settings, key, 0);
    return entry && entry->kind == RUNTIME_VARIANT_BOOLEAN &&
        entry->unsigned_value != 0U;
}

int g_settings_set_boolean(GSettings *settings, const char *key, int value) {
    struct runtime_settings_value *entry =
        runtime_settings_find(settings, key, 1);
    if (!entry) return 0;
    entry->kind = RUNTIME_VARIANT_BOOLEAN;
    entry->unsigned_value = value != 0;
    return 1;
}

GFile *g_file_new_for_path(const char *path) {
    if (!path || !path[0]) return NULL;
    GFile *file = g_malloc0(sizeof(*file));
    if (!file) return NULL;
    file->references = 1U;
    copy_text(file->path, sizeof(file->path), path);
    size_t length = 0U;
    while (file->path[length]) ++length;
    while (length > 1U && file->path[length - 1U] == '/') {
        file->path[--length] = 0;
    }
    return file;
}

GFile *g_file_new_for_uri(const char *uri) {
    static const char prefix[] = "file://";
    if (!uri || !uri[0]) return NULL;
    size_t prefix_length = sizeof(prefix) - 1U;
    for (size_t index = 0; index < prefix_length; ++index)
        if (uri[index] != prefix[index]) return NULL;
    return g_file_new_for_path(uri + prefix_length);
}

GFile *g_file_ref(GFile *file) {
    if (!file || file->references == 0U ||
        file->references == (uint32_t)-1) return NULL;
    ++file->references;
    return file;
}

void g_file_unref(GFile *file) {
    if (!file || file->references == 0U) return;
    if (file->references > 1U) {
        --file->references;
        return;
    }
    file->references = 0U;
    g_free(file);
}

char *g_file_get_path(const GFile *file) {
    return file && file->references ? g_strdup(file->path) : NULL;
}

char *g_file_get_uri(const GFile *file) {
    if (!file || !file->references) return NULL;
    size_t length = 0U;
    while (file->path[length]) ++length;
    size_t total = length + sizeof("file://");
    char *uri = g_malloc(total);
    if (!uri) return NULL;
    copy_text(uri, total, "file://");
    size_t offset = sizeof("file://") - 1U;
    for (size_t index = 0; index <= length; ++index)
        uri[offset + index] = file->path[index];
    return uri;
}

char *g_file_get_basename(const GFile *file) {
    if (!file || !file->references) return NULL;
    size_t length = 0U;
    while (file->path[length]) ++length;
    if (length == 1U && file->path[0] == '/') return g_strdup("/");
    size_t start = length;
    while (start > 0U && file->path[start - 1U] != '/') --start;
    return g_strdup(file->path + start);
}

GFile *g_file_get_parent(const GFile *file) {
    if (!file || !file->references) return NULL;
    size_t length = 0U;
    while (file->path[length]) ++length;
    if (length == 1U && file->path[0] == '/') return NULL;
    size_t slash = length;
    while (slash > 0U && file->path[slash - 1U] != '/') --slash;
    if (slash == 0U) return g_file_new_for_path(".");
    if (slash == 1U) return g_file_new_for_path("/");
    char parent_path[192];
    size_t parent_length = slash - 1U;
    if (parent_length >= sizeof(parent_path)) return NULL;
    for (size_t index = 0; index < parent_length; ++index)
        parent_path[index] = file->path[index];
    parent_path[parent_length] = 0;
    return g_file_new_for_path(parent_path);
}

int g_file_equal(const GFile *left, const GFile *right) {
    return left && right && left->references && right->references &&
        strcmp_local(left->path, right->path) == 0;
}

int g_file_has_prefix(const GFile *file, const GFile *prefix) {
    if (!file || !prefix || !file->references || !prefix->references) return 0;
    size_t prefix_length = 0U;
    while (prefix->path[prefix_length]) ++prefix_length;
    for (size_t index = 0; index < prefix_length; ++index)
        if (file->path[index] != prefix->path[index]) return 0;
    if (file->path[prefix_length] == 0) return 1;
    return prefix_length == 1U && prefix->path[0] == '/'
        ? file->path[0] == '/' : file->path[prefix_length] == '/';
}

/* Bounded GIO asynchronous primitives.  Completion callbacks are dispatched
 * synchronously by g_task_return_*; callers that need deferred execution can
 * schedule that call through the native GLib-shaped main-loop source API. */
GCancellable *g_cancellable_new(void) {
    GCancellable *cancellable = g_malloc0(sizeof(*cancellable));
    if (cancellable) cancellable->references = 1U;
    return cancellable;
}

GCancellable *g_cancellable_ref(GCancellable *cancellable) {
    if (!cancellable || cancellable->references == 0U ||
        cancellable->references == (uint32_t)-1) return NULL;
    ++cancellable->references;
    return cancellable;
}

void g_cancellable_unref(GCancellable *cancellable) {
    if (!cancellable || cancellable->references == 0U) return;
    if (cancellable->references > 1U) {
        --cancellable->references;
        return;
    }
    cancellable->references = 0U;
    g_free(cancellable);
}

void g_cancellable_cancel(GCancellable *cancellable) {
    if (cancellable && cancellable->references) cancellable->cancelled = 1U;
}

int g_cancellable_is_cancelled(const GCancellable *cancellable) {
    return cancellable && cancellable->references && cancellable->cancelled;
}

GError *g_error_new(uint32_t domain, int code, const char *message) {
    GError *error = g_malloc0(sizeof(*error));
    if (!error) return NULL;
    error->domain = domain;
    error->code = code;
    copy_text(error->message, sizeof(error->message), message);
    return error;
}

GError *g_error_copy(const GError *error) {
    if (!error) return NULL;
    return g_error_new(error->domain, error->code, error->message);
}

void g_error_free(GError *error) {
    if (error) g_free(error);
}

int g_error_matches(const GError *error, uint32_t domain, int code) {
    return error && error->domain == domain && error->code == code;
}

uint32_t g_error_get_domain(const GError *error) {
    return error ? error->domain : 0U;
}

int g_error_get_code(const GError *error) {
    return error ? error->code : 0;
}

const char *g_error_get_message(const GError *error) {
    return error ? error->message : NULL;
}

GTask *g_task_new(void *source_object, GCancellable *cancellable,
                  void (*callback)(void *, void *, void *), void *user_data) {
    GTask *task = g_malloc0(sizeof(*task));
    if (!task) return NULL;
    task->references = 1U;
    task->source_object = source_object;
    task->callback = callback;
    task->user_data = user_data;
    if (cancellable) {
        task->cancellable = g_cancellable_ref(cancellable);
        if (!task->cancellable) {
            g_free(task);
            return NULL;
        }
    }
    return task;
}

GTask *g_task_ref(GTask *task) {
    if (!task || task->references == 0U ||
        task->references == (uint32_t)-1) return NULL;
    ++task->references;
    return task;
}

void g_task_unref(GTask *task) {
    if (!task || task->references == 0U) return;
    if (task->references > 1U) {
        --task->references;
        return;
    }
    task->references = 0U;
    if (task->cancellable) g_cancellable_unref(task->cancellable);
    task->cancellable = NULL;
    if (task->error) g_error_free(task->error);
    task->error = NULL;
    if (task->contents) g_free(task->contents);
    task->contents = NULL;
    task->contents_length = 0U;
    task->callback = NULL;
    task->user_data = NULL;
    g_free(task);
}

void *g_task_get_source_object(const GTask *task) {
    return task && task->references ? task->source_object : NULL;
}

int g_task_get_completed(const GTask *task) {
    return task && task->references && task->completed;
}

void g_task_return_boolean(GTask *task, int result) {
    if (!task || task->references == 0U || task->completed) return;
    task->result = result != 0;
    task->completed = 1U;
    if (task->callback)
        task->callback(task->source_object, task, task->user_data);
}

void g_task_return_error(GTask *task, GError *error) {
    if (!task || task->references == 0U || task->completed) {
        if (error) g_error_free(error);
        return;
    }
    task->error = error;
    task->result = 0U;
    task->completed = 1U;
    if (task->callback)
        task->callback(task->source_object, task, task->user_data);
}

int g_task_return_error_if_cancelled(GTask *task) {
    if (!task || task->references == 0U || task->completed ||
        !g_cancellable_is_cancelled(task->cancellable)) return 0;
    g_task_return_error(task,
                        g_error_new(1U, 125, "Operation cancelled"));
    return 1;
}

int g_task_propagate_boolean(const GTask *task, void **error) {
    if (!task || task->references == 0U || !task->completed) return 0;
    if (error) *error = task->error ? g_error_copy(task->error) : NULL;
    return task->result != 0U;
}

/* Bounded native GIO content APIs.  They intentionally use the akaOS VFS
 * syscall ABI rather than host libc calls so the same DSO works on x86,
 * AArch64, ARM32, and both RISC-V ports. */
#define RUNTIME_FILE_CONTENTS_MAX 8192U
#define RUNTIME_FILE_INITIAL_CAPACITY 128U
#define RUNTIME_FILE_ERROR_DOMAIN 1U
#define RUNTIME_FILE_ERROR_INVALID 22
#define RUNTIME_FILE_ERROR_IO 5
#define RUNTIME_FILE_ERROR_TOO_LARGE 7
#define RUNTIME_FILE_ERROR_CANCELLED 125
#define RUNTIME_O_WRONLY 1
#define RUNTIME_O_CREAT 0x40
#define RUNTIME_O_TRUNC 0x200

static int runtime_file_path_length(const char *path, size_t *length) {
    if (!path || !path[0] || !length) return 0;
    size_t value = 0U;
    while (value <= 127U && path[value]) ++value;
    if (value == 0U || value > 127U || path[value] != 0) return 0;
    *length = value;
    return 1;
}

static void runtime_file_set_error(GError **error, int code,
                                   const char *message);

static int runtime_file_child_path(const GFile *container, const char *name,
                                   char *path, size_t capacity,
                                   size_t *length) {
    if (!container || !container->references || !name || !name[0] ||
        !path || capacity == 0U || !length) return 0;
    size_t parent_length = 0U;
    size_t name_length = 0U;
    if (!runtime_file_path_length(container->path, &parent_length)) return 0;
    while (name_length <= RUNTIME_DIRECTORY_NAME_MAX && name[name_length])
        ++name_length;
    if (name_length == 0U || name_length > RUNTIME_DIRECTORY_NAME_MAX ||
        name[name_length] != 0) return 0;
    int root = parent_length == 1U && container->path[0] == '/';
    size_t separator = root ? 0U : 1U;
    if (parent_length > (size_t)-1 - separator ||
        parent_length + separator > (size_t)-1 - name_length ||
        parent_length + separator + name_length + 1U > capacity) return 0;
    size_t offset = 0U;
    for (size_t index = 0; index < parent_length; ++index)
        path[offset++] = container->path[index];
    if (separator) path[offset++] = '/';
    for (size_t index = 0; index < name_length; ++index)
        path[offset++] = name[index];
    path[offset] = 0;
    *length = offset;
    return 1;
}

GFileEnumerator *g_file_enumerate_children(
    GFile *file, const char *attributes, int flags, GCancellable *cancellable,
    GError **error) {
    (void)attributes;
    (void)flags;
    if (error) *error = NULL;
    if (!file || !file->references ||
        g_cancellable_is_cancelled(cancellable)) {
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_INVALID,
                               "Invalid directory");
        return NULL;
    }
    size_t path_length = 0U;
    if (!runtime_file_path_length(file->path, &path_length)) {
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_INVALID,
                               "Invalid directory path");
        return NULL;
    }
    long descriptor = akaos_desktop_open(file->path, path_length, 0);
    if (descriptor < 0 || descriptor > 0x7fffffffL) {
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_IO,
                               "Unable to open directory");
        return NULL;
    }
    GFileEnumerator *enumerator = g_malloc0(sizeof(*enumerator));
    if (!enumerator) {
        (void)akaos_desktop_close((int)descriptor);
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_IO,
                               "Unable to allocate directory enumerator");
        return NULL;
    }
    enumerator->references = 1U;
    enumerator->descriptor = (int)descriptor;
    enumerator->container = g_file_ref(file);
    if (!enumerator->container) {
        g_free(enumerator);
        (void)akaos_desktop_close((int)descriptor);
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_IO,
                               "Unable to retain directory");
        return NULL;
    }
    return enumerator;
}

GFileEnumerator *g_file_enumerator_ref(GFileEnumerator *enumerator) {
    if (!enumerator || enumerator->references == 0U ||
        enumerator->references == (uint32_t)-1) return NULL;
    ++enumerator->references;
    return enumerator;
}

int g_file_enumerator_close(GFileEnumerator *enumerator,
                            GCancellable *cancellable, GError **error) {
    if (error) *error = NULL;
    if (!enumerator || enumerator->references == 0U) {
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_INVALID,
                               "Invalid directory enumerator");
        return 0;
    }
    if (g_cancellable_is_cancelled(cancellable)) {
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_CANCELLED,
                               "Operation cancelled");
        return 0;
    }
    if (enumerator->closed) return 1;
    long result = akaos_desktop_close(enumerator->descriptor);
    enumerator->descriptor = -1;
    enumerator->closed = 1U;
    if (result < 0) {
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_IO,
                               "Unable to close directory");
        return 0;
    }
    return 1;
}

void g_file_enumerator_unref(GFileEnumerator *enumerator) {
    if (!enumerator || enumerator->references == 0U) return;
    if (enumerator->references > 1U) {
        --enumerator->references;
        return;
    }
    (void)g_file_enumerator_close(enumerator, NULL, NULL);
    enumerator->references = 0U;
    if (enumerator->container) g_file_unref(enumerator->container);
    enumerator->container = NULL;
    g_free(enumerator);
}

GFile *g_file_enumerator_get_container(const GFileEnumerator *enumerator) {
    return enumerator && enumerator->references && !enumerator->closed
        ? enumerator->container : NULL;
}

GFileInfo *g_file_enumerator_next_file(GFileEnumerator *enumerator,
                                       GCancellable *cancellable,
                                       GError **error) {
    if (error) *error = NULL;
    if (!enumerator || enumerator->references == 0U || enumerator->closed) {
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_INVALID,
                               "Directory enumerator is closed");
        return NULL;
    }
    if (g_cancellable_is_cancelled(cancellable)) {
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_CANCELLED,
                               "Operation cancelled");
        return NULL;
    }
    if (enumerator->entry_index >= enumerator->entry_count) {
        long count = akaos_desktop_getdents(
            enumerator->descriptor, enumerator->entries,
            RUNTIME_DIRECTORY_BATCH);
        if (count < 0 || count > (long)RUNTIME_DIRECTORY_BATCH) {
            runtime_file_set_error(error, RUNTIME_FILE_ERROR_IO,
                                   "Unable to enumerate directory");
            return NULL;
        }
        if (count == 0) return NULL;
        enumerator->entry_count = (size_t)count;
        enumerator->entry_index = 0U;
    }
    const struct runtime_dirent *entry =
        &enumerator->entries[enumerator->entry_index++];
    GFileInfo *info = g_malloc0(sizeof(*info));
    if (!info) {
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_IO,
                               "Unable to allocate file information");
        return NULL;
    }
    info->references = 1U;
    info->inode = entry->inode;
    info->file_type = entry->type == 1U ? RUNTIME_FILE_TYPE_DIRECTORY :
                      entry->type == 2U ? RUNTIME_FILE_TYPE_REGULAR :
                      entry->type == 3U ? RUNTIME_FILE_TYPE_SPECIAL :
                      RUNTIME_FILE_TYPE_UNKNOWN;
    copy_text(info->name, sizeof(info->name), entry->name);
    copy_text(info->display_name, sizeof(info->display_name), entry->name);
    char child_path[192];
    size_t child_length = 0U;
    struct runtime_stat status;
    if (runtime_file_child_path(enumerator->container, entry->name,
                                child_path, sizeof(child_path),
                                &child_length) &&
        akaos_desktop_stat(child_path, child_length, &status) >= 0) {
        info->inode = status.inode;
        info->size = status.size;
        info->mode = status.mode;
        info->uid = status.uid;
        info->gid = status.gid;
        info->file_type = status.type == 1U ? RUNTIME_FILE_TYPE_DIRECTORY :
                          status.type == 2U ? RUNTIME_FILE_TYPE_REGULAR :
                          status.type == 3U ? RUNTIME_FILE_TYPE_SPECIAL :
                          info->file_type;
    }
    return info;
}

GFileInfo *g_file_info_ref(GFileInfo *info) {
    if (!info || info->references == 0U ||
        info->references == (uint32_t)-1) return NULL;
    ++info->references;
    return info;
}

void g_file_info_unref(GFileInfo *info) {
    if (!info || info->references == 0U) return;
    if (info->references > 1U) {
        --info->references;
        return;
    }
    info->references = 0U;
    g_free(info);
}

const char *g_file_info_get_name(const GFileInfo *info) {
    return info && info->references ? info->name : NULL;
}

const char *g_file_info_get_display_name(const GFileInfo *info) {
    return info && info->references ? info->display_name : NULL;
}

int g_file_info_get_file_type(const GFileInfo *info) {
    return info && info->references ? (int)info->file_type :
        RUNTIME_FILE_TYPE_UNKNOWN;
}

uint64_t g_file_info_get_size(const GFileInfo *info) {
    return info && info->references ? info->size : 0U;
}

uint64_t g_file_info_get_attribute_uint64(const GFileInfo *info,
                                          const char *attribute) {
    if (!info || !info->references || !attribute) return 0U;
    if (strcmp_local(attribute, "standard::size") == 0 ||
        strcmp_local(attribute, "size") == 0) return info->size;
    if (strcmp_local(attribute, "standard::inode") == 0 ||
        strcmp_local(attribute, "unix::inode") == 0) return info->inode;
    if (strcmp_local(attribute, "unix::uid") == 0) return info->uid;
    if (strcmp_local(attribute, "unix::gid") == 0) return info->gid;
    return 0U;
}

const char *g_file_info_get_attribute_string(const GFileInfo *info,
                                             const char *attribute) {
    if (!info || !info->references || !attribute) return NULL;
    if (strcmp_local(attribute, "standard::name") == 0 ||
        strcmp_local(attribute, "name") == 0) return info->name;
    if (strcmp_local(attribute, "standard::display-name") == 0 ||
        strcmp_local(attribute, "display-name") == 0) return info->display_name;
    return NULL;
}

static void runtime_file_set_error(GError **error, int code,
                                   const char *message) {
    if (error) *error = g_error_new(RUNTIME_FILE_ERROR_DOMAIN, code, message);
}

static int runtime_file_read_path(const char *path, char **contents,
                                  size_t *length, GError **error) {
    if (contents) *contents = NULL;
    if (length) *length = 0U;
    size_t path_length = 0U;
    if (!contents || !length || !runtime_file_path_length(path, &path_length)) {
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_INVALID,
                               "Invalid file path");
        return 0;
    }
    long descriptor = akaos_desktop_open(path, path_length, 0);
    if (descriptor < 0 || descriptor > 0x7fffffffL) {
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_IO,
                               "Unable to open file");
        return 0;
    }
    size_t capacity = RUNTIME_FILE_INITIAL_CAPACITY;
    char *buffer = g_malloc(capacity + 1U);
    if (!buffer) {
        (void)akaos_desktop_close((int)descriptor);
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_IO,
                               "Unable to allocate file contents");
        return 0;
    }
    size_t used = 0U;
    int ok = 1;
    while (1) {
        if (used == RUNTIME_FILE_CONTENTS_MAX) {
            ok = 0;
            runtime_file_set_error(error, RUNTIME_FILE_ERROR_TOO_LARGE,
                                   "File contents exceed runtime limit");
            break;
        }
        size_t available = capacity - used;
        if (available == 0U) {
            size_t next = capacity < 2048U ? capacity * 2U : capacity + 2048U;
            if (next > RUNTIME_FILE_CONTENTS_MAX) next = RUNTIME_FILE_CONTENTS_MAX;
            char *replacement = g_realloc(buffer, next + 1U);
            if (!replacement) {
                ok = 0;
                runtime_file_set_error(error, RUNTIME_FILE_ERROR_IO,
                                       "Unable to grow file contents");
                break;
            }
            buffer = replacement;
            capacity = next;
            available = capacity - used;
        }
        if (available > 4096U) available = 4096U;
        long count = akaos_desktop_read((int)descriptor, buffer + used,
                                        available);
        if (count < 0 || (uint64_t)count > (uint64_t)available) {
            ok = 0;
            runtime_file_set_error(error, RUNTIME_FILE_ERROR_IO,
                                   "Unable to read file");
            break;
        }
        if (count == 0) break;
        used += (size_t)count;
    }
    long close_result = akaos_desktop_close((int)descriptor);
    if (ok && close_result < 0) {
        ok = 0;
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_IO,
                               "Unable to close file");
    }
    if (!ok) {
        g_free(buffer);
        return 0;
    }
    buffer[used] = 0;
    *contents = buffer;
    *length = used;
    return 1;
}

int g_file_get_contents(const char *filename, char **contents, size_t *length,
                        GError **error) {
    return runtime_file_read_path(filename, contents, length, error);
}

int g_file_set_contents(const char *filename, const char *contents, long length,
                        GError **error) {
    size_t path_length = 0U;
    if (!contents || !runtime_file_path_length(filename, &path_length)) {
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_INVALID,
                               "Invalid file path or contents");
        return 0;
    }
    size_t content_length = 0U;
    if (length < 0) {
        while (contents[content_length]) ++content_length;
    } else {
        content_length = (size_t)length;
    }
    long descriptor = akaos_desktop_open(filename, path_length,
                                          RUNTIME_O_WRONLY | RUNTIME_O_CREAT |
                                          RUNTIME_O_TRUNC);
    if (descriptor < 0 || descriptor > 0x7fffffffL) {
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_IO,
                               "Unable to open file for writing");
        return 0;
    }
    size_t offset = 0U;
    int ok = 1;
    while (offset < content_length) {
        size_t chunk = content_length - offset;
        if (chunk > 4096U) chunk = 4096U;
        long written = akaos_desktop_write((int)descriptor, contents + offset,
                                            chunk);
        if (written <= 0 || (uint64_t)written > (uint64_t)chunk) {
            ok = 0;
            runtime_file_set_error(error, RUNTIME_FILE_ERROR_IO,
                                   "Unable to write file");
            break;
        }
        offset += (size_t)written;
    }
    long close_result = akaos_desktop_close((int)descriptor);
    if (ok && close_result < 0) {
        ok = 0;
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_IO,
                               "Unable to close file");
    }
    return ok;
}

int g_file_query_exists(const GFile *file, GCancellable *cancellable) {
    if (!file || !file->references || g_cancellable_is_cancelled(cancellable))
        return 0;
    size_t path_length = 0U;
    if (!runtime_file_path_length(file->path, &path_length)) return 0;
    long descriptor = akaos_desktop_open(file->path, path_length, 0);
    if (descriptor < 0 || descriptor > 0x7fffffffL) return 0;
    (void)akaos_desktop_close((int)descriptor);
    return 1;
}

void g_file_load_contents_async(GFile *file, GCancellable *cancellable,
                                void (*callback)(void *, void *, void *),
                                void *user_data) {
    GTask *task = g_task_new(file, cancellable, callback, user_data);
    if (!task) return;
    if (g_task_return_error_if_cancelled(task)) return;
    char *contents = NULL;
    size_t length = 0U;
    GError *error = NULL;
    if (!file || !file->references ||
        !runtime_file_read_path(file->path, &contents, &length, &error)) {
        if (!error) error = g_error_new(RUNTIME_FILE_ERROR_DOMAIN,
                                        RUNTIME_FILE_ERROR_INVALID,
                                        "Invalid file");
        g_task_return_error(task, error);
        return;
    }
    task->contents = contents;
    task->contents_length = length;
    g_task_return_boolean(task, 1);
}

int g_file_load_contents_finish(GFile *file, GTask *result, char **contents,
                                size_t *length, char **etag_out,
                                GError **error) {
    (void)file;
    if (contents) *contents = NULL;
    if (length) *length = 0U;
    if (etag_out) *etag_out = NULL;
    if (error) *error = NULL;
    if (!result || !result->references || !result->completed) {
        runtime_file_set_error(error, RUNTIME_FILE_ERROR_INVALID,
                               "Async result is incomplete");
        return 0;
    }
    if (result->error) {
        if (error) *error = g_error_copy(result->error);
        return 0;
    }
    if (!result->result) return 0;
    if (contents) {
        *contents = result->contents;
        result->contents = NULL;
    } else if (result->contents) {
        g_free(result->contents);
        result->contents = NULL;
    }
    if (length) *length = result->contents_length;
    result->contents_length = 0U;
    return 1;
}

static void copy_text(char *destination, size_t capacity, const char *source) {
    if (!destination || capacity == 0) return;
    if (!source) source = "";
    size_t index = 0;
    while (index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static int strcmp_local(const char *left, const char *right) {
    if (!left) left = "";
    if (!right) right = "";
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

static struct runtime_loop *find_loop(void *value) {
    for (size_t index = 0; index < RUNTIME_LOOP_CAPACITY; ++index) {
        if (value == &loops[index] && loops[index].used) return &loops[index];
    }
    return NULL;
}

static struct runtime_window *find_window(void *value) {
    for (size_t index = 0; index < RUNTIME_WINDOW_CAPACITY; ++index) {
        if (value == &windows[index] && windows[index].used) return &windows[index];
    }
    return NULL;
}

void *akaos_runtime_loop_new(void) {
    for (size_t index = 0; index < RUNTIME_LOOP_CAPACITY; ++index) {
        if (!loops[index].used) {
            loops[index].used = 1U;
            loops[index].running = 0U;
            loops[index].references = 1U;
            loops[index].context = runtime_context_default();
            runtime_context_ref_internal(loops[index].context);
            return &loops[index];
        }
    }
    return NULL;
}

void *akaos_runtime_loop_ref(void *value) {
    struct runtime_loop *loop = find_loop(value);
    if (loop && loop->references != UINT32_MAX) ++loop->references;
    return value;
}

void akaos_runtime_loop_unref(void *value) {
    struct runtime_loop *loop = find_loop(value);
    if (!loop) return;
    if (loop->references > 1U) {
        --loop->references;
        return;
    }
    loop->running = 0U;
    loop->references = 0U;
    runtime_context_unref_internal(loop->context);
    loop->context = NULL;
    loop->used = 0U;
}

static int runtime_source_due(const struct runtime_source *source) {
    if (!source || !source->used) return 0;
    if (source->kind == RUNTIME_SOURCE_FD)
        return source->revents != 0;
    if (source->interval_ms == 0U) return 1;
    return (int32_t)(runtime_clock_ms - source->due_ms) >= 0;
}

/* Return the next bounded wait needed by the source queue.  A short slice for
 * descriptor-only loops keeps g_main_loop_quit() responsive even though the
 * current kernel poll ABI has no separate wake-up descriptor. */
static int runtime_source_timeout(struct runtime_context *context) {
    context = find_context(context);
    int timeout = -1;
    int have_descriptor = 0;
    int have_source = 0;
    for (size_t index = 0; index < RUNTIME_SOURCE_CAPACITY; ++index) {
        const struct runtime_source *source = &sources[index];
        if (!source->used || source->context != context) continue;
        have_source = 1;
        if (source->kind == RUNTIME_SOURCE_FD) {
            have_descriptor = 1;
            continue;
        }
        if (source->interval_ms == 0U) return 0;
        int32_t delta = (int32_t)(source->due_ms - runtime_clock_ms);
        if (delta < 0) delta = 0;
        if (timeout < 0 || delta < timeout) timeout = delta;
    }
    if (!have_source) return 0;
    if (have_descriptor && (timeout < 0 || timeout > RUNTIME_FD_POLL_SLICE_MS))
        timeout = RUNTIME_FD_POLL_SLICE_MS;
    return timeout;
}

static int runtime_poll_sources(struct runtime_context *context,
                                int timeout_ms) {
    context = find_context(context);
    struct runtime_pollfd descriptors[RUNTIME_SOURCE_CAPACITY];
    size_t source_indices[RUNTIME_SOURCE_CAPACITY];
    size_t count = 0U;
    for (size_t index = 0; index < RUNTIME_SOURCE_CAPACITY; ++index) {
        struct runtime_source *source = &sources[index];
        if (!source->used || source->kind != RUNTIME_SOURCE_FD ||
            source->context != context) continue;
        descriptors[count].fd = source->fd;
        descriptors[count].events = source->events;
        descriptors[count].revents = 0;
        source_indices[count] = index;
        ++count;
    }
    if (count == 0U) return 0;
    long result = akaos_desktop_poll(descriptors, count, timeout_ms);
    if (result < 0) return 0;
    for (size_t index = 0; index < count; ++index) {
        struct runtime_source *source = &sources[source_indices[index]];
        if (source->used && source->kind == RUNTIME_SOURCE_FD)
            source->revents = descriptors[index].revents;
    }
    return (int)result;
}

static int runtime_has_sources(struct runtime_context *context) {
    context = find_context(context);
    for (size_t index = 0; index < RUNTIME_SOURCE_CAPACITY; ++index)
        if ((sources[index].used || sources[index].dispatching) &&
            sources[index].context == context) return 1;
    return 0;
}

static void runtime_source_destroy_notify(struct runtime_source *source) {
    if (!source || !source->destroy_notify) return;
    void (*notify)(void *) = source->destroy_notify;
    void *data = source->data;
    source->destroy_notify = NULL;
    notify(data);
}

static void runtime_source_release(struct runtime_source *source) {
    if (!source) return;
    struct runtime_context *context = source->context;
    source->context = NULL;
    runtime_context_unref_internal(context);
}

static void runtime_source_clear(struct runtime_source *source) {
    if (!source) return;
    runtime_source_release(source);
    source->id = 0U;
    source->callback = NULL;
    source->io_callback = NULL;
    source->fd = -1;
    source->events = 0;
    source->revents = 0;
    source->data = NULL;
}

static int run_one_source(struct runtime_context *context) {
    context = find_context(context);
    for (size_t index = 0; index < RUNTIME_SOURCE_CAPACITY; ++index) {
        struct runtime_source *source = &sources[index];
        if (source->context != context || !runtime_source_due(source)) continue;
        unsigned (*callback)(void *) = source->callback;
        unsigned (*io_callback)(int, unsigned, void *) = source->io_callback;
        uint32_t kind = source->kind;
        int32_t fd = source->fd;
        unsigned condition = (unsigned)(uint16_t)source->revents;
        void *data = source->data;
        source->used = 0U;
        source->dispatching = 1U;
        source->removed = 0U;
        source->revents = 0;
        unsigned keep = kind == RUNTIME_SOURCE_FD
            ? (io_callback && io_callback(fd, condition, data) != 0U)
            : (callback && callback(data) != 0U);
        source->dispatching = 0U;
        if (keep && !source->removed) {
            source->used = 1U;
            if (kind != RUNTIME_SOURCE_FD)
                source->due_ms = runtime_clock_ms + source->interval_ms;
        } else {
            runtime_source_destroy_notify(source);
            source->removed = 0U;
            runtime_source_clear(source);
        }
        return 1;
    }
    return 0;
}

/* Run each source that was present at the start of this pass once.  A source
 * that returns non-zero remains registered for the next pass, but cannot starve
 * later sources by immediately being selected again. */
static unsigned run_source_batch(struct runtime_context *context) {
    context = find_context(context);
    unsigned processed = 0U;
    uint32_t epoch = ++runtime_source_epoch;
    /* Snapshot eligibility before invoking any callback.  A callback may add
     * another source, but that source belongs to the next dispatch pass. */
    for (size_t index = 0; index < RUNTIME_SOURCE_CAPACITY; ++index) {
        struct runtime_source *source = &sources[index];
        if (source->context == context && runtime_source_due(source))
            source->dispatch_epoch = epoch;
    }
    for (size_t index = 0; index < RUNTIME_SOURCE_CAPACITY; ++index) {
        struct runtime_source *source = &sources[index];
        if (source->context != context || !runtime_source_due(source) ||
            source->dispatch_epoch != epoch)
            continue;
        unsigned (*callback)(void *) = source->callback;
        unsigned (*io_callback)(int, unsigned, void *) = source->io_callback;
        uint32_t kind = source->kind;
        int32_t fd = source->fd;
        unsigned condition = (unsigned)(uint16_t)source->revents;
        void *data = source->data;
        source->used = 0U;
        source->dispatching = 1U;
        source->removed = 0U;
        source->revents = 0;
        unsigned keep = kind == RUNTIME_SOURCE_FD
            ? (io_callback && io_callback(fd, condition, data) != 0U)
            : (callback && callback(data) != 0U);
        source->dispatching = 0U;
        if (keep && !source->removed) {
            source->used = 1U;
            if (kind != RUNTIME_SOURCE_FD)
                source->due_ms = runtime_clock_ms + source->interval_ms;
        } else {
            runtime_source_destroy_notify(source);
            source->removed = 0U;
            runtime_source_clear(source);
        }
        source->dispatch_epoch = 0U;
        ++processed;
    }
    return processed;
}

static void runtime_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield" ::: "memory");
#elif defined(__riscv)
    __asm__ volatile("nop" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

void akaos_runtime_loop_run(void *value) {
    struct runtime_loop *loop = find_loop(value);
    if (!loop) return;
    struct runtime_context *context = find_context(loop->context);
    loop->running = 1U;
    /* A real toolkit loop remains active until its owner calls quit.  If a
     * malformed client never registers a source, leave after a bounded idle
     * window instead of burning a CPU forever in a freestanding guest. */
    unsigned empty_passes = 0U;
    while (loop->running) {
        int timeout = runtime_source_timeout(context);
        int ready = runtime_poll_sources(context, timeout);
        if (timeout > 0 && ready == 0)
            runtime_clock_ms += (uint32_t)timeout;
        else
            ++runtime_clock_ms;
        unsigned processed = run_source_batch(context);
        if (processed != 0U) {
            empty_passes = 0U;
            continue;
        }
        if (!runtime_has_sources(context) && ++empty_passes >= 1024U) {
            loop->running = 0U;
            break;
        }
        runtime_relax();
    }
    loop->running = 0U;
}

void akaos_runtime_loop_quit(void *value) {
    struct runtime_loop *loop = find_loop(value);
    if (loop) loop->running = 0U;
}

int akaos_runtime_loop_running(void *value) {
    struct runtime_loop *loop = find_loop(value);
    return loop ? (int)loop->running : 0;
}

static unsigned runtime_source_add(struct runtime_context *context,
                                   unsigned interval_ms,
                                   unsigned (*callback)(void *data),
                                   void *data,
                                   void (*destroy_notify)(void *data)) {
    context = find_context(context);
    for (size_t index = 0; index < RUNTIME_SOURCE_CAPACITY; ++index) {
        if (sources[index].used || sources[index].dispatching) continue;
        unsigned id = next_source_id++;
        if (id == 0U) id = next_source_id++;
        sources[index].used = 1U;
        sources[index].id = id;
        sources[index].kind = RUNTIME_SOURCE_TIMER;
        sources[index].interval_ms = interval_ms;
        sources[index].due_ms = runtime_clock_ms + interval_ms;
        sources[index].dispatching = 0U;
        sources[index].removed = 0U;
        sources[index].dispatch_epoch = 0U;
        sources[index].context = context;
        runtime_context_ref_internal(context);
        sources[index].callback = callback;
        sources[index].io_callback = NULL;
        sources[index].fd = -1;
        sources[index].events = 0;
        sources[index].revents = 0;
        sources[index].data = data;
        sources[index].destroy_notify = destroy_notify;
        return id;
    }
    return 0U;
}

unsigned akaos_runtime_timeout_add(unsigned interval_ms,
                                   unsigned (*callback)(void *data),
                                   void *data) {
    return runtime_source_add(runtime_context_current(), interval_ms, callback,
                               data, NULL);
}

static unsigned runtime_fd_source_add(
    struct runtime_context *context, int fd, unsigned condition,
    unsigned (*callback)(int, unsigned, void *), void *data,
    void (*destroy_notify)(void *data)) {
    if (fd < 0 || !callback || condition == 0U) return 0U;
    context = find_context(context);
    for (size_t index = 0; index < RUNTIME_SOURCE_CAPACITY; ++index) {
        if (sources[index].used || sources[index].dispatching) continue;
        unsigned id = next_source_id++;
        if (id == 0U) id = next_source_id++;
        sources[index].used = 1U;
        sources[index].id = id;
        sources[index].kind = RUNTIME_SOURCE_FD;
        sources[index].interval_ms = 0U;
        sources[index].due_ms = 0U;
        sources[index].dispatching = 0U;
        sources[index].removed = 0U;
        sources[index].dispatch_epoch = 0U;
        sources[index].context = context;
        runtime_context_ref_internal(context);
        sources[index].callback = NULL;
        sources[index].io_callback = callback;
        sources[index].fd = fd;
        sources[index].events = (int16_t)condition;
        sources[index].revents = 0;
        sources[index].data = data;
        sources[index].destroy_notify = destroy_notify;
        return id;
    }
    return 0U;
}

int akaos_runtime_source_remove(unsigned source_id) {
    if (source_id == 0U) return 0;
    for (size_t index = 0; index < RUNTIME_SOURCE_CAPACITY; ++index) {
        if ((sources[index].used || sources[index].dispatching) &&
            sources[index].id == source_id) {
            sources[index].used = 0U;
            if (sources[index].dispatching) {
                sources[index].removed = 1U;
                return 1;
            }
            runtime_source_destroy_notify(&sources[index]);
            runtime_source_clear(&sources[index]);
            return 1;
        }
    }
    return 0;
}

void *akaos_runtime_window_new(void) {
    for (size_t index = 0; index < RUNTIME_WINDOW_CAPACITY; ++index) {
        if (!windows[index].used) {
            windows[index].kind = AKAOS_RUNTIME_WIDGET_CONTAINER;
            windows[index].used = 1U;
            windows[index].visible = 0U;
            windows[index].references = 1U;
            windows[index].sensitive = 1U;
            windows[index].active = 0U;
            windows[index].resizable = 1U;
            windows[index].maximized = 0U;
            windows[index].minimized = 0U;
            windows[index].closed = 0U;
            windows[index].x = 0;
            windows[index].y = 0;
            windows[index].width = 640;
            windows[index].height = 400;
            windows[index].title[0] = '\0';
            windows[index].text[0] = '\0';
            windows[index].name[0] = '\0';
            windows[index].parent = NULL;
            windows[index].child_count = 0U;
            for (size_t child = 0; child < RUNTIME_WIDGET_CHILD_CAPACITY; ++child)
                windows[index].children[child] = NULL;
            for (size_t signal = 0; signal < RUNTIME_SIGNAL_CAPACITY; ++signal)
                windows[index].signals[signal].used = 0U;
            for (size_t data = 0; data < RUNTIME_DATA_CAPACITY; ++data) {
                windows[index].data[data].used = 0U;
                windows[index].data[data].instance = NULL;
                windows[index].data[data].key[0] = 0;
                windows[index].data[data].value = NULL;
            }
            return &windows[index];
        }
    }
    return NULL;
}

void *akaos_runtime_widget_new(unsigned kind) {
    if (kind < AKAOS_RUNTIME_WIDGET_CONTAINER ||
        kind > AKAOS_RUNTIME_WIDGET_SCROLL)
        return NULL;
    void *value = akaos_runtime_window_new();
    struct runtime_window *widget = find_window(value);
    if (widget) widget->kind = kind;
    return value;
}

int akaos_runtime_widget_add(void *parent_value, void *child_value) {
    struct runtime_window *parent = find_window(parent_value);
    struct runtime_window *child = find_window(child_value);
    if (!parent || !child || parent == child || child->parent ||
        parent->child_count >= RUNTIME_WIDGET_CHILD_CAPACITY)
        return 0;
    parent->children[parent->child_count++] = child;
    child->parent = parent;
    return 1;
}

int akaos_runtime_widget_remove(void *parent_value, void *child_value) {
    struct runtime_window *parent = find_window(parent_value);
    struct runtime_window *child = find_window(child_value);
    if (!parent || !child || child->parent != parent) return 0;
    for (uint32_t index = 0; index < parent->child_count; ++index) {
        if (parent->children[index] != child) continue;
        for (uint32_t move = index + 1U; move < parent->child_count; ++move)
            parent->children[move - 1U] = parent->children[move];
        --parent->child_count;
        parent->children[parent->child_count] = NULL;
        child->parent = NULL;
        return 1;
    }
    return 0;
}

int akaos_runtime_widget_set_text(void *value, const char *text) {
    struct runtime_window *widget = find_window(value);
    if (!widget) return 0;
    copy_text(widget->text, sizeof(widget->text), text);
    return 1;
}

int akaos_runtime_widget_set_size_request(void *value, int width, int height) {
    struct runtime_window *widget = find_window(value);
    if (!widget || width <= 0 || height <= 0) return 0;
    widget->width = width;
    widget->height = height;
    return 1;
}

int akaos_runtime_widget_get_size(void *value, int *width, int *height) {
    struct runtime_window *widget = find_window(value);
    if (!widget || !width || !height) return 0;
    *width = widget->width;
    *height = widget->height;
    return 1;
}

int akaos_runtime_widget_get_visible(void *value) {
    struct runtime_window *widget = find_window(value);
    return widget ? (int)widget->visible : 0;
}

void akaos_runtime_widget_set_visible(void *value, int visible) {
    struct runtime_window *widget = find_window(value);
    if (widget) widget->visible = visible != 0;
}

void akaos_runtime_widget_set_sensitive(void *value, int sensitive) {
    struct runtime_window *widget = find_window(value);
    if (widget) widget->sensitive = sensitive != 0;
}

int akaos_runtime_widget_get_sensitive(void *value) {
    struct runtime_window *widget = find_window(value);
    return widget ? (int)widget->sensitive : 0;
}

void *akaos_runtime_widget_get_parent(void *value) {
    struct runtime_window *widget = find_window(value);
    return widget ? (void *)widget->parent : NULL;
}

int akaos_runtime_widget_set_name(void *value, const char *name) {
    struct runtime_window *widget = find_window(value);
    if (!widget) return 0;
    copy_text(widget->name, sizeof(widget->name), name);
    return 1;
}

const char *akaos_runtime_widget_get_name(void *value) {
    struct runtime_window *widget = find_window(value);
    return widget ? widget->name : NULL;
}

int akaos_runtime_window_set_title(void *value, const char *title) {
    struct runtime_window *window = find_window(value);
    if (!window) return 0;
    copy_text(window->title, sizeof(window->title), title);
    return 1;
}

int akaos_runtime_window_set_size(void *value, int width, int height) {
    struct runtime_window *window = find_window(value);
    if (!window || width <= 0 || height <= 0) return 0;
    window->width = width;
    window->height = height;
    return 1;
}

int akaos_runtime_window_set_resizable(void *value, int resizable) {
    struct runtime_window *window = find_window(value);
    if (!window) return 0;
    window->resizable = resizable != 0;
    return 1;
}

int akaos_runtime_window_get_resizable(const void *value) {
    struct runtime_window *window = find_window((void *)value);
    return window ? (int)window->resizable : 0;
}

int akaos_runtime_window_maximize(void *value) {
    struct runtime_window *window = find_window(value);
    if (!window) return 0;
    window->maximized = 1U;
    window->minimized = 0U;
    window->closed = 0U;
    window->visible = 1U;
    return 1;
}

int akaos_runtime_window_unmaximize(void *value) {
    struct runtime_window *window = find_window(value);
    if (!window) return 0;
    window->maximized = 0U;
    return 1;
}

int akaos_runtime_window_is_maximized(const void *value) {
    struct runtime_window *window = find_window((void *)value);
    return window ? (int)window->maximized : 0;
}

int akaos_runtime_window_minimize(void *value) {
    struct runtime_window *window = find_window(value);
    if (!window) return 0;
    window->minimized = 1U;
    window->maximized = 0U;
    window->visible = 0U;
    return 1;
}

int akaos_runtime_window_unminimize(void *value) {
    struct runtime_window *window = find_window(value);
    if (!window) return 0;
    window->minimized = 0U;
    window->visible = 1U;
    return 1;
}

int akaos_runtime_window_is_minimized(const void *value) {
    struct runtime_window *window = find_window((void *)value);
    return window ? (int)window->minimized : 0;
}

int akaos_runtime_window_close(void *value) {
    struct runtime_window *window = find_window(value);
    if (!window) return 0;
    window->closed = 1U;
    window->visible = 0U;
    window->minimized = 0U;
    window->maximized = 0U;
    return 1;
}

int akaos_runtime_window_show(void *value) {
    struct runtime_window *window = find_window(value);
    if (!window) return 0;
    window->visible = 1U;
    window->minimized = 0U;
    window->closed = 0U;
    return 1;
}

int akaos_runtime_window_hide(void *value) {
    struct runtime_window *window = find_window(value);
    if (!window) return 0;
    window->visible = 0U;
    return 1;
}

int akaos_runtime_window_destroy(void *value) {
    struct runtime_window *window = find_window(value);
    if (!window) return 0;
    if (window->parent) {
        struct runtime_window *parent = window->parent;
        for (uint32_t index = 0; index < parent->child_count; ++index) {
            if (parent->children[index] != window) continue;
            for (uint32_t move = index + 1U; move < parent->child_count; ++move)
                parent->children[move - 1U] = parent->children[move];
            --parent->child_count;
            parent->children[parent->child_count] = NULL;
            break;
        }
        window->parent = NULL;
    }
    for (uint32_t index = 0; index < window->child_count; ++index) {
        if (window->children[index]) window->children[index]->parent = NULL;
        window->children[index] = NULL;
    }
    window->child_count = 0U;
    for (uint32_t index = 0; index < RUNTIME_SIGNAL_CAPACITY; ++index)
        window->signals[index].used = 0U;
    for (uint32_t index = 0; index < RUNTIME_GLOBAL_SIGNAL_CAPACITY; ++index) {
        if (global_signals[index].used &&
            global_signals[index].instance == value) {
            global_signals[index].used = 0U;
            global_signals[index].instance = NULL;
            global_signals[index].id = 0U;
            global_signals[index].callback = NULL;
            global_signals[index].data = NULL;
            global_signals[index].name[0] = 0;
        }
    }
    for (uint32_t index = 0; index < RUNTIME_GLOBAL_SIGNAL_CAPACITY; ++index) {
        if (global_object_data[index].used &&
            global_object_data[index].instance == value) {
            global_object_data[index].used = 0U;
            global_object_data[index].instance = NULL;
            global_object_data[index].key[0] = 0;
            global_object_data[index].value = NULL;
        }
    }
    for (uint32_t index = 0; index < RUNTIME_PROPERTY_CAPACITY; ++index) {
        if (global_object_properties[index].used &&
            global_object_properties[index].instance == value) {
            global_object_properties[index].used = 0U;
            global_object_properties[index].instance = NULL;
            global_object_properties[index].name[0] = 0;
            zero_bytes(&global_object_properties[index].value,
                       sizeof(global_object_properties[index].value));
        }
    }
    for (uint32_t index = 0; index < RUNTIME_BINDING_CAPACITY; ++index) {
        if (global_bindings[index].used &&
            (global_bindings[index].source == value ||
             global_bindings[index].target == value))
            runtime_binding_clear(&global_bindings[index]);
    }
    window->used = 0U;
    return 1;
}

/* GLib-shaped event-loop and identity entry points. */
void *g_main_loop_new(void *context, int running) {
    void *loop = akaos_runtime_loop_new();
    /* GLib's constructor records the requested state but never enters the
     * dispatch loop; callers must be able to receive the handle, add sources,
     * and then invoke g_main_loop_run(). */
    if (loop && running) {
        struct runtime_loop *runtime = find_loop(loop);
        if (runtime) {
            struct runtime_context *requested = find_context(context);
            if (runtime->context != requested) {
                runtime_context_ref_internal(requested);
                runtime_context_unref_internal(runtime->context);
                runtime->context = requested;
            }
            runtime->running = 1U;
        }
    } else if (loop) {
        struct runtime_loop *runtime = find_loop(loop);
        if (runtime) {
            struct runtime_context *requested = find_context(context);
            if (runtime->context != requested) {
                runtime_context_ref_internal(requested);
                runtime_context_unref_internal(runtime->context);
                runtime->context = requested;
            }
        }
    }
    return loop;
}
void g_main_loop_run(void *loop) { akaos_runtime_loop_run(loop); }
void g_main_loop_quit(void *loop) { akaos_runtime_loop_quit(loop); }
void *g_main_loop_ref(void *loop) { return akaos_runtime_loop_ref(loop); }
void g_main_loop_unref(void *loop) { akaos_runtime_loop_unref(loop); }
int g_main_loop_is_running(void *loop) { return akaos_runtime_loop_running(loop); }
unsigned g_timeout_add(unsigned interval, unsigned (*callback)(void *), void *data) {
    return akaos_runtime_timeout_add(interval, callback, data);
}
unsigned g_idle_add(unsigned (*callback)(void *), void *data) {
    return akaos_runtime_timeout_add(0U, callback, data);
}
unsigned g_timeout_add_full(int priority, unsigned interval,
                            unsigned (*callback)(void *), void *data,
                            void (*destroy_notify)(void *)) {
    (void)priority;
    return runtime_source_add(runtime_context_current(), interval, callback,
                               data, destroy_notify);
}
unsigned g_idle_add_full(int priority, unsigned (*callback)(void *),
                         void *data, void (*destroy_notify)(void *)) {
    (void)priority;
    return runtime_source_add(runtime_context_current(), 0U, callback, data,
                               destroy_notify);
}
unsigned g_unix_fd_add(int fd, unsigned condition,
                       unsigned (*callback)(int, unsigned, void *),
                       void *data) {
    return runtime_fd_source_add(runtime_context_current(), fd, condition,
                                 callback, data, NULL);
}
unsigned g_unix_fd_add_full(int priority, int fd, unsigned condition,
                            unsigned (*callback)(int, unsigned, void *),
                            void *data, void (*destroy_notify)(void *)) {
    (void)priority;
    return runtime_fd_source_add(runtime_context_current(), fd, condition,
                                 callback, data, destroy_notify);
}
void *g_io_channel_unix_new(int fd) {
    if (fd < 0) return NULL;
    for (size_t index = 0; index < RUNTIME_IO_CHANNEL_CAPACITY; ++index) {
        struct runtime_io_channel *channel = &io_channels[index];
        if (channel->used) continue;
        channel->used = 1U;
        channel->references = 1U;
        channel->fd = fd;
        return channel;
    }
    return NULL;
}
void *g_io_channel_ref(void *value) {
    struct runtime_io_channel *channel = find_io_channel(value);
    if (channel && channel->references != UINT32_MAX) ++channel->references;
    return channel;
}
void g_io_channel_unref(void *value) {
    runtime_io_channel_unref_internal(find_io_channel(value));
}
int g_io_channel_get_fd(void *value) {
    struct runtime_io_channel *channel = find_io_channel(value);
    return channel ? channel->fd : -1;
}

static unsigned runtime_io_watch_add(
    int priority, void *channel_value, unsigned condition,
    unsigned (*callback)(void *channel, unsigned condition, void *data),
    void *data, void (*destroy_notify)(void *data)) {
    (void)priority;
    struct runtime_io_channel *channel = find_io_channel(channel_value);
    if (!channel || !callback || condition == 0U) return 0U;
    for (size_t index = 0; index < RUNTIME_IO_WATCH_CAPACITY; ++index) {
        struct runtime_io_watch *watch = &io_watches[index];
        if (watch->used) continue;
        if (!g_io_channel_ref(channel)) return 0U;
        watch->used = 1U;
        watch->source_id = 0U;
        watch->channel = channel;
        watch->callback = callback;
        watch->data = data;
        watch->destroy_notify = destroy_notify;
        unsigned source_id = runtime_fd_source_add(
            runtime_context_current(), channel->fd, condition,
            runtime_io_watch_dispatch, watch, runtime_io_watch_destroy);
        if (source_id == 0U) {
            watch->used = 0U;
            watch->channel = NULL;
            watch->callback = NULL;
            watch->data = NULL;
            watch->destroy_notify = NULL;
            runtime_io_channel_unref_internal(channel);
            return 0U;
        }
        watch->source_id = source_id;
        return source_id;
    }
    return 0U;
}

unsigned g_io_add_watch(void *channel, unsigned condition,
                        unsigned (*callback)(void *, unsigned, void *),
                        void *data) {
    return runtime_io_watch_add(0, channel, condition, callback, data, NULL);
}
unsigned g_io_add_watch_full(int priority, void *channel, unsigned condition,
                             unsigned (*callback)(void *, unsigned, void *),
                             void *data, void (*destroy_notify)(void *)) {
    return runtime_io_watch_add(priority, channel, condition, callback, data,
                                destroy_notify);
}
int g_source_remove(unsigned source_id) { return akaos_runtime_source_remove(source_id); }
int g_main_context_iteration(void *context, int may_block) {
    context = find_context(context);
    int timeout = may_block ? runtime_source_timeout(context) : 0;
    int ready = runtime_poll_sources(context, timeout);
    if (timeout > 0 && ready == 0)
        runtime_clock_ms += (uint32_t)timeout;
    else
        ++runtime_clock_ms;
    return run_one_source(context);
}
int g_main_context_pending(void *context) {
    context = find_context(context);
    for (size_t index = 0; index < RUNTIME_SOURCE_CAPACITY; ++index)
        if (sources[index].context == context &&
            runtime_source_due(&sources[index])) return 1;
    return 0;
}
void *g_main_context_default(void) { return runtime_context_default(); }
void *g_main_context_new(void) {
    for (size_t index = 1U; index < RUNTIME_CONTEXT_CAPACITY; ++index) {
        if (contexts[index].used) continue;
        contexts[index].used = 1U;
        contexts[index].references = 1U;
        return &contexts[index];
    }
    return NULL;
}
void *g_main_context_ref(void *context) {
    struct runtime_context *runtime = find_context(context);
    runtime_context_ref_internal(runtime);
    return runtime;
}
void g_main_context_unref(void *context) {
    struct runtime_context *runtime = find_context(context);
    runtime_context_unref_internal(runtime);
}
void g_main_context_push_thread_default(void *context) {
    struct runtime_context *runtime = find_context(context);
    if (!runtime) return;
    if (runtime_thread_default_context == runtime) return;
    if (runtime_thread_default_context)
        runtime_context_unref_internal(runtime_thread_default_context);
    runtime_context_ref_internal(runtime);
    runtime_thread_default_context = runtime;
}
void g_main_context_pop_thread_default(void *context) {
    struct runtime_context *runtime = find_context(context);
    if (!runtime_thread_default_context || runtime_thread_default_context != runtime)
        return;
    runtime_thread_default_context = NULL;
    runtime_context_unref_internal(runtime);
}
const char *g_get_user_name(void) { return "root"; }
const char *g_get_home_dir(void) { return "/root"; }
const char *g_get_tmp_dir(void) { return "/tmp"; }
void g_set_application_name(const char *name) { application_name = name; }
const char *g_get_application_name(void) { return application_name; }

/* GObject-shaped lifetime helpers for runtime windows. */
void *g_object_ref(void *object) {
    struct runtime_window *window = find_window(object);
    if (window) ++window->references;
    return object;
}
void g_object_unref(void *object) {
    struct runtime_window *window = find_window(object);
    if (!window) return;
    if (window->references > 1U) --window->references;
    else (void)akaos_runtime_window_destroy(object);
}
void *g_object_ref_sink(void *object) { return g_object_ref(object); }
int g_object_is_floating(void *object) {
    (void)object;
    return 0;
}

void g_object_set_data(void *object, const char *key, void *data) {
    if (!object || !key) return;
    struct runtime_data *free_slot = NULL;
    for (size_t index = 0; index < RUNTIME_GLOBAL_SIGNAL_CAPACITY; ++index) {
        struct runtime_data *slot = &global_object_data[index];
        if (!slot->used) {
            if (!free_slot) free_slot = slot;
            continue;
        }
        if (slot->instance == object && strcmp_local(slot->key, key) == 0) {
            slot->value = data;
            return;
        }
    }
    if (!free_slot) return;
    free_slot->used = 1U;
    free_slot->instance = object;
    copy_text(free_slot->key, sizeof(free_slot->key), key);
    free_slot->value = data;
}

void *g_object_get_data(const void *object, const char *key) {
    if (!object || !key) return NULL;
    for (size_t index = 0; index < RUNTIME_GLOBAL_SIGNAL_CAPACITY; ++index) {
        if (global_object_data[index].used &&
            global_object_data[index].instance == object &&
            strcmp_local(global_object_data[index].key, key) == 0)
            return global_object_data[index].value;
    }
    return NULL;
}

void *g_object_steal_data(void *object, const char *key) {
    if (!object || !key) return NULL;
    for (size_t index = 0; index < RUNTIME_GLOBAL_SIGNAL_CAPACITY; ++index) {
        struct runtime_data *slot = &global_object_data[index];
        if (!slot->used || slot->instance != object ||
            strcmp_local(slot->key, key) != 0) continue;
        void *value = slot->value;
        slot->used = 0U;
        slot->instance = NULL;
        slot->key[0] = 0;
        slot->value = NULL;
        return value;
    }
    return NULL;
}

static void runtime_value_clear(struct runtime_gvalue *value) {
    if (value) zero_bytes(value, sizeof(*value));
}

static int runtime_value_type(const struct runtime_gvalue *value,
                              char expected) {
    return value && value->initialized && value->type_string[0] == expected;
}

static void runtime_value_set_type(struct runtime_gvalue *value,
                                   const char *type_string) {
    if (!value) return;
    runtime_value_clear(value);
    if (!type_string || (type_string[0] != 's' && type_string[0] != 'b' &&
                         type_string[0] != 'i' && type_string[0] != 'u') ||
        type_string[1] != 0) return;
    value->initialized = 1U;
    copy_text(value->type_string, sizeof(value->type_string), type_string);
}

void g_value_init(struct runtime_gvalue *value, const char *type_string) {
    runtime_value_set_type(value, type_string);
}

void g_value_unset(struct runtime_gvalue *value) {
    runtime_value_clear(value);
}

void g_value_set_string(struct runtime_gvalue *value, const char *text) {
    runtime_value_set_type(value, "s");
    if (value && value->initialized)
        copy_text(value->string_value, sizeof(value->string_value), text);
}

const char *g_value_get_string(const struct runtime_gvalue *value) {
    return runtime_value_type(value, 's') ? value->string_value : NULL;
}

void g_value_set_boolean(struct runtime_gvalue *value, int state) {
    runtime_value_set_type(value, "b");
    if (value && value->initialized) value->unsigned_value = state != 0;
}

int g_value_get_boolean(const struct runtime_gvalue *value) {
    return runtime_value_type(value, 'b') ?
        (value->unsigned_value != 0U) : 0;
}

void g_value_set_int(struct runtime_gvalue *value, int value_int) {
    runtime_value_set_type(value, "i");
    if (value && value->initialized) value->signed_value = value_int;
}

int g_value_get_int(const struct runtime_gvalue *value) {
    return runtime_value_type(value, 'i') ? (int)value->signed_value : 0;
}

void g_value_set_uint(struct runtime_gvalue *value, unsigned value_uint) {
    runtime_value_set_type(value, "u");
    if (value && value->initialized) value->unsigned_value = value_uint;
}

unsigned g_value_get_uint(const struct runtime_gvalue *value) {
    return runtime_value_type(value, 'u') ?
        (unsigned)value->unsigned_value : 0U;
}

static struct runtime_property *runtime_property_find(void *instance,
                                                       const char *name) {
    if (!instance || !name) return NULL;
    for (size_t index = 0; index < RUNTIME_PROPERTY_CAPACITY; ++index) {
        struct runtime_property *property = &global_object_properties[index];
        if (property->used && property->instance == instance &&
            strcmp_local(property->name, name) == 0) return property;
    }
    return NULL;
}

int g_object_set_property(void *object, const char *property_name,
                          const struct runtime_gvalue *value) {
    if (!object || !property_name || !value || !value->initialized ||
        property_name[0] == 0) return 0;
    struct runtime_property *property = runtime_property_find(object,
                                                               property_name);
    if (!property) {
        for (size_t index = 0; index < RUNTIME_PROPERTY_CAPACITY; ++index) {
            if (global_object_properties[index].used) continue;
            property = &global_object_properties[index];
            property->used = 1U;
            property->instance = object;
            copy_text(property->name, sizeof(property->name), property_name);
            break;
        }
    }
    if (!property) return 0;
    copy_bytes(&property->value, value, sizeof(property->value));
    runtime_binding_propagate(object, property_name, value);
    return 1;
}

int g_object_get_property(const void *object, const char *property_name,
                          struct runtime_gvalue *value) {
    if (!object || !property_name || !value) return 0;
    struct runtime_property *property = runtime_property_find((void *)object,
                                                               property_name);
    if (!property) return 0;
    copy_bytes(value, &property->value, sizeof(*value));
    return 1;
}

static int runtime_property_is_string(const char *name) {
    return name && (strcmp_local(name, "title") == 0 ||
                    strcmp_local(name, "text") == 0 ||
                    strcmp_local(name, "name") == 0 ||
                    strcmp_local(name, "objectName") == 0 ||
                    strcmp_local(name, "windowTitle") == 0);
}

static int runtime_property_is_boolean(const char *name) {
    return name && (strcmp_local(name, "visible") == 0 ||
                    strcmp_local(name, "sensitive") == 0 ||
                    strcmp_local(name, "enabled") == 0 ||
                    strcmp_local(name, "active") == 0 ||
                    strcmp_local(name, "resizable") == 0 ||
                    strcmp_local(name, "maximized") == 0 ||
                    strcmp_local(name, "minimized") == 0);
}

/* GTK and GLib compatibility objects are separate DSOs but intentionally
 * share the fixed runtime_window layout.  Local find_window() tables cannot
 * validate a handle allocated by the other DSO; the used bit is the common
 * lifetime marker at this ABI boundary. */
static struct runtime_window *shared_window(void *object) {
    struct runtime_window *window = object;
    return window && window->used ? window : NULL;
}

static void runtime_apply_property(void *object, const char *name,
                                   const struct runtime_gvalue *value) {
    if (!object || !name || !value) return;
    struct runtime_window *window = shared_window(object);
    if (!window) return;
    if (runtime_property_is_string(name)) {
        const char *text = value->string_value;
        if (strcmp_local(name, "title") == 0 ||
            strcmp_local(name, "windowTitle") == 0)
            copy_text(window->title, sizeof(window->title), text);
        else if (strcmp_local(name, "name") == 0 ||
                 strcmp_local(name, "objectName") == 0)
            copy_text(window->name, sizeof(window->name), text);
        else
            copy_text(window->text, sizeof(window->text), text);
    } else if (runtime_property_is_boolean(name)) {
        int enabled = value->unsigned_value != 0U;
        if (strcmp_local(name, "visible") == 0) {
            window->visible = enabled;
        } else if (strcmp_local(name, "sensitive") == 0 ||
                   strcmp_local(name, "enabled") == 0) {
            window->sensitive = enabled;
        } else if (strcmp_local(name, "resizable") == 0) {
            window->resizable = enabled;
        } else if (strcmp_local(name, "maximized") == 0) {
            window->maximized = enabled;
            if (enabled) window->minimized = 0U;
        } else if (strcmp_local(name, "minimized") == 0) {
            window->minimized = enabled;
            if (enabled) window->maximized = 0U;
        } else if (strcmp_local(name, "active") == 0) {
            window->active = enabled;
        }
    } else if (strcmp_local(name, "width") == 0) {
        window->width = (int32_t)value->signed_value;
    } else if (strcmp_local(name, "height") == 0) {
        window->height = (int32_t)value->signed_value;
    } else if (strcmp_local(name, "x") == 0) {
        window->x = (int32_t)value->signed_value;
    } else if (strcmp_local(name, "y") == 0) {
        window->y = (int32_t)value->signed_value;
    }
}

void g_object_set(void *object, const char *first_property_name, ...) {
    if (!object || !first_property_name) return;
    va_list arguments;
    va_start(arguments, first_property_name);
    const char *name = first_property_name;
    while (name) {
        struct runtime_gvalue value;
        zero_bytes(&value, sizeof(value));
        if (runtime_property_is_string(name)) {
            const char *text = va_arg(arguments, const char *);
            runtime_value_set_type(&value, "s");
            copy_text(value.string_value, sizeof(value.string_value), text);
        } else if (runtime_property_is_boolean(name)) {
            int state = va_arg(arguments, int);
            runtime_value_set_type(&value, "b");
            value.unsigned_value = state != 0;
        } else if (strcmp_local(name, "width") == 0 ||
                   strcmp_local(name, "height") == 0 ||
                   strcmp_local(name, "x") == 0 ||
                   strcmp_local(name, "y") == 0) {
            int number = va_arg(arguments, int);
            runtime_value_set_type(&value, "i");
            value.signed_value = number;
        } else {
            /* Unknown vararg types cannot be decoded safely at this bounded
             * ABI boundary.  Stop before consuming an incorrectly typed
             * value rather than corrupting the caller's stack/register save
             * area. */
            break;
        }
        if (value.initialized && g_object_set_property(object, name, &value))
            runtime_apply_property(object, name, &value);
        name = va_arg(arguments, const char *);
    }
    va_end(arguments);
}

void g_object_get(const void *object, const char *first_property_name, ...) {
    if (!object || !first_property_name) return;
    va_list arguments;
    va_start(arguments, first_property_name);
    const char *name = first_property_name;
    while (name) {
        void *destination = va_arg(arguments, void *);
        struct runtime_gvalue value;
        zero_bytes(&value, sizeof(value));
        int found = g_object_get_property(object, name, &value);
        if (!found && runtime_property_is_boolean(name)) {
            runtime_value_set_type(&value, "b");
            if (strcmp_local(name, "visible") == 0)
                value.unsigned_value = akaos_runtime_widget_get_visible((void *)object);
            else if (strcmp_local(name, "sensitive") == 0 ||
                     strcmp_local(name, "enabled") == 0)
                value.unsigned_value = akaos_runtime_widget_get_sensitive((void *)object);
            else if (strcmp_local(name, "resizable") == 0)
                value.unsigned_value = akaos_runtime_window_get_resizable(object);
            else {
                struct runtime_window *window = find_window((void *)object);
                if (window) {
                    if (strcmp_local(name, "active") == 0)
                        value.unsigned_value = window->active;
                    else if (strcmp_local(name, "maximized") == 0)
                        value.unsigned_value = window->maximized;
                    else if (strcmp_local(name, "minimized") == 0)
                        value.unsigned_value = window->minimized;
                }
            }
            found = 1;
        }
        if (!found && runtime_property_is_string(name)) {
            const char *text = NULL;
            if (strcmp_local(name, "title") == 0 ||
                strcmp_local(name, "windowTitle") == 0)
                text = shared_window((void *)object)
                    ? shared_window((void *)object)->title : NULL;
            else if (strcmp_local(name, "name") == 0 ||
                     strcmp_local(name, "objectName") == 0)
                text = akaos_runtime_widget_get_name((void *)object);
            if (text) {
                runtime_value_set_type(&value, "s");
                copy_text(value.string_value, sizeof(value.string_value), text);
                found = 1;
            }
        }
        if (destination && found) {
            if (runtime_property_is_string(name))
                *(char **)destination = g_strdup(value.string_value);
            else if (runtime_property_is_boolean(name))
                *(int *)destination = value.unsigned_value != 0U;
            else
                *(int *)destination = (int)value.signed_value;
        }
        name = va_arg(arguments, const char *);
    }
    va_end(arguments);
}

static void runtime_binding_clear(struct runtime_binding *binding) {
    if (!binding) return;
    binding->used = 0U;
    binding->references = 0U;
    binding->flags = 0U;
    binding->updating = 0U;
    binding->source = NULL;
    binding->target = NULL;
    binding->source_property[0] = 0;
    binding->target_property[0] = 0;
}

static void runtime_binding_propagate(void *instance, const char *property_name,
                                      const struct runtime_gvalue *value) {
    if (!instance || !property_name || !value) return;
    for (size_t index = 0; index < RUNTIME_BINDING_CAPACITY; ++index) {
        struct runtime_binding *binding = &global_bindings[index];
        if (!binding->used || binding->updating) continue;
        void *destination = NULL;
        const char *destination_name = NULL;
        if (binding->source == instance &&
            strcmp_local(binding->source_property, property_name) == 0) {
            destination = binding->target;
            destination_name = binding->target_property;
        } else if ((binding->flags & 1U) != 0U && binding->target == instance &&
                   strcmp_local(binding->target_property, property_name) == 0) {
            destination = binding->source;
            destination_name = binding->source_property;
        }
        if (!destination || !destination_name) continue;
        binding->updating = 1U;
        (void)g_object_set_property(destination, destination_name, value);
        binding->updating = 0U;
    }
}

GBinding *g_object_bind_property(void *source, const char *source_property,
                                 void *target, const char *target_property,
                                 unsigned flags) {
    if (!source || !source_property || !source_property[0] || !target ||
        !target_property || !target_property[0]) return NULL;
    for (size_t index = 0; index < RUNTIME_BINDING_CAPACITY; ++index) {
        struct runtime_binding *binding = &global_bindings[index];
        if (binding->used) continue;
        binding->used = 1U;
        binding->references = 1U;
        binding->flags = flags & 3U;
        binding->updating = 0U;
        binding->source = source;
        binding->target = target;
        copy_text(binding->source_property, sizeof(binding->source_property),
                  source_property);
        copy_text(binding->target_property, sizeof(binding->target_property),
                  target_property);
        /* G_BINDING_SYNC_CREATE is bit 2: copy an already-present source
         * value once when the binding is created. */
        if ((flags & 2U) != 0U) {
            struct runtime_gvalue initial;
            runtime_value_clear(&initial);
            if (g_object_get_property(source, source_property, &initial))
                runtime_binding_propagate(source, source_property, &initial);
        }
        return (GBinding *)binding;
    }
    return NULL;
}

GBinding *g_binding_ref(GBinding *value) {
    struct runtime_binding *binding = (struct runtime_binding *)value;
    if (binding && binding->used) ++binding->references;
    return value;
}

void g_binding_unbind(GBinding *value) {
    struct runtime_binding *binding = (struct runtime_binding *)value;
    if (!binding || !binding->used) return;
    if (binding->references > 1U) {
        --binding->references;
        return;
    }
    runtime_binding_clear(binding);
}

unsigned g_signal_connect(void *instance, const char *detailed_signal,
                          void (*callback)(void *, void *), void *data) {
    if (!instance || !detailed_signal || !callback) return 0U;
    for (uint32_t index = 0; index < RUNTIME_GLOBAL_SIGNAL_CAPACITY; ++index) {
        struct runtime_signal *signal = &global_signals[index];
        if (signal->used) continue;
        unsigned id = next_signal_id++;
        if (id == 0U) id = next_signal_id++;
        signal->used = 1U;
        signal->instance = instance;
        signal->id = id;
        copy_text(signal->name, sizeof(signal->name), detailed_signal);
        signal->callback = callback;
        signal->data = data;
        return id;
    }
    return 0U;
}

int g_signal_emit_by_name(void *instance, const char *detailed_signal) {
    if (!instance || !detailed_signal) return 0;
    int emitted = 0;
    for (uint32_t index = 0; index < RUNTIME_GLOBAL_SIGNAL_CAPACITY; ++index) {
        struct runtime_signal *signal = &global_signals[index];
        if (!signal->used || signal->instance != instance ||
            strcmp_local(signal->name, detailed_signal) != 0)
            continue;
        if (signal->callback) signal->callback(instance, signal->data);
        ++emitted;
    }
    return emitted;
}

int g_signal_handler_disconnect(void *instance, unsigned handler_id) {
    if (!instance || handler_id == 0U) return 0;
    for (uint32_t index = 0; index < RUNTIME_GLOBAL_SIGNAL_CAPACITY; ++index) {
        struct runtime_signal *signal = &global_signals[index];
        if (!signal->used || signal->instance != instance ||
            signal->id != handler_id) continue;
        signal->used = 0U;
        signal->instance = NULL;
        signal->id = 0U;
        signal->callback = NULL;
        signal->data = NULL;
        signal->name[0] = 0;
        return 1;
    }
    return 0;
}

unsigned g_signal_handlers_disconnect_by_func(
    void *instance, void (*callback)(void *, void *), void *data) {
    if (!instance || !callback) return 0U;
    unsigned removed = 0U;
    for (uint32_t index = 0; index < RUNTIME_GLOBAL_SIGNAL_CAPACITY; ++index) {
        struct runtime_signal *signal = &global_signals[index];
        if (!signal->used || signal->instance != instance ||
            signal->callback != callback || signal->data != data) continue;
        signal->used = 0U;
        signal->instance = NULL;
        signal->id = 0U;
        signal->callback = NULL;
        signal->data = NULL;
        signal->name[0] = 0;
        ++removed;
    }
    return removed;
}

/* GTK-shaped window entry points. */
void gtk_init(int *argc, char ***argv) { (void)argc; (void)argv; }
int gtk_init_check(int *argc, char ***argv) {
    gtk_init(argc, argv);
    return 1;
}
void *gtk_window_new(int type) { (void)type; return akaos_runtime_window_new(); }
void gtk_window_set_title(void *window, const char *title) {
    (void)akaos_runtime_window_set_title(window, title);
}
void gtk_window_set_default_size(void *window, int width, int height) {
    (void)akaos_runtime_window_set_size(window, width, height);
}
void gtk_window_set_resizable(void *window, int resizable) {
    (void)akaos_runtime_window_set_resizable(window, resizable);
}
int gtk_window_get_resizable(const void *window) {
    return akaos_runtime_window_get_resizable(window);
}
void gtk_window_maximize(void *window) {
    (void)akaos_runtime_window_maximize(window);
}
void gtk_window_unmaximize(void *window) {
    (void)akaos_runtime_window_unmaximize(window);
}
int gtk_window_is_maximized(const void *window) {
    return akaos_runtime_window_is_maximized(window);
}
void gtk_window_minimize(void *window) {
    (void)akaos_runtime_window_minimize(window);
}
void gtk_window_unminimize(void *window) {
    (void)akaos_runtime_window_unminimize(window);
}
int gtk_window_is_minimized(const void *window) {
    return akaos_runtime_window_is_minimized(window);
}
void gtk_window_close(void *window) {
    (void)akaos_runtime_window_close(window);
}
void gtk_window_present(void *window) { (void)akaos_runtime_window_show(window); }
void gtk_widget_show(void *window) { (void)akaos_runtime_window_show(window); }
static void runtime_widget_show_all(struct runtime_window *window,
                                    unsigned depth) {
    if (!window || !window->used || depth >= RUNTIME_WINDOW_CAPACITY) return;
    window->visible = 1U;
    window->minimized = 0U;
    window->closed = 0U;
    for (uint32_t index = 0; index < window->child_count; ++index)
        runtime_widget_show_all(window->children[index], depth + 1U);
}
void gtk_widget_show_all(void *value) {
    runtime_widget_show_all(find_window(value), 0U);
}
void gtk_widget_hide(void *window) { (void)akaos_runtime_window_hide(window); }
void gtk_widget_destroy(void *window) { (void)akaos_runtime_window_destroy(window); }
void *gtk_box_new(int orientation, int spacing) {
    (void)orientation;
    (void)spacing;
    return akaos_runtime_widget_new(AKAOS_RUNTIME_WIDGET_BOX);
}
void *gtk_label_new(const char *text) {
    void *label = akaos_runtime_widget_new(AKAOS_RUNTIME_WIDGET_LABEL);
    if (label) (void)akaos_runtime_widget_set_text(label, text);
    return label;
}
void *gtk_button_new_with_label(const char *text) {
    void *button = akaos_runtime_widget_new(AKAOS_RUNTIME_WIDGET_BUTTON);
    if (button) (void)akaos_runtime_widget_set_text(button, text);
    return button;
}
void *gtk_entry_new(void) {
    return akaos_runtime_widget_new(AKAOS_RUNTIME_WIDGET_ENTRY);
}
const char *gtk_entry_get_text(void *entry) {
    struct runtime_window *widget = find_window(entry);
    return widget ? widget->text : NULL;
}
void gtk_entry_set_text(void *entry, const char *text) {
    (void)akaos_runtime_widget_set_text(entry, text);
}
void *gtk_check_button_new_with_label(const char *text) {
    void *button = akaos_runtime_widget_new(AKAOS_RUNTIME_WIDGET_CHECK_BUTTON);
    if (button) (void)akaos_runtime_widget_set_text(button, text);
    return button;
}
void gtk_toggle_button_set_active(void *button, int active) {
    struct runtime_window *widget = find_window(button);
    if (widget) widget->active = active != 0;
}
int gtk_toggle_button_get_active(void *button) {
    struct runtime_window *widget = find_window(button);
    return widget ? (int)widget->active : 0;
}
void *gtk_grid_new(void) {
    return akaos_runtime_widget_new(AKAOS_RUNTIME_WIDGET_GRID);
}
void gtk_grid_attach(void *grid, void *child, int column, int row,
                     int width, int height) {
    (void)column;
    (void)row;
    (void)width;
    (void)height;
    (void)akaos_runtime_widget_add(grid, child);
}
void gtk_container_add(void *parent, void *child) {
    (void)akaos_runtime_widget_add(parent, child);
}
void gtk_container_remove(void *parent, void *child) {
    (void)akaos_runtime_widget_remove(parent, child);
}
void gtk_box_pack_start(void *box, void *child, int expand, int fill,
                        unsigned padding) {
    (void)expand;
    (void)fill;
    (void)padding;
    (void)akaos_runtime_widget_add(box, child);
}
void gtk_widget_set_size_request(void *widget, int width, int height) {
    (void)akaos_runtime_widget_set_size_request(widget, width, height);
}
int gtk_widget_get_visible(void *widget) {
    return akaos_runtime_widget_get_visible(widget);
}
void gtk_widget_set_visible(void *widget, int visible) {
    akaos_runtime_widget_set_visible(widget, visible);
}
void gtk_widget_set_sensitive(void *widget, int sensitive) {
    akaos_runtime_widget_set_sensitive(widget, sensitive);
}
int gtk_widget_get_sensitive(void *widget) {
    return akaos_runtime_widget_get_sensitive(widget);
}
void *gtk_widget_get_parent(void *widget) {
    return akaos_runtime_widget_get_parent(widget);
}
void gtk_widget_set_name(void *widget, const char *name) {
    (void)akaos_runtime_widget_set_name(widget, name);
}
const char *gtk_widget_get_name(void *widget) {
    return akaos_runtime_widget_get_name(widget);
}
static struct runtime_loop *gtk_default_loop;
void gtk_main(void) {
    if (!gtk_default_loop) gtk_default_loop = akaos_runtime_loop_new();
    if (gtk_default_loop) akaos_runtime_loop_run(gtk_default_loop);
}
void gtk_main_quit(void) {
    if (gtk_default_loop) akaos_runtime_loop_quit(gtk_default_loop);
}
void *gtk_application_new(const char *application_id, unsigned flags) {
    (void)flags;
    struct runtime_window *application = akaos_runtime_window_new();
    if (!application) return NULL;
    application->is_application = 1U;
    copy_text(application->application_id, sizeof(application->application_id),
              application_id);
    return application;
}
/* Applications are created by libgtk and their lifecycle is driven through
 * libgio/libglib.  Those DSOs intentionally have separate bounded object
 * tables, so validating the pointer against GLib's local table would reject
 * every legitimate cross-library application.  The public application
 * object has a fixed layout; use the passed object directly at this ABI
 * boundary and only accept objects carrying the application tag. */
static struct runtime_window *application_object(void *application) {
    if (!application) return NULL;
    struct runtime_window *object = application;
    return object->is_application ? object : NULL;
}
/* Signal dispatch is defined later with the rest of the GObject-shaped ABI;
 * keep the application lifecycle independent of declaration order. */
int g_signal_emit_by_name(void *instance, const char *detailed_signal);
int g_application_register(void *application, void *cancellable, void **error) {
    (void)cancellable;
    if (error) *error = NULL;
    struct runtime_window *object = application_object(application);
    if (!object || object->application_id[0] == '\0')
        return 0;
    object->application_registered = 1U;
    return 1;
}
void g_application_hold(void *application) {
    struct runtime_window *object = application_object(application);
    if (object && object->application_holds != UINT32_MAX)
        ++object->application_holds;
}
void g_application_release(void *application) {
    struct runtime_window *object = application_object(application);
    if (object && object->application_holds) --object->application_holds;
}
int g_application_get_is_registered(const void *application) {
    const struct runtime_window *object = application_object((void *)application);
    return object && object->application_registered;
}
const char *g_application_get_application_id(const void *application) {
    const struct runtime_window *object = application_object((void *)application);
    return object ? object->application_id : NULL;
}
void g_application_activate(void *application) {
    struct runtime_window *object = application_object(application);
    if (!object) return;
    object->visible = 1U;
    (void)g_signal_emit_by_name(application, "activate");
}
int g_application_run(void *application, int argc, char **argv) {
    (void)argc;
    (void)argv;
    if (!g_application_register(application, NULL, NULL)) return 1;
    if (g_application_loop) return 1;
    struct runtime_window *object = application_object(application);
    if (!object) return 1;
    object->application_quit_requested = 0U;
    g_application_loop = akaos_runtime_loop_new();
    if (!g_application_loop) return 1;
    g_application_activate(application);
    if (!object->application_quit_requested)
        akaos_runtime_loop_run(g_application_loop);
    g_application_loop = NULL;
    return 0;
}
void g_application_quit(void *application) {
    struct runtime_window *object = application_object(application);
    if (!object) return;
    object->visible = 0U;
    if (g_application_loop) {
        /* A quit requested from inside g_application_run ends dispatch but
         * leaves registration intact until the caller explicitly quits or
         * releases the application after run() returns. */
        object->application_quit_requested = 1U;
        akaos_runtime_loop_quit(g_application_loop);
        return;
    }
    object->application_registered = 0U;
    object->application_holds = 0U;
    object->application_quit_requested = 1U;
}
void *gtk_application_window_new(void *application) {
    struct runtime_window *window = akaos_runtime_window_new();
    if (window) window->parent = application_object(application);
    return window;
}

/* Bounded GSimpleAction/application-action-map support.  Action instances
 * are kept in the GLib compatibility object and are intentionally driven by
 * the existing fixed callback registry (`g_signal_connect(..., "activate",`)
 * rather than exposing GLib's variadic marshaller ABI. */
GSimpleAction *g_simple_action_new(const char *name, const char *parameter_type) {
    if (!name || !name[0]) return NULL;
    for (size_t index = 0; index < RUNTIME_ACTION_CAPACITY; ++index) {
        struct runtime_action *action = &actions[index];
        if (action->used) continue;
        action->used = 1U;
        action->references = 1U;
        action->enabled = 1U;
        copy_text(action->name, sizeof(action->name), name);
        copy_text(action->parameter_type, sizeof(action->parameter_type),
                  parameter_type);
        return (GSimpleAction *)action;
    }
    return NULL;
}

GSimpleAction *g_simple_action_ref(GSimpleAction *value) {
    struct runtime_action *action = find_action(value);
    if (action && action->references != UINT32_MAX) ++action->references;
    return value;
}

void g_simple_action_unref(GSimpleAction *value) {
    struct runtime_action *action = find_action(value);
    if (!action) return;
    if (action->references > 1U) {
        --action->references;
        return;
    }
    for (size_t index = 0; index < RUNTIME_ACTION_MAP_CAPACITY; ++index) {
        struct runtime_action_map_entry *entry = &action_map_entries[index];
        if (entry->used && entry->action == action) {
            entry->used = 0U;
            entry->action_map = NULL;
            entry->action = NULL;
            entry->name[0] = 0;
        }
    }
    for (size_t index = 0; index < RUNTIME_GLOBAL_SIGNAL_CAPACITY; ++index) {
        struct runtime_signal *signal = &global_signals[index];
        if (!signal->used || signal->instance != value) continue;
        signal->used = 0U;
        signal->instance = NULL;
        signal->id = 0U;
        signal->callback = NULL;
        signal->data = NULL;
        signal->name[0] = 0;
    }
    action->used = 0U;
    action->references = 0U;
    action->enabled = 0U;
    action->name[0] = 0;
    action->parameter_type[0] = 0;
}

const char *g_action_get_name(const GSimpleAction *value) {
    const struct runtime_action *action = find_action(value);
    return action ? action->name : NULL;
}

void g_simple_action_set_enabled(GSimpleAction *value, int enabled) {
    struct runtime_action *action = find_action(value);
    if (action) action->enabled = enabled != 0;
}

int g_action_get_enabled(const GSimpleAction *value) {
    const struct runtime_action *action = find_action(value);
    return action ? (int)action->enabled : 0;
}

void g_action_activate(GSimpleAction *value, void *parameter) {
    (void)parameter;
    struct runtime_action *action = find_action(value);
    if (action && action->enabled) (void)g_signal_emit_by_name(value, "activate");
}

int g_action_map_add_action(void *action_map, GSimpleAction *value) {
    struct runtime_action *action = find_action(value);
    if (!action_map || !action) return 0;
    for (size_t index = 0; index < RUNTIME_ACTION_MAP_CAPACITY; ++index) {
        struct runtime_action_map_entry *entry = &action_map_entries[index];
        if (!entry->used || entry->action_map != action_map) continue;
        if (strcmp_local(entry->name, action->name) == 0) return 0;
    }
    for (size_t index = 0; index < RUNTIME_ACTION_MAP_CAPACITY; ++index) {
        struct runtime_action_map_entry *entry = &action_map_entries[index];
        if (entry->used) continue;
        entry->used = 1U;
        entry->action_map = action_map;
        entry->action = action;
        copy_text(entry->name, sizeof(entry->name), action->name);
        return 1;
    }
    return 0;
}

int g_action_map_remove_action(void *action_map, const char *name) {
    if (!action_map || !name || !name[0]) return 0;
    for (size_t index = 0; index < RUNTIME_ACTION_MAP_CAPACITY; ++index) {
        struct runtime_action_map_entry *entry = &action_map_entries[index];
        if (!entry->used || entry->action_map != action_map ||
            strcmp_local(entry->name, name) != 0) continue;
        entry->used = 0U;
        entry->action_map = NULL;
        entry->action = NULL;
        entry->name[0] = 0;
        return 1;
    }
    return 0;
}

GSimpleAction *g_action_map_lookup_action(const void *action_map,
                                          const char *name) {
    if (!action_map || !name || !name[0]) return NULL;
    for (size_t index = 0; index < RUNTIME_ACTION_MAP_CAPACITY; ++index) {
        const struct runtime_action_map_entry *entry = &action_map_entries[index];
        if (entry->used && entry->action_map == action_map &&
            strcmp_local(entry->name, name) == 0 && find_action(entry->action))
            return (GSimpleAction *)entry->action;
    }
    return NULL;
}

/* C-facing Qt bridge; C++ source ports can wrap this without C++ name
 * mangling in the freestanding loader.  The object and signal operations
 * intentionally reuse the bounded runtime tables above, so Qt and GTK
 * compatibility objects share deterministic ownership semantics. */
void *akaos_qt_application_new(void) { return akaos_runtime_loop_new(); }
int akaos_qt_application_exec(void *application) {
    akaos_runtime_loop_run(application);
    return 0;
}
void akaos_qt_application_quit(void *application) { akaos_runtime_loop_quit(application); }
int akaos_qt_application_process_events(void *application, int iterations) {
    if (!application || iterations <= 0) return 0;
    int processed = 0;
    for (int index = 0; index < iterations; ++index)
        if (run_one_source(runtime_context_current())) ++processed;
    return processed;
}

void *akaos_qt_object_new(void *parent) {
    void *object = akaos_runtime_widget_new(AKAOS_RUNTIME_WIDGET_CONTAINER);
    if (!object) return NULL;
    if (parent && !akaos_runtime_widget_add(parent, object)) {
        (void)akaos_runtime_window_destroy(object);
        return NULL;
    }
    return object;
}

int akaos_qt_object_set_property(void *object, const char *name,
                                 const char *value) {
    if (!object || !name || !value) return 0;
    if (strcmp_local(name, "objectName") == 0)
        return akaos_runtime_widget_set_name(object, value);
    if (strcmp_local(name, "windowTitle") == 0)
        return akaos_runtime_window_set_title(object, value);
    if (strcmp_local(name, "text") == 0)
        return akaos_runtime_widget_set_text(object, value);
    return 0;
}

const char *akaos_qt_object_get_property(const void *object,
                                         const char *name) {
    if (!object || !name) return NULL;
    if (strcmp_local(name, "objectName") == 0)
        return akaos_runtime_widget_get_name((void *)object);
    struct runtime_window *window = find_window((void *)object);
    if (!window) return NULL;
    if (strcmp_local(name, "windowTitle") == 0) return window->title;
    if (strcmp_local(name, "text") == 0) return window->text;
    return NULL;
}

unsigned akaos_qt_signal_connect(void *instance, const char *signal,
                                 void (*callback)(void *, void *), void *data) {
    return g_signal_connect(instance, signal, callback, data);
}

int akaos_qt_signal_emit(void *instance, const char *signal) {
    return g_signal_emit_by_name(instance, signal);
}

int akaos_qt_signal_disconnect(void *instance, unsigned handler_id) {
    return g_signal_handler_disconnect(instance, handler_id);
}

void *akaos_qt_widget_new(void) { return akaos_runtime_window_new(); }
int akaos_qt_widget_set_geometry(void *widget, int x, int y,
                                 int width, int height) {
    struct runtime_window *window = find_window(widget);
    if (!window || width <= 0 || height <= 0) return 0;
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    return 1;
}

int akaos_qt_widget_get_geometry(const void *widget, int *x, int *y,
                                 int *width, int *height) {
    struct runtime_window *window = find_window((void *)widget);
    if (!window || !x || !y || !width || !height) return 0;
    *x = window->x;
    *y = window->y;
    *width = window->width;
    *height = window->height;
    return 1;
}

void akaos_qt_widget_set_title(void *widget, const char *title) {
    (void)akaos_runtime_window_set_title(widget, title);
}
void akaos_qt_widget_show(void *widget) { (void)akaos_runtime_window_show(widget); }
void akaos_qt_widget_hide(void *widget) { (void)akaos_runtime_window_hide(widget); }
int akaos_qt_widget_is_visible(const void *widget) {
    return akaos_runtime_widget_get_visible((void *)widget);
}
void akaos_qt_widget_set_enabled(void *widget, int enabled) {
    akaos_runtime_widget_set_sensitive(widget, enabled);
}
int akaos_qt_widget_is_enabled(const void *widget) {
    return akaos_runtime_widget_get_sensitive((void *)widget);
}
void akaos_qt_widget_destroy(void *widget) { (void)akaos_runtime_window_destroy(widget); }
