#include <errno.h>
#include <endian.h>
#include <fenv.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <math.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <semaphore.h>
#include <setjmp.h>
#include <akaos/input.h>
#include <akaos/display.h>
#include <akaos/dbus.h>
#include <akaos/futex.h>
#include <akaos/syscall.h>
#include <akaos/toolkit.h>
#include <ctype.h>
#include <dlfcn.h>
#include <dirent.h>
#include <netdb.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <wordexp.h>

#if defined(__clang__)
#define AKAOS_HELLO_NOOPT __attribute__((noinline, optnone))
#else
#define AKAOS_HELLO_NOOPT
#endif

/* The resident image loader relocates code as a whole and intentionally does
 * not apply ordinary absolute data relocations.  Keep this newly added long
 * self-test option out of compiler-generated literal/pointer tables. */
static int AKAOS_HELLO_NOOPT is_properties_signal_option(const char *value) {
    if (!value || strlen(value) != 29U) return 0;
    return value[0] == '-' && value[1] == '-' && value[2] == 'd' &&
           value[3] == 'b' && value[4] == 'u' && value[5] == 's' &&
           value[6] == '-' && value[7] == 'p' && value[8] == 'r' &&
           value[9] == 'o' && value[10] == 'p' && value[11] == 'e' &&
           value[12] == 'r' && value[13] == 't' && value[14] == 'i' &&
           value[15] == 'e' && value[16] == 's' && value[17] == '-' &&
           value[18] == 's' && value[19] == 'i' && value[20] == 'g' &&
           value[21] == 'n' && value[22] == 'a' && value[23] == 'l' &&
           value[24] == '-' && value[25] == 't' && value[26] == 'e' &&
           value[27] == 's' && value[28] == 't';
}
#include <locale.h>
#include <langinfo.h>
#include <wchar.h>

static const char expected_motd[] = "akaOS XSCore initramfs\n";
static const char hello_message[] = "[USER] Hello from akaOS C userspace\n";
static volatile uint32_t cow_test_value;
static volatile char usercopy_cow_buffer[64];
static volatile int received_signal = 0;
static volatile int signal_handler_counter = 0;
static volatile int spawn_sigchld_count;
static volatile int sigchld_info_calls;
static volatile int sigchld_info_reaped_pid;
static volatile int sigchld_info_reaped_status;
static volatile int sigchld_info_signo;
static volatile int siginfo_probe_calls;
static volatile int siginfo_probe_signo;
static volatile int siginfo_probe_code;
static volatile uintptr_t siginfo_probe_address;
static volatile int siginfo_probe_context_present;
static volatile int pthread_once_value;
static pthread_key_t pthread_test_key;
static volatile int pthread_destructor_called;
static pthread_key_t pthread_rearm_test_key;
static volatile uint32_t pthread_rearm_destructor_calls;
static volatile uint32_t pthread_rearm_destructor_failed;
static pthread_barrier_t *pthread_test_barrier;
static volatile int pthread_barrier_worker_passed;
static volatile uint32_t pthread_scale_start;
static volatile uint32_t pthread_scale_workers_passed;
static volatile int pthread_cleanup_cancel_called;
static volatile int pthread_cleanup_pop_called;
static volatile int runtime_signal_seen;
static volatile int runtime_task_callback_seen;

struct runtime_fd_probe {
    volatile int called;
    volatile unsigned condition;
};

struct runtime_io_probe {
    volatile int called;
    volatile unsigned condition;
    void *channel;
};

struct runtime_dbus_async_probe {
    struct akaos_dbus_async_ping ping;
    volatile int called;
    volatile int result;
};

struct runtime_dbus_binary_async_probe {
    struct akaos_dbus_async_binary_ping ping;
    volatile int called;
    volatile int result;
};

struct runtime_dbus_queue_probe {
    struct akaos_dbus_async_client client;
    volatile int called;
    volatile int failed;
};

struct runtime_file_async_probe {
    volatile int callback_seen;
    void *result;
};

static void runtime_file_async_callback(void *source_object, void *result,
                                        void *user_data) {
    (void)source_object;
    struct runtime_file_async_probe *probe = user_data;
    if (probe) {
        probe->callback_seen = 1;
        probe->result = result;
    }
}

struct runtime_loop_quit_probe {
    void (*quit)(void *loop);
    void *loop;
    volatile int *called;
};

static unsigned runtime_loop_quit_callback(void *data) {
    struct runtime_loop_quit_probe *probe = data;
    if (!probe || !probe->quit || !probe->loop) return 0U;
    if (probe->called) *probe->called = 1;
    probe->quit(probe->loop);
    return 0U;
}

static unsigned runtime_fd_callback(int fd, unsigned condition, void *data) {
    struct runtime_fd_probe *probe = data;
    if (probe && fd >= 0) {
        probe->called = 1;
        probe->condition = condition;
    }
    return 0U;
}

static unsigned runtime_io_callback(void *channel, unsigned condition,
                                    void *data) {
    struct runtime_io_probe *probe = data;
    if (probe && channel) {
        probe->called = 1;
        probe->condition = condition;
        probe->channel = channel;
    }
    return 0U;
}

static unsigned runtime_dbus_async_callback(int fd, unsigned condition,
                                            void *data) {
    struct runtime_dbus_async_probe *probe = data;
    if (!probe || fd != probe->ping.descriptor) return 0U;
    probe->called++;
    int result = akaos_dbus_async_ping_pump(&probe->ping,
                                            (short)condition);
    if (result != 0) probe->result = result;
    return result == 0 ? 1U : 0U;
}

static unsigned runtime_dbus_binary_async_callback(int fd, unsigned condition,
                                                    void *data) {
    struct runtime_dbus_binary_async_probe *probe = data;
    if (!probe || fd != probe->ping.descriptor) return 0U;
    probe->called++;
    int result = akaos_dbus_async_binary_ping_pump(&probe->ping,
                                                   (short)condition);
    if (result != 0) probe->result = result;
    return result == 0 ? 1U : 0U;
}

static unsigned runtime_dbus_queue_callback(int fd, unsigned condition,
                                            void *data) {
    struct runtime_dbus_queue_probe *probe = data;
    if (!probe || fd != probe->client.descriptor) return 0U;
    int result = akaos_dbus_async_client_pump(&probe->client,
                                              (short)condition);
    if (result < 0) probe->failed = 1;
    else if (result > 0) probe->called++;
    return akaos_dbus_async_client_pending(&probe->client) != 0U ? 1U : 0U;
}

struct runtime_source_destroy_probe {
    volatile int callback_called;
    volatile int destroy_called;
};

static unsigned runtime_source_once_callback(void *data) {
    struct runtime_source_destroy_probe *probe = data;
    if (probe) probe->callback_called++;
    return 0U;
}

static void runtime_source_destroy_callback(void *data) {
    struct runtime_source_destroy_probe *probe = data;
    if (probe) probe->destroy_called++;
}

static void runtime_button_callback(void *instance, void *data) {
    (void)instance;
    if (data == &runtime_signal_seen) runtime_signal_seen = 1;
}

static void runtime_task_callback(void *source_object, void *result,
                                  void *user_data) {
    (void)source_object;
    if (result && user_data == (void *)&runtime_task_callback_seen)
        runtime_task_callback_seen = 1;
}

struct qt_probe_api {
    void *(*application_new)(void);
    int (*application_exec)(void *);
    void (*application_quit)(void *);
    int (*application_process_events)(void *, int);
    void *(*object_new)(void *);
    int (*object_set_property)(void *, const char *, const char *);
    const char *(*object_get_property)(const void *, const char *);
    unsigned (*signal_connect)(void *, const char *,
                               void (*)(void *, void *), void *);
    int (*signal_emit)(void *, const char *);
    int (*signal_disconnect)(void *, unsigned);
    void *(*widget_new)(void);
    int (*widget_set_geometry)(void *, int, int, int, int);
    int (*widget_get_geometry)(const void *, int *, int *, int *, int *);
    void (*widget_set_title)(void *, const char *);
    void (*widget_show)(void *);
    void (*widget_hide)(void *);
    int (*widget_is_visible)(const void *);
    void (*widget_set_enabled)(void *, int);
    int (*widget_is_enabled)(const void *);
    void (*widget_destroy)(void *);
};

static int test_qt_bridge(void *qt) {
    if (!qt) return 0;
    struct qt_probe_api api;
    memset(&api, 0, sizeof(api));
    api.application_new = (void *(*)(void))dlsym(
        qt, "akaos_qt_application_new");
    api.application_exec = (int (*)(void *))dlsym(
        qt, "akaos_qt_application_exec");
    api.application_quit = (void (*)(void *))dlsym(
        qt, "akaos_qt_application_quit");
    api.application_process_events = (int (*)(void *, int))dlsym(
        qt, "akaos_qt_application_process_events");
    api.object_new = (void *(*)(void *))dlsym(qt, "akaos_qt_object_new");
    api.object_set_property = (int (*)(void *, const char *, const char *))dlsym(
        qt, "akaos_qt_object_set_property");
    api.object_get_property = (const char *(*)(const void *, const char *))dlsym(
        qt, "akaos_qt_object_get_property");
    api.signal_connect = (unsigned (*)(void *, const char *,
                                       void (*)(void *, void *), void *))dlsym(
        qt, "akaos_qt_signal_connect");
    api.signal_emit = (int (*)(void *, const char *))dlsym(
        qt, "akaos_qt_signal_emit");
    api.signal_disconnect = (int (*)(void *, unsigned))dlsym(
        qt, "akaos_qt_signal_disconnect");
    api.widget_new = (void *(*)(void))dlsym(qt, "akaos_qt_widget_new");
    api.widget_set_geometry = (int (*)(void *, int, int, int, int))dlsym(
        qt, "akaos_qt_widget_set_geometry");
    api.widget_get_geometry = (int (*)(const void *, int *, int *, int *, int *))dlsym(
        qt, "akaos_qt_widget_get_geometry");
    api.widget_set_title = (void (*)(void *, const char *))dlsym(
        qt, "akaos_qt_widget_set_title");
    api.widget_show = (void (*)(void *))dlsym(qt, "akaos_qt_widget_show");
    api.widget_hide = (void (*)(void *))dlsym(qt, "akaos_qt_widget_hide");
    api.widget_is_visible = (int (*)(const void *))dlsym(
        qt, "akaos_qt_widget_is_visible");
    api.widget_set_enabled = (void (*)(void *, int))dlsym(
        qt, "akaos_qt_widget_set_enabled");
    api.widget_is_enabled = (int (*)(const void *))dlsym(
        qt, "akaos_qt_widget_is_enabled");
    api.widget_destroy = (void (*)(void *))dlsym(
        qt, "akaos_qt_widget_destroy");
    if (!api.application_new || !api.application_exec || !api.application_quit ||
        !api.application_process_events || !api.object_new ||
        !api.object_set_property || !api.object_get_property ||
        !api.signal_connect || !api.signal_emit || !api.signal_disconnect ||
        !api.widget_new || !api.widget_set_geometry ||
        !api.widget_get_geometry || !api.widget_set_title || !api.widget_show ||
        !api.widget_hide || !api.widget_is_visible ||
        !api.widget_set_enabled || !api.widget_is_enabled || !api.widget_destroy)
        return 0;

    void *application = api.application_new();
    void *widget = api.widget_new();
    void *object = widget ? api.object_new(widget) : NULL;
    int x = 0, y = 0, width = 0, height = 0;
    int ok = application && widget && object &&
        api.object_set_property(object, "objectName", "qt-root") == 1 &&
        api.object_set_property(object, "text", "Qt runtime") == 1 &&
        api.object_get_property(object, "objectName") &&
        strcmp(api.object_get_property(object, "objectName"), "qt-root") == 0 &&
        api.object_get_property(object, "text") &&
        strcmp(api.object_get_property(object, "text"), "Qt runtime") == 0 &&
        api.widget_set_geometry(widget, 12, 24, 320, 200) == 1 &&
        api.widget_get_geometry(widget, &x, &y, &width, &height) == 1 &&
        x == 12 && y == 24 && width == 320 && height == 200;
    runtime_signal_seen = 0;
    unsigned signal = ok
        ? api.signal_connect(widget, "clicked", runtime_button_callback,
                             (void *)&runtime_signal_seen) : 0U;
    if (ok) {
        api.widget_set_title(widget, "Qt window");
        api.widget_show(widget);
        ok = api.widget_is_visible(widget) == 1;
        api.widget_set_enabled(widget, 0);
        ok = ok && api.widget_is_enabled(widget) == 0;
        api.widget_set_enabled(widget, 1);
        ok = ok && api.widget_is_enabled(widget) == 1;
        ok = signal != 0U && api.signal_emit(widget, "clicked") == 1 &&
            runtime_signal_seen == 1;
        ok = ok && api.signal_disconnect(widget, signal) == 1 &&
            api.signal_emit(widget, "clicked") == 0;
        api.widget_hide(widget);
        ok = ok && api.widget_is_visible(widget) == 0;
    }
    if (ok) {
        (void)api.application_process_events(application, 1);
        api.application_quit(application);
        ok = api.application_exec(application) == 0;
    }
    if (object) api.widget_destroy(object);
    if (widget) api.widget_destroy(widget);
    return ok;
}

static int test_gtk_window_state(void *gtk) {
    if (!gtk) return 0;
    typedef void *(*window_new_fn)(int);
    typedef void (*window_action_fn)(void *);
    typedef void (*window_title_fn)(void *, const char *);
    typedef int (*window_state_fn)(const void *);
    typedef void (*resizable_set_fn)(void *, int);
    typedef int (*resizable_get_fn)(const void *);
    typedef int (*visible_get_fn)(void *);
    window_new_fn window_new = (window_new_fn)dlsym(gtk, "gtk_window_new");
    window_title_fn set_title = (window_title_fn)dlsym(
        gtk, "gtk_window_set_title");
    resizable_set_fn set_resizable = (resizable_set_fn)dlsym(
        gtk, "gtk_window_set_resizable");
    resizable_get_fn get_resizable = (resizable_get_fn)dlsym(
        gtk, "gtk_window_get_resizable");
    window_action_fn maximize = (window_action_fn)dlsym(
        gtk, "gtk_window_maximize");
    window_action_fn unmaximize = (window_action_fn)dlsym(
        gtk, "gtk_window_unmaximize");
    window_state_fn is_maximized = (window_state_fn)dlsym(
        gtk, "gtk_window_is_maximized");
    window_action_fn minimize = (window_action_fn)dlsym(
        gtk, "gtk_window_minimize");
    window_action_fn unminimize = (window_action_fn)dlsym(
        gtk, "gtk_window_unminimize");
    window_state_fn is_minimized = (window_state_fn)dlsym(
        gtk, "gtk_window_is_minimized");
    window_action_fn close = (window_action_fn)dlsym(gtk, "gtk_window_close");
    window_action_fn show = (window_action_fn)dlsym(gtk, "gtk_widget_show");
    visible_get_fn is_visible = (visible_get_fn)dlsym(
        gtk, "akaos_runtime_widget_get_visible");
    int (*destroy)(void *) = (int (*)(void *))dlsym(
        gtk, "akaos_runtime_window_destroy");
    if (!window_new || !set_title || !set_resizable || !get_resizable ||
        !maximize || !unmaximize || !is_maximized || !minimize ||
        !unminimize || !is_minimized || !close || !show || !is_visible ||
        !destroy) return 0;
    void *window = window_new(0);
    int ok = window && get_resizable(window) == 1;
    if (ok) {
        set_title(window, "GTK window");
        set_resizable(window, 0);
        ok = get_resizable(window) == 0;
        set_resizable(window, 1);
    }
    if (ok) {
        maximize(window);
        ok = is_maximized(window) == 1 && is_minimized(window) == 0 &&
            is_visible(window) == 1;
    }
    if (ok) {
        minimize(window);
        ok = is_minimized(window) == 1 && is_maximized(window) == 0 &&
            is_visible(window) == 0;
        unminimize(window);
    }
    if (ok) {
        unmaximize(window);
        close(window);
        ok = is_maximized(window) == 0 && is_minimized(window) == 0 &&
            is_visible(window) == 0;
        show(window);
        ok = ok && is_visible(window) == 1;
    }
    if (window) (void)destroy(window);
    return ok;
}

static void pthread_test_destructor(void *value) {
    if ((uintptr_t)value == 42U) pthread_destructor_called = 1;
}

static void pthread_rearm_test_destructor(void *value) {
    uintptr_t iteration = (uintptr_t)value;
    __atomic_add_fetch(&pthread_rearm_destructor_calls, 1U, __ATOMIC_RELEASE);
    if (iteration == 0U || iteration > PTHREAD_DESTRUCTOR_ITERATIONS ||
        pthread_getspecific(pthread_rearm_test_key) != NULL) {
        __atomic_store_n(&pthread_rearm_destructor_failed, 1U,
                         __ATOMIC_RELEASE);
        return;
    }
    /* Reinsert even on the final permitted call. POSIX requires the value
     * slot to be reclaimed when the thread exits after that last iteration. */
    if (pthread_setspecific(pthread_rearm_test_key,
                            (void *)(iteration + 1U)) != 0)
        __atomic_store_n(&pthread_rearm_destructor_failed, 1U,
                         __ATOMIC_RELEASE);
}

static uint64_t timespec_elapsed_milliseconds(const struct timespec *start,
                                             const struct timespec *finish) {
    if (!start || !finish || finish->tv_sec < start->tv_sec) return 0U;
    uint64_t seconds = (uint64_t)(finish->tv_sec - start->tv_sec);
    int64_t nanoseconds = (int64_t)finish->tv_nsec - start->tv_nsec;
    if (nanoseconds < 0) {
        if (seconds == 0U) return 0U;
        --seconds;
        nanoseconds += INT64_C(1000000000);
    }
    uint32_t fractional_milliseconds = (uint32_t)nanoseconds / 1000000U;
    return seconds * UINT64_C(1000) + fractional_milliseconds;
}

static void pthread_cancel_cleanup(void *value) {
    if (value) pthread_cleanup_cancel_called = 1;
}

static void pthread_pop_cleanup(void *value) {
    if (value) pthread_cleanup_pop_called = 1;
}

static void *pthread_worker(void *argument) {
    if (pthread_setspecific(pthread_test_key, argument) != 0) return 0;
    if (pthread_test_barrier) {
        int barrier_result = pthread_barrier_wait(pthread_test_barrier);
        if (barrier_result != 0 &&
            barrier_result != PTHREAD_BARRIER_SERIAL_THREAD)
            return 0;
        pthread_barrier_worker_passed = 1;
    }
    return pthread_getspecific(pthread_test_key);
}

static _Thread_local uint64_t hello_executable_tls_data =
    UINT64_C(0x6a6b6c6d12345678);
static _Thread_local uint64_t hello_executable_tls_bss;

struct pthread_executable_tls_context {
    volatile unsigned start;
    volatile unsigned failed;
    volatile unsigned started;
    volatile unsigned finished;
    uint64_t value;
};

static void *pthread_executable_tls_worker(void *argument) {
    struct pthread_executable_tls_context *context = argument;
    if (context)
        __atomic_store_n(&context->started, 1U, __ATOMIC_RELEASE);
    if (!context ||
        hello_executable_tls_data != UINT64_C(0x6a6b6c6d12345678) ||
        hello_executable_tls_bss != 0U) {
        if (context) {
            __atomic_store_n(&context->failed, 1U, __ATOMIC_RELEASE);
            __atomic_store_n(&context->finished, 1U, __ATOMIC_RELEASE);
        }
        return (void *)(uintptr_t)1U;
    }
    hello_executable_tls_data = context->value;
    hello_executable_tls_bss = context->value ^ UINT64_C(0xa5a5a5a5);
    while (!__atomic_load_n(&context->start, __ATOMIC_ACQUIRE))
        (void)sched_yield();
    for (unsigned attempt = 0U; attempt < 128U; ++attempt) {
        if (hello_executable_tls_data != context->value ||
            hello_executable_tls_bss !=
                (context->value ^ UINT64_C(0xa5a5a5a5)))
            __atomic_store_n(&context->failed, 1U, __ATOMIC_RELEASE);
        (void)sched_yield();
    }
    __atomic_store_n(&context->finished, 1U, __ATOMIC_RELEASE);
    return (void *)(uintptr_t)__atomic_load_n(&context->failed,
                                              __ATOMIC_ACQUIRE);
}

static int test_pthread_executable_tls_isolation(void) {
    if (hello_executable_tls_data != UINT64_C(0x6a6b6c6d12345678) ||
        hello_executable_tls_bss != 0U) {
        printf("executable TLS main template mismatch: data=%llx bss=%llx\n",
               (unsigned long long)hello_executable_tls_data,
               (unsigned long long)hello_executable_tls_bss);
        return 565;
    }
    puts("executable TLS main-thread template verified");
    hello_executable_tls_data = UINT64_C(0x1111222233334444);
    hello_executable_tls_bss = UINT64_C(0xaaaabbbbccccdddd);
    struct pthread_executable_tls_context contexts[2] = {
        {0U, 0U, 0U, 0U, UINT64_C(0x123456789abcdef0)},
        {0U, 0U, 0U, 0U, UINT64_C(0x0fedcba987654321)}};
    pthread_t workers[2];
    unsigned created = 0U;
    for (; created < 2U; ++created) {
        if (pthread_create(&workers[created], NULL,
                           pthread_executable_tls_worker,
                           &contexts[created]) != 0)
            break;
    }
    printf("executable TLS worker creation complete: %u\n", created);
    int valid = created == 2U;
    for (unsigned index = 0U; index < created; ++index)
        __atomic_store_n(&contexts[index].start, 1U, __ATOMIC_RELEASE);
    puts("executable TLS worker start gates released");
    unsigned finished = 0U;
    for (unsigned attempt = 0U; attempt < 4096U; ++attempt) {
        finished = 0U;
        for (unsigned index = 0U; index < created; ++index)
            finished += __atomic_load_n(&contexts[index].finished,
                                        __ATOMIC_ACQUIRE) != 0U;
        if (finished == created) break;
        (void)sched_yield();
    }
    if (finished != created) {
        printf("executable TLS worker timeout: started=%u/%u finished=%u/%u failed=%u/%u\n",
               __atomic_load_n(&contexts[0].started, __ATOMIC_ACQUIRE) +
                   __atomic_load_n(&contexts[1].started, __ATOMIC_ACQUIRE),
               created, finished, created,
               __atomic_load_n(&contexts[0].failed, __ATOMIC_ACQUIRE) +
                   __atomic_load_n(&contexts[1].failed, __ATOMIC_ACQUIRE),
               created);
        return 566;
    }
    for (unsigned index = 0U; index < created; ++index) {
        void *worker_result = (void *)(uintptr_t)1U;
        printf("executable TLS joining worker %u\n", index);
        if (pthread_join(workers[index], &worker_result) != 0 ||
            worker_result ||
            __atomic_load_n(&contexts[index].failed, __ATOMIC_ACQUIRE))
            valid = 0;
    }
    puts("executable TLS worker joins complete");
    if (hello_executable_tls_data != UINT64_C(0x1111222233334444) ||
        hello_executable_tls_bss != UINT64_C(0xaaaabbbbccccdddd))
        valid = 0;
    if (!valid) {
        printf("executable TLS worker isolation failed: created=%u main=%llx/%llx\n",
               created,
               (unsigned long long)hello_executable_tls_data,
               (unsigned long long)hello_executable_tls_bss);
        return 566;
    }
    puts("POSIX executable TLS isolation verified");
    return 0;
}

struct pthread_errno_test_context {
    volatile unsigned start;
    volatile unsigned failed;
    volatile unsigned started;
    volatile unsigned finished;
    int expected_errno;
};

static void *pthread_errno_test_worker(void *argument) {
    struct pthread_errno_test_context *context = argument;
    if (!context) return (void *)(uintptr_t)1U;
    errno = context->expected_errno;
    __atomic_store_n(&context->started, 1U, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&context->start, __ATOMIC_ACQUIRE))
        (void)sched_yield();
    for (unsigned attempt = 0U; attempt < 128U; ++attempt) {
        if (errno != context->expected_errno)
            __atomic_store_n(&context->failed, 1U, __ATOMIC_RELEASE);
        (void)sched_yield();
    }
    __atomic_store_n(&context->finished, 1U, __ATOMIC_RELEASE);
    return (void *)(uintptr_t)__atomic_load_n(&context->failed,
                                              __ATOMIC_ACQUIRE);
}

static int test_pthread_errno_isolation(void) {
    int saved_errno = errno;
    errno = EBUSY;
    struct pthread_errno_test_context contexts[2] = {
        {0U, 0U, 0U, 0U, E2BIG}, {0U, 0U, 0U, 0U, ENOENT}};
    pthread_t workers[2];
    unsigned created = 0U;
    for (; created < 2U; ++created) {
        if (pthread_create(&workers[created], NULL, pthread_errno_test_worker,
                           &contexts[created]) != 0)
            break;
    }
    int valid = created == 2U && errno == EBUSY;
    for (unsigned index = 0U; index < created; ++index)
        __atomic_store_n(&contexts[index].start, 1U, __ATOMIC_RELEASE);
    unsigned finished = 0U;
    for (unsigned attempt = 0U; attempt < 4096U; ++attempt) {
        finished = 0U;
        for (unsigned index = 0U; index < created; ++index)
            finished += __atomic_load_n(&contexts[index].finished,
                                        __ATOMIC_ACQUIRE) != 0U;
        if (finished == created) break;
        (void)sched_yield();
    }
    if (finished != created) {
        printf("errno worker timeout: started=%u/%u finished=%u/%u failed=%u/%u\n",
               __atomic_load_n(&contexts[0].started, __ATOMIC_ACQUIRE) +
                   __atomic_load_n(&contexts[1].started, __ATOMIC_ACQUIRE),
               created, finished, created,
               __atomic_load_n(&contexts[0].failed, __ATOMIC_ACQUIRE) +
                   __atomic_load_n(&contexts[1].failed, __ATOMIC_ACQUIRE),
               created);
        valid = 0;
    }
    for (unsigned index = 0U; index < created; ++index) {
        void *worker_result = (void *)(uintptr_t)1U;
        if (pthread_join(workers[index], &worker_result) != 0 || worker_result ||
            __atomic_load_n(&contexts[index].failed, __ATOMIC_ACQUIRE))
            valid = 0;
    }
    if (errno != EBUSY) valid = 0;
    errno = saved_errno;
    if (!valid) return 564;
    puts("POSIX per-thread errno isolation verified");
    return 0;
}

static void *pthread_rearm_test_worker(void *argument) {
    (void)argument;
    if (pthread_setspecific(pthread_rearm_test_key, (void *)(uintptr_t)1U) != 0)
        return (void *)(uintptr_t)1U;
    return NULL;
}

#define PTHREAD_SCALE_WORKERS 24U
static void *pthread_scale_worker(void *argument) {
    while (!__atomic_load_n(&pthread_scale_start, __ATOMIC_ACQUIRE))
        (void)sched_yield();
    if (pthread_setspecific(pthread_test_key, argument) != 0 ||
        pthread_getspecific(pthread_test_key) != argument)
        return 0;
    __atomic_add_fetch(&pthread_scale_workers_passed, 1U, __ATOMIC_RELEASE);
    return argument;
}

static void *pthread_cancel_worker(void *argument) {
    volatile int *started = (volatile int *)argument;
    pthread_cleanup_push(pthread_cancel_cleanup, argument);
    if (started) *started = 1;
    for (;;) {
        pthread_testcancel();
        (void)sched_yield();
    }
    pthread_cleanup_pop(0);
    return 0;
}

static void *pthread_cleanup_pop_worker(void *argument) {
    pthread_cleanup_push(pthread_pop_cleanup, argument);
    pthread_cleanup_pop(1);
    return 0;
}

static void pthread_once_initializer(void) {
    pthread_once_value = 42;
}

static volatile int pthread_atfork_enabled;
static volatile int pthread_atfork_order;
static volatile int pthread_atfork_worker_release;

static void pthread_atfork_prepare_first(void) {
    if (!pthread_atfork_enabled) return;
    pthread_atfork_order = pthread_atfork_order == 1 ? 2 : -1;
}

static void pthread_atfork_prepare_second(void) {
    if (!pthread_atfork_enabled) return;
    pthread_atfork_order = pthread_atfork_order == 0 ? 1 : -1;
}

static void pthread_atfork_parent_first(void) {
    if (!pthread_atfork_enabled) return;
    pthread_atfork_order = pthread_atfork_order == 2 ? 3 : -1;
}

static void pthread_atfork_parent_second(void) {
    if (!pthread_atfork_enabled) return;
    pthread_atfork_order = pthread_atfork_order == 3 ? 4 : -1;
}

static void pthread_atfork_child_first(void) {
    if (!pthread_atfork_enabled) return;
    pthread_atfork_order = pthread_atfork_order == 2 ? 5 : -1;
}

static void pthread_atfork_child_second(void) {
    if (!pthread_atfork_enabled) return;
    pthread_atfork_order = pthread_atfork_order == 5 ? 6 : -1;
}

static void *pthread_atfork_wait_worker(void *argument) {
    while (!pthread_atfork_worker_release) (void)sched_yield();
    return argument;
}

static void *pthread_atfork_child_worker(void *argument) {
    return argument;
}

static int test_pthread_atfork(void) {
    if (pthread_atfork(pthread_atfork_prepare_first,
                       pthread_atfork_parent_first,
                       pthread_atfork_child_first) != 0 ||
        pthread_atfork(pthread_atfork_prepare_second,
                       pthread_atfork_parent_second,
                       pthread_atfork_child_second) != 0)
        return 548;
    int descriptors[2] = {-1, -1};
    if (pipe(descriptors) != 0) return 549;

    pthread_t worker;
    pthread_atfork_worker_release = 0;
    if (pthread_create(&worker, NULL, pthread_atfork_wait_worker,
                       (void *)(uintptr_t)0x42U) != 0) {
        close(descriptors[0]);
        close(descriptors[1]);
        return 550;
    }
    pthread_atfork_enabled = 1;
    pthread_atfork_order = 0;
    pid_t child = fork();
    if (child == 0) {
        close(descriptors[0]);
        int valid = pthread_atfork_order == 6;
        pthread_atfork_enabled = 0;
        pthread_t child_worker;
        void *child_value = NULL;
        if (!valid || pthread_create(&child_worker, NULL,
                                     pthread_atfork_child_worker,
                                     (void *)(uintptr_t)0x1234U) != 0 ||
            pthread_join(child_worker, &child_value) != 0 ||
            child_value != (void *)(uintptr_t)0x1234U) {
            close(descriptors[1]);
            _exit(51);
        }
        const char marker = 'C';
        ssize_t written = write(descriptors[1], &marker, sizeof(marker));
        close(descriptors[1]);
        _exit(written == (ssize_t)sizeof(marker) ? 0 : 52);
    }

    close(descriptors[1]);
    int parent_valid = child > 0 && pthread_atfork_order == 4;
    pthread_atfork_enabled = 0;
    pthread_atfork_worker_release = 1;
    void *worker_value = NULL;
    int worker_valid = pthread_join(worker, &worker_value) == 0 &&
                       worker_value == (void *)(uintptr_t)0x42U;
    char marker = 0;
    ssize_t received = child > 0 ? read(descriptors[0], &marker, 1U) : -1;
    close(descriptors[0]);
    int status = -1;
    int child_valid = child > 0 && waitpid(child, &status, 0) == child &&
                      status == 0 && received == 1 && marker == 'C';
    if (!parent_valid || !worker_valid || !child_valid) return 551;
    puts("pthread_atfork ordering and post-fork thread state verified");
    return 0;
}

static int test_pthread_compatibility(void) {
    int executable_tls_status = test_pthread_executable_tls_isolation();
    if (executable_tls_status != 0) return executable_tls_status;
    int errno_status = test_pthread_errno_isolation();
    if (errno_status != 0) return errno_status;

    pthread_condattr_t monotonic_attributes;
    pthread_cond_t monotonic_condition;
    pthread_mutex_t monotonic_mutex;
    int selected_clock = -1;
    struct timespec monotonic_deadline;
    if (pthread_condattr_init(&monotonic_attributes) != 0 ||
        pthread_condattr_setclock(&monotonic_attributes,
                                  CLOCK_MONOTONIC) != 0 ||
        pthread_condattr_getclock(&monotonic_attributes,
                                  &selected_clock) != 0 ||
        selected_clock != CLOCK_MONOTONIC ||
        pthread_cond_init(&monotonic_condition,
                          &monotonic_attributes) != 0 ||
        pthread_condattr_destroy(&monotonic_attributes) != 0 ||
        pthread_mutex_init(&monotonic_mutex, NULL) != 0 ||
        pthread_mutex_lock(&monotonic_mutex) != 0 ||
        clock_gettime(CLOCK_MONOTONIC, &monotonic_deadline) != 0)
        return 564;
    monotonic_deadline.tv_nsec += 5000000L;
    if (monotonic_deadline.tv_nsec >= 1000000000L) {
        ++monotonic_deadline.tv_sec;
        monotonic_deadline.tv_nsec -= 1000000000L;
    }
    int monotonic_wait = pthread_cond_timedwait(
        &monotonic_condition, &monotonic_mutex, &monotonic_deadline);
    if (monotonic_wait != ETIMEDOUT ||
        pthread_mutex_unlock(&monotonic_mutex) != 0 ||
        pthread_mutex_destroy(&monotonic_mutex) != 0 ||
        pthread_cond_destroy(&monotonic_condition) != 0)
        return 565;
    puts("POSIX monotonic condition-variable clock selection verified");

    pthread_attr_t attributes;
    size_t configured_stack = 0;
    int configured_detach = -1;
    if (pthread_attr_init(&attributes) != 0 ||
        pthread_attr_setstacksize(&attributes, 32U * 1024U) != 0 ||
        pthread_attr_getstacksize(&attributes, &configured_stack) != 0 ||
        configured_stack != 32U * 1024U ||
        pthread_attr_getdetachstate(&attributes, &configured_detach) != 0 ||
        configured_detach != PTHREAD_CREATE_JOINABLE ||
        pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED) != 0 ||
        pthread_attr_getdetachstate(&attributes, &configured_detach) != 0 ||
        configured_detach != PTHREAD_CREATE_DETACHED ||
        pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_JOINABLE) != 0)
        return 540;

    pthread_attr_t observed_stack;
    void *observed_stack_address = NULL;
    size_t observed_stack_size = 0U;
    volatile uint8_t current_stack_byte = 0U;
    uintptr_t current_stack_address = (uintptr_t)&current_stack_byte;
    if (pthread_getattr_np(pthread_self(), &observed_stack) != 0 ||
        pthread_attr_getstack(&observed_stack, &observed_stack_address,
                              &observed_stack_size) != 0 ||
        !observed_stack_address || observed_stack_size != 256U * 1024U ||
        current_stack_address < (uintptr_t)observed_stack_address ||
        current_stack_address >= (uintptr_t)observed_stack_address +
                                     observed_stack_size ||
        pthread_attr_destroy(&observed_stack) != 0)
        return 566;

    int atfork_status = test_pthread_atfork();
    if (atfork_status != 0) return atfork_status;

    pthread_mutexattr_t mutex_attributes;
    int mutex_type = -1;
    int mutex_pshared = -1;
    if (pthread_mutexattr_init(&mutex_attributes) != 0 ||
        pthread_mutexattr_settype(&mutex_attributes,
                                  PTHREAD_MUTEX_RECURSIVE) != 0 ||
        pthread_mutexattr_gettype(&mutex_attributes, &mutex_type) != 0 ||
        mutex_type != PTHREAD_MUTEX_RECURSIVE ||
        pthread_mutexattr_setpshared(&mutex_attributes,
                                     PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_mutexattr_getpshared(&mutex_attributes, &mutex_pshared) != 0 ||
        mutex_pshared != PTHREAD_PROCESS_SHARED)
        return 541;
    pthread_mutex_t recursive_mutex;
    if (pthread_mutex_init(&recursive_mutex, &mutex_attributes) != 0 ||
        pthread_mutex_lock(&recursive_mutex) != 0 ||
        pthread_mutex_lock(&recursive_mutex) != 0 ||
        pthread_mutex_unlock(&recursive_mutex) != 0 ||
        pthread_mutex_unlock(&recursive_mutex) != 0 ||
        pthread_mutex_destroy(&recursive_mutex) != 0 ||
        pthread_mutexattr_destroy(&mutex_attributes) != 0)
        return 542;

    if (pthread_mutexattr_init(&mutex_attributes) != 0 ||
        pthread_mutexattr_settype(&mutex_attributes,
                                  PTHREAD_MUTEX_ERRORCHECK) != 0 ||
        pthread_mutex_init(&recursive_mutex, &mutex_attributes) != 0 ||
        pthread_mutex_lock(&recursive_mutex) != 0 ||
        pthread_mutex_trylock(&recursive_mutex) != EBUSY ||
        pthread_mutex_unlock(&recursive_mutex) != 0 ||
        pthread_mutex_destroy(&recursive_mutex) != 0 ||
        pthread_mutexattr_destroy(&mutex_attributes) != 0)
        return 543;

    pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;
    pthread_rwlockattr_t rwlock_attributes;
    int rwlock_pshared = -1;
    if (pthread_rwlockattr_init(&rwlock_attributes) != 0 ||
        pthread_rwlockattr_setpshared(&rwlock_attributes,
                                      PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_rwlockattr_getpshared(&rwlock_attributes, &rwlock_pshared) != 0 ||
        rwlock_pshared != PTHREAD_PROCESS_SHARED ||
        pthread_rwlock_init(&rwlock, &rwlock_attributes) != 0 ||
        pthread_rwlock_rdlock(&rwlock) != 0 ||
        pthread_rwlock_tryrdlock(&rwlock) != 0 ||
        pthread_rwlock_unlock(&rwlock) != 0 ||
        pthread_rwlock_unlock(&rwlock) != 0 ||
        pthread_rwlock_trywrlock(&rwlock) != 0 ||
        pthread_rwlock_unlock(&rwlock) != 0 ||
        pthread_rwlock_destroy(&rwlock) != 0 ||
        pthread_rwlockattr_destroy(&rwlock_attributes) != 0)
        return 544;

    pthread_mutex_t timed_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t timed_condition = PTHREAD_COND_INITIALIZER;
    pthread_condattr_t condition_attributes;
    int condition_pshared = -1;
    if (pthread_condattr_init(&condition_attributes) != 0 ||
        pthread_condattr_setpshared(&condition_attributes,
                                    PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_condattr_getpshared(&condition_attributes, &condition_pshared) != 0 ||
        condition_pshared != PTHREAD_PROCESS_SHARED ||
        pthread_cond_destroy(&timed_condition) != 0 ||
        pthread_cond_init(&timed_condition, &condition_attributes) != 0 ||
        pthread_condattr_destroy(&condition_attributes) != 0)
        return 545;
    if (pthread_mutex_lock(&timed_mutex) != 0) return 546;
    struct timespec deadline;
    struct timespec condition_wait_started;
    struct timespec condition_wait_finished;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        (void)pthread_mutex_unlock(&timed_mutex);
        return 547;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &condition_wait_started) != 0) {
        (void)pthread_mutex_unlock(&timed_mutex);
        return 547;
    }
    deadline.tv_nsec += 100000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }
    int timed_result = pthread_cond_timedwait(&timed_condition, &timed_mutex,
                                               &deadline);
    int elapsed_read =
        clock_gettime(CLOCK_MONOTONIC, &condition_wait_finished);
    uint64_t condition_elapsed = elapsed_read == 0
        ? timespec_elapsed_milliseconds(&condition_wait_started,
                                        &condition_wait_finished)
        : 0U;
    if (timed_result != ETIMEDOUT || elapsed_read != 0 ||
        condition_elapsed < 70U || condition_elapsed > 2000U ||
        pthread_mutex_unlock(&timed_mutex) != 0 ||
        pthread_mutex_destroy(&timed_mutex) != 0 ||
        pthread_cond_destroy(&timed_condition) != 0)
        return 548;

    volatile uint32_t sub_tick_futex = 0U;
    struct timespec sub_tick_timeout = {.tv_sec = 0, .tv_nsec = 1000000L};
    for (uint32_t attempt = 0; attempt < 4U; ++attempt) {
        struct timespec sub_tick_started;
        struct timespec sub_tick_finished;
        if (clock_gettime(CLOCK_MONOTONIC, &sub_tick_started) != 0) {
            printf("pthread futex timeout start clock failed: errno=%d\n",
                   errno);
            return 560;
        }
        long futex_result = akaos_futex_timed(
            &sub_tick_futex,
            akaos_futex_operation(AKAOS_FUTEX_WAIT_TIMEOUT, 0),
            0U, &sub_tick_timeout);
        int finish_clock_result =
            clock_gettime(CLOCK_MONOTONIC, &sub_tick_finished);
        if (finish_clock_result != 0 || futex_result != -ETIMEDOUT) {
            printf("pthread futex timeout failed: attempt=%u result=%ld "
                   "clock=%d errno=%d\n",
                   (unsigned)attempt, futex_result, finish_clock_result,
                   errno);
            return 560;
        }
    }

    /* CLOCK_MONOTONIC currently reports /proc/uptime at centisecond
     * resolution. Keep the 1 ms probes focused on timeout expiration, and
     * use a longer wait to verify blocking duration without demanding
     * sub-centisecond precision from that clock ABI. */
    struct timespec blocking_timeout = {.tv_sec = 0, .tv_nsec = 50000000L};
    struct timespec blocking_started;
    struct timespec blocking_finished;
    if (clock_gettime(CLOCK_MONOTONIC, &blocking_started) != 0) return 562;
    long blocking_result = akaos_futex_timed(
        &sub_tick_futex,
        akaos_futex_operation(AKAOS_FUTEX_WAIT_TIMEOUT, 0),
        0U, &blocking_timeout);
    int blocking_clock_result =
        clock_gettime(CLOCK_MONOTONIC, &blocking_finished);
    uint64_t blocking_elapsed_milliseconds = blocking_clock_result == 0
        ? timespec_elapsed_milliseconds(&blocking_started,
                                        &blocking_finished)
        : 0U;
    if (blocking_clock_result != 0 || blocking_result != -ETIMEDOUT ||
        blocking_elapsed_milliseconds < 40U ||
        blocking_elapsed_milliseconds > 2000U) {
        printf("pthread blocking futex wait failed: result=%ld clock=%d "
               "elapsed_ms=%llu errno=%d\n",
               blocking_result, blocking_clock_result,
               (unsigned long long)blocking_elapsed_milliseconds, errno);
        return 562;
    }

    sem_t semaphore;
    int semaphore_value = -1;
    if (sem_init(&semaphore, PTHREAD_PROCESS_SHARED, 2U) != 0 ||
        sem_getvalue(&semaphore, &semaphore_value) != 0 ||
        semaphore_value != 2 || sem_trywait(&semaphore) != 0 ||
        sem_trywait(&semaphore) != 0 || sem_trywait(&semaphore) != EAGAIN ||
        sem_post(&semaphore) != 0 ||
        sem_getvalue(&semaphore, &semaphore_value) != 0 ||
        semaphore_value != 1 || sem_wait(&semaphore) != 0 ||
        sem_getvalue(&semaphore, &semaphore_value) != 0 ||
        semaphore_value != 0)
        return 550;
    struct timespec semaphore_deadline;
    struct timespec semaphore_wait_started;
    struct timespec semaphore_wait_finished;
    if (clock_gettime(CLOCK_REALTIME, &semaphore_deadline) != 0)
        return 551;
    if (clock_gettime(CLOCK_MONOTONIC, &semaphore_wait_started) != 0)
        return 551;
    semaphore_deadline.tv_nsec += 100000000L;
    if (semaphore_deadline.tv_nsec >= 1000000000L) {
        ++semaphore_deadline.tv_sec;
        semaphore_deadline.tv_nsec -= 1000000000L;
    }
    int semaphore_timed_result = sem_timedwait(&semaphore,
                                               &semaphore_deadline);
    elapsed_read = clock_gettime(CLOCK_MONOTONIC, &semaphore_wait_finished);
    uint64_t semaphore_elapsed = elapsed_read == 0
        ? timespec_elapsed_milliseconds(&semaphore_wait_started,
                                        &semaphore_wait_finished)
        : 0U;
    if (semaphore_timed_result != ETIMEDOUT || elapsed_read != 0 ||
        semaphore_elapsed < 70U || semaphore_elapsed > 2000U ||
        sem_destroy(&semaphore) != 0)
        return 552;

    pthread_barrierattr_t barrier_attributes;
    int barrier_pshared = -1;
    pthread_barrier_t barrier;
    if (pthread_barrierattr_init(&barrier_attributes) != 0 ||
        pthread_barrierattr_setpshared(&barrier_attributes,
                                       PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_barrierattr_getpshared(&barrier_attributes,
                                       &barrier_pshared) != 0 ||
        barrier_pshared != PTHREAD_PROCESS_SHARED ||
        pthread_barrier_init(&barrier, &barrier_attributes, 2U) != 0 ||
        pthread_barrierattr_destroy(&barrier_attributes) != 0)
        return 553;
    pthread_test_barrier = &barrier;
    pthread_barrier_worker_passed = 0;

    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
    pthread_once_t once = PTHREAD_ONCE_INIT;
    if (pthread_mutex_lock(&mutex) != 0 || pthread_mutex_trylock(&mutex) != EBUSY)
        return 481;
    if (pthread_mutex_unlock(&mutex) != 0 || pthread_mutex_destroy(&mutex) != 0)
        return 482;
    if (pthread_cond_signal(&condition) != 0 ||
        pthread_cond_broadcast(&condition) != 0 ||
        pthread_cond_destroy(&condition) != 0) return 483;
    pthread_once_value = 0;
    if (pthread_once(&once, pthread_once_initializer) != 0 ||
        pthread_once(&once, pthread_once_initializer) != 0 ||
        pthread_once_value != 42) return 484;
    if (!pthread_equal(pthread_self(), pthread_self())) return 485;
    if (pthread_key_create(&pthread_test_key, pthread_test_destructor) != 0)
        return 486;
    if (pthread_setspecific(pthread_test_key, (void *)(uintptr_t)7U) != 0 ||
        pthread_getspecific(pthread_test_key) != (void *)(uintptr_t)7U ||
        pthread_setspecific(pthread_test_key, NULL) != 0 ||
        pthread_getspecific(pthread_test_key) != NULL)
        return 560;
    if (pthread_key_create(&pthread_rearm_test_key,
                           pthread_rearm_test_destructor) != 0)
        return 561;
    pthread_cleanup_pop_called = 0;
    pthread_cleanup_push(pthread_pop_cleanup, (void *)(uintptr_t)1);
    pthread_cleanup_pop(1);
    if (!pthread_cleanup_pop_called) return 491;
    pthread_destructor_called = 0;
    if (pthread_create(NULL, NULL, NULL, NULL) != EINVAL) return 487;
    pthread_t worker;
    int create_result = pthread_create(&worker, &attributes, pthread_worker,
                                       (void *)(uintptr_t)42);
    if (create_result != 0) {
        printf("pthread_create failed: %d\n", create_result);
        return 488;
    }
    if (pthread_getattr_np(worker, &observed_stack) != 0 ||
        pthread_attr_getstack(&observed_stack, &observed_stack_address,
                              &observed_stack_size) != 0 ||
        !observed_stack_address || observed_stack_size != 32U * 1024U ||
        pthread_attr_destroy(&observed_stack) != 0)
        return 567;
    int barrier_result = pthread_barrier_wait(&barrier);
    void *worker_value = 0;
    int join_result = pthread_join(worker, &worker_value);
    pthread_test_barrier = 0;
    if (join_result != 0 || (uintptr_t)worker_value != 42 ||
        (barrier_result != 0 &&
         barrier_result != PTHREAD_BARRIER_SERIAL_THREAD) ||
        !pthread_barrier_worker_passed || pthread_barrier_destroy(&barrier) != 0) {
        printf("pthread_join failed: %d value=%u\n", join_result,
               (unsigned)(uintptr_t)worker_value);
        return 489;
    }
    pthread_rearm_destructor_calls = 0U;
    pthread_rearm_destructor_failed = 0U;
    /* Reuse the bounded per-key value table across more threads than it has
     * slots. Each destructor re-arms its value through all four permitted
     * passes, so success also verifies that exit frees the final slot. */
    for (unsigned index = 0; index < 65U; ++index) {
        pthread_t rearm_worker;
        void *rearm_result = (void *)(uintptr_t)1U;
        if (pthread_create(&rearm_worker, NULL, pthread_rearm_test_worker,
                           NULL) != 0 ||
            pthread_join(rearm_worker, &rearm_result) != 0 || rearm_result)
            return 562;
    }
    if (__atomic_load_n(&pthread_rearm_destructor_calls, __ATOMIC_ACQUIRE) !=
            65U * PTHREAD_DESTRUCTOR_ITERATIONS ||
        __atomic_load_n(&pthread_rearm_destructor_failed, __ATOMIC_ACQUIRE) ||
        pthread_key_delete(pthread_rearm_test_key) != 0)
        return 563;
    pthread_t scale_threads[PTHREAD_SCALE_WORKERS];
    unsigned scale_created = 0;
    int scale_failed = 0;
    if (pthread_attr_setstacksize(&attributes, 16U * 1024U) != 0)
        return 559;
    pthread_scale_workers_passed = 0;
    pthread_scale_start = 0;
    for (; scale_created < PTHREAD_SCALE_WORKERS; ++scale_created) {
        int scale_result = pthread_create(&scale_threads[scale_created],
                                          &attributes,
                                          pthread_scale_worker,
                                          (void *)(uintptr_t)(scale_created + 1U));
        if (scale_result != 0) {
            printf("pthread scaling create failed at %u/%u: %d\n",
                   scale_created, PTHREAD_SCALE_WORKERS, scale_result);
            scale_failed = 1;
            break;
        }
    }
    /* Keep every successfully-created worker parked until all 24 TCBs are
     * allocated, so this probes simultaneous scheduler capacity, not just
     * repeated create/join reuse.  Releasing the gate also drains cleanly if
     * a constrained guest cannot allocate the full set. */
    __atomic_store_n(&pthread_scale_start, 1U, __ATOMIC_RELEASE);
    for (unsigned i = 0; i < scale_created; ++i) {
        void *scale_value = 0;
        if (pthread_join(scale_threads[i], &scale_value) != 0 ||
            (uintptr_t)scale_value != (uintptr_t)(i + 1U)) {
            printf("pthread scaling join/value failed for worker %u\n", i);
            scale_failed = 1;
        }
    }
    if (__atomic_load_n(&pthread_scale_workers_passed, __ATOMIC_ACQUIRE) !=
            scale_created ||
        scale_created != PTHREAD_SCALE_WORKERS) {
        printf("pthread scaling workers completed %u/%u (created %u)\n",
               (unsigned)__atomic_load_n(&pthread_scale_workers_passed,
                                         __ATOMIC_ACQUIRE),
               PTHREAD_SCALE_WORKERS, scale_created);
        scale_failed = 1;
    }
    if (scale_failed) return 559;
    puts("POSIX initial and worker pthread stack bounds verified");
    if (!pthread_destructor_called || pthread_key_delete(pthread_test_key) != 0)
        return 490;
    if (pthread_attr_destroy(&attributes) != 0) return 549;
    int old_cancel_state = -1;
    int old_cancel_type = -1;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_cancel_state) != 0 ||
        old_cancel_state != PTHREAD_CANCEL_ENABLE ||
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0 ||
        pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_cancel_type) != 0 ||
        old_cancel_type != PTHREAD_CANCEL_DEFERRED ||
        pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL) != 0)
        return 554;
    pthread_cleanup_cancel_called = 0;
    volatile int cancel_started = 0;
    pthread_t cancel_thread;
    if (pthread_create(&cancel_thread, NULL, pthread_cancel_worker,
                       (void *)&cancel_started) != 0)
        return 555;
    for (unsigned attempt = 0; attempt < 100U && !cancel_started; ++attempt)
        (void)sched_yield();
    if (!cancel_started || pthread_cancel(cancel_thread) != 0)
        return 556;
    void *cancel_value = 0;
    if (pthread_join(cancel_thread, &cancel_value) != 0 ||
        cancel_value != PTHREAD_CANCELED || !pthread_cleanup_cancel_called)
        return 557;
    pthread_cleanup_pop_called = 0;
    pthread_t cleanup_thread;
    if (pthread_create(&cleanup_thread, NULL, pthread_cleanup_pop_worker,
                       (void *)(uintptr_t)1) != 0 ||
        pthread_join(cleanup_thread, NULL) != 0 ||
        !pthread_cleanup_pop_called)
        return 558;
    puts("pthread compatibility and shared thread verified (attributes, rwlock, timed wait, semaphore, barrier, cleanup, cancellation, 24-worker scheduler scaling, blocking and tick-rounded futex waits)");
    return 0;
}

static int test_dbus_bus(int system_bus) {
    int descriptor = akaos_dbus_connect(system_bus);
    if (descriptor < 0) return 487;
    int result = akaos_dbus_ping(descriptor);
    if (akaos_dbus_close(descriptor) != 0) return 488;
    if (result != 0) return 489;
    puts(system_bus ? "D-Bus system bus verified" :
         "D-Bus session bus verified");
    return 0;
}

static int test_dbus_binary_bus(void) {
    if (akaos_dbus_binary_ping() != 0) {
        printf("D-Bus binary method call failed (errno=%d)\n", errno);
        return 490;
    }
    puts("D-Bus binary framing verified");
    return 0;
}

static int test_dbus_names(void) {
    if (akaos_dbus_binary_bus_test() != 0) {
        printf("D-Bus name service test failed (errno=%d)\n", errno);
        return 491;
    }
    puts("D-Bus name service verified");
    return 0;
}

static int test_dbus_matches(void) {
    if (akaos_dbus_binary_match_test() != 0) {
        printf("D-Bus match/name-owner test failed (errno=%d)\n", errno);
        return 493;
    }
    puts("D-Bus match and name-owner semantics verified");
    return 0;
}

static int test_dbus_routing(void) {
    if (akaos_dbus_binary_route_test() != 0) {
        printf("D-Bus method routing test failed (errno=%d)\n", errno);
        return 494;
    }
    puts("D-Bus method routing and UNIX_FD passing verified");
    return 0;
}

static int test_toolkit_bridge(void) {
    struct akaos_display_info info;
    if (akaos_toolkit_display_info(&info) != 0 ||
        info.abi_version != AKAOS_DISPLAY_ABI_VERSION || info.width == 0 ||
        info.height == 0 || info.stride_pixels < info.width) {
        printf("toolkit display bridge failed (errno=%d)\n", errno);
        return 492;
    }
    printf("toolkit display bridge verified (%ux%u)\n", info.width, info.height);
    return 0;
}

static void make_unix_address(struct sockaddr_un *address, const char *path) {
    if (!address) return;
    memset(address, 0, sizeof(*address));
    address->sun_family = AF_UNIX;
    /* Volatile byte stores keep freestanding ARM images from lowering this
     * short, unaligned literal copy into an alignment-trapping wide load. */
    volatile char *destination = address->sun_path;
    size_t index = 0;
    if (path) {
        while (index + 1U < sizeof(address->sun_path) && path[index] != '\0') {
            destination[index] = path[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

static int test_unix_stream_socket(void) {
    const char path[] = "/run/akaos-unix-test";
    uid_t listener_uid = geteuid();
    gid_t listener_gid = getegid();
    pid_t listener_pid = getpid();
    uid_t client_uid = listener_uid == 0 ? 1201U : listener_uid;
    gid_t client_gid = listener_uid == 0 ? 1202U : listener_gid;
    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0) return 498;
    int pipe_descriptors[2] = {-1, -1};
    if (pipe(pipe_descriptors) != 0) {
        close(listener);
        return 504;
    }
    struct sockaddr_un address;
    make_unix_address(&address, path);
    if (bind(listener, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(listener, 1) != 0) {
        close(pipe_descriptors[0]);
        close(pipe_descriptors[1]);
        close(listener);
        return 499;
    }
    struct sockaddr_un bound_address;
    socklen_t bound_length = sizeof(bound_address);
    int socket_type = 0;
    socklen_t type_length = sizeof(socket_type);
    if (getsockname(listener, (struct sockaddr *)&bound_address,
                     &bound_length) != 0 ||
        bound_address.sun_family != AF_UNIX ||
        strcmp(bound_address.sun_path, path) != 0 ||
        getsockopt(listener, SOL_SOCKET, SO_TYPE, &socket_type,
                   &type_length) != 0 || socket_type != SOCK_STREAM)
        return 566;
    struct ucred credentials;
    socklen_t credentials_length = sizeof(credentials);
    errno = 0;
    if (getsockopt(listener, SOL_SOCKET, SO_PEERCRED, &credentials,
                   &credentials_length) == 0 || errno != ENOTCONN) {
        close(pipe_descriptors[0]);
        close(pipe_descriptors[1]);
        close(listener);
        return 503;
    }

    pid_t child = fork();
    if (child < 0) {
        close(pipe_descriptors[0]);
        close(pipe_descriptors[1]);
        close(listener);
        return 500;
    }
    if (child == 0) {
        close(pipe_descriptors[0]);
        if (listener_uid == 0 &&
            (setgid(client_gid) != 0 || setuid(client_uid) != 0)) {
            _exit(3);
        }
        int client = socket(AF_UNIX, SOCK_STREAM, 0);
        if (client < 0 ||
            connect(client, (const struct sockaddr *)&address,
                    sizeof(address)) != 0) {
            if (client >= 0) close(client);
            _exit(1);
        }
        struct ucred server_credentials;
        socklen_t server_credentials_length = sizeof(server_credentials);
        if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &server_credentials,
                       &server_credentials_length) != 0 ||
            server_credentials_length != sizeof(server_credentials) ||
            server_credentials.pid != listener_pid ||
            server_credentials.uid != listener_uid ||
            server_credentials.gid != listener_gid) {
            close(client);
            _exit(4);
        }
        const char payload[] = "akaos-af-unix";
        struct iovec vector = {
            .iov_base = (void *)payload,
            .iov_len = sizeof(payload) - 1U,
        };
        union {
            struct cmsghdr alignment;
            unsigned char bytes[CMSG_SPACE(sizeof(int))];
        } control;
        memset(&control, 0, sizeof(control));
        struct msghdr message;
        memset(&message, 0, sizeof(message));
        message.msg_iov = &vector;
        message.msg_iovlen = 1U;
        message.msg_control = control.bytes;
        message.msg_controllen = sizeof(control.bytes);
        struct cmsghdr *header = CMSG_FIRSTHDR(&message);
        if (!header) {
            close(client);
            close(pipe_descriptors[1]);
            _exit(5);
        }
        header->cmsg_len = CMSG_LEN(sizeof(int));
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        memcpy(CMSG_DATA(header), &pipe_descriptors[1], sizeof(int));
        ssize_t sent = sendmsg(client, &message, 0);
        if (sent != (ssize_t)(sizeof(payload) - 1U) ||
            shutdown(client, SHUT_WR) != 0) _exit(6);
        errno = 0;
        if (write(client, payload, 1U) != -1 || errno != EPIPE) _exit(7);
        struct pollfd reply_ready = {.fd = client, .events = POLLIN};
        char reply = 0;
        if (poll(&reply_ready, 1U, 5000) != 1 ||
            read(client, &reply, 1U) != 1 || reply != 'A') _exit(8);
        close(client);
        close(pipe_descriptors[1]);
        _exit(sent == (ssize_t)(sizeof(payload) - 1U) ? 0 : 2);
    }

    close(pipe_descriptors[1]);

    int peer = accept(listener, NULL, NULL);
    if (peer < 0) {
        close(pipe_descriptors[0]);
        close(listener);
        return 501;
    }
    credentials_length = sizeof(credentials);
    int credentials_valid =
        getsockopt(peer, SOL_SOCKET, SO_PEERCRED, &credentials,
                   &credentials_length) == 0 &&
        credentials_length == sizeof(credentials) &&
        credentials.pid == child && credentials.uid == client_uid &&
        credentials.gid == client_gid;
    char payload[32] = {0};
    struct iovec receive_vector = {
        .iov_base = payload,
        .iov_len = sizeof(payload),
    };
    union {
        struct cmsghdr alignment;
        unsigned char bytes[CMSG_SPACE(sizeof(int))];
    } received_control;
    memset(&received_control, 0, sizeof(received_control));
    struct msghdr receive_message;
    memset(&receive_message, 0, sizeof(receive_message));
    receive_message.msg_iov = &receive_vector;
    receive_message.msg_iovlen = 1U;
    receive_message.msg_control = received_control.bytes;
    receive_message.msg_controllen = sizeof(received_control.bytes);
    struct pollfd readable_socket = {.fd = peer, .events = POLLIN};
    if (poll(&readable_socket, 1U, 5000) != 1 ||
        (readable_socket.revents & POLLIN) == 0) {
        close(pipe_descriptors[0]);
        close(peer);
        close(listener);
        return 505;
    }
    ssize_t received = recvmsg(peer, &receive_message, MSG_CMSG_CLOEXEC);
    int transferred_writer = -1;
    struct cmsghdr *received_header = CMSG_FIRSTHDR(&receive_message);
    if (received_header &&
        received_header->cmsg_level == SOL_SOCKET &&
        received_header->cmsg_type == SCM_RIGHTS &&
        received_header->cmsg_len >= CMSG_LEN(sizeof(int))) {
        memcpy(&transferred_writer, CMSG_DATA(received_header), sizeof(int));
    }
    const char pipe_marker = 'R';
    char pipe_result = 0;
    int transfer_valid = transferred_writer >= 0 &&
        (receive_message.msg_flags & MSG_CTRUNC) == 0 &&
        fcntl(transferred_writer, F_GETFD) == FD_CLOEXEC &&
        write(transferred_writer, &pipe_marker, 1U) == 1 &&
        read(pipe_descriptors[0], &pipe_result, 1U) == 1 &&
        pipe_result == pipe_marker;
    if (transferred_writer >= 0) close(transferred_writer);
    close(pipe_descriptors[0]);
    char eof_byte = 0;
    int half_close_valid = poll(&readable_socket, 1U, 5000) == 1 &&
        read(peer, &eof_byte, 1U) == 0 && write(peer, "A", 1U) == 1;
    close(peer);
    close(listener);
    int status = -1;
    if (!credentials_valid || !transfer_valid || !half_close_valid ||
        waitpid(child, &status, 0) != child || status != 0 ||
        received != 13 || memcmp(payload, "akaos-af-unix", 13) != 0) {
        return 502;
    }
    puts("AF_UNIX stream transport verified");
    puts("AF_UNIX peer credentials verified");
    puts("AF_UNIX SCM_RIGHTS descriptor passing verified");
    puts("AF_UNIX SCM_RIGHTS close-on-exec verified");
    puts("AF_UNIX socket names, type query, and bidirectional half-close verified");
    return 0;
}

static int test_select_compatibility(void) {
    int descriptors[2] = {-1, -1};
    if (pipe(descriptors) != 0) return 503;
    fd_set read_set;
    fd_set write_set;
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    FD_SET(descriptors[0], &read_set);
    FD_SET(descriptors[1], &write_set);
    struct timeval timeout = {0, 500000};
    int ready = select(descriptors[1] + 1, &read_set, &write_set, NULL,
                       &timeout);
    if (ready < 1 || !FD_ISSET(descriptors[1], &write_set) ||
        FD_ISSET(descriptors[0], &read_set)) {
        close(descriptors[0]);
        close(descriptors[1]);
        return 504;
    }
    const char marker = 'S';
    if (write(descriptors[1], &marker, 1) != 1) {
        close(descriptors[0]);
        close(descriptors[1]);
        return 505;
    }
    FD_ZERO(&read_set);
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000;
    FD_SET(descriptors[0], &read_set);
    ready = select(descriptors[0] + 1, &read_set, NULL, NULL, &timeout);
    char received = 0;
    int result = ready == 1 && FD_ISSET(descriptors[0], &read_set) &&
                 read(descriptors[0], &received, 1) == 1 && received == marker;
    close(descriptors[0]);
    close(descriptors[1]);
    if (!result) return 506;
    puts("select() descriptor readiness verified");
    return 0;
}

static int test_epoll_compatibility(void) {
    int descriptors[2] = {-1, -1};
    if (pipe(descriptors) != 0) return 507;
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        close(descriptors[0]);
        close(descriptors[1]);
        return 508;
    }
    struct epoll_event watch;
    memset(&watch, 0, sizeof(watch));
    watch.events = EPOLLIN | EPOLLRDHUP;
    watch.data.fd = descriptors[0];
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, descriptors[0], &watch) != 0 ||
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, descriptors[0], &watch) == 0 ||
        errno != EEXIST) {
        close(epoll_fd);
        close(descriptors[0]);
        close(descriptors[1]);
        return 509;
    }
    struct epoll_event events[2];
    memset(events, 0, sizeof(events));
    if (epoll_wait(epoll_fd, events, 2, 0) != 0) {
        close(epoll_fd);
        close(descriptors[0]);
        close(descriptors[1]);
        return 510;
    }
    const char marker = 'E';
    if (write(descriptors[1], &marker, 1) != 1 ||
        epoll_wait(epoll_fd, events, 2, 500) != 1 ||
        events[0].data.fd != descriptors[0] ||
        !(events[0].events & EPOLLIN)) {
        close(epoll_fd);
        close(descriptors[0]);
        close(descriptors[1]);
        return 511;
    }
    char received = 0;
    if (read(descriptors[0], &received, 1) != 1 || received != marker ||
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, descriptors[0], &watch) != 0 ||
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, descriptors[0], NULL) != 0 ||
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, descriptors[0], NULL) == 0 ||
        errno != ENOENT) {
        close(epoll_fd);
        close(descriptors[0]);
        close(descriptors[1]);
        return 512;
    }
    close(epoll_fd);
    close(descriptors[0]);
    close(descriptors[1]);
    puts("epoll descriptor readiness verified");

    enum { WATCH_COUNT = 20U };
    int high_pipes[WATCH_COUNT][2];
    for (size_t index = 0; index < WATCH_COUNT; ++index) {
        high_pipes[index][0] = -1;
        high_pipes[index][1] = -1;
    }
    for (size_t index = 0; index < WATCH_COUNT; ++index) {
        if (pipe(high_pipes[index]) != 0) {
            for (size_t cleanup = 0; cleanup < WATCH_COUNT; ++cleanup) {
                if (high_pipes[cleanup][0] >= 0) close(high_pipes[cleanup][0]);
                if (high_pipes[cleanup][1] >= 0) close(high_pipes[cleanup][1]);
            }
            return 524;
        }
    }
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        for (size_t index = 0; index < WATCH_COUNT; ++index) {
            close(high_pipes[index][0]);
            close(high_pipes[index][1]);
        }
        return 525;
    }
    int high_descriptor_failure = 0;
    for (size_t index = 0; index < WATCH_COUNT; ++index) {
        struct epoll_event high_watch;
        memset(&high_watch, 0, sizeof(high_watch));
        high_watch.events = EPOLLIN;
        high_watch.data.fd = high_pipes[index][0];
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, high_pipes[index][0],
                      &high_watch) != 0) {
            high_descriptor_failure = 1;
            break;
        }
    }
    if (high_pipes[WATCH_COUNT - 1U][0] < 16)
        high_descriptor_failure = 1;
    const char high_marker = 'H';
    struct epoll_event high_events[WATCH_COUNT];
    memset(high_events, 0, sizeof(high_events));
    if (high_descriptor_failure ||
        write(high_pipes[WATCH_COUNT - 1U][1], &high_marker, 1U) != 1 ||
        epoll_wait(epoll_fd, high_events, WATCH_COUNT, 500) != 1 ||
        high_events[0].data.fd != high_pipes[WATCH_COUNT - 1U][0] ||
        !(high_events[0].events & EPOLLIN))
        high_descriptor_failure = 1;
    close(epoll_fd);
    for (size_t index = 0; index < WATCH_COUNT; ++index) {
        close(high_pipes[index][0]);
        close(high_pipes[index][1]);
    }
    if (high_descriptor_failure) return 526;
    puts("epoll accepts native descriptors above 16 and 20 simultaneous watches");
    return 0;
}

static volatile uint32_t exit_group_worker_started;

static void *exit_group_worker(void *argument) {
    (void)argument;
    __atomic_store_n(&exit_group_worker_started, 1U, __ATOMIC_RELEASE);
    for (;;) (void)poll(NULL, 0U, 10);
    return NULL;
}

static int test_exit_group_compatibility(void) {
    pthread_t worker;
    exit_group_worker_started = 0U;
    if (pthread_create(&worker, NULL, exit_group_worker, NULL) != 0)
        return 527;
    if (pthread_detach(worker) != 0) return 528;
    for (unsigned attempt = 0U; attempt < 1000U; ++attempt) {
        if (__atomic_load_n(&exit_group_worker_started, __ATOMIC_ACQUIRE)) {
            puts("POSIX detached worker started before exit-group");
            puts("POSIX process exit-group requested with detached worker");
            return 0;
        }
        (void)poll(NULL, 0U, 1);
    }
    return 529;
}

static int test_eventfd_compatibility(void) {
    int descriptor = eventfd(0, 0);
    if (descriptor < 0) return 513;
    int duplicate = fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
    uint64_t duplicate_value = 0;
    if (duplicate < 0 || fcntl(duplicate, F_GETFD) != FD_CLOEXEC ||
        eventfd_write(descriptor, 1) != 0 ||
        eventfd_read(duplicate, &duplicate_value) != 0 ||
        duplicate_value != 1U || close(duplicate) != 0) {
        if (duplicate >= 0) close(duplicate);
        close(descriptor);
        return 520;
    }
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        close(descriptor);
        return 514;
    }
    struct epoll_event watch;
    memset(&watch, 0, sizeof(watch));
    watch.events = EPOLLIN;
    watch.data.u64 = UINT64_C(0xfeedbeef);
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, descriptor, &watch) != 0) {
        close(epoll_fd);
        close(descriptor);
        return 515;
    }
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    if (epoll_wait(epoll_fd, &event, 1, 0) != 0 ||
        eventfd_write(descriptor, 5) != 0 ||
        epoll_wait(epoll_fd, &event, 1, 500) != 1 ||
        event.data.u64 != UINT64_C(0xfeedbeef) ||
        !(event.events & EPOLLIN)) {
        close(epoll_fd);
        close(descriptor);
        return 516;
    }
    uint64_t value = 0;
    if (eventfd_read(descriptor, &value) != 0 || value != 5U ||
        epoll_wait(epoll_fd, &event, 1, 0) != 0) {
        close(epoll_fd);
        close(descriptor);
        return 517;
    }
    close(epoll_fd);
    close(descriptor);
    descriptor = eventfd(7, EFD_SEMAPHORE);
    if (descriptor < 0 || eventfd_read(descriptor, &value) != 0 ||
        value != 1U || close(descriptor) != 0) {
        if (descriptor >= 0) close(descriptor);
        return 518;
    }
    descriptor = eventfd(0, EFD_NONBLOCK);
    errno = 0;
    if (descriptor < 0 || eventfd_read(descriptor, &value) != -1 ||
        errno != EAGAIN || close(descriptor) != 0) {
        if (descriptor >= 0) close(descriptor);
        return 519;
    }
    int concurrent[16];
    size_t concurrent_count = 0;
    for (; concurrent_count < sizeof(concurrent) / sizeof(concurrent[0]);
         ++concurrent_count) {
        concurrent[concurrent_count] = eventfd(
            0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (concurrent[concurrent_count] < 0) {
            while (concurrent_count > 0) close(concurrent[--concurrent_count]);
            return 521;
        }
    }
    while (concurrent_count > 0) close(concurrent[--concurrent_count]);
    puts("eventfd sixteen concurrent handles verified");
    puts("eventfd readiness and counter semantics verified");
    return 0;
}

static int test_timerfd_compatibility(void) {
    int descriptor = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (descriptor < 0) return 520;
    struct pollfd watcher = {descriptor, POLLIN, 0};
    if (poll(&watcher, 1, 0) != 0 || (watcher.revents & POLLIN)) {
        close(descriptor);
        return 521;
    }
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        close(descriptor);
        return 522;
    }
    struct epoll_event watch;
    memset(&watch, 0, sizeof(watch));
    watch.events = EPOLLIN;
    watch.data.u64 = UINT64_C(0x71efed);
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, descriptor, &watch) != 0 ||
        epoll_wait(epoll_fd, &watch, 1, 0) != 0) {
        close(epoll_fd);
        close(descriptor);
        return 523;
    }

    struct itimerspec value;
    memset(&value, 0, sizeof(value));
    value.it_interval.tv_nsec = 20000000L;
    value.it_value.tv_nsec = 20000000L;
    if (timerfd_settime(descriptor, 0, &value, NULL) != 0 ||
        poll(&watcher, 1, 500) != 1 || !(watcher.revents & POLLIN)) {
        close(epoll_fd);
        close(descriptor);
        return 524;
    }
    if (epoll_wait(epoll_fd, &watch, 1, 500) != 1 ||
        !(watch.events & EPOLLIN) ||
        watch.data.u64 != UINT64_C(0x71efed)) {
        close(epoll_fd);
        close(descriptor);
        return 525;
    }
    uint64_t expirations = 0;
    if (read(descriptor, &expirations, sizeof(expirations)) !=
            (ssize_t)sizeof(expirations) || expirations == 0) {
        close(epoll_fd);
        close(descriptor);
        return 526;
    }

    struct itimerspec current;
    memset(&current, 0, sizeof(current));
    if (timerfd_gettime(descriptor, &current) != 0 ||
        current.it_interval.tv_nsec != 20000000L) {
        close(epoll_fd);
        close(descriptor);
        return 527;
    }
    memset(&value, 0, sizeof(value));
    if (timerfd_settime(descriptor, 0, &value, NULL) != 0) {
        close(epoll_fd);
        close(descriptor);
        return 528;
    }
    errno = 0;
    if (read(descriptor, &expirations, sizeof(expirations)) != -1 ||
        errno != EAGAIN || close(epoll_fd) != 0 || close(descriptor) != 0) {
        close(epoll_fd);
        if (descriptor >= 0) close(descriptor);
        return 529;
    }
    puts("timerfd readiness, interval, and disarm semantics verified");
    return 0;
}

static int test_eventfd_timerfd_descriptor_ranges(void) {
    int event_descriptors[25];
    size_t event_count = 0U;
    int timer_descriptor = -1;
    int result = 0;

    for (; event_count < sizeof(event_descriptors) /
                         sizeof(event_descriptors[0]); ++event_count) {
        event_descriptors[event_count] = eventfd(0, EFD_NONBLOCK);
        if (event_descriptors[event_count] < 0) {
            result = 670;
            goto cleanup;
        }
    }

    timer_descriptor = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timer_descriptor < 0) {
        result = 671;
        goto cleanup;
    }
    for (size_t index = 0; index < event_count; ++index) {
        if (event_descriptors[index] == timer_descriptor) {
            result = 672;
            goto cleanup;
        }
    }

cleanup:
    if (timer_descriptor >= 0) (void)close(timer_descriptor);
    while (event_count > 0U)
        (void)close(event_descriptors[--event_count]);
    if (result != 0) {
        printf("eventfd/timerfd descriptor overlap probe failed at stage %d\n",
               result);
        return result;
    }
    puts("POSIX eventfd/timerfd descriptor ranges verified");
    return 0;
}

static int test_getaddrinfo_compatibility(void) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;
    struct addrinfo *info = NULL;
    int result = getaddrinfo("localhost", "8080", &hints, &info);
    if (result != 0 || !info || info->ai_family != AF_INET ||
        info->ai_socktype != SOCK_STREAM || !info->ai_addr ||
        ntohs(((struct sockaddr_in *)info->ai_addr)->sin_port) != 8080U ||
        ((struct sockaddr_in *)info->ai_addr)->sin_addr.s_addr !=
            htonl(INADDR_LOOPBACK) || !info->ai_canonname) {
        printf("getaddrinfo first probe failed: code=%d info=%p fam=%d type=%d port=%u addr=%u canon=%p\n",
               result, (void *)info, info ? info->ai_family : -1,
               info ? info->ai_socktype : -1,
               info && info->ai_addr ? (unsigned)ntohs(
                   ((struct sockaddr_in *)info->ai_addr)->sin_port) : 0U,
               info && info->ai_addr ?
                   (unsigned)((struct sockaddr_in *)info->ai_addr)->sin_addr.s_addr : 0U,
               info ? (void *)info->ai_canonname : NULL);
        freeaddrinfo(info);
        return 507;
    }
    freeaddrinfo(info);
    hints.ai_flags = AI_NUMERICHOST;
    result = getaddrinfo("127.0.0.1", NULL, &hints, &info);
    int numeric_ok = result == 0 && info && info->ai_addr &&
                     ((struct sockaddr_in *)info->ai_addr)->sin_addr.s_addr ==
                         htonl(INADDR_LOOPBACK);
    freeaddrinfo(info);
    if (!numeric_ok) {
        printf("getaddrinfo numeric probe failed: code=%d info=%p\n", result,
               (void *)info);
        return 508;
    }
    puts("getaddrinfo() IPv4 resolution verified");
    return 0;
}

static int test_locale_utf8_compatibility(void) {
    if (!setlocale(LC_ALL, "") || !setlocale(LC_CTYPE, NULL) ||
        !isalpha('A') || !isdigit('7') || !isspace('\n') ||
        tolower('Q') != 'q' || toupper('m') != 'M') {
        return 517;
    }
    struct lconv *format = localeconv();
    if (!format || !format->decimal_point ||
        strcmp(format->decimal_point, ".") != 0) {
        return 518;
    }
    locale_t c_locale = newlocale(LC_CTYPE_MASK, "C", (locale_t)0);
    if (!c_locale || strcmp(nl_langinfo(CODESET), "UTF-8") != 0 ||
        strcmp(nl_langinfo_l(CODESET, c_locale), "US-ASCII") != 0 ||
        strcmp(nl_langinfo_l(RADIXCHAR, c_locale), ".") != 0 ||
        strcmp(nl_langinfo(T_FMT), "%H:%M:%S") != 0) {
        freelocale(c_locale);
        return 523;
    }
    freelocale(c_locale);
    const char utf8_text[] = "akaOS \xe2\x82\xac";
    wchar_t wide[16];
    size_t wide_count = mbstowcs(wide, utf8_text, 16);
    if (wide_count != 7U || wide[6] != (wchar_t)0x20acU || wide[7] != 0 ||
        !iswalpha((wint_t)'a') || !iswdigit((wint_t)'3') ||
        !iswspace((wint_t)' ') || towupper((wint_t)'z') != 'Z') {
        return 519;
    }
    char roundtrip[32];
    size_t byte_count = wcstombs(roundtrip, wide, sizeof(roundtrip));
    if (byte_count != sizeof(utf8_text) - 1U ||
        memcmp(roundtrip, utf8_text, sizeof(utf8_text)) != 0) {
        return 520;
    }
    wchar_t euro = 0;
    size_t consumed = mbrtowc(&euro, utf8_text + 6, 3, NULL);
    char encoded[4] = {0, 0, 0, 0};
    size_t encoded_count = wcrtomb(encoded, euro, NULL);
    if (consumed != 3U || euro != (wchar_t)0x20acU || encoded_count != 3U ||
        memcmp(encoded, utf8_text + 6, 3) != 0) {
        return 521;
    }
    puts("locale and UTF-8 compatibility verified");
    puts("POSIX nl_langinfo locale codeset verified");
    return 0;
}

static int compare_qsort_integer(const void *left, const void *right) {
    int left_value = *(const int *)left;
    int right_value = *(const int *)right;
    return (left_value > right_value) - (left_value < right_value);
}

static int test_qsort(void) {
    int values[] = {9, -3, 4, 4, 0, 12, -9, 1};
    static const int expected[] = {-9, -3, 0, 1, 4, 4, 9, 12};
    qsort(values, sizeof(values) / sizeof(values[0]), sizeof(values[0]),
          compare_qsort_integer);
    for (size_t index = 0; index < sizeof(values) / sizeof(values[0]); ++index) {
        if (values[index] != expected[index]) return 505;
    }
    puts("ISO C qsort ordering and duplicate handling verified");
    int search_key = 4;
    int *found = bsearch(&search_key, values,
                         sizeof(values) / sizeof(values[0]), sizeof(values[0]),
                         compare_qsort_integer);
    int missing_key = 3;
    if (!found || *found != search_key ||
        bsearch(&missing_key, values,
                sizeof(values) / sizeof(values[0]), sizeof(values[0]),
                compare_qsort_integer) != NULL)
        return 519;
    puts("ISO C bsearch sorted lookup and miss handling verified");
    return 0;
}

static int test_endian_compatibility(void) {
    uint16_t value16 = 0x1234U;
    uint32_t value32 = 0x12345678U;
    uint64_t value64 = 0x0123456789abcdefULL;
    if (BYTE_ORDER != LITTLE_ENDIAN || bswap_16(value16) != 0x3412U ||
        bswap_32(value32) != 0x78563412U ||
        bswap_64(value64) != 0xefcdab8967452301ULL ||
        htole32(value32) != value32 || le32toh(value32) != value32 ||
        htobe32(value32) != 0x78563412U ||
        be32toh(0x78563412U) != value32) {
        return 527;
    }
    puts("POSIX endian constants and byte-swap macros verified");
    return 0;
}

static int test_strdup(void) {
    static const char source[] = "akaOS upstream library ABI";
    char *copy = strdup(source);
    if (!copy || strcmp(copy, source) != 0) {
        free(copy);
        return 504;
    }
    free(copy);

    char *bounded = strndup(source, 11U);
    if (!bounded || strcmp(bounded, "akaOS upstr") != 0) {
        free(bounded);
        return 503;
    }
    free(bounded);
    puts("POSIX strdup/strndup allocation and contents verified");
    return 0;
}

static int test_atol(void) {
    if (atol("-123456trailing") != -123456L || atol("  +42x") != 42L ||
        atof(" -3.5e1trailing") != -35.0 || atof("not-a-number") != 0.0)
        return 511;
    puts("ISO C atol/atof integer and floating parsing verified");
    return 0;
}

static int test_math_runtime(void) {
    int exponent = 0;
    if (frexp(8.0, &exponent) != 0.5 || exponent != 4) return 532;
    if (frexpf(0.25f, &exponent) != 0.5f || exponent != -1) return 533;
    if (frexpl(16.0L, &exponent) != 0.5L || exponent != 5) return 534;
    if (ldexp(0.75, 3) != 6.0 || ldexpf(0.75f, 3) != 6.0f ||
        ldexpl(0.75L, 3) != 6.0L)
        return 536;
    errno = 0;
    if (!isinf(ldexpf(1.0f, 300)) || errno != ERANGE) return 537;
    double natural_log_two = log(2.0);
    if (floor(3.75) != 3.0 || ceil(-3.75) != -3.0 ||
        floorf(-0.25f) != -1.0f || ceill(1.25L) != 2.0L ||
        natural_log_two < 0.693147180559 ||
        natural_log_two > 0.693147180561 || !isnan(log(-1.0)))
        return 535;
    puts("frexp/floor/ceil/log shared math primitives verified");
    puts("ISO C ldexp scaling and range handling verified");
    return 0;
}

static int test_graphics_math(void) {
    const double pi_over_4 = 0.785398163397448309615660845819875721;
    const double exp_one = 2.718281828459045235360287471352662498;
    double integral = 0.0;
    float integral_float = 0.0f;
    long double integral_long = 0.0L;
    int saved_rounding = fegetround();
    int quotient_bits = 0;
    void *aligned_memory = NULL;
    int aligned_result = posix_memalign(&aligned_memory, 64U, 37U);
    int aligned_ok = aligned_result == 0 && aligned_memory &&
                     (((uintptr_t)aligned_memory & 63U) == 0U);
    free(aligned_memory);
    int fenv_ok = saved_rounding >= FE_TONEAREST &&
                  saved_rounding <= FE_TOWARDZERO;
    if (fenv_ok) {
        fenv_ok = fesetround(FE_UPWARD) == 0 &&
                  fegetround() == FE_UPWARD &&
                  lrint(2.1) == 3L && lrint(-2.1) == -2L &&
                  fesetround(FE_DOWNWARD) == 0 &&
                  fegetround() == FE_DOWNWARD &&
                  lrint(2.9) == 2L && lrint(-2.1) == -3L;
        if (fesetround(saved_rounding) != 0) fenv_ok = 0;
    }
    if (fabs(-3.25) != 3.25 || fabsf(-1.5f) != 1.5f ||
        fmaxf(NAN, -3.0f) != -3.0f || fminf(NAN, 4.0f) != 4.0f ||
        signbit(fmaxf(-0.0f, 0.0f)) || !signbit(fminf(-0.0f, 0.0f)) ||
        fmax(2.0, 7.0) != 7.0 || fminl(-2.0L, 1.0L) != -2.0L ||
        modf(-3.75, &integral) != -0.75 || integral != -3.0 ||
        modff(4.25f, &integral_float) != 0.25f || integral_float != 4.0f ||
        modfl(-6.5L, &integral_long) != -0.5L || integral_long != -6.0L ||
        !signbit(modf(-__builtin_inf(), &integral)) || !isinf(integral) ||
        lrint(2.5) != 2L || lrint(3.5) != 4L || lrintf(-2.5f) != -2L ||
        lrintl(-3.6L) != -4L || !fenv_ok || !aligned_ok ||
        fabs(sqrt(2.0) - 1.4142135623730950488) > 1.0e-14 ||
        sqrtf(9.0f) != 3.0f ||
        fabs(exp(1.0) - exp_one) > 1.0e-12 ||
        fabs(sin(0.5) - 0.47942553860420300027) > 1.0e-12 ||
        fabs(cos(0.5) - 0.87758256189037271612) > 1.0e-12 ||
        fabs(atan2(1.0, 1.0) - pi_over_4) > 1.0e-12 ||
        hypot(3.0, 4.0) != 5.0 ||
        !signbit(copysign(2.0, -1.0)) || !signbit(copysignf(2.0f, -1.0f)) ||
        fabs(cosh(0.5) - 1.1276259652063807852) > 1.0e-12 ||
        fabs(sinh(0.5) - 0.5210953054937473616) > 1.0e-12 ||
        fabs(tanh(0.5) - 0.4621171572600097585) > 1.0e-12 ||
        fabs(acosh(2.0) - 1.3169578969248167086) > 1.0e-12 ||
        fabs(asinh(1.0) - 0.8813735870195430252) > 1.0e-12 ||
        fabs(atanh(0.5) - 0.5493061443340548457) > 1.0e-12 ||
        remainder(5.0, 2.0) != 1.0 || remainder(7.0, 2.0) != -1.0 ||
        remquo(7.0, 2.0, &quotient_bits) != -1.0 || quotient_bits != 4 ||
        fabs(expm1(1.0e-8) / 1.0e-8 - 1.000000005) > 1.0e-15 ||
        fabs(log1p(1.0e-8) / 1.0e-8 - 0.999999995) > 1.0e-15)
        return 536;

    errno = 0;
    if (!isnan(acosh(0.5)) || errno != EDOM) return 540;
    errno = 0;
    if (!isnan(atanh(2.0)) || errno != EDOM) return 541;
    errno = 0;
    if (!isinf(log1p(-1.0)) || errno != ERANGE) return 542;

    errno = 0;
    if (!isnan(sqrt(-1.0)) || errno != EDOM) return 537;
    errno = 0;
    if (!isinf(exp(1000.0)) || errno != ERANGE) return 538;
    puts("POSIX Cairo math/allocation primitives sqrt/fabs/atan2/exp/sin/log2/log10/exp2/cbrt/nearbyint/fmax/fmin/modf/lrint/fenv/posix_memalign verified");
    puts("POSIX expm1/log1p, hyperbolic/inverse-hyperbolic, remainder/remquo, copysign, and domain semantics verified");
    return 0;
}

static int test_setjmp_abi(void) {
    jmp_buf environment;
    volatile int updated = 0;
    int resumed = setjmp(environment);
    if (resumed == 0) {
        updated = 17;
        longjmp(environment, 0);
    }
    if (resumed != 1 || updated != 17) return 539;
    puts("POSIX setjmp/longjmp caller-frame and zero-value semantics verified");
    return 0;
}

static int test_tmpfile_stream(void) {
    FILE *stream = tmpfile();
    if (!stream) return 540;
    static const char expected[] = "akaOS cairo temporary stream";
    char buffer[sizeof(expected)];
    if (fputs(expected, stream) == EOF || fflush(stream) != 0) {
        fclose(stream);
        return 541;
    }
    rewind(stream);
    if (!fgets(buffer, sizeof(buffer), stream) ||
        strcmp(buffer, expected) != 0 || fclose(stream) != 0)
        return 542;
    puts("POSIX tmpfile anonymous temporary stream verified");
    return 0;
}

static int test_getline_mkstemp(void) {
    char path[] = "/tmp/akaos-line-XXXXXX";
    int descriptor = mkstemp(path);
    if (descriptor < 0) return 549;

    char second_path[] = "/tmp/akaos-line-XXXXXX";
    int second_descriptor = mkstemp(second_path);
    if (second_descriptor < 0 || strcmp(path, second_path) == 0) {
        if (second_descriptor >= 0) close(second_descriptor);
        close(descriptor);
        unlink(path);
        if (second_descriptor >= 0) unlink(second_path);
        return 550;
    }
    close(second_descriptor);
    unlink(second_path);

    char payload[303];
    memset(payload, 'x', 300U);
    payload[300] = '\n';
    payload[301] = 'z';
    payload[302] = '\n';
    if (write(descriptor, payload, sizeof(payload)) !=
            (ssize_t)sizeof(payload) || lseek(descriptor, 0, SEEK_SET) != 0) {
        close(descriptor);
        unlink(path);
        return 551;
    }

    FILE *stream = fdopen(descriptor, "r");
    if (!stream) {
        close(descriptor);
        unlink(path);
        return 552;
    }
    char *line = NULL;
    size_t capacity = 0U;
    ssize_t line_length = getline(&line, &capacity, stream);
    int valid = line_length == 301 && capacity >= 302U &&
                line[0] == 'x' && line[299] == 'x' && line[300] == '\n' &&
                line[301] == '\0';
    ssize_t second_length = getdelim(&line, &capacity, '\n', stream);
    valid = valid && second_length == 2 && strcmp(line, "z\n") == 0;
    valid = valid && getline(&line, &capacity, stream) == -1 && line[0] == '\0';

    int close_status = fclose(stream);
    free(line);
    unlink(path);
    if (!valid || close_status != 0) return 553;
    puts("POSIX getline/getdelim growth and exclusive mkstemp creation verified");
    return 0;
}

static int test_open_memstream(void) {
    char *buffer = NULL;
    size_t length = 0;
    FILE *stream = open_memstream(&buffer, &length);
    if (!stream) return 543;
    if (fprintf(stream, "wayland-%u", 42U) < 0 || fflush(stream) != 0 ||
        !buffer || length != 10U || strcmp(buffer, "wayland-42") != 0) {
        (void)fclose(stream);
        free(buffer);
        return 544;
    }
    if (fclose(stream) != 0 || !buffer || length != 10U ||
        strcmp(buffer, "wayland-42") != 0) {
        free(buffer);
        return 545;
    }
    free(buffer);
    puts("POSIX open_memstream flush and close snapshot verified");
    return 0;
}

static int test_memfd_msync(void) {
    const char shm_cloexec_name[] = "/akaos-hello-shm-cloexec";
    int shm_cloexec = shm_open(shm_cloexec_name,
                               O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (shm_cloexec < 0) return 574;
    int shm_cloexec_flags = fcntl(shm_cloexec, F_GETFD);
    int shm_cloexec_close = close(shm_cloexec);
    int shm_cloexec_unlink = shm_unlink(shm_cloexec_name);
    if (shm_cloexec_flags != FD_CLOEXEC || shm_cloexec_close != 0 ||
        shm_cloexec_unlink != 0)
        return 575;
    puts("POSIX shm_open O_CLOEXEC descriptor semantics verified");

    int unsealable = memfd_create("akaos-unsealable", MFD_CLOEXEC);
    if (unsealable < 0 || fcntl(unsealable, F_GET_SEALS) != F_SEAL_SEAL) {
        if (unsealable >= 0) close(unsealable);
        return 560;
    }
    errno = 0;
    int rejected_seal = fcntl(unsealable, F_ADD_SEALS, F_SEAL_SHRINK);
    int unsealable_errno = errno;
    close(unsealable);
    if (rejected_seal != -1 || unsealable_errno != EPERM) return 561;

    int descriptor = memfd_create("akaos-wayland-smoke",
                                  MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (descriptor < 0) return 546;
    if (fcntl(descriptor, F_GETFD) != FD_CLOEXEC ||
        fcntl(descriptor, F_GET_SEALS) != 0 ||
        ftruncate(descriptor, 4096) != 0) {
        close(descriptor);
        return 562;
    }
    if (fcntl(descriptor, F_ADD_SEALS,
              F_SEAL_SHRINK | F_SEAL_SEAL) != 0 ||
        fcntl(descriptor, F_GET_SEALS) !=
            (F_SEAL_SHRINK | F_SEAL_SEAL)) {
        close(descriptor);
        return 563;
    }
    errno = 0;
    int shrink_status = ftruncate(descriptor, 2048);
    int shrink_errno = errno;
    if (shrink_status == 0 || shrink_errno != EPERM ||
        ftruncate(descriptor, 8192) != 0) {
        close(descriptor);
        return 564;
    }

    struct stat memfd_status;
    if (fstat(descriptor, &memfd_status) != 0 ||
        !S_ISREG(memfd_status.st_mode) || memfd_status.st_size != 8192 ||
        lseek(descriptor, 0, SEEK_END) != 8192 ||
        lseek(descriptor, 0, SEEK_SET) != 0) {
        close(descriptor);
        return 569;
    }

    unsigned char transfer[AKAOS_SYSCALL_MAX_IO * 2U];
    unsigned char readback[AKAOS_SYSCALL_MAX_IO * 2U];
    memset(transfer, 0x5c, sizeof(transfer));
    if (lseek(descriptor, 0, SEEK_SET) != 0) {
        close(descriptor);
        return 566;
    }
    ssize_t first_write = write(descriptor, transfer, sizeof(transfer));
    ssize_t second_write = first_write > 0
        ? write(descriptor, transfer + first_write,
                sizeof(transfer) - (size_t)first_write)
        : -1;
    if (first_write != (ssize_t)AKAOS_SYSCALL_MAX_IO ||
        second_write != (ssize_t)AKAOS_SYSCALL_MAX_IO ||
        lseek(descriptor, 0, SEEK_SET) != 0) {
        close(descriptor);
        return 567;
    }
    ssize_t first_read = read(descriptor, readback, sizeof(readback));
    ssize_t second_read = first_read > 0
        ? read(descriptor, readback + first_read,
               sizeof(readback) - (size_t)first_read)
        : -1;
    if (first_read != (ssize_t)AKAOS_SYSCALL_MAX_IO ||
        second_read != (ssize_t)AKAOS_SYSCALL_MAX_IO ||
        memcmp(transfer, readback, sizeof(transfer)) != 0) {
        close(descriptor);
        return 568;
    }
    puts("POSIX memfd fstat, seek, read/write, and shared mapping verified");
    puts("POSIX read/write short transfers above the syscall I/O limit verified");

    errno = 0;
    int late_seal_status = fcntl(descriptor, F_ADD_SEALS, F_SEAL_GROW);
    int late_seal_errno = errno;
    if (late_seal_status != -1 || late_seal_errno != EPERM) {
        close(descriptor);
        return 565;
    }

    unsigned char *first = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                MAP_SHARED, descriptor, 0);
    unsigned char *second = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, descriptor, 0);
    if (first == MAP_FAILED || second == MAP_FAILED) {
        if (first != MAP_FAILED) munmap(first, 4096);
        if (second != MAP_FAILED) munmap(second, 4096);
        close(descriptor);
        return 547;
    }
    const off_t mapped_growth_size = (off_t)(sizeof(transfer) + 8192U);
    struct stat mapped_growth_status;
    if (ftruncate(descriptor, mapped_growth_size) != 0) {
        printf("POSIX memfd mapped growth ftruncate failed (errno=%d)\n",
               errno);
        munmap(first, 4096);
        munmap(second, 4096);
        close(descriptor);
        return 573;
    }
    if (fstat(descriptor, &mapped_growth_status) != 0 ||
        mapped_growth_status.st_size != mapped_growth_size) {
        printf("POSIX memfd mapped growth size check failed (errno=%d, size=%ld)\n",
               errno, (long)mapped_growth_status.st_size);
        munmap(first, 4096);
        munmap(second, 4096);
        close(descriptor);
        return 573;
    }
    puts("POSIX memfd growth with active shared mappings verified");
    first[0] = 0x5aU;
    int sync_status = msync(first, 4096, MS_SYNC);
    int coherent = second[0] == 0x5aU;
    int first_unmap = munmap(first, 4096);
    int second_unmap = munmap(second, 4096);
    int close_status = close(descriptor);
    if (sync_status != 0 || !coherent || first_unmap != 0 ||
        second_unmap != 0 || close_status != 0)
        return 548;
    puts("POSIX memfd seals, shared mapping, and msync coherence verified");
    return 0;
}

static int test_vfs_ftruncate(void) {
    static const char path[] = "/tmp/akaos-ftruncate-test";
    (void)unlink(path);
    int descriptor = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (descriptor < 0) return 680;

    int result = 0;
    const unsigned char initial[] = {'a', 'b', 'c', 'd'};
    unsigned char contents[4096];
    struct stat status;
    if (write(descriptor, initial, sizeof(initial)) !=
            (ssize_t)sizeof(initial) ||
        ftruncate(descriptor, (off_t)sizeof(contents)) != 0 ||
        lseek(descriptor, 0, SEEK_SET) != 0 ||
        read(descriptor, contents, sizeof(contents)) !=
            (ssize_t)sizeof(contents)) {
        result = 681;
        goto cleanup;
    }
    if (memcmp(contents, initial, sizeof(initial)) != 0) {
        result = 682;
        goto cleanup;
    }
    for (size_t index = sizeof(initial); index < sizeof(contents); ++index) {
        if (contents[index] != 0U) {
            result = 683;
            goto cleanup;
        }
    }

    if (ftruncate(descriptor, 2) != 0 ||
        ftruncate(descriptor, (off_t)sizeof(initial)) != 0 ||
        lseek(descriptor, 0, SEEK_SET) != 0 ||
        read(descriptor, contents, sizeof(initial)) !=
            (ssize_t)sizeof(initial) ||
        contents[0] != 'a' || contents[1] != 'b' ||
        contents[2] != 0U || contents[3] != 0U ||
        fstat(descriptor, &status) != 0 || status.st_size != 4) {
        result = 684;
        goto cleanup;
    }

    errno = 0;
    if (ftruncate(descriptor, 131073) != -1 || errno != ENOSPC) {
        result = 685;
        goto cleanup;
    }
    if (close(descriptor) != 0) {
        descriptor = -1;
        result = 686;
        goto cleanup;
    }
    descriptor = open(path, O_RDONLY);
    if (descriptor < 0) {
        result = 687;
        goto cleanup;
    }
    errno = 0;
    if (ftruncate(descriptor, 1) != -1 || errno != EBADF) result = 688;

cleanup:
    if (descriptor >= 0 && close(descriptor) != 0 && result == 0)
        result = 689;
    if (unlink(path) != 0 && result == 0) result = 690;
    if (result != 0) return result;
    puts("POSIX regular-file ftruncate grow, shrink, zero-fill, and access verified");
    return 0;
}

static int test_network_header_compatibility(void) {
    _Static_assert(sizeof(struct in6_pktinfo) == 20U,
                   "Linux IPv6 packet-info ABI mismatch");
#if UINTPTR_MAX == UINT64_MAX
    _Static_assert(sizeof(struct ifreq) == 40U,
                   "64-bit Linux ifreq ABI mismatch");
    _Static_assert(sizeof(struct ifconf) == 16U,
                   "64-bit Linux ifconf ABI mismatch");
#else
    _Static_assert(sizeof(struct ifreq) == 32U,
                   "32-bit Linux ifreq ABI mismatch");
    _Static_assert(sizeof(struct ifconf) == 8U,
                   "32-bit Linux ifconf ABI mismatch");
#endif
    _Static_assert(INADDR_BROADCAST == UINT32_MAX,
                   "IPv4 broadcast constant mismatch");
    _Static_assert(INADDR_NONE == UINT32_MAX,
                   "IPv4 invalid-address constant mismatch");
    _Static_assert(IP_MULTICAST_IF == 32 && IPV6_MULTICAST_IF == 17,
                   "multicast interface option mismatch");
    _Static_assert(IPV6_RECVPKTINFO == 49 && IPV6_PKTINFO == 50 &&
                   IPV6_RECVHOPLIMIT == 51 && IPV6_HOPLIMIT == 52,
                   "IPv6 ancillary-data option mismatch");
    _Static_assert(SO_OOBINLINE == 10 && MSG_EOR == 0x80,
                   "socket option/message flag mismatch");
    _Static_assert(SIOCGIFNAME == 0x8910 && SIOCGIFCONF == 0x8912 &&
                   SIOCGIFFLAGS == 0x8913 && SIOCGIFADDR == 0x8915 &&
                   SIOCGIFBRDADDR == 0x8919 && SIOCGIFNETMASK == 0x891b &&
                   SIOCGIFMTU == 0x8921 && SIOCGIFHWADDR == 0x8927 &&
                   SIOCGIFINDEX == 0x8933,
                   "Linux network interface ioctl mismatch");
    _Static_assert(SIOCGIFSTATS != SIOCGIFNAME,
                   "akaOS private network ioctl collides with Linux ABI");

    struct ifreq request = {0};
    struct ifconf configuration = {0};
    struct in6_pktinfo packet_info = {0};
    request.ifr_ifindex = 1;
    request.ifr_flags = IFF_UP;
    configuration.ifc_buf = (char *)&request;
    configuration.ifc_len = (int)sizeof(request);
    packet_info.ipi6_ifindex = 1U;
    if (request.ifr_ifindex != 1 || request.ifr_flags != IFF_UP ||
        configuration.ifc_len != (int)sizeof(request) ||
        packet_info.ipi6_ifindex != 1U) return 691;
    puts("POSIX IPv4/IPv6 header constants and network-interface layouts verified");
    return 0;
}

static int test_flock(void) {
    char path[] = "/tmp/akaos-flock-XXXXXX";
    int first = mkstemp(path);
    if (first < 0) return 570;
    int second = open(path, O_RDWR);
    if (second < 0) {
        close(first);
        unlink(path);
        return 571;
    }

    int status = 0;
    errno = 0;
    if (flock(first, LOCK_EX | LOCK_NB) != 0) {
        status = 572;
        goto cleanup;
    }
    errno = 0;
    if (flock(second, LOCK_EX | LOCK_NB) != -1 || errno != EAGAIN) {
        status = 573;
        goto cleanup;
    }
    if (flock(first, LOCK_UN) != 0 ||
        flock(first, LOCK_SH | LOCK_NB) != 0 ||
        flock(second, LOCK_SH | LOCK_NB) != 0) {
        status = 574;
        goto cleanup;
    }
    errno = 0;
    if (flock(first, LOCK_EX | LOCK_NB) != -1 || errno != EAGAIN) {
        status = 575;
        goto cleanup;
    }
    if (flock(second, LOCK_UN) != 0 ||
        flock(first, LOCK_EX | LOCK_NB) != 0) {
        status = 576;
        goto cleanup;
    }

    close(first);
    first = -1;
    if (flock(second, LOCK_EX | LOCK_NB) != 0 ||
        flock(second, LOCK_UN) != 0) status = 577;

cleanup:
    if (first >= 0) close(first);
    close(second);
    unlink(path);
    if (status != 0) {
        printf("POSIX flock probe failed at step %d (errno=%d)\n",
               status, errno);
        return status;
    }
    puts("POSIX flock shared/exclusive conflicts and close release verified");
    return 0;
}

static int test_fnmatch(void) {
    if (fnmatch("*.desktop", "settings.desktop", 0) != 0 ||
        fnmatch("*.desktop", "apps/settings.desktop", FNM_PATHNAME) !=
            FNM_NOMATCH ||
        fnmatch("[a-z]*.conf", "user.conf", FNM_PATHNAME) != 0 ||
        fnmatch("*", ".hidden", FNM_PERIOD) != FNM_NOMATCH ||
        fnmatch("\\*.txt", "*.txt", 0) != 0)
        return 528;
    puts("POSIX fnmatch pathname, class, period, and escape rules verified");
    return 0;
}

static int test_random_sequence(void) {
    srand(1234U);
    int first = rand();
    if (first < 0 || first > RAND_MAX) return 529;
    srand(1234U);
    if (rand() != first) return 530;
    unsigned int seed = 1234U;
    int reentrant = rand_r(&seed);
    if (reentrant < 0 || reentrant > RAND_MAX || seed == 1234U)
        return 531;
    puts("ISO C rand/srand sequence and rand_r state verified");
    return 0;
}

static int test_string_spans(void) {
    static const char fields[] = "abc,def";
    char joined[8] = "aka";
    if (strcat(joined, "OS") != joined || strcmp(joined, "akaOS") != 0)
        return 518;
    char bounded_join[8] = "aka";
    if (strncat(bounded_join, "OSkernel", 2U) != bounded_join ||
        strcmp(bounded_join, "akaOS") != 0)
        return 520;
    if (strncat(bounded_join, "ignored", 0U) != bounded_join ||
        strcmp(bounded_join, "akaOS") != 0)
        return 521;
    if (strspn("alphabet", "alpha") != 5U ||
        strcspn("akaOS:root", ":/") != 5U ||
        strnlen("bounded", 3U) != 3U || strnlen("abc", 8U) != 3U ||
        strcasecmp("UTF-8", "utf-8") != 0 ||
        strncasecmp("Alpha", "alpine", 2U) != 0 ||
        strpbrk(fields, ",") != fields + 3U)
        return 512;
    puts("ISO C strncat bounded append verified");
    puts("ISO C strspn/strcspn byte-set scans verified");

    char token_buffer[] = "left,,right,";
    char *remaining = token_buffer;
    char *token = strsep(&remaining, ",");
    if (!token || strcmp(token, "left") != 0) return 610;
    token = strsep(&remaining, ",");
    if (!token || token[0] != '\0') return 611;
    token = strsep(&remaining, ",");
    if (!token || strcmp(token, "right") != 0) return 612;
    token = strsep(&remaining, ",");
    if (!token || token[0] != '\0' || remaining != NULL ||
        strsep(&remaining, ",") != NULL)
        return 613;
    puts("BSD strsep delimiter fields, including empty tokens, verified");

    char reentrant_tokens[] = ";;alpha;;;beta;";
    char *save_pointer = NULL;
    token = strtok_r(reentrant_tokens, ";", &save_pointer);
    if (!token || strcmp(token, "alpha") != 0) return 616;
    token = strtok_r(NULL, ";", &save_pointer);
    if (!token || strcmp(token, "beta") != 0 ||
        strtok_r(NULL, ";", &save_pointer) != NULL)
        return 617;
    char legacy_tokens[] = ",one,,two,";
    token = strtok(legacy_tokens, ",");
    if (!token || strcmp(token, "one") != 0) return 618;
    token = strtok(NULL, ",");
    if (!token || strcmp(token, "two") != 0 || strtok(NULL, ",") != NULL)
        return 619;
    puts("POSIX strtok and strtok_r tokenization verified");
    return 0;
}

static int test_fd_table_growth(void) {
    enum { PIPE_COUNT = 300 };
    int pipes[PIPE_COUNT][2];
    unsigned opened = 0U;
    for (; opened < PIPE_COUNT; ++opened) {
        if (pipe(pipes[opened]) != 0) {
            for (unsigned index = 0U; index < opened; ++index) {
                (void)close(pipes[index][0]);
                (void)close(pipes[index][1]);
            }
            printf("POSIX fd table expansion failed at pipe %u: errno=%d\n",
                   opened, errno);
            return 614;
        }
    }

    int highest_descriptor = pipes[PIPE_COUNT - 1U][1];
    for (unsigned index = PIPE_COUNT; index > 0U; --index) {
        (void)close(pipes[index - 1U][0]);
        (void)close(pipes[index - 1U][1]);
    }
    if (highest_descriptor < 512) {
        printf("POSIX fd table did not expand far enough: highest=%d\n",
               highest_descriptor);
        return 615;
    }
    puts("POSIX native fd table growth past 512 verified");
    return 0;
}

static int test_timezone(void) {
    if (setenv("TZ", "UTC-7", 1) != 0) return 513;
    tzset();
    time_t epoch = 0;
    struct tm local;
    if (!localtime_r(&epoch, &local) || local.tm_hour != 7 ||
        local.tm_gmtoff != 25200L || strcmp(local.tm_zone, "UTC") != 0) {
        (void)setenv("TZ", "UTC0", 1);
        tzset();
        return 514;
    }
    struct tm roundtrip = {0};
    roundtrip.tm_year = 70;
    roundtrip.tm_mday = 1;
    roundtrip.tm_hour = 7;
    time_t converted = mktime(&roundtrip);
    (void)setenv("TZ", "UTC0", 1);
    tzset();
    if (converted != 0) return 515;
    if (difftime((time_t)60, (time_t)1) != 59.0 ||
        difftime((time_t)0, (time_t)60) != -60.0)
        return 522;
    puts("POSIX difftime arithmetic verified");
    static const char utc_text[] = "Thu Jan  1 00:00:00 1970\n";
    static const char local_text[] = "Thu Jan  1 07:00:00 1970\n";
    char time_buffer[26];
    const char *ctime_text = ctime(&epoch);
    if (!ctime_text || strcmp(ctime_text, utc_text) != 0) return 516;
    if (!ctime_r(&epoch, time_buffer) ||
        strcmp(time_buffer, utc_text) != 0)
        return 517;
    if (!asctime_r(&local, time_buffer) ||
        strcmp(time_buffer, local_text) != 0)
        return 518;
    const char *asctime_text = asctime(&local);
    if (!asctime_text || strcmp(asctime_text, local_text) != 0) return 519;
    puts("POSIX ctime/asctime and reentrant variants verified");
    puts("POSIX TZ offset, localtime, and mktime round-trip verified");
    return 0;
}

static int test_signal_sets(void) {
    sigset_t blocked, previous, current, full;
    if (sigemptyset(&blocked) != 0 ||
        sigaddset(&blocked, SIGUSR1) != 0 ||
        sigismember(&blocked, SIGUSR1) != 1 ||
        sigfillset(&full) != 0 || sigismember(&full, SIGKILL) != 1 ||
        sigismember(&full, SIGSTOP) != 1)
        return 516;
    if (sigprocmask(SIG_BLOCK, &blocked, &previous) != 0 ||
        sigprocmask(SIG_SETMASK, NULL, &current) != 0 ||
        sigismember(&current, SIGUSR1) != 1 ||
        sigismember(&current, SIGKILL) != 0 ||
        pthread_sigmask(SIG_UNBLOCK, &blocked, NULL) != 0 ||
        sigprocmask(SIG_SETMASK, &previous, NULL) != 0)
        return 517;
    puts("POSIX signal-set and mask operations verified");
    return 0;
}

static void siginfo_probe_handler(int signal_number, siginfo_t *information,
                                  void *context) {
    ++siginfo_probe_calls;
    siginfo_probe_signo = information ? information->si_signo : 0;
    siginfo_probe_code = information ? information->si_code : -1;
    siginfo_probe_address = information ? (uintptr_t)information->si_addr : 1U;
    siginfo_probe_context_present = context != NULL;
    (void)signal_number;
}

static int test_siginfo_delivery(void) {
    struct sigaction installed = {0};
    struct sigaction previous;
    installed.sa_sigaction = siginfo_probe_handler;
    installed.sa_flags = SA_SIGINFO;
    int mask_status = sigemptyset(&installed.sa_mask);
    int action_status = mask_status == 0
        ? sigaction(SIGUSR1, &installed, &previous) : -1;
    if (mask_status != 0 || action_status != 0) {
        printf("POSIX SA_SIGINFO setup failed: mask=%d action=%d errno=%d\n",
               mask_status, action_status, errno);
        return 578;
    }
    siginfo_probe_calls = 0;
    siginfo_probe_signo = 0;
    siginfo_probe_code = -1;
    siginfo_probe_address = 1U;
    siginfo_probe_context_present = 0;
    puts("POSIX SA_SIGINFO raise entering");
    int raised = raise(SIGUSR1);
    puts("POSIX SA_SIGINFO raise returned");
    int restored = sigaction(SIGUSR1, &previous, NULL);
    if (raised != 0 || restored != 0 || siginfo_probe_calls != 1 ||
        siginfo_probe_signo != SIGUSR1 || siginfo_probe_code != SI_USER ||
        siginfo_probe_address != 0U || !siginfo_probe_context_present) {
        printf("POSIX SA_SIGINFO probe failed: raised=%d restored=%d "
               "calls=%d signo=%d code=%d address=%p context=%d errno=%d\n",
               raised, restored, siginfo_probe_calls, siginfo_probe_signo,
               siginfo_probe_code, (void *)siginfo_probe_address,
               siginfo_probe_context_present, errno);
        return 579;
    }
    puts("POSIX SA_SIGINFO signal number, code, address and context verified");
    return 0;
}

static void sigchld_siginfo_reap_handler(int signal_number,
                                         siginfo_t *information,
                                         void *context) {
    int status = -1;
    pid_t child = waitpid(-1, &status, WNOHANG);
    sigchld_info_signo = information ? information->si_signo : 0;
    sigchld_info_reaped_pid = child;
    sigchld_info_reaped_status = status;
    sigchld_info_calls += signal_number == SIGCHLD && context != NULL;
}

static int test_sigchld_siginfo_reap(void) {
    struct sigaction installed = {0};
    struct sigaction previous;
    installed.sa_sigaction = sigchld_siginfo_reap_handler;
    installed.sa_flags = SA_SIGINFO;
    if (sigemptyset(&installed.sa_mask) != 0 ||
        sigaction(SIGCHLD, &installed, &previous) != 0) return 580;

    sigchld_info_calls = 0;
    sigchld_info_reaped_pid = -1;
    sigchld_info_reaped_status = -1;
    sigchld_info_signo = 0;
    pid_t child = fork();
    if (child == 0) {
        (void)usleep(50000);
        _exit(23);
    }
    if (child < 0) {
        (void)sigaction(SIGCHLD, &previous, NULL);
        return 581;
    }

    int poll_result = poll(NULL, 0, 2000);
    int poll_errno = errno;
    int restored = sigaction(SIGCHLD, &previous, NULL);
    if (poll_result != -1 || poll_errno != EINTR || restored != 0 ||
        sigchld_info_calls != 1 || sigchld_info_signo != SIGCHLD ||
        sigchld_info_reaped_pid != child ||
        !WIFEXITED(sigchld_info_reaped_status) ||
        WEXITSTATUS(sigchld_info_reaped_status) != 23) {
        printf("POSIX SA_SIGINFO SIGCHLD reap failed: child=%d poll=%d/%d "
               "calls=%d signo=%d reaped=%d status=%x restore=%d errno=%d\n",
               child, poll_result, poll_errno, sigchld_info_calls,
               sigchld_info_signo, sigchld_info_reaped_pid,
               sigchld_info_reaped_status, restored, errno);
        int cleanup_status = 0;
        (void)waitpid(child, &cleanup_status, 0);
        return 582;
    }
    puts("POSIX SA_SIGINFO SIGCHLD reaping from interruptible poll verified");
    return 0;
}

static int test_waitpid_signal_status(void) {
    pid_t child = fork();
    if (child == 0) {
        (void)raise(SIGTERM);
        _exit(99);
    }
    if (child < 0) return 583;

    int status = 0;
    pid_t waited = waitpid(child, &status, 0);
    if (waited != child || !WIFSIGNALED(status) ||
        WTERMSIG(status) != SIGTERM) {
        printf("POSIX waitpid signal status failed: child=%d waited=%d "
               "status=%x errno=%d\n", child, waited, status, errno);
        if (waited != child) (void)waitpid(child, &status, 0);
        return 584;
    }
    puts("POSIX waitpid nonzero exit and signal status encoding verified");
    return 0;
}

static int test_signalfd_compatibility(void) {
    sigset_t watched, previous;
    if (sigemptyset(&watched) != 0 ||
        sigaddset(&watched, SIGUSR1) != 0) return 530;

    int descriptor = signalfd(-1, &watched, SFD_CLOEXEC | SFD_NONBLOCK);
    if (descriptor < 0 ||
        fcntl(descriptor, F_GETFD) != FD_CLOEXEC ||
        (fcntl(descriptor, F_GETFL) & O_NONBLOCK) == 0) {
        if (descriptor >= 0) close(descriptor);
        return 531;
    }

    struct pollfd item = {.fd = descriptor, .events = POLLIN, .revents = 0};
    if (poll(&item, 1, 0) != 0) {
        close(descriptor);
        return 532;
    }

    if (sigprocmask(SIG_BLOCK, &watched, &previous) != 0) {
        close(descriptor);
        return 533;
    }
    if (raise(SIGUSR1) != 0) {
        (void)sigprocmask(SIG_SETMASK, &previous, NULL);
        close(descriptor);
        return 533;
    }
    item.revents = 0;
    if (poll(&item, 1, 0) != 1 || (item.revents & POLLIN) == 0) {
        (void)sigprocmask(SIG_SETMASK, &previous, NULL);
        close(descriptor);
        return 534;
    }

    struct signalfd_siginfo info;
    ssize_t received = read(descriptor, &info, sizeof(info));
    if (received != (ssize_t)sizeof(info) || info.ssi_signo != SIGUSR1 ||
        info.ssi_pid != (uint32_t)getpid()) {
        (void)sigprocmask(SIG_SETMASK, &previous, NULL);
        close(descriptor);
        return 535;
    }
    item.revents = 0;
    int still_ready = poll(&item, 1, 0);
    int restore_result = sigprocmask(SIG_SETMASK, &previous, NULL);
    int close_result = close(descriptor);
    if (still_ready != 0 || restore_result != 0 || close_result != 0)
        return 536;
    puts("POSIX signalfd mask, nonblocking read, poll readiness, and signal record verified");
    return 0;
}

static int test_passwd_database(void) {
    struct passwd *entry = getpwuid(getuid());
    if (!entry || !entry->pw_name || !entry->pw_dir || !entry->pw_shell)
        return 520;
    struct passwd reentrant_entry;
    struct passwd *reentrant_result = NULL;
    char buffer[512];
    if (getpwnam_r(entry->pw_name, &reentrant_entry, buffer, sizeof(buffer),
                   &reentrant_result) != 0 || !reentrant_result ||
        reentrant_result->pw_uid != getuid() ||
        strcmp(reentrant_result->pw_name, entry->pw_name) != 0)
        return 521;
    puts("POSIX passwd lookup and reentrant account records verified");
    return 0;
}

static int test_fcntl_execvp(void) {
    int descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (descriptor < 0 || fcntl(descriptor, F_GETFD) != FD_CLOEXEC) {
        if (descriptor >= 0) close(descriptor);
        return 506;
    }
    int duplicate = fcntl(descriptor, F_DUPFD, 6);
    if (duplicate < 6 || fcntl(duplicate, F_GETFD) != 0 ||
        (fcntl(duplicate, F_GETFL) & 3) != O_RDONLY) {
        if (duplicate >= 0) close(duplicate);
        close(descriptor);
        return 507;
    }
    close(duplicate);

    pid_t child = fork();
    if (child < 0) {
        close(descriptor);
        return 508;
    }
    if (child == 0) {
        char descriptor_text[16];
        (void)snprintf(descriptor_text, sizeof(descriptor_text), "%d",
                       descriptor);
        char *child_arguments[] = {
            (char *)"hello", (char *)"--cloexec-check", descriptor_text, NULL
        };
        (void)execvp("hello", child_arguments);
        _exit(127);
    }
    int status = -1;
    if (waitpid(child, &status, 0) != child || status != 0) {
        close(descriptor);
        return 509;
    }
    close(descriptor);
    puts("POSIX fcntl descriptor flags and execvp PATH search verified");
    return 0;
}

static void posix_spawn_signal_handler(int signal_number) {
    (void)signal_number;
}

static int test_posix_spawn_attributes(void) {
    sigset_t child_mask, child_default;
    if (sigemptyset(&child_mask) != 0 ||
        sigaddset(&child_mask, SIGUSR2) != 0 ||
        sigemptyset(&child_default) != 0 ||
        sigaddset(&child_default, SIGUSR1) != 0)
        return 522;

    struct sigaction installed = {0};
    struct sigaction previous;
    installed.sa_handler = posix_spawn_signal_handler;
    if (sigemptyset(&installed.sa_mask) != 0 ||
        sigaction(SIGUSR1, &installed, &previous) != 0)
        return 523;

    posix_spawnattr_t attributes;
    int result = posix_spawnattr_init(&attributes);
    if (result == 0) result = posix_spawnattr_setsigmask(&attributes,
                                                         &child_mask);
    if (result == 0) result = posix_spawnattr_setsigdefault(
        &attributes, &child_default);
    if (result == 0) result = posix_spawnattr_setflags(
        &attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    if (result != 0) {
        (void)sigaction(SIGUSR1, &previous, NULL);
        (void)posix_spawnattr_destroy(&attributes);
        return 524;
    }

    char child_argv0[] = "hello";
    char child_argv1[] = "--spawn-signal-check";
    char *child_arguments[] = {child_argv0, child_argv1, NULL};
    char child_env_path[] = "PATH=/bin:/usr/bin";
    char child_env_marker[] = "AKAOS_POSIX_SPAWN=1";
    char *child_environment[] = {child_env_path, child_env_marker, NULL};
    pid_t child = -1;
    result = posix_spawn(&child, "/bin/hello", NULL, &attributes,
                         child_arguments, child_environment);
    (void)posix_spawnattr_destroy(&attributes);
    int status = -1;
    pid_t waited = child > 0 ? waitpid(child, &status, 0) : -1;
    if (result != 0 || child <= 0 || waited != child || status != 0) {
        printf("POSIX spawn attribute run failed result=%d child=%d "
               "waited=%d status=%d errno=%d\n", result, child, waited,
               status, errno);
        (void)sigaction(SIGUSR1, &previous, NULL);
        return 525;
    }
    if (sigaction(SIGUSR1, &previous, NULL) != 0) return 526;
    puts("POSIX spawn signal attributes and child inheritance verified");
    return 0;
}

static void spawn_sigchld_handler(int signal_number) {
    if (signal_number == SIGCHLD) ++spawn_sigchld_count;
}

static int test_spawn_environment_inheritance(void) {
    if (setenv("AKAOS_TEST_SPAWN_ENV", "inherited", 1) != 0) return 528;
    struct sigaction action = {0};
    struct sigaction previous_action = {0};
    action.sa_handler = spawn_sigchld_handler;
    sigemptyset(&action.sa_mask);
    spawn_sigchld_count = 0;
    if (sigaction(SIGCHLD, &action, &previous_action) != 0) {
        (void)unsetenv("AKAOS_TEST_SPAWN_ENV");
        return 530;
    }

    char child_argv0[] = "hello";
    char child_argv1[] = "--spawn-environment-check";
    char *child_arguments[] = {child_argv0, child_argv1, NULL};
    pid_t child = spawn("/bin/hello", child_arguments);
    int status = -1;
    pid_t waited = child > 0 ? waitpid(child, &status, 0) : -1;
    for (unsigned attempt = 0; !spawn_sigchld_count && attempt < 10U;
         ++attempt) {
        (void)usleep(1000);
    }
    int signal_count = spawn_sigchld_count;
    int restore_result = sigaction(SIGCHLD, &previous_action, NULL);
    (void)unsetenv("AKAOS_TEST_SPAWN_ENV");
    if (child <= 0 || waited != child || status != 0 || signal_count == 0 ||
        restore_result != 0) {
        printf("spawn environment inheritance failed child=%d waited=%d "
               "status=%d sigchld=%d restore=%d errno=%d\n", child, waited,
               status, signal_count, restore_result, errno);
        return 529;
    }
    puts("akaOS spawn() environment inheritance verified");
    puts("POSIX SIGCHLD child-exit notification verified");
    return 0;
}

static int test_at_filesystem_calls(void) {
    char directory_name[24];
    int name_length = snprintf(directory_name, sizeof(directory_name),
                               "at-%ld", (long)getpid());
    if (name_length <= 0 || (size_t)name_length >= sizeof(directory_name))
        return 620;

    int root_fd = open("/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (root_fd < 0) return 621;
    int directory_fd = -1;
    int subdirectory_fd = -1;
    int file_fd = -1;
    char original_cwd[PATH_MAX];
    char changed_cwd[PATH_MAX];
    char expected_cwd[PATH_MAX];
    int cwd_saved = 0;
    int cwd_changed = 0;
    int directory_created = 0;
    int subdirectory_created = 0;
    int source_file_created = 0;
    int moved_file_created = 0;
    int result = 0;
    const char expected_contents[] = "akaos at filesystem calls";

    if (mkdirat(root_fd, directory_name, 0700) != 0) {
        result = 622;
        goto cleanup;
    }
    directory_created = 1;
    errno = 0;
    if (mkdirat(root_fd, directory_name, 0700) != -1 || errno != EEXIST) {
        result = 623;
        goto cleanup;
    }
    directory_fd = openat(root_fd, directory_name,
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd < 0) {
        result = 624;
        goto cleanup;
    }
    if (!getcwd(original_cwd, sizeof(original_cwd))) {
        result = 646;
        goto cleanup;
    }
    cwd_saved = 1;
    int expected_length = snprintf(expected_cwd, sizeof(expected_cwd),
                                   "/tmp/%s", directory_name);
    if (expected_length <= 0 ||
        (size_t)expected_length >= sizeof(expected_cwd)) {
        result = 647;
        goto cleanup;
    }
    if (fchdir(directory_fd) != 0) {
        result = 648;
        goto cleanup;
    }
    cwd_changed = 1;
    if (!getcwd(changed_cwd, sizeof(changed_cwd)) ||
        strcmp(changed_cwd, expected_cwd) != 0) {
        result = 649;
        goto cleanup;
    }
    if (chdir(original_cwd) != 0) {
        result = 650;
        goto cleanup;
    }
    cwd_changed = 0;
    if (mkdirat(directory_fd, "sub", 0700) != 0) {
        result = 625;
        goto cleanup;
    }
    subdirectory_created = 1;
    subdirectory_fd = openat(directory_fd, "sub",
                             O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (subdirectory_fd < 0) {
        result = 626;
        goto cleanup;
    }
    file_fd = openat(directory_fd, "old",
                     O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (file_fd < 0) {
        result = 627;
        goto cleanup;
    }
    source_file_created = 1;
    if (write(file_fd, expected_contents, sizeof(expected_contents)) !=
        (ssize_t)sizeof(expected_contents)) {
        result = 628;
        goto cleanup;
    }
    if (close(file_fd) != 0) {
        file_fd = -1;
        result = 628;
        goto cleanup;
    }
    file_fd = -1;
    if (renameat(directory_fd, "old", subdirectory_fd, "moved") != 0) {
        result = 629;
        goto cleanup;
    }
    source_file_created = 0;
    moved_file_created = 1;

    errno = 0;
    if (linkat(subdirectory_fd, "moved", subdirectory_fd, "linked", 0) !=
            -1 || errno != EPERM) {
        result = 630;
        goto cleanup;
    }
    file_fd = openat(subdirectory_fd, "moved", O_RDONLY | O_CLOEXEC);
    if (file_fd < 0) {
        result = 631;
        goto cleanup;
    }
    char contents[sizeof(expected_contents)];
    if (read(file_fd, contents, sizeof(contents)) !=
            (ssize_t)sizeof(contents) ||
        memcmp(contents, expected_contents, sizeof(contents)) != 0) {
        result = 632;
        goto cleanup;
    }
    if (close(file_fd) != 0) {
        file_fd = -1;
        result = 632;
        goto cleanup;
    }
    file_fd = -1;
    if (unlinkat(subdirectory_fd, "moved", 0) != 0) {
        result = 633;
        goto cleanup;
    }
    moved_file_created = 0;
    if (close(subdirectory_fd) != 0) {
        subdirectory_fd = -1;
        result = 634;
        goto cleanup;
    }
    subdirectory_fd = -1;
    if (unlinkat(directory_fd, "sub", AT_REMOVEDIR) != 0) {
        result = 635;
        goto cleanup;
    }
    subdirectory_created = 0;
    if (close(directory_fd) != 0) {
        directory_fd = -1;
        result = 636;
        goto cleanup;
    }
    directory_fd = -1;
    if (unlinkat(root_fd, directory_name, AT_REMOVEDIR) != 0) {
        result = 637;
        goto cleanup;
    }
    directory_created = 0;

cleanup:
    if (cwd_saved && cwd_changed && chdir(original_cwd) != 0 && result == 0)
        result = 651;
    if (file_fd >= 0 && close(file_fd) != 0 && result == 0) result = 638;
    if (moved_file_created && subdirectory_fd >= 0 &&
        unlinkat(subdirectory_fd, "moved", 0) != 0 && result == 0)
        result = 639;
    if (source_file_created && directory_fd >= 0 &&
        unlinkat(directory_fd, "old", 0) != 0 && result == 0)
        result = 640;
    if (subdirectory_fd >= 0 && close(subdirectory_fd) != 0 && result == 0)
        result = 641;
    if (subdirectory_created && directory_fd >= 0 &&
        unlinkat(directory_fd, "sub", AT_REMOVEDIR) != 0 && result == 0)
        result = 642;
    if (directory_fd >= 0 && close(directory_fd) != 0 && result == 0)
        result = 643;
    if (directory_created &&
        unlinkat(root_fd, directory_name, AT_REMOVEDIR) != 0 && result == 0)
        result = 644;
    if (close(root_fd) != 0 && result == 0) result = 645;
    if (result != 0) return result;
    puts("POSIX directory-relative mkdirat/openat/fchdir/renameat/unlinkat verified");
    puts("POSIX linkat reports unsupported hard-link capability");
    return 0;
}

static int test_posix_name_max(void) {
    if (NAME_MAX != 255 || pathconf("/", _PC_NAME_MAX) != NAME_MAX)
        return 681;

    char component[65];
    memset(component, 'n', sizeof(component) - 1U);
    component[sizeof(component) - 1U] = '\0';
    char path[PATH_MAX];
    int path_length = snprintf(path, sizeof(path), "/tmp/%s", component);
    if (path_length <= 0 || (size_t)path_length >= sizeof(path)) return 682;

    int descriptor = open(path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC,
                          0600);
    if (descriptor < 0) return 683;
    const char marker = 'n';
    ssize_t written = write(descriptor, &marker, 1);
    int close_status = close(descriptor);
    if (written != 1 || close_status != 0) {
        (void)unlink(path);
        return 684;
    }

    struct stat status;
    if (stat(path, &status) != 0 || status.st_size != 1) {
        (void)unlink(path);
        return 685;
    }
    DIR *directory = opendir("/tmp");
    if (!directory) {
        (void)unlink(path);
        return 686;
    }
    int found = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, component) == 0) {
            found = 1;
            break;
        }
    }
    int directory_close_status = closedir(directory);
    int unlink_status = unlink(path);
    if (!found || directory_close_status != 0 || unlink_status != 0)
        return 687;

    puts("POSIX 255-byte NAME_MAX and long-name VFS lookup verified");
    return 0;
}

static int test_posix_socketpair(void) {
    int pair[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) return 688;

    static const char left_to_right[] = "left-to-right";
    static const char right_to_left[] = "right-to-left";
    char buffer[sizeof(left_to_right)];
    if (write(pair[0], left_to_right, sizeof(left_to_right)) !=
            (ssize_t)sizeof(left_to_right) ||
        read(pair[1], buffer, sizeof(left_to_right)) !=
            (ssize_t)sizeof(left_to_right) ||
        memcmp(buffer, left_to_right, sizeof(left_to_right)) != 0 ||
        write(pair[1], right_to_left, sizeof(right_to_left)) !=
            (ssize_t)sizeof(right_to_left) ||
        read(pair[0], buffer, sizeof(right_to_left)) !=
            (ssize_t)sizeof(right_to_left) ||
        memcmp(buffer, right_to_left, sizeof(right_to_left)) != 0) {
        (void)close(pair[0]);
        (void)close(pair[1]);
        return 689;
    }
    if (close(pair[0]) != 0 || close(pair[1]) != 0) return 690;

    int flagged[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
                   0, flagged) != 0) return 691;
    int flags_valid = fcntl(flagged[0], F_GETFD) == FD_CLOEXEC &&
        fcntl(flagged[1], F_GETFD) == FD_CLOEXEC &&
        (fcntl(flagged[0], F_GETFL) & O_NONBLOCK) != 0 &&
        (fcntl(flagged[1], F_GETFL) & O_NONBLOCK) != 0;
    char byte = 0;
    errno = 0;
    int nonblocking_valid = read(flagged[0], &byte, 1) == -1 &&
        (errno == EAGAIN || errno == EWOULDBLOCK);
    int close_left = close(flagged[0]);
    int close_right = close(flagged[1]);
    if (!flags_valid || !nonblocking_valid || close_left != 0 ||
        close_right != 0) return 692;

    int concurrent_pairs[64][2];
    size_t pair_count = 0U;
    for (; pair_count < sizeof(concurrent_pairs) /
                        sizeof(concurrent_pairs[0]); ++pair_count) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0,
                       concurrent_pairs[pair_count]) != 0) {
            while (pair_count > 0U) {
                --pair_count;
                (void)close(concurrent_pairs[pair_count][0]);
                (void)close(concurrent_pairs[pair_count][1]);
            }
            return 693;
        }
    }
    int concurrent_close_error = 0;
    while (pair_count > 0U) {
        --pair_count;
        if (close(concurrent_pairs[pair_count][0]) != 0)
            concurrent_close_error = 1;
        if (close(concurrent_pairs[pair_count][1]) != 0)
            concurrent_close_error = 1;
    }
    if (concurrent_close_error) return 694;

    puts("POSIX AF_UNIX socketpair bidirectional data and flags verified");
    puts("POSIX AF_UNIX socketpair 64-pair socket-pool capacity verified");
    return 0;
}

static int test_posix_wordexp(void) {
    const char *home = getenv("HOME");
    if (!home || !*home) {
        struct passwd *account = getpwuid(getuid());
        if (!account || !account->pw_dir || !*account->pw_dir ||
            setenv("HOME", account->pw_dir, 1) != 0) {
            puts("wordexp self-test: no home directory is available");
            return 693;
        }
        home = getenv("HOME");
    }

    wordexp_t expanded = {0};
    int expansion_status = wordexp(
        "akaOS \"$HOME\" 'two words' escaped\\ word", &expanded,
        WRDE_NOCMD);
    if (expansion_status != 0) {
        printf("wordexp self-test: initial expansion returned %d\n",
               expansion_status);
        return 694;
    }
    if (expanded.we_wordc != 4U ||
        strcmp(expanded.we_wordv[0], "akaOS") != 0 ||
        strcmp(expanded.we_wordv[1], home) != 0 ||
        strcmp(expanded.we_wordv[2], "two words") != 0 ||
        strcmp(expanded.we_wordv[3], "escaped word") != 0) {
        printf("wordexp self-test: expected four words; got %zu\n",
               expanded.we_wordc);
        wordfree(&expanded);
        return 695;
    }
    wordfree(&expanded);

    if (wordexp("~", &expanded, WRDE_NOCMD) != 0 ||
        expanded.we_wordc != 1U || strcmp(expanded.we_wordv[0], home) != 0) {
        wordfree(&expanded);
        return 696;
    }
    wordfree(&expanded);

    if (wordexp("$(touch /tmp/akaos-wordexp-command)", &expanded,
                WRDE_NOCMD) != WRDE_CMDSUB) {
        wordfree(&expanded);
        (void)unlink("/tmp/akaos-wordexp-command");
        return 697;
    }
    if (access("/tmp/akaos-wordexp-command", F_OK) == 0) {
        (void)unlink("/tmp/akaos-wordexp-command");
        return 698;
    }

    const char *saved_value = getenv("AKAOS_WORDEXP_UNSET_TEST");
    char *saved_copy = saved_value ? strdup(saved_value) : NULL;
    if (saved_value && !saved_copy) return 705;
    if (unsetenv("AKAOS_WORDEXP_UNSET_TEST") != 0) {
        free(saved_copy);
        return 706;
    }
    int undefined_status = wordexp("${AKAOS_WORDEXP_UNSET_TEST}", &expanded,
                                   WRDE_UNDEF);
    if (saved_copy) {
        (void)setenv("AKAOS_WORDEXP_UNSET_TEST", saved_copy, 1);
        free(saved_copy);
    }
    if (undefined_status != WRDE_BADVAL) {
        wordfree(&expanded);
        return 707;
    }

    static const char *glob_paths[] = {
        "/tmp/akaos-wordexp-a", "/tmp/akaos-wordexp-b"
    };
    (void)unlink(glob_paths[0]);
    (void)unlink(glob_paths[1]);
    for (size_t index = 0; index < 2U; ++index) {
        int descriptor = open(glob_paths[index], O_WRONLY | O_CREAT | O_TRUNC,
                              0600);
        if (descriptor < 0) {
            (void)unlink(glob_paths[0]);
            (void)unlink(glob_paths[1]);
            return 699;
        }
        if (close(descriptor) != 0) {
            (void)unlink(glob_paths[0]);
            (void)unlink(glob_paths[1]);
            return 700;
        }
    }
    if (wordexp("'/tmp/akaos-wordexp-*'", &expanded, WRDE_NOCMD) != 0 ||
        expanded.we_wordc != 1U ||
        strcmp(expanded.we_wordv[0], "/tmp/akaos-wordexp-*") != 0) {
        wordfree(&expanded);
        (void)unlink(glob_paths[0]);
        (void)unlink(glob_paths[1]);
        return 708;
    }
    wordfree(&expanded);
    if (wordexp("/tmp/akaos-wordexp-*", &expanded, WRDE_NOCMD) != 0 ||
        expanded.we_wordc != 2U ||
        strcmp(expanded.we_wordv[0], glob_paths[0]) != 0 ||
        strcmp(expanded.we_wordv[1], glob_paths[1]) != 0) {
        wordfree(&expanded);
        (void)unlink(glob_paths[0]);
        (void)unlink(glob_paths[1]);
        return 701;
    }
    wordfree(&expanded);
    (void)unlink(glob_paths[0]);
    (void)unlink(glob_paths[1]);

    wordexp_t offset_words = {0};
    offset_words.we_offs = 2U;
    if (wordexp("first", &offset_words, WRDE_DOOFFS) != 0 ||
        offset_words.we_wordc != 1U || !offset_words.we_wordv ||
        offset_words.we_wordv[0] != NULL || offset_words.we_wordv[1] != NULL ||
        strcmp(offset_words.we_wordv[2], "first") != 0 ||
        offset_words.we_wordv[3] != NULL) {
        wordfree(&offset_words);
        return 702;
    }
    if (wordexp("second", &offset_words,
                WRDE_DOOFFS | WRDE_APPEND) != 0 ||
        offset_words.we_wordc != 2U ||
        strcmp(offset_words.we_wordv[2], "first") != 0 ||
        strcmp(offset_words.we_wordv[3], "second") != 0 ||
        offset_words.we_wordv[4] != NULL) {
        wordfree(&offset_words);
        return 703;
    }
    if (wordexp("reused", &offset_words,
                WRDE_DOOFFS | WRDE_REUSE) != 0 ||
        offset_words.we_wordc != 1U ||
        strcmp(offset_words.we_wordv[2], "reused") != 0 ||
        offset_words.we_wordv[3] != NULL) {
        wordfree(&offset_words);
        return 704;
    }
    wordfree(&offset_words);

    puts("POSIX wordexp quoting, environment, tilde, glob, and offset semantics verified");
    return 0;
}

static int test_pipe2_flags(void) {
    int descriptors[2] = {-1, -1};
    if (pipe2(descriptors, O_CLOEXEC | O_NONBLOCK) != 0) return 700;

    int result = 0;
    int read_descriptor_flags = fcntl(descriptors[0], F_GETFD);
    int write_descriptor_flags = fcntl(descriptors[1], F_GETFD);
    int read_status_flags = fcntl(descriptors[0], F_GETFL);
    int write_status_flags = fcntl(descriptors[1], F_GETFL);
    if (read_descriptor_flags < 0 || write_descriptor_flags < 0 ||
        read_status_flags < 0 || write_status_flags < 0 ||
        (read_descriptor_flags & FD_CLOEXEC) == 0 ||
        (write_descriptor_flags & FD_CLOEXEC) == 0 ||
        (read_status_flags & O_NONBLOCK) == 0 ||
        (write_status_flags & O_NONBLOCK) == 0) {
        result = 701;
        goto cleanup;
    }

    char byte = 0;
    errno = 0;
    if (read(descriptors[0], &byte, 1U) != -1 ||
        (errno != EAGAIN && errno != EWOULDBLOCK)) {
        result = 702;
        goto cleanup;
    }
    byte = 'P';
    if (write(descriptors[1], &byte, 1U) != 1 ||
        read(descriptors[0], &byte, 1U) != 1 || byte != 'P') {
        result = 703;
        goto cleanup;
    }

cleanup:
    if (descriptors[0] >= 0 && close(descriptors[0]) != 0 && result == 0)
        result = 704;
    if (descriptors[1] >= 0 && close(descriptors[1]) != 0 && result == 0)
        result = 705;
    if (result != 0) return result;

    int invalid_descriptors[2] = {-1, -1};
    errno = 0;
    if (pipe2(invalid_descriptors, O_CLOEXEC | 0x40000000) != -1 ||
        errno != EINVAL || invalid_descriptors[0] != -1 ||
        invalid_descriptors[1] != -1)
        return 706;

    puts("POSIX pipe2 atomic CLOEXEC, nonblocking I/O, and flag validation verified");
    return 0;
}

static int test_pipe_fionread(void) {
    int pipe_descriptors[2] = {-1, -1};
    if (pipe(pipe_descriptors) != 0) return 652;
    const char expected[] = "qprocess pipe";
    int result = 0;
    if (write(pipe_descriptors[1], expected, sizeof(expected)) !=
        (ssize_t)sizeof(expected)) {
        result = 653;
        goto cleanup;
    }
    int available = -1;
    if (ioctl(pipe_descriptors[0], FIONREAD, &available) != 0 ||
        available != (int)sizeof(expected)) {
        result = 654;
        goto cleanup;
    }
    char contents[sizeof(expected)];
    if (read(pipe_descriptors[0], contents, sizeof(contents)) !=
            (ssize_t)sizeof(contents) ||
        memcmp(contents, expected, sizeof(contents)) != 0) {
        result = 655;
        goto cleanup;
    }

cleanup:
    if (close(pipe_descriptors[0]) != 0 && result == 0) result = 656;
    if (close(pipe_descriptors[1]) != 0 && result == 0) result = 657;
    if (result != 0) return result;
    puts("POSIX pipe FIONREAD byte availability verified");
    return 0;
}

static int test_resource_limits(void) {
    struct rlimit descriptor_limit;
    struct rlimit core_limit;
    if (sysconf(_SC_OPEN_MAX) != 1024) return 662;
    if (getrlimit(RLIMIT_NOFILE, &descriptor_limit) != 0 ||
        descriptor_limit.rlim_cur < 3U ||
        descriptor_limit.rlim_cur != descriptor_limit.rlim_max)
        return 658;
    if (getrlimit(RLIMIT_CORE, &core_limit) != 0 ||
        core_limit.rlim_cur != 0U || core_limit.rlim_max != 0U)
        return 659;
    if (setrlimit(RLIMIT_CORE, &core_limit) != 0 ||
        setrlimit(RLIMIT_NOFILE, &descriptor_limit) != 0)
        return 660;
    if (sysconf(_SC_GETGR_R_SIZE_MAX) < 512 ||
        sysconf(_SC_GETPW_R_SIZE_MAX) < 512)
        return 661;
    puts("POSIX sysconf reentrant account buffer limits verified");
    puts("POSIX resource-limit query and core-disable semantics verified");
    return 0;
}

static int test_posix_process_compatibility(void) {
    int qsort_status = test_qsort();
    if (qsort_status != 0) return qsort_status;
    int endian_status = test_endian_compatibility();
    if (endian_status != 0) return endian_status;
    int at_filesystem_status = test_at_filesystem_calls();
    if (at_filesystem_status != 0) return at_filesystem_status;
    int name_max_status = test_posix_name_max();
    if (name_max_status != 0) return name_max_status;
    int socketpair_status = test_posix_socketpair();
    if (socketpair_status != 0) return socketpair_status;
    int wordexp_status = test_posix_wordexp();
    if (wordexp_status != 0) return wordexp_status;
    int pipe2_status = test_pipe2_flags();
    if (pipe2_status != 0) return pipe2_status;
    int pipe_fionread_status = test_pipe_fionread();
    if (pipe_fionread_status != 0) return pipe_fionread_status;
    int resource_limit_status = test_resource_limits();
    if (resource_limit_status != 0) return resource_limit_status;
    int virtual_fd_range_status = test_eventfd_timerfd_descriptor_ranges();
    if (virtual_fd_range_status != 0) return virtual_fd_range_status;
    int strdup_status = test_strdup();
    if (strdup_status != 0) return strdup_status;
    int atol_status = test_atol();
    if (atol_status != 0) return atol_status;
    int math_status = test_math_runtime();
    if (math_status != 0) return math_status;
    int graphics_math_status = test_graphics_math();
    if (graphics_math_status != 0) return graphics_math_status;
    int setjmp_status = test_setjmp_abi();
    if (setjmp_status != 0) return setjmp_status;
    int tmpfile_status = test_tmpfile_stream();
    if (tmpfile_status != 0) return tmpfile_status;
    int line_io_status = test_getline_mkstemp();
    if (line_io_status != 0) return line_io_status;
    int memstream_status = test_open_memstream();
    if (memstream_status != 0) return memstream_status;
    int memfd_status = test_memfd_msync();
    if (memfd_status != 0) return memfd_status;
    int regular_ftruncate_status = test_vfs_ftruncate();
    if (regular_ftruncate_status != 0) {
        printf("regular-file ftruncate probe failed at step %d (errno=%d)\n",
               regular_ftruncate_status, errno);
        return regular_ftruncate_status;
    }
    int network_header_status = test_network_header_compatibility();
    if (network_header_status != 0) return network_header_status;
    int flock_status = test_flock();
    if (flock_status != 0) return flock_status;
    int fnmatch_status = test_fnmatch();
    if (fnmatch_status != 0) return fnmatch_status;
    int random_status = test_random_sequence();
    if (random_status != 0) return random_status;
    int span_status = test_string_spans();
    if (span_status != 0) return span_status;
    int fd_growth_status = test_fd_table_growth();
    if (fd_growth_status != 0) return fd_growth_status;
    int timezone_status = test_timezone();
    if (timezone_status != 0) return timezone_status;
    int signal_set_status = test_signal_sets();
    if (signal_set_status != 0) return signal_set_status;
    int siginfo_status = test_siginfo_delivery();
    if (siginfo_status != 0) return siginfo_status;
    int sigchld_siginfo_status = test_sigchld_siginfo_reap();
    if (sigchld_siginfo_status != 0) return sigchld_siginfo_status;
    int waitpid_signal_status = test_waitpid_signal_status();
    if (waitpid_signal_status != 0) return waitpid_signal_status;
    int signalfd_status = test_signalfd_compatibility();
    if (signalfd_status != 0) return signalfd_status;
    int passwd_status = test_passwd_database();
    if (passwd_status != 0) return passwd_status;
    int descriptor_status = test_fcntl_execvp();
    if (descriptor_status != 0) return descriptor_status;
    int spawn_attribute_status = test_posix_spawn_attributes();
    if (spawn_attribute_status != 0) return spawn_attribute_status;
    int spawn_environment_status = test_spawn_environment_inheritance();
    if (spawn_environment_status != 0) return spawn_environment_status;
    pid_t parent_pid = getpid();
    if (parent_pid <= 0 || getppid() < 0) return 508;
    pid_t ancestry_child = fork();
    if (ancestry_child < 0) return 507;
    if (ancestry_child == 0)
        _exit(getppid() == parent_pid ? 0 : 1);
    int ancestry_status = -1;
    if (waitpid(ancestry_child, &ancestry_status, 0) != ancestry_child ||
        ancestry_status != 0) {
        printf("POSIX getppid fork ancestry failed: parent=%d status=%d\n",
               parent_pid, ancestry_status);
        return 506;
    }
    puts("POSIX getppid fork ancestry verified");

    struct orphan_ppid_record {
        pid_t child_pid;
        pid_t observed_parent_pid;
    } orphan_record = {-1, -1};
    int orphan_pipe[2];
    if (pipe(orphan_pipe) != 0) return 522;
    pid_t orphan_parent = fork();
    if (orphan_parent < 0) {
        close(orphan_pipe[0]);
        close(orphan_pipe[1]);
        return 523;
    }
    if (orphan_parent == 0) {
        close(orphan_pipe[0]);
        pid_t orphan_child = fork();
        if (orphan_child < 0) {
            close(orphan_pipe[1]);
            _exit(1);
        }
        if (orphan_child == 0) {
            pid_t original_parent = getppid();
            pid_t observed_parent = original_parent;
            /* Wait for the exiting parent to publish adoption.  This avoids
             * making correctness depend on QEMU's per-architecture speed. */
            for (unsigned attempt = 0;
                 attempt < 200U && observed_parent == original_parent;
                 ++attempt) {
                (void)usleep(10000);
                observed_parent = getppid();
            }
            orphan_record.child_pid = getpid();
            orphan_record.observed_parent_pid = observed_parent;
            ssize_t written = write(orphan_pipe[1], &orphan_record,
                                    sizeof(orphan_record));
            close(orphan_pipe[1]);
            _exit(written == (ssize_t)sizeof(orphan_record) &&
                          observed_parent > 0 &&
                          observed_parent != original_parent
                      ? 0
                      : 1);
        }
        close(orphan_pipe[1]);
        _exit(0);
    }
    close(orphan_pipe[1]);
    int orphan_parent_status = -1;
    if (waitpid(orphan_parent, &orphan_parent_status, 0) != orphan_parent ||
        orphan_parent_status != 0) {
        close(orphan_pipe[0]);
        return 524;
    }
    size_t orphan_bytes = 0;
    while (orphan_bytes < sizeof(orphan_record)) {
        ssize_t received = read(orphan_pipe[0],
                                (char *)&orphan_record + orphan_bytes,
                                sizeof(orphan_record) - orphan_bytes);
        if (received <= 0) break;
        orphan_bytes += (size_t)received;
    }
    close(orphan_pipe[0]);
    if (orphan_bytes != sizeof(orphan_record) ||
        orphan_record.child_pid <= 0 ||
        orphan_record.observed_parent_pid <= 0 ||
        orphan_record.observed_parent_pid == orphan_parent) {
        printf("POSIX orphan reparenting failed: former=%d observed=%d\n",
               orphan_parent, orphan_record.observed_parent_pid);
        return 525;
    }
    errno = 0;
    if (waitpid(orphan_record.child_pid, NULL, WNOHANG) != -1 ||
        errno != ECHILD) {
        return 526;
    }
    puts("POSIX orphan reparenting and wait ownership verified");

    if (setenv("AKAOS_TEST_ENV", "one", 1) != 0 ||
        !getenv("AKAOS_TEST_ENV") ||
        strcmp(getenv("AKAOS_TEST_ENV"), "one") != 0 ||
        setenv("AKAOS_TEST_ENV", "two", 0) != 0 ||
        strcmp(getenv("AKAOS_TEST_ENV"), "one") != 0 ||
        setenv("AKAOS_TEST_ENV", "two", 1) != 0 ||
        strcmp(getenv("AKAOS_TEST_ENV"), "two") != 0 ||
        putenv("AKAOS_PUT_ENV=ok") != 0 ||
        !getenv("AKAOS_PUT_ENV") ||
        unsetenv("AKAOS_TEST_ENV") != 0 ||
        unsetenv("AKAOS_PUT_ENV") != 0 ||
        getenv("AKAOS_TEST_ENV") || getenv("AKAOS_PUT_ENV")) {
        return 509;
    }

    /* Exercise libc after an application replaces environ with a shorter,
     * non-owned vector. Keep PATH so the spawn checks below remain valid. */
    char rebased_path[] = "PATH=/bin:/usr/bin";
    char rebased_seed[] = "AKAOS_REBASE=seed";
    char *repointed_environment[] = {
        rebased_path, rebased_seed, NULL,
    };
    environ = repointed_environment;
    if (setenv("AKAOS_REBASE", "first", 1) != 0 ||
        !getenv("AKAOS_REBASE") ||
        strcmp(getenv("AKAOS_REBASE"), "first") != 0 ||
        setenv("AKAOS_REBASE_EXTRA", "added", 1) != 0 ||
        !getenv("AKAOS_REBASE_EXTRA") ||
        strcmp(getenv("AKAOS_REBASE_EXTRA"), "added") != 0 ||
        setenv("AKAOS_REBASE", "replacement", 1) != 0 ||
        strcmp(getenv("AKAOS_REBASE"), "replacement") != 0 ||
        unsetenv("AKAOS_REBASE_EXTRA") != 0 ||
        getenv("AKAOS_REBASE_EXTRA") != NULL) {
        return 527;
    }
    puts("POSIX environment vector rebase verified");

    unsigned environment_count = 0;
    if (environ) {
        while (environ[environment_count] && environment_count < 32U)
            ++environment_count;
    }
    char spawn_name[] = "echo";
    char spawn_message[] = "posix-spawn-child";
    char *spawn_arguments[] = {spawn_name, spawn_message, NULL};
    pid_t child = -1;
    int spawn_result = posix_spawnp(&child, spawn_name, NULL, NULL,
                                    spawn_arguments, NULL);
    if (spawn_result != 0 || child < 0) {
        printf("POSIX spawn failed: code=%d pid=%d\n", spawn_result, child);
        return 510;
    }
    int status = -1;
    if (waitpid(child, &status, 0) != child || status != 0) {
        printf("POSIX spawn child failed: status=%d env=%u\n", status,
               environment_count);
        return 511;
    }
    char child_environment_entry[] = "AKAOS_CHILD=1";
    char *child_environment[] = {child_environment_entry, NULL};
    child = -1;
    spawn_result = posix_spawnp(&child, spawn_name, NULL, NULL,
                                spawn_arguments, child_environment);
    if (spawn_result != 0 || child < 0) {
        printf("POSIX spawn env failed: code=%d pid=%d\n", spawn_result,
               child);
        return 512;
    }
    status = -1;
    if (waitpid(child, &status, 0) != child || status != 0) {
        printf("POSIX spawn env child failed: status=%d\n", status);
        return 513;
    }
    int output_pipe[2];
    if (pipe(output_pipe) != 0) return 514;
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0 ||
        posix_spawn_file_actions_adddup2(&actions, output_pipe[1],
                                         STDOUT_FILENO) != 0 ||
        posix_spawn_file_actions_addclose(&actions, output_pipe[0]) != 0 ||
        posix_spawn_file_actions_addclose(&actions, output_pipe[1]) != 0) {
        (void)close(output_pipe[0]);
        (void)close(output_pipe[1]);
        return 515;
    }
    child = -1;
    spawn_result = posix_spawnp(&child, spawn_name, &actions, NULL,
                                spawn_arguments, child_environment);
    (void)posix_spawn_file_actions_destroy(&actions);
    (void)close(output_pipe[1]);
    if (spawn_result != 0 || child < 0) {
        (void)close(output_pipe[0]);
        printf("POSIX spawn actions failed: code=%d pid=%d\n", spawn_result,
               child);
        return 516;
    }
    status = -1;
    if (waitpid(child, &status, 0) != child || status != 0) {
        (void)close(output_pipe[0]);
        printf("POSIX spawn actions child failed: status=%d\n", status);
        return 517;
    }
    char spawn_output[64];
    size_t spawn_output_length = 0;
    while (spawn_output_length < sizeof(spawn_output) - 1U) {
        ssize_t bytes_read = read(output_pipe[0],
                                  spawn_output + spawn_output_length,
                                  sizeof(spawn_output) - 1U -
                                      spawn_output_length);
        if (bytes_read < 0 && errno == EINTR) continue;
        if (bytes_read < 0) {
            (void)close(output_pipe[0]);
            return 518;
        }
        if (bytes_read == 0) break;
        spawn_output_length += (size_t)bytes_read;
    }
    (void)close(output_pipe[0]);
    spawn_output[spawn_output_length] = '\0';
    if (strcmp(spawn_output, "posix-spawn-child\n") != 0) {
        printf("POSIX spawn actions output mismatch: %s\n", spawn_output);
        return 519;
    }
    puts("POSIX spawn file actions verified");
    puts("POSIX environment and spawn verified");
    return 0;
}

static void display_put_le16(unsigned char *bytes, uint16_t value) {
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
}

static void display_put_le32(unsigned char *bytes, uint32_t value) {
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
    bytes[2] = (unsigned char)(value >> 16);
    bytes[3] = (unsigned char)(value >> 24);
}

static size_t display_put_wayland_string(unsigned char *bytes, size_t offset,
                                         const char *value) {
    size_t length = strlen(value) + 1U;
    display_put_le32(bytes + offset, (uint32_t)length);
    offset += 4U;
    memcpy(bytes + offset, value, length - 1U);
    bytes[offset + length - 1U] = 0;
    offset += length;
    while ((offset & 3U) != 0) bytes[offset++] = 0;
    return offset;
}

static uint16_t display_get_le16(const unsigned char *bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t display_get_le32(const unsigned char *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int display_send_all(int descriptor, const void *buffer, size_t length) {
    const unsigned char *bytes = (const unsigned char *)buffer;
    size_t sent = 0;
    while (sent < length) {
        ssize_t result = send(descriptor, bytes + sent, length - sent, 0);
        if (result < 0 && (errno == EAGAIN || errno == EINTR)) {
            (void)sched_yield();
            continue;
        }
        if (result <= 0) return 0;
        sent += (size_t)result;
    }
    return 1;
}

static int display_recv_all(int descriptor, void *buffer, size_t length) {
    unsigned char *bytes = (unsigned char *)buffer;
    size_t received = 0;
    while (received < length) {
        ssize_t result = recv(descriptor, bytes + received, length - received, 0);
        if (result < 0 && (errno == EAGAIN || errno == EINTR)) {
            (void)sched_yield();
            continue;
        }
        if (result <= 0) return 0;
        received += (size_t)result;
    }
    return 1;
}

static int display_connect_unix(const char *path) {
    if (!path) return -1;
    for (int retry = 0; retry < 80; ++retry) {
        int descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
        if (descriptor >= 0) {
            struct sockaddr_un address;
            memset(&address, 0, sizeof(address));
            address.sun_family = AF_UNIX;
            volatile char *destination = address.sun_path;
            size_t index = 0;
            while (index + 1U < sizeof(address.sun_path) && path[index] != '\0') {
                destination[index] = path[index];
                ++index;
            }
            destination[index] = '\0';
            if (connect(descriptor, (const struct sockaddr *)&address,
                        sizeof(address)) == 0) return descriptor;
            close(descriptor);
        }
        (void)poll(NULL, 0, 25);
    }
    return -1;
}

static int test_x11_protocol(pid_t server_pid) {
    (void)server_pid;
    int descriptor = display_connect_unix("/run/X11-unix/X0");
    if (descriptor < 0) return 503;
    unsigned char setup[12];
    memset(setup, 0, sizeof(setup));
    setup[0] = 'l';
    display_put_le16(setup + 2, 11);
    if (!display_send_all(descriptor, setup, sizeof(setup))) {
        close(descriptor);
        return 504;
    }
    unsigned char setup_header[8];
    if (!display_recv_all(descriptor, setup_header, sizeof(setup_header)) ||
        setup_header[0] != 1 || display_get_le16(setup_header + 2) != 11) {
        close(descriptor);
        return 505;
    }
    size_t setup_bytes = (size_t)display_get_le16(setup_header + 6) * 4U;
    if (setup_bytes == 0 || setup_bytes > 2048U) {
        close(descriptor);
        return 506;
    }
    unsigned char setup_tail[2048];
    if (!display_recv_all(descriptor, setup_tail, setup_bytes)) {
        close(descriptor);
        return 507;
    }
    unsigned char request[4] = {43, 0, 1, 0};
    if (!display_send_all(descriptor, request, sizeof(request))) {
        close(descriptor);
        return 508;
    }
    unsigned char reply[32];
    if (!display_recv_all(descriptor, reply, sizeof(reply)) || reply[0] != 1) {
        close(descriptor);
        return 509;
    }
    /* Exercise the small but stateful X11 window lifecycle used by toolkit
     * clients: create, map, configure, then query the resulting geometry. */
    unsigned char create[32];
    memset(create, 0, sizeof(create));
    create[0] = 1;                         /* CreateWindow */
    create[1] = 24;                        /* depth */
    display_put_le16(create + 2, 8);        /* 32-byte request */
    display_put_le32(create + 8, 0x01000010U); /* window id */
    display_put_le32(create + 12, 1U);      /* root parent */
    display_put_le16(create + 16, 32);      /* x */
    display_put_le16(create + 18, 48);      /* y */
    display_put_le16(create + 20, 640);     /* width */
    display_put_le16(create + 22, 480);     /* height */
    display_put_le16(create + 24, 0);       /* border width */
    display_put_le16(create + 26, 1);       /* class: input/output */
    display_put_le32(create + 28, 0x20);    /* visual */
    if (!display_send_all(descriptor, create, sizeof(create))) {
        close(descriptor);
        return 510;
    }
    unsigned char map[8];
    memset(map, 0, sizeof(map));
    map[0] = 8;                             /* MapWindow */
    display_put_le16(map + 2, 2);
    display_put_le32(map + 4, 0x01000010U);
    if (!display_send_all(descriptor, map, sizeof(map))) {
        close(descriptor);
        return 511;
    }
    unsigned char configure[16];
    memset(configure, 0, sizeof(configure));
    configure[0] = 12;                      /* ConfigureWindow */
    display_put_le16(configure + 2, 4);
    display_put_le32(configure + 4, 0x01000010U);
    display_put_le16(configure + 8, 0x000c); /* width + height */
    display_put_le32(configure + 12, 800);
    /* The request has room for both values; the second value follows the
     * first in the wire body.  Extend the packet to the required 20 bytes. */
    unsigned char configure_full[20];
    memcpy(configure_full, configure, sizeof(configure));
    display_put_le32(configure_full + 16, 600);
    display_put_le16(configure_full + 2, 5);
    if (!display_send_all(descriptor, configure_full, sizeof(configure_full))) {
        close(descriptor);
        return 512;
    }
    unsigned char geometry_request[8];
    memset(geometry_request, 0, sizeof(geometry_request));
    geometry_request[0] = 14;               /* GetGeometry */
    display_put_le16(geometry_request + 2, 2);
    display_put_le32(geometry_request + 4, 0x01000010U);
    if (!display_send_all(descriptor, geometry_request, sizeof(geometry_request)) ||
        !display_recv_all(descriptor, reply, sizeof(reply)) || reply[0] != 1 ||
        display_get_le16(reply + 16) != 800 ||
        display_get_le16(reply + 18) != 600) {
        close(descriptor);
        return 513;
    }
    unsigned char tree_request[8];
    memset(tree_request, 0, sizeof(tree_request));
    tree_request[0] = 15;                   /* QueryTree */
    display_put_le16(tree_request + 2, 2);
    display_put_le32(tree_request + 4, 0x01000010U);
    if (!display_send_all(descriptor, tree_request, sizeof(tree_request)) ||
        !display_recv_all(descriptor, reply, sizeof(reply)) || reply[0] != 1 ||
        display_get_le16(reply + 16) != 0) {
        close(descriptor);
        return 514;
    }
    close(descriptor);
    puts("X11 window lifecycle verified");
    puts("X11 protocol handshake verified");
    return 0;
}

static int test_wayland_protocol(pid_t server_pid) {
    (void)server_pid;
    int descriptor = display_connect_unix("/run/user/0/wayland-0");
    if (descriptor < 0) return 511;
    unsigned char request[12];
    memset(request, 0, sizeof(request));
    display_put_le32(request, 1);       /* wl_display */
    display_put_le32(request + 4, 12U << 16 | 1U); /* get_registry */
    display_put_le32(request + 8, 2);   /* registry object */
    if (!display_send_all(descriptor, request, sizeof(request))) {
        close(descriptor);
        return 512;
    }
    unsigned char global[128];
    if (!display_recv_all(descriptor, global, 8)) {
        close(descriptor);
        return 513;
    }
    uint32_t size = display_get_le32(global + 4) >> 16;
    if (display_get_le32(global) != 2 || (display_get_le32(global + 4) & 0xffffU) != 0 ||
        size < 20U || size > sizeof(global)) {
        close(descriptor);
        return 514;
    }
    if (!display_recv_all(descriptor, global + 8, size - 8U)) {
        close(descriptor);
        return 515;
    }
    uint32_t announced_name = display_get_le32(global + 8);
    uint32_t interface_length = display_get_le32(global + 12);
    if (announced_name != 1 || interface_length != 14U ||
        memcmp(global + 16, "wl_compositor", 13) != 0) {
        close(descriptor);
        return 516;
    }
    /* get_registry produces one event for each advertised global.  Drain the
     * remaining wl_shm and wl_seat announcements before issuing sync; leaving
     * them queued would make the next recv interpret a global event as the
     * wl_display.sync callback. */
    int saw_xdg_wm_base = 0;
    for (unsigned global_index = 1; global_index < 4U; ++global_index) {
        if (!display_recv_all(descriptor, global, 8)) {
            close(descriptor);
            return 517;
        }
        uint32_t global_size = display_get_le32(global + 4) >> 16;
        if (global_size < 8U || global_size > sizeof(global)) {
            close(descriptor);
            return 518;
        }
        if (!display_recv_all(descriptor, global + 8, global_size - 8U)) {
            close(descriptor);
            return 519;
        }
        if (display_get_le32(global + 8) == 4U &&
            display_get_le32(global + 12) == 12U &&
            memcmp(global + 16, "xdg_wm_base", 11) == 0)
            saw_xdg_wm_base = 1;
    }
    if (!saw_xdg_wm_base) {
        close(descriptor);
        return 522;
    }
    unsigned char sync[12];
    memset(sync, 0, sizeof(sync));
    display_put_le32(sync, 1);
    display_put_le32(sync + 4, 12U << 16); /* wl_display.sync */
    display_put_le32(sync + 8, 3);
    if (!display_send_all(descriptor, sync, sizeof(sync))) {
        close(descriptor);
        return 520;
    }
    unsigned char done[12];
    if (!display_recv_all(descriptor, done, sizeof(done)) ||
        display_get_le32(done) != 3 || (display_get_le32(done + 4) & 0xffffU) != 0) {
        close(descriptor);
        return 521;
    }
    /* Bind wl_compositor and xdg_wm_base, then exercise the normal
     * xdg_surface/xdg_toplevel configure round-trip used by GTK and Qt.
     * The bridge keeps the backing pixels in Lumen; this verifies the wire
     * negotiation and configure acknowledgement without fabricating a full
     * compositor policy engine. */
    unsigned char bind[96];
    memset(bind, 0, sizeof(bind));
    display_put_le32(bind, 2); /* wl_registry */
    size_t bind_size = 8U;
    display_put_le32(bind + bind_size, 1U);
    bind_size += 4U;
    bind_size = display_put_wayland_string(bind, bind_size, "wl_compositor");
    display_put_le32(bind + bind_size, 4U);
    bind_size += 4U;
    display_put_le32(bind + bind_size, 10U);
    bind_size += 4U;
    display_put_le32(bind + 4, (uint32_t)(bind_size << 16));
    if (!display_send_all(descriptor, bind, bind_size)) {
        close(descriptor);
        return 523;
    }
    unsigned char create_surface[12];
    memset(create_surface, 0, sizeof(create_surface));
    display_put_le32(create_surface, 10U);
    display_put_le32(create_surface + 4, 12U << 16);
    display_put_le32(create_surface + 8, 11U);
    if (!display_send_all(descriptor, create_surface, sizeof(create_surface))) {
        close(descriptor);
        return 524;
    }
    memset(bind, 0, sizeof(bind));
    display_put_le32(bind, 2U);
    bind_size = 8U;
    display_put_le32(bind + bind_size, 4U);
    bind_size += 4U;
    bind_size = display_put_wayland_string(bind, bind_size, "xdg_wm_base");
    display_put_le32(bind + bind_size, 6U);
    bind_size += 4U;
    display_put_le32(bind + bind_size, 12U);
    bind_size += 4U;
    display_put_le32(bind + 4, (uint32_t)(bind_size << 16));
    if (!display_send_all(descriptor, bind, bind_size)) {
        close(descriptor);
        return 525;
    }
    unsigned char ping[12];
    if (!display_recv_all(descriptor, ping, sizeof(ping)) ||
        display_get_le32(ping) != 12U ||
        (display_get_le32(ping + 4) & 0xffffU) != 0U) {
        close(descriptor);
        return 526;
    }
    unsigned char pong[12];
    memset(pong, 0, sizeof(pong));
    display_put_le32(pong, 12U);
    display_put_le32(pong + 4, 12U << 16 | 3U);
    display_put_le32(pong + 8, display_get_le32(ping + 8));
    if (!display_send_all(descriptor, pong, sizeof(pong))) {
        close(descriptor);
        return 527;
    }
    unsigned char get_surface[16];
    memset(get_surface, 0, sizeof(get_surface));
    display_put_le32(get_surface, 12U);
    display_put_le32(get_surface + 4, 16U << 16 | 2U);
    display_put_le32(get_surface + 8, 13U);
    display_put_le32(get_surface + 12, 11U);
    if (!display_send_all(descriptor, get_surface, sizeof(get_surface))) {
        close(descriptor);
        return 528;
    }
    unsigned char get_toplevel[12];
    memset(get_toplevel, 0, sizeof(get_toplevel));
    display_put_le32(get_toplevel, 13U);
    display_put_le32(get_toplevel + 4, 12U << 16 | 1U);
    display_put_le32(get_toplevel + 8, 14U);
    if (!display_send_all(descriptor, get_toplevel, sizeof(get_toplevel))) {
        close(descriptor);
        return 529;
    }
    unsigned char configure[24];
    if (!display_recv_all(descriptor, configure, 20U) ||
        display_get_le32(configure) != 14U ||
        (display_get_le32(configure + 4) & 0xffffU) != 0U ||
        display_get_le32(configure + 8) != 1024U ||
        display_get_le32(configure + 12) != 768U ||
        display_get_le32(configure + 16) != 0U) {
        close(descriptor);
        return 530;
    }
    unsigned char surface_configure[12];
    if (!display_recv_all(descriptor, surface_configure,
                          sizeof(surface_configure)) ||
        display_get_le32(surface_configure) != 13U ||
        (display_get_le32(surface_configure + 4) & 0xffffU) != 0U ||
        display_get_le32(surface_configure + 8) == 0U) {
        close(descriptor);
        return 531;
    }
    unsigned char ack[12];
    memset(ack, 0, sizeof(ack));
    display_put_le32(ack, 13U);
    display_put_le32(ack + 4, 12U << 16 | 4U);
    display_put_le32(ack + 8, display_get_le32(surface_configure + 8));
    if (!display_send_all(descriptor, ack, sizeof(ack))) {
        close(descriptor);
        return 532;
    }
    unsigned char title[32];
    memset(title, 0, sizeof(title));
    display_put_le32(title, 14U);
    size_t title_size = 8U;
    title_size = display_put_wayland_string(title, title_size, "akaOS");
    display_put_le32(title + 4, (uint32_t)(title_size << 16 | 2U));
    if (!display_send_all(descriptor, title, title_size)) {
        close(descriptor);
        return 533;
    }
    unsigned char commit[8];
    memset(commit, 0, sizeof(commit));
    display_put_le32(commit, 11U);
    display_put_le32(commit + 4, 8U << 16 | 6U);
    if (!display_send_all(descriptor, commit, sizeof(commit))) {
        close(descriptor);
        return 534;
    }
    puts("Wayland xdg-shell negotiation verified");
    close(descriptor);
    puts("Wayland protocol handshake verified");
    return 0;
}

static int test_display_protocols(void) {
    /* Exercise both local display endpoints through one persistent bridge.
     * This mirrors a real session service and avoids making correctness
     * depend on rapid process-table slot reuse between two servers. */
    char bridge_path[] = "/bin/display-protocols";
    char bridge_mode[] = "--both";
    char bridge_once[] = "--once";
    char *bridge_arguments[] = {bridge_path, bridge_mode, bridge_once, NULL};
    pid_t bridge_server = spawn(bridge_path, bridge_arguments);
    if (bridge_server < 0) return 520;
    int result = test_x11_protocol(bridge_server);
    if (result != 0) {
        (void)kill(bridge_server, SIGTERM);
        (void)waitpid(bridge_server, NULL, 0);
        return result;
    }
    result = test_wayland_protocol(bridge_server);
    if (result != 0) {
        (void)kill(bridge_server, SIGTERM);
        (void)waitpid(bridge_server, NULL, 0);
        fprintf(2, "Wayland protocol handshake failed (code=%d)\n", result);
        return result;
    }
    int status = -1;
    if (waitpid(bridge_server, &status, 0) != bridge_server || status != 0)
        return 523;
    return 0;
}

static int desktop_runtime_path(size_t index, char *path, size_t capacity) {
    if (!path || capacity == 0) return -1;
    /* Keep path selection explicit so each profile mapping stays easy to audit. */
    if (index == 0) strcpy(path, "/lib/libpthread.so");
    else if (index == 1) strcpy(path, "/lib/libglib.so");
    else if (index == 2) strcpy(path, "/lib/libgio.so");
    else if (index == 3) strcpy(path, "/lib/libgtk.so");
    else if (index == 4) strcpy(path, "/lib/libgtk-4.so");
    else if (index == 5) strcpy(path, "/lib/libQt6Core.so");
    else if (index == 6) strcpy(path, "/lib/libQt6Gui.so");
    else if (index == 7) strcpy(path, "/lib/libQt6Widgets.so");
    else return -1;
    return strlen(path) < capacity ? 0 : -1;
}

static int test_desktop_runtime(void) {
    static const size_t runtime_count = 8U;
    typedef uint32_t (*runtime_function)(void);
    for (size_t index = 0; index < runtime_count; ++index) {
        char path[32];
        if (desktop_runtime_path(index, path, sizeof(path)) != 0) return 495;
        void *handle = dlopen(path, RTLD_NOW);
        if (!handle) {
            char *error = dlerror();
            printf("desktop runtime dlopen failed: %s (%s)\n", path,
                   error ? error : "unknown");
            return 495;
        }
        runtime_function abi = (runtime_function)dlsym(handle, "akaos_runtime_abi");
        runtime_function family = (runtime_function)dlsym(handle, "akaos_runtime_family");
        uint32_t expected = (uint32_t)index + 1U;
        if (!abi || !family || abi() != 2U || family() != expected) {
            printf("desktop runtime ABI mismatch: %s\n", path);
            (void)dlclose(handle);
            return 496;
        }
        if (dlclose(handle) != 0) return 497;
    }
    /* Normal soname spellings must resolve to the same native objects. */
    void *glib_alias = dlopen("/lib/libglib-2.0.so.0", RTLD_NOW);
    void *gtk_alias = dlopen("/lib/libgtk-3.so.0", RTLD_NOW);
    void *qt_alias = dlopen("/lib/libQt6Core.so.6", RTLD_NOW);
    if (!glib_alias || !gtk_alias || !qt_alias) {
        char *alias_error = dlerror();
        printf("desktop runtime alias dlopen failed: glib=%u gtk=%u qt=%u (%s)\n",
               glib_alias != NULL, gtk_alias != NULL, qt_alias != NULL,
               alias_error ? alias_error : "unknown");
        if (qt_alias) (void)dlclose(qt_alias);
        if (gtk_alias) (void)dlclose(gtk_alias);
        if (glib_alias) (void)dlclose(glib_alias);
        return 498;
    }
    if (dlclose(qt_alias) != 0 || dlclose(gtk_alias) != 0 ||
        dlclose(glib_alias) != 0) return 499;
    /* Exercise the event-loop and widget lifecycle exported by the native
     * compatibility objects.  These calls are intentionally resolved through
     * dlsym so the test covers the same ABI boundary that a source port uses. */
    void *glib = dlopen("/lib/libglib.so", RTLD_NOW);
    void *gtk = dlopen("/lib/libgtk.so", RTLD_NOW);
    void *qt = dlopen("/lib/libQt6Core.so", RTLD_NOW);
    if (!glib || !gtk || !qt) {
        if (qt) (void)dlclose(qt);
        if (gtk) (void)dlclose(gtk);
        if (glib) (void)dlclose(glib);
        return 500;
    }
    typedef void *(*loop_new_function)(void);
    typedef void (*loop_run_function)(void *);
    typedef void (*loop_quit_function)(void *);
    typedef int (*loop_running_function)(void *);
    typedef unsigned (*idle_add_function)(unsigned (*)(void *), void *);
    typedef unsigned (*timeout_add_function)(unsigned, unsigned (*)(void *),
                                             void *);
    typedef int (*context_iteration_function)(void *, int);
    typedef void *(*context_default_function)(void);
    typedef void *(*context_new_function)(void);
    typedef void *(*context_ref_function)(void *);
    typedef void (*context_unref_function)(void *);
    typedef void (*context_push_function)(void *);
    typedef void (*context_pop_function)(void *);
    typedef unsigned (*timeout_add_full_function)(
        int, unsigned, unsigned (*)(void *), void *, void (*)(void *));
    typedef unsigned (*idle_add_full_function)(
        int, unsigned (*)(void *), void *, void (*)(void *));
    typedef unsigned (*unix_fd_add_function)(
        int, unsigned, unsigned (*)(int, unsigned, void *), void *);
    typedef void *(*io_channel_unix_new_function)(int);
    typedef void *(*io_channel_ref_function)(void *);
    typedef void (*io_channel_unref_function)(void *);
    typedef int (*io_channel_get_fd_function)(void *);
    typedef unsigned (*io_add_watch_function)(
        void *, unsigned, unsigned (*)(void *, unsigned, void *), void *);
    typedef int (*source_remove_function)(unsigned);
    typedef void *(*g_main_loop_new_function)(void *, int);
    typedef void (*g_main_loop_unref_function)(void *);
    typedef void *(*window_new_function)(int);
    typedef void (*window_title_function)(void *, const char *);
    typedef void (*window_size_function)(void *, int, int);
    typedef void (*window_action_function)(void *);
    typedef int (*window_state_function)(void *);
    typedef void *(*widget_new_function)(unsigned);
    typedef int (*widget_add_function)(void *, void *);
    typedef int (*widget_remove_function)(void *, void *);
    typedef int (*widget_text_function)(void *, const char *);
    typedef int (*widget_size_function)(void *, int, int);
    typedef int (*widget_get_size_function)(void *, int *, int *);
    typedef int (*widget_get_visible_function)(void *);
    typedef void *(*widget_parent_function)(void *);
    typedef unsigned (*signal_connect_function)(
        void *, const char *, void (*)(void *, void *), void *);
    typedef int (*signal_emit_function)(void *, const char *);
    typedef void *(*runtime_malloc_function)(size_t);
    typedef void *(*runtime_realloc_function)(void *, size_t);
    typedef void (*runtime_free_function)(void *);
    typedef char *(*runtime_strdup_function)(const char *);
    typedef void *(*array_new_function)(int, int, unsigned);
    typedef void *(*array_append_function)(void *, const void *, size_t);
    typedef void *(*array_set_size_function)(void *, size_t);
    typedef size_t (*array_length_function)(const void *);
    typedef unsigned (*array_element_size_function)(const void *);
    typedef void *(*array_free_function)(void *, int);
    typedef void *(*ptr_array_new_function)(void);
    typedef int (*ptr_array_add_function)(void *, void *);
    typedef size_t (*ptr_array_length_function)(const void *);
    typedef void *(*ptr_array_index_function)(const void *, size_t);
    typedef void **(*ptr_array_free_function)(void *, int);
    typedef void *(*string_new_function)(const char *);
    typedef void *(*string_append_function)(void *, const char *);
    typedef void *(*string_append_len_function)(void *, const char *, size_t);
    typedef void *(*string_truncate_function)(void *, size_t);
    typedef char *(*string_free_function)(void *, int);
    typedef void *(*hash_new_function)(unsigned (*)(const void *),
                                       int (*)(const void *, const void *));
    typedef int (*hash_insert_function)(void *, void *, void *);
    typedef void *(*hash_lookup_function)(const void *, const void *);
    typedef int (*hash_contains_function)(const void *, const void *);
    typedef int (*hash_remove_function)(void *, const void *);
    typedef size_t (*hash_size_function)(const void *);
    typedef void (*hash_destroy_function)(void *);
    typedef unsigned (*hash_string_function)(const void *);
    typedef int (*hash_equal_function)(const void *, const void *);
    typedef void *(*list_prepend_function)(void *, void *);
    typedef void *(*list_append_function)(void *, void *);
    typedef void *(*list_remove_function)(void *, const void *);
    typedef size_t (*list_length_function)(const void *);
    typedef void *(*list_nth_data_function)(const void *, size_t);
    typedef void (*list_free_function)(void *);
    typedef void *(*bytes_new_function)(const void *, size_t);
    typedef void *(*bytes_ref_function)(void *);
    typedef void (*bytes_unref_function)(void *);
    typedef const void *(*bytes_data_function)(const void *, size_t *);
    typedef size_t (*bytes_size_function)(const void *);
    typedef int (*bytes_equal_function)(const void *, const void *);
    typedef void *(*variant_string_new_function)(const char *);
    typedef void *(*variant_boolean_new_function)(int);
    typedef void *(*variant_uint32_new_function)(uint32_t);
    typedef void *(*variant_int32_new_function)(int32_t);
    typedef void *(*variant_uint64_new_function)(uint64_t);
    typedef void *(*variant_int64_new_function)(int64_t);
    typedef void *(*variant_ref_function)(void *);
    typedef void (*variant_unref_function)(void *);
    typedef const char *(*variant_string_get_function)(const void *, size_t *);
    typedef int (*variant_boolean_get_function)(const void *);
    typedef uint32_t (*variant_uint32_get_function)(const void *);
    typedef int32_t (*variant_int32_get_function)(const void *);
    typedef uint64_t (*variant_uint64_get_function)(const void *);
    typedef int64_t (*variant_int64_get_function)(const void *);
    typedef const char *(*variant_type_function)(const void *);
    typedef int (*variant_is_type_function)(const void *, const char *);
    typedef void *(*variant_children_new_function)(const void *const [], size_t);
    typedef size_t (*variant_children_count_function)(const void *);
    typedef void *(*variant_child_get_function)(const void *, size_t);
    typedef void *(*variant_format_new_function)(const char *format_string, ...);
    typedef void *(*variant_dict_new_function)(const void *, const void *);
    typedef void *(*variant_dict_child_function)(const void *);
    typedef void *(*variant_lookup_function)(const void *, const char *, const char *);
    typedef void *(*variant_wrap_new_function)(const void *);
    typedef void *(*variant_wrap_get_function)(const void *);
    typedef void *(*variant_builder_new_function)(const char *);
    typedef int (*variant_builder_add_value_function)(void *, void *);
    typedef int (*variant_builder_add_function)(void *, const char *, ...);
    typedef void *(*variant_builder_end_function)(void *);
    typedef void (*variant_builder_clear_function)(void *);
    typedef void (*object_set_data_function)(void *, const char *, void *);
    typedef void *(*object_get_data_function)(const void *, const char *);
    typedef void *(*object_steal_data_function)(void *, const char *);
    typedef void (*object_set_function)(void *, const char *, ...);
    typedef void (*object_get_function)(const void *, const char *, ...);
    typedef void *(*widget_entry_new_function)(void);
    typedef const char *(*widget_entry_get_text_function)(void *);
    typedef void (*widget_entry_set_text_function)(void *, const char *);
    typedef void *(*widget_check_new_function)(const char *);
    typedef void (*widget_active_set_function)(void *, int);
    typedef int (*widget_active_get_function)(void *);
    typedef void *(*widget_grid_new_function)(void);
    typedef void (*widget_grid_attach_function)(void *, void *, int, int, int, int);
    typedef void (*widget_visible_set_function)(void *, int);
    typedef void (*widget_show_all_function)(void *);
    typedef void (*widget_sensitive_set_function)(void *, int);
    typedef int (*widget_sensitive_get_function)(void *);
    typedef void *(*application_new_function)(const char *, unsigned);
    typedef int (*application_register_function)(void *, void *, void **);
    typedef void (*application_action_function)(void *);
    typedef int (*application_registered_function)(const void *);
    typedef const char *(*application_id_function)(const void *);
    typedef int (*application_run_function)(void *, int, char **);
    typedef void *(*application_window_new_function)(void *);
    typedef void *(*action_new_function)(const char *, const char *);
    typedef void *(*action_ref_function)(void *);
    typedef void (*action_unref_function)(void *);
    typedef const char *(*action_name_function)(const void *);
    typedef void (*action_enabled_set_function)(void *, int);
    typedef int (*action_enabled_get_function)(const void *);
    typedef void (*action_activate_function)(void *, void *);
    typedef int (*action_map_add_function)(void *, void *);
    typedef int (*action_map_remove_function)(void *, const char *);
    typedef void *(*action_map_lookup_function)(const void *, const char *);
    typedef int (*signal_disconnect_function)(void *, unsigned);
    typedef unsigned (*signal_disconnect_by_func_function)(
        void *, void (*)(void *, void *), void *);
    typedef void *(*settings_new_function)(const char *);
    typedef void *(*settings_ref_function)(void *);
    typedef void (*settings_unref_function)(void *);
    typedef void *(*settings_get_value_function)(const void *, const char *);
    typedef int (*settings_set_value_function)(void *, const char *, const void *);
    typedef char *(*settings_get_string_function)(const void *, const char *);
    typedef int (*settings_set_string_function)(void *, const char *, const char *);
    typedef int (*settings_get_boolean_function)(const void *, const char *);
    typedef int (*settings_set_boolean_function)(void *, const char *, int);
    typedef struct runtime_gvalue_probe {
        uint32_t initialized;
        char type_string[4];
        int64_t signed_value;
        uint64_t unsigned_value;
        char string_value[96];
    } runtime_gvalue_probe;
    typedef void (*value_init_function)(runtime_gvalue_probe *, const char *);
    typedef void (*value_unset_function)(runtime_gvalue_probe *);
    typedef void (*value_set_string_function)(runtime_gvalue_probe *, const char *);
    typedef const char *(*value_get_string_function)(const runtime_gvalue_probe *);
    typedef void (*value_set_boolean_function)(runtime_gvalue_probe *, int);
    typedef int (*value_get_boolean_function)(const runtime_gvalue_probe *);
    typedef void (*value_set_int_function)(runtime_gvalue_probe *, int);
    typedef int (*value_get_int_function)(const runtime_gvalue_probe *);
    typedef void (*value_set_uint_function)(runtime_gvalue_probe *, unsigned);
    typedef unsigned (*value_get_uint_function)(const runtime_gvalue_probe *);
    typedef int (*object_set_property_function)(void *, const char *,
                                                const runtime_gvalue_probe *);
    typedef int (*object_get_property_function)(const void *, const char *,
                                                runtime_gvalue_probe *);
    typedef void *(*binding_bind_function)(void *, const char *, void *,
                                           const char *, unsigned);
    typedef void *(*binding_ref_function)(void *);
    typedef void (*binding_unbind_function)(void *);
    typedef void *(*cancellable_new_function)(void);
    typedef void *(*cancellable_ref_function)(void *);
    typedef void (*cancellable_unref_function)(void *);
    typedef void (*cancellable_cancel_function)(void *);
    typedef int (*cancellable_is_cancelled_function)(const void *);
    typedef void *(*error_new_function)(unsigned, int, const char *);
    typedef void *(*error_copy_function)(const void *);
    typedef void (*error_free_function)(void *);
    typedef int (*error_matches_function)(const void *, unsigned, int);
    typedef unsigned (*error_domain_function)(const void *);
    typedef int (*error_code_function)(const void *);
    typedef const char *(*error_message_function)(const void *);
    typedef void *(*task_new_function)(void *, void *,
                                       void (*)(void *, void *, void *), void *);
    typedef void *(*task_ref_function)(void *);
    typedef void (*task_unref_function)(void *);
    typedef void *(*task_source_function)(const void *);
    typedef int (*task_state_function)(const void *);
    typedef void (*task_return_boolean_function)(void *, int);
    typedef void (*task_return_error_function)(void *, void *);
    typedef int (*task_cancelled_function)(void *);
    typedef int (*task_propagate_boolean_function)(const void *, void **);
    typedef void *(*file_new_function)(const char *);
    typedef void *(*file_ref_function)(void *);
    typedef void (*file_unref_function)(void *);
    typedef char *(*file_string_function)(const void *);
    typedef void *(*file_parent_function)(const void *);
    typedef int (*file_compare_function)(const void *, const void *);
    typedef void *(*file_enumerate_function)(
        void *, const char *, int, void *, void **);
    typedef void *(*file_enumerator_ref_function)(void *);
    typedef void (*file_enumerator_unref_function)(void *);
    typedef void *(*file_enumerator_container_function)(const void *);
    typedef void *(*file_enumerator_next_function)(void *, void *, void **);
    typedef int (*file_enumerator_close_function)(void *, void *, void **);
    typedef void *(*file_info_ref_function)(void *);
    typedef void (*file_info_unref_function)(void *);
    typedef const char *(*file_info_string_function)(const void *);
    typedef int (*file_info_type_function)(const void *);
    typedef uint64_t (*file_info_size_function)(const void *);
    typedef uint64_t (*file_info_uint64_function)(const void *, const char *);
    typedef const char *(*file_info_attribute_string_function)(
        const void *, const char *);
    typedef int (*file_get_contents_function)(const char *, char **, size_t *,
                                              void **);
    typedef int (*file_set_contents_function)(const char *, const char *, long,
                                              void **);
    typedef int (*file_query_exists_function)(const void *, void *);
    typedef void (*file_load_async_function)(
        void *, void *, void (*)(void *, void *, void *), void *);
    typedef int (*file_load_finish_function)(void *, void *, char **, size_t *,
                                             char **, void **);
    loop_new_function loop_new = (loop_new_function)dlsym(
        glib, "akaos_runtime_loop_new");
    loop_run_function loop_run = (loop_run_function)dlsym(
        glib, "akaos_runtime_loop_run");
    loop_quit_function loop_quit = (loop_quit_function)dlsym(
        glib, "akaos_runtime_loop_quit");
    loop_running_function loop_running = (loop_running_function)dlsym(
        glib, "akaos_runtime_loop_running");
    idle_add_function idle_add = (idle_add_function)dlsym(
        glib, "g_idle_add");
    timeout_add_function timeout_add =
        (timeout_add_function)dlsym(glib, "g_timeout_add");
    context_iteration_function context_iteration =
        (context_iteration_function)dlsym(glib, "g_main_context_iteration");
    context_default_function context_default =
        (context_default_function)dlsym(glib, "g_main_context_default");
    context_new_function context_new =
        (context_new_function)dlsym(glib, "g_main_context_new");
    context_ref_function context_ref =
        (context_ref_function)dlsym(glib, "g_main_context_ref");
    context_unref_function context_unref =
        (context_unref_function)dlsym(glib, "g_main_context_unref");
    context_push_function context_push =
        (context_push_function)dlsym(glib, "g_main_context_push_thread_default");
    context_pop_function context_pop =
        (context_pop_function)dlsym(glib, "g_main_context_pop_thread_default");
    timeout_add_full_function timeout_add_full =
        (timeout_add_full_function)dlsym(glib, "g_timeout_add_full");
    idle_add_full_function idle_add_full =
        (idle_add_full_function)dlsym(glib, "g_idle_add_full");
    unix_fd_add_function unix_fd_add =
        (unix_fd_add_function)dlsym(glib, "g_unix_fd_add");
    io_channel_unix_new_function io_channel_unix_new =
        (io_channel_unix_new_function)dlsym(glib, "g_io_channel_unix_new");
    io_channel_ref_function io_channel_ref =
        (io_channel_ref_function)dlsym(glib, "g_io_channel_ref");
    io_channel_unref_function io_channel_unref =
        (io_channel_unref_function)dlsym(glib, "g_io_channel_unref");
    io_channel_get_fd_function io_channel_get_fd =
        (io_channel_get_fd_function)dlsym(glib, "g_io_channel_get_fd");
    io_add_watch_function io_add_watch =
        (io_add_watch_function)dlsym(glib, "g_io_add_watch");
    source_remove_function source_remove =
        (source_remove_function)dlsym(glib, "g_source_remove");
    g_main_loop_new_function g_main_loop_new_api =
        (g_main_loop_new_function)dlsym(glib, "g_main_loop_new");
    g_main_loop_unref_function g_main_loop_unref_api =
        (g_main_loop_unref_function)dlsym(glib, "g_main_loop_unref");
    window_new_function window_new = (window_new_function)dlsym(
        gtk, "gtk_window_new");
    window_title_function window_title = (window_title_function)dlsym(
        gtk, "gtk_window_set_title");
    window_size_function window_size = (window_size_function)dlsym(
        gtk, "gtk_window_set_default_size");
    window_action_function window_show = (window_action_function)dlsym(
        gtk, "gtk_widget_show");
    window_state_function window_destroy = (window_state_function)dlsym(
        gtk, "akaos_runtime_window_destroy");
    widget_new_function widget_new = (widget_new_function)dlsym(
        gtk, "akaos_runtime_widget_new");
    widget_add_function widget_add = (widget_add_function)dlsym(
        gtk, "akaos_runtime_widget_add");
    widget_remove_function widget_remove = (widget_remove_function)dlsym(
        gtk, "akaos_runtime_widget_remove");
    widget_text_function widget_text = (widget_text_function)dlsym(
        gtk, "akaos_runtime_widget_set_text");
    widget_size_function widget_size = (widget_size_function)dlsym(
        gtk, "akaos_runtime_widget_set_size_request");
    widget_get_size_function widget_get_size = (widget_get_size_function)dlsym(
        gtk, "akaos_runtime_widget_get_size");
    widget_get_visible_function widget_get_visible =
        (widget_get_visible_function)dlsym(gtk, "akaos_runtime_widget_get_visible");
    widget_parent_function widget_parent = (widget_parent_function)dlsym(
        gtk, "akaos_runtime_widget_get_parent");
    widget_entry_new_function widget_entry_new =
        (widget_entry_new_function)dlsym(gtk, "gtk_entry_new");
    widget_entry_get_text_function widget_entry_get_text =
        (widget_entry_get_text_function)dlsym(gtk, "gtk_entry_get_text");
    widget_entry_set_text_function widget_entry_set_text =
        (widget_entry_set_text_function)dlsym(gtk, "gtk_entry_set_text");
    widget_check_new_function widget_check_new =
        (widget_check_new_function)dlsym(gtk, "gtk_check_button_new_with_label");
    widget_active_set_function widget_active_set =
        (widget_active_set_function)dlsym(gtk, "gtk_toggle_button_set_active");
    widget_active_get_function widget_active_get =
        (widget_active_get_function)dlsym(gtk, "gtk_toggle_button_get_active");
    widget_grid_new_function widget_grid_new =
        (widget_grid_new_function)dlsym(gtk, "gtk_grid_new");
    widget_grid_attach_function widget_grid_attach =
        (widget_grid_attach_function)dlsym(gtk, "gtk_grid_attach");
    widget_visible_set_function widget_visible_set =
        (widget_visible_set_function)dlsym(gtk, "gtk_widget_set_visible");
    widget_show_all_function widget_show_all =
        (widget_show_all_function)dlsym(gtk, "gtk_widget_show_all");
    widget_sensitive_set_function widget_sensitive_set =
        (widget_sensitive_set_function)dlsym(gtk, "gtk_widget_set_sensitive");
    widget_sensitive_get_function widget_sensitive_get =
        (widget_sensitive_get_function)dlsym(gtk, "gtk_widget_get_sensitive");
    application_new_function application_new =
        (application_new_function)dlsym(gtk, "gtk_application_new");
    application_register_function application_register =
        (application_register_function)dlsym(glib, "g_application_register");
    application_action_function application_hold =
        (application_action_function)dlsym(glib, "g_application_hold");
    application_action_function application_release =
        (application_action_function)dlsym(glib, "g_application_release");
    application_registered_function application_registered =
        (application_registered_function)dlsym(glib, "g_application_get_is_registered");
    application_id_function application_id =
        (application_id_function)dlsym(glib, "g_application_get_application_id");
    application_action_function application_activate =
        (application_action_function)dlsym(glib, "g_application_activate");
    application_run_function application_run =
        (application_run_function)dlsym(glib, "g_application_run");
    application_action_function application_quit =
        (application_action_function)dlsym(glib, "g_application_quit");
    application_window_new_function application_window_new =
        (application_window_new_function)dlsym(gtk, "gtk_application_window_new");
    action_new_function action_new =
        (action_new_function)dlsym(glib, "g_simple_action_new");
    action_ref_function action_ref =
        (action_ref_function)dlsym(glib, "g_simple_action_ref");
    action_unref_function action_unref =
        (action_unref_function)dlsym(glib, "g_simple_action_unref");
    action_name_function action_name =
        (action_name_function)dlsym(glib, "g_action_get_name");
    action_enabled_set_function action_enabled_set =
        (action_enabled_set_function)dlsym(glib, "g_simple_action_set_enabled");
    action_enabled_get_function action_enabled_get =
        (action_enabled_get_function)dlsym(glib, "g_action_get_enabled");
    action_activate_function action_activate =
        (action_activate_function)dlsym(glib, "g_action_activate");
    action_map_add_function action_map_add =
        (action_map_add_function)dlsym(glib, "g_action_map_add_action");
    action_map_remove_function action_map_remove =
        (action_map_remove_function)dlsym(glib, "g_action_map_remove_action");
    action_map_lookup_function action_map_lookup =
        (action_map_lookup_function)dlsym(glib, "g_action_map_lookup_action");
    signal_connect_function signal_connect = (signal_connect_function)dlsym(
        glib, "g_signal_connect");
    signal_emit_function signal_emit = (signal_emit_function)dlsym(
        glib, "g_signal_emit_by_name");
    signal_disconnect_function signal_disconnect =
        (signal_disconnect_function)dlsym(glib, "g_signal_handler_disconnect");
    signal_disconnect_by_func_function signal_disconnect_by_func =
        (signal_disconnect_by_func_function)dlsym(
            glib, "g_signal_handlers_disconnect_by_func");
    object_set_data_function object_set_data =
        (object_set_data_function)dlsym(glib, "g_object_set_data");
    object_get_data_function object_get_data =
        (object_get_data_function)dlsym(glib, "g_object_get_data");
    object_steal_data_function object_steal_data =
        (object_steal_data_function)dlsym(glib, "g_object_steal_data");
    object_set_function object_set =
        (object_set_function)dlsym(glib, "g_object_set");
    object_get_function object_get =
        (object_get_function)dlsym(glib, "g_object_get");
    settings_new_function settings_new =
        (settings_new_function)dlsym(glib, "g_settings_new");
    settings_ref_function settings_ref =
        (settings_ref_function)dlsym(glib, "g_settings_ref");
    settings_unref_function settings_unref =
        (settings_unref_function)dlsym(glib, "g_settings_unref");
    settings_get_value_function settings_get_value =
        (settings_get_value_function)dlsym(glib, "g_settings_get_value");
    settings_set_value_function settings_set_value =
        (settings_set_value_function)dlsym(glib, "g_settings_set_value");
    settings_get_string_function settings_get_string =
        (settings_get_string_function)dlsym(glib, "g_settings_get_string");
    settings_set_string_function settings_set_string =
        (settings_set_string_function)dlsym(glib, "g_settings_set_string");
    settings_get_boolean_function settings_get_boolean =
        (settings_get_boolean_function)dlsym(glib, "g_settings_get_boolean");
    settings_set_boolean_function settings_set_boolean =
        (settings_set_boolean_function)dlsym(glib, "g_settings_set_boolean");
    value_init_function value_init =
        (value_init_function)dlsym(glib, "g_value_init");
    value_unset_function value_unset =
        (value_unset_function)dlsym(glib, "g_value_unset");
    value_set_string_function value_set_string =
        (value_set_string_function)dlsym(glib, "g_value_set_string");
    value_get_string_function value_get_string =
        (value_get_string_function)dlsym(glib, "g_value_get_string");
    value_set_boolean_function value_set_boolean =
        (value_set_boolean_function)dlsym(glib, "g_value_set_boolean");
    value_get_boolean_function value_get_boolean =
        (value_get_boolean_function)dlsym(glib, "g_value_get_boolean");
    value_set_int_function value_set_int =
        (value_set_int_function)dlsym(glib, "g_value_set_int");
    value_get_int_function value_get_int =
        (value_get_int_function)dlsym(glib, "g_value_get_int");
    value_set_uint_function value_set_uint =
        (value_set_uint_function)dlsym(glib, "g_value_set_uint");
    value_get_uint_function value_get_uint =
        (value_get_uint_function)dlsym(glib, "g_value_get_uint");
    object_set_property_function object_set_property =
        (object_set_property_function)dlsym(glib, "g_object_set_property");
    object_get_property_function object_get_property =
        (object_get_property_function)dlsym(glib, "g_object_get_property");
    binding_bind_function binding_bind =
        (binding_bind_function)dlsym(glib, "g_object_bind_property");
    binding_ref_function binding_ref =
        (binding_ref_function)dlsym(glib, "g_binding_ref");
    binding_unbind_function binding_unbind =
        (binding_unbind_function)dlsym(glib, "g_binding_unbind");
    cancellable_new_function cancellable_new =
        (cancellable_new_function)dlsym(glib, "g_cancellable_new");
    cancellable_ref_function cancellable_ref =
        (cancellable_ref_function)dlsym(glib, "g_cancellable_ref");
    cancellable_unref_function cancellable_unref =
        (cancellable_unref_function)dlsym(glib, "g_cancellable_unref");
    cancellable_cancel_function cancellable_cancel =
        (cancellable_cancel_function)dlsym(glib, "g_cancellable_cancel");
    cancellable_is_cancelled_function cancellable_is_cancelled =
        (cancellable_is_cancelled_function)dlsym(glib, "g_cancellable_is_cancelled");
    error_new_function error_new =
        (error_new_function)dlsym(glib, "g_error_new");
    error_copy_function error_copy =
        (error_copy_function)dlsym(glib, "g_error_copy");
    error_free_function error_free =
        (error_free_function)dlsym(glib, "g_error_free");
    error_matches_function error_matches =
        (error_matches_function)dlsym(glib, "g_error_matches");
    error_domain_function error_domain =
        (error_domain_function)dlsym(glib, "g_error_get_domain");
    error_code_function error_code =
        (error_code_function)dlsym(glib, "g_error_get_code");
    error_message_function error_message =
        (error_message_function)dlsym(glib, "g_error_get_message");
    task_new_function task_new =
        (task_new_function)dlsym(glib, "g_task_new");
    task_ref_function task_ref =
        (task_ref_function)dlsym(glib, "g_task_ref");
    task_unref_function task_unref =
        (task_unref_function)dlsym(glib, "g_task_unref");
    task_source_function task_source =
        (task_source_function)dlsym(glib, "g_task_get_source_object");
    task_state_function task_completed =
        (task_state_function)dlsym(glib, "g_task_get_completed");
    task_return_boolean_function task_return_boolean =
        (task_return_boolean_function)dlsym(glib, "g_task_return_boolean");
    task_return_error_function task_return_error =
        (task_return_error_function)dlsym(glib, "g_task_return_error");
    task_cancelled_function task_return_cancelled =
        (task_cancelled_function)dlsym(glib, "g_task_return_error_if_cancelled");
    task_propagate_boolean_function task_propagate_boolean =
        (task_propagate_boolean_function)dlsym(glib, "g_task_propagate_boolean");
    runtime_free_function settings_free =
        (runtime_free_function)dlsym(glib, "g_free");
    variant_unref_function settings_variant_unref =
        (variant_unref_function)dlsym(glib, "g_variant_unref");
    file_new_function file_new_path =
        (file_new_function)dlsym(glib, "g_file_new_for_path");
    file_new_function file_new_uri =
        (file_new_function)dlsym(glib, "g_file_new_for_uri");
    file_ref_function file_ref =
        (file_ref_function)dlsym(glib, "g_file_ref");
    file_unref_function file_unref =
        (file_unref_function)dlsym(glib, "g_file_unref");
    file_string_function file_get_path =
        (file_string_function)dlsym(glib, "g_file_get_path");
    file_string_function file_get_uri =
        (file_string_function)dlsym(glib, "g_file_get_uri");
    file_string_function file_get_basename =
        (file_string_function)dlsym(glib, "g_file_get_basename");
    file_parent_function file_get_parent =
        (file_parent_function)dlsym(glib, "g_file_get_parent");
    file_compare_function file_equal =
        (file_compare_function)dlsym(glib, "g_file_equal");
    file_compare_function file_has_prefix =
        (file_compare_function)dlsym(glib, "g_file_has_prefix");
    file_enumerate_function file_enumerate =
        (file_enumerate_function)dlsym(glib, "g_file_enumerate_children");
    file_enumerator_ref_function file_enumerator_ref =
        (file_enumerator_ref_function)dlsym(glib, "g_file_enumerator_ref");
    file_enumerator_unref_function file_enumerator_unref =
        (file_enumerator_unref_function)dlsym(glib, "g_file_enumerator_unref");
    file_enumerator_container_function file_enumerator_container =
        (file_enumerator_container_function)dlsym(
            glib, "g_file_enumerator_get_container");
    file_enumerator_next_function file_enumerator_next =
        (file_enumerator_next_function)dlsym(
            glib, "g_file_enumerator_next_file");
    file_enumerator_close_function file_enumerator_close =
        (file_enumerator_close_function)dlsym(glib, "g_file_enumerator_close");
    file_info_ref_function file_info_ref =
        (file_info_ref_function)dlsym(glib, "g_file_info_ref");
    file_info_unref_function file_info_unref =
        (file_info_unref_function)dlsym(glib, "g_file_info_unref");
    file_info_string_function file_info_name =
        (file_info_string_function)dlsym(glib, "g_file_info_get_name");
    file_info_string_function file_info_display_name =
        (file_info_string_function)dlsym(glib, "g_file_info_get_display_name");
    file_info_type_function file_info_type =
        (file_info_type_function)dlsym(glib, "g_file_info_get_file_type");
    file_info_size_function file_info_size =
        (file_info_size_function)dlsym(glib, "g_file_info_get_size");
    file_info_uint64_function file_info_attribute_uint64 =
        (file_info_uint64_function)dlsym(
            glib, "g_file_info_get_attribute_uint64");
    file_info_attribute_string_function file_info_attribute_string =
        (file_info_attribute_string_function)dlsym(
            glib, "g_file_info_get_attribute_string");
    file_get_contents_function file_get_contents =
        (file_get_contents_function)dlsym(glib, "g_file_get_contents");
    file_set_contents_function file_set_contents =
        (file_set_contents_function)dlsym(glib, "g_file_set_contents");
    file_query_exists_function file_query_exists =
        (file_query_exists_function)dlsym(glib, "g_file_query_exists");
    file_load_async_function file_load_async =
        (file_load_async_function)dlsym(glib, "g_file_load_contents_async");
    file_load_finish_function file_load_finish =
        (file_load_finish_function)dlsym(glib, "g_file_load_contents_finish");
    runtime_malloc_function runtime_malloc =
        (runtime_malloc_function)dlsym(glib, "g_malloc0");
    runtime_realloc_function runtime_realloc =
        (runtime_realloc_function)dlsym(glib, "g_realloc");
    runtime_free_function runtime_free =
        (runtime_free_function)dlsym(glib, "g_free");
    runtime_strdup_function runtime_strdup =
        (runtime_strdup_function)dlsym(glib, "g_strdup");
    array_new_function array_new =
        (array_new_function)dlsym(glib, "g_array_new");
    array_append_function array_append =
        (array_append_function)dlsym(glib, "g_array_append_vals");
    array_set_size_function array_set_size =
        (array_set_size_function)dlsym(glib, "g_array_set_size");
    array_length_function array_length =
        (array_length_function)dlsym(glib, "g_array_get_length");
    array_element_size_function array_element_size =
        (array_element_size_function)dlsym(glib, "g_array_get_element_size");
    array_free_function array_free =
        (array_free_function)dlsym(glib, "g_array_free");
    ptr_array_new_function ptr_array_new =
        (ptr_array_new_function)dlsym(glib, "g_ptr_array_new");
    ptr_array_add_function ptr_array_add =
        (ptr_array_add_function)dlsym(glib, "g_ptr_array_add");
    ptr_array_length_function ptr_array_length =
        (ptr_array_length_function)dlsym(glib, "g_ptr_array_get_length");
    ptr_array_index_function ptr_array_index =
        (ptr_array_index_function)dlsym(glib, "g_ptr_array_index");
    ptr_array_free_function ptr_array_free =
        (ptr_array_free_function)dlsym(glib, "g_ptr_array_free");
    string_new_function string_new =
        (string_new_function)dlsym(glib, "g_string_new");
    string_append_function string_append =
        (string_append_function)dlsym(glib, "g_string_append");
    string_append_len_function string_append_len =
        (string_append_len_function)dlsym(glib, "g_string_append_len");
    string_truncate_function string_truncate =
        (string_truncate_function)dlsym(glib, "g_string_truncate");
    string_free_function string_free =
        (string_free_function)dlsym(glib, "g_string_free");
    hash_new_function hash_new =
        (hash_new_function)dlsym(glib, "g_hash_table_new");
    hash_insert_function hash_insert =
        (hash_insert_function)dlsym(glib, "g_hash_table_insert");
    hash_lookup_function hash_lookup =
        (hash_lookup_function)dlsym(glib, "g_hash_table_lookup");
    hash_contains_function hash_contains =
        (hash_contains_function)dlsym(glib, "g_hash_table_contains");
    hash_remove_function hash_remove =
        (hash_remove_function)dlsym(glib, "g_hash_table_remove");
    hash_size_function hash_size =
        (hash_size_function)dlsym(glib, "g_hash_table_size");
    hash_destroy_function hash_destroy =
        (hash_destroy_function)dlsym(glib, "g_hash_table_destroy");
    hash_string_function hash_string =
        (hash_string_function)dlsym(glib, "g_str_hash");
    hash_equal_function hash_equal =
        (hash_equal_function)dlsym(glib, "g_str_equal");
    list_prepend_function list_prepend =
        (list_prepend_function)dlsym(glib, "g_list_prepend");
    list_append_function list_append =
        (list_append_function)dlsym(glib, "g_list_append");
    list_remove_function list_remove =
        (list_remove_function)dlsym(glib, "g_list_remove");
    list_length_function list_length =
        (list_length_function)dlsym(glib, "g_list_length");
    list_nth_data_function list_nth_data =
        (list_nth_data_function)dlsym(glib, "g_list_nth_data");
    list_free_function list_free =
        (list_free_function)dlsym(glib, "g_list_free");
    list_prepend_function slist_prepend =
        (list_prepend_function)dlsym(glib, "g_slist_prepend");
    list_append_function slist_append =
        (list_append_function)dlsym(glib, "g_slist_append");
    list_remove_function slist_remove =
        (list_remove_function)dlsym(glib, "g_slist_remove");
    list_length_function slist_length =
        (list_length_function)dlsym(glib, "g_slist_length");
    list_nth_data_function slist_nth_data =
        (list_nth_data_function)dlsym(glib, "g_slist_nth_data");
    list_free_function slist_free =
        (list_free_function)dlsym(glib, "g_slist_free");
    bytes_new_function bytes_new =
        (bytes_new_function)dlsym(glib, "g_bytes_new");
    bytes_ref_function bytes_ref =
        (bytes_ref_function)dlsym(glib, "g_bytes_ref");
    bytes_unref_function bytes_unref =
        (bytes_unref_function)dlsym(glib, "g_bytes_unref");
    bytes_data_function bytes_data =
        (bytes_data_function)dlsym(glib, "g_bytes_get_data");
    bytes_size_function bytes_size =
        (bytes_size_function)dlsym(glib, "g_bytes_get_size");
    bytes_equal_function bytes_equal =
        (bytes_equal_function)dlsym(glib, "g_bytes_equal");
    variant_string_new_function variant_string_new =
        (variant_string_new_function)dlsym(glib, "g_variant_new_string");
    variant_boolean_new_function variant_boolean_new =
        (variant_boolean_new_function)dlsym(glib, "g_variant_new_boolean");
    variant_uint32_new_function variant_uint32_new =
        (variant_uint32_new_function)dlsym(glib, "g_variant_new_uint32");
    variant_int32_new_function variant_int32_new =
        (variant_int32_new_function)dlsym(glib, "g_variant_new_int32");
    variant_uint64_new_function variant_uint64_new =
        (variant_uint64_new_function)dlsym(glib, "g_variant_new_uint64");
    variant_int64_new_function variant_int64_new =
        (variant_int64_new_function)dlsym(glib, "g_variant_new_int64");
    variant_ref_function variant_ref =
        (variant_ref_function)dlsym(glib, "g_variant_ref");
    variant_unref_function variant_unref =
        (variant_unref_function)dlsym(glib, "g_variant_unref");
    variant_string_get_function variant_string_get =
        (variant_string_get_function)dlsym(glib, "g_variant_get_string");
    variant_boolean_get_function variant_boolean_get =
        (variant_boolean_get_function)dlsym(glib, "g_variant_get_boolean");
    variant_uint32_get_function variant_uint32_get =
        (variant_uint32_get_function)dlsym(glib, "g_variant_get_uint32");
    variant_int32_get_function variant_int32_get =
        (variant_int32_get_function)dlsym(glib, "g_variant_get_int32");
    variant_uint64_get_function variant_uint64_get =
        (variant_uint64_get_function)dlsym(glib, "g_variant_get_uint64");
    variant_int64_get_function variant_int64_get =
        (variant_int64_get_function)dlsym(glib, "g_variant_get_int64");
    variant_type_function variant_type =
        (variant_type_function)dlsym(glib, "g_variant_get_type_string");
    variant_is_type_function variant_is_type =
        (variant_is_type_function)dlsym(glib, "g_variant_is_of_type");
    variant_children_new_function variant_new_tuple =
        (variant_children_new_function)dlsym(glib, "g_variant_new_tuple");
    variant_children_new_function variant_new_array =
        (variant_children_new_function)dlsym(glib, "g_variant_new_array");
    variant_children_count_function variant_n_children =
        (variant_children_count_function)dlsym(glib, "g_variant_n_children");
    variant_child_get_function variant_get_child =
        (variant_child_get_function)dlsym(glib, "g_variant_get_child_value");
    variant_format_new_function variant_new_format =
        (variant_format_new_function)dlsym(glib, "g_variant_new");
    variant_dict_new_function variant_new_dict =
        (variant_dict_new_function)dlsym(glib, "g_variant_new_dict_entry");
    variant_dict_child_function variant_get_key =
        (variant_dict_child_function)dlsym(glib, "g_variant_get_key");
    variant_dict_child_function variant_get_value =
        (variant_dict_child_function)dlsym(glib, "g_variant_get_value");
    variant_lookup_function variant_lookup =
        (variant_lookup_function)dlsym(glib, "g_variant_lookup_value");
    variant_wrap_new_function variant_new_variant =
        (variant_wrap_new_function)dlsym(glib, "g_variant_new_variant");
    variant_wrap_get_function variant_get_variant =
        (variant_wrap_get_function)dlsym(glib, "g_variant_get_variant");
    variant_builder_new_function variant_builder_new =
        (variant_builder_new_function)dlsym(glib, "g_variant_builder_new");
    variant_builder_add_value_function variant_builder_add_value =
        (variant_builder_add_value_function)dlsym(glib, "g_variant_builder_add_value");
    variant_builder_add_function variant_builder_add =
        (variant_builder_add_function)dlsym(glib, "g_variant_builder_add");
    variant_builder_end_function variant_builder_end =
        (variant_builder_end_function)dlsym(glib, "g_variant_builder_end");
    variant_builder_clear_function variant_builder_clear =
        (variant_builder_clear_function)dlsym(glib, "g_variant_builder_clear");
    if (!loop_new || !loop_run || !loop_quit || !loop_running || !idle_add ||
        !timeout_add || !context_iteration || !context_default ||
        !context_new || !context_ref || !context_unref ||
        !context_push || !context_pop ||
        !timeout_add_full || !unix_fd_add || !io_channel_unix_new ||
        !io_channel_ref || !io_channel_unref || !io_channel_get_fd ||
        !io_add_watch ||
        !idle_add_full || !source_remove ||
        !g_main_loop_new_api || !g_main_loop_unref_api ||
        !window_new || !window_title ||
        !window_size || !window_show || !window_destroy || !widget_new ||
        !widget_add || !widget_remove || !widget_text || !widget_size ||
        !widget_get_size ||
        !widget_get_visible || !widget_parent || !signal_connect || !signal_emit ||
        !runtime_malloc || !runtime_realloc || !runtime_free || !runtime_strdup ||
        !file_get_contents || !file_set_contents || !file_query_exists ||
        !file_load_async || !file_load_finish) {
        printf("desktop runtime symbol probe failed: loop=%u/%u/%u window=%u/%u/%u/%u "
               "widget=%u/%u/%u/%u/%u signal=%u/%u memory=%u/%u/%u/%u\n",
               loop_new != NULL, loop_run != NULL, loop_quit != NULL,
               window_new != NULL, window_title != NULL, window_size != NULL,
               window_show != NULL, widget_new != NULL, widget_add != NULL,
               widget_text != NULL, widget_size != NULL,
               widget_get_size != NULL, signal_connect != NULL,
               signal_emit != NULL, runtime_malloc != NULL,
               runtime_realloc != NULL, runtime_free != NULL,
               runtime_strdup != NULL);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 501;
    }
    if (!object_set_data || !object_get_data || !object_steal_data ||
        !widget_entry_new || !widget_entry_get_text || !widget_entry_set_text ||
        !widget_check_new || !widget_active_set || !widget_active_get ||
        !widget_grid_new || !widget_grid_attach || !widget_visible_set ||
        !widget_show_all || !widget_sensitive_set || !widget_sensitive_get) {
        puts("desktop runtime extended widget/object symbols missing");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 520;
    }
    if (!application_new || !application_register || !application_hold ||
        !application_release || !application_registered || !application_id ||
        !application_activate || !application_run || !application_quit ||
        !application_window_new) {
        puts("desktop runtime GApplication symbols missing");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 528;
    }
    if (!action_new || !action_ref || !action_unref || !action_name ||
        !action_enabled_set || !action_enabled_get || !action_activate ||
        !action_map_add || !action_map_remove || !action_map_lookup) {
        puts("desktop runtime GAction symbols missing");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 530;
    }
    /* Exercise the bounded GApplication lifecycle used by GTK/GNOME-style
     * session entry points.  Keep this probe on the dlsym boundary so a port
     * cannot accidentally rely on private runtime symbols. */
    void *application = application_new("org.akaos.Runtime", 0U);
    void *application_window = application
        ? application_window_new(application) : NULL;
    void *application_error = (void *)1;
    char application_name[] = "hello";
    char *application_argv[] = {application_name, NULL};
    volatile int application_quit_callback_called = 0;
    struct runtime_loop_quit_probe application_quit_probe = {
        application_quit, application, &application_quit_callback_called
    };
    unsigned application_quit_source = application && idle_add
        ? idle_add(runtime_loop_quit_callback, &application_quit_probe) : 0U;
    int application_lifecycle_ok = application && application_window &&
        application_quit_source != 0U &&
        application_id(application) &&
        strcmp(application_id(application), "org.akaos.Runtime") == 0 &&
        !application_registered(application) &&
        application_register(application, NULL, &application_error) == 1 &&
        application_error == NULL && application_registered(application);
    if (application_lifecycle_ok) {
        application_hold(application);
        application_hold(application);
        application_release(application);
        application_activate(application);
        application_lifecycle_ok = application_run(application, 1,
                                                   application_argv) == 0 &&
            application_registered(application) &&
            application_quit_callback_called;
    }
    if (application_lifecycle_ok) {
        application_quit(application);
        application_lifecycle_ok = !application_registered(application);
    }
    if (application_window) (void)window_destroy(application_window);
    if (application) (void)window_destroy(application);
    if (!application_lifecycle_ok) {
        puts("desktop runtime GApplication lifecycle probe failed");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 529;
    }
    int window_state_ok = test_gtk_window_state(gtk);
    if (!window_state_ok) {
        puts("desktop runtime GTK window-state probe failed");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 532;
    }
    int qt_ok = test_qt_bridge(qt);
    if (!qt_ok) {
        puts("desktop runtime Qt core/widget probe failed");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 534;
    }
    void *action_application = application_new("org.akaos.Actions", 0U);
    void *action = action_new("open", NULL);
    void *action_refd = action ? action_ref(action) : NULL;
    runtime_signal_seen = 0;
    unsigned action_signal = action && signal_connect
        ? signal_connect(action, "activate", runtime_button_callback,
                         (void *)&runtime_signal_seen) : 0U;
    int action_ok = action_application && action && action_refd == action &&
        action_name(action) && strcmp(action_name(action), "open") == 0 &&
        action_enabled_get(action) == 1 && action_map_add(action_application,
                                                           action) == 1 &&
        action_map_lookup(action_application, "open") == action;
    if (action_ok) {
        action_enabled_set(action, 0);
        action_activate(action, NULL);
        action_ok = runtime_signal_seen == 0 && action_enabled_get(action) == 0;
        action_enabled_set(action, 1);
        action_activate(action, NULL);
        action_ok = action_ok && runtime_signal_seen == 1;
    }
    if (action_ok)
        action_ok = action_map_remove(action_application, "open") == 1 &&
            action_map_lookup(action_application, "open") == NULL;
    if (action_signal && signal_disconnect)
        (void)signal_disconnect(action, action_signal);
    if (action_refd) action_unref(action_refd);
    if (action) action_unref(action);
    if (action_application) (void)window_destroy(action_application);
    if (!action_ok) {
        puts("desktop runtime GAction map probe failed");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 531;
    }
    if (!signal_disconnect || !signal_disconnect_by_func) {
        puts("desktop runtime signal-disconnect symbols missing");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 522;
    }
    if (!settings_new || !settings_ref || !settings_unref ||
        !settings_get_value || !settings_set_value || !settings_get_string ||
        !settings_set_string || !settings_get_boolean ||
        !settings_set_boolean || !settings_free || !settings_variant_unref) {
        puts("desktop runtime settings symbols missing");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 523;
    }
    if (!value_init || !value_unset || !value_set_string ||
        !value_get_string || !value_set_boolean || !value_get_boolean ||
        !value_set_int || !value_get_int || !value_set_uint ||
        !value_get_uint || !object_set_property || !object_get_property ||
        !binding_bind || !binding_ref || !binding_unbind || !cancellable_new ||
        !cancellable_ref || !cancellable_unref || !cancellable_cancel ||
        !cancellable_is_cancelled || !error_new || !error_copy ||
        !error_free || !error_matches || !error_domain || !error_code ||
        !error_message || !task_new || !task_ref || !task_unref ||
        !task_source || !task_completed || !task_return_boolean ||
        !task_return_error ||
        !task_return_cancelled || !task_propagate_boolean) {
        puts("desktop runtime GIO/property symbols missing");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 527;
    }
    if (!file_new_path || !file_new_uri || !file_ref || !file_unref ||
        !file_get_path || !file_get_uri || !file_get_basename ||
        !file_get_parent || !file_equal || !file_has_prefix ||
        !file_enumerate || !file_enumerator_ref || !file_enumerator_unref ||
        !file_enumerator_container || !file_enumerator_next ||
        !file_enumerator_close || !file_info_ref || !file_info_unref ||
        !file_info_name || !file_info_display_name || !file_info_type ||
        !file_info_size || !file_info_attribute_uint64 ||
        !file_info_attribute_string) {
        puts("desktop runtime GFile symbols missing");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 525;
    }
    if (!array_new || !array_append || !array_set_size || !array_length ||
        !array_element_size || !array_free || !ptr_array_new || !ptr_array_add ||
        !ptr_array_length || !ptr_array_index || !ptr_array_free || !string_new ||
        !string_append || !string_append_len || !string_truncate || !string_free) {
        puts("desktop runtime GLib collection symbols missing");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 507;
    }
    if (!hash_new || !hash_insert || !hash_lookup || !hash_contains ||
        !hash_remove || !hash_size || !hash_destroy || !hash_string ||
        !hash_equal) {
        puts("desktop runtime hash-table symbols missing");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 509;
    }
    if (!list_prepend || !list_append || !list_remove || !list_length ||
        !list_nth_data || !list_free || !slist_prepend || !slist_append ||
        !slist_remove || !slist_length || !slist_nth_data || !slist_free) {
        puts("desktop runtime list symbols missing");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 511;
    }
    if (!bytes_new || !bytes_ref || !bytes_unref || !bytes_data ||
        !bytes_size || !bytes_equal) {
        puts("desktop runtime bytes symbols missing");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 513;
    }
    if (!variant_string_new || !variant_boolean_new || !variant_uint32_new ||
        !variant_int32_new || !variant_uint64_new || !variant_int64_new ||
        !variant_ref || !variant_unref || !variant_string_get ||
        !variant_boolean_get || !variant_uint32_get || !variant_int32_get ||
        !variant_uint64_get || !variant_int64_get || !variant_type ||
        !variant_is_type) {
        puts("desktop runtime variant symbols missing");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 515;
    }
    if (!variant_new_tuple || !variant_new_array || !variant_n_children ||
        !variant_get_child || !variant_new_format || !variant_new_dict ||
        !variant_get_key || !variant_get_value || !variant_lookup ||
        !variant_new_variant || !variant_get_variant ||
        !variant_builder_new || !variant_builder_add_value ||
        !variant_builder_add || !variant_builder_end || !variant_builder_clear) {
        puts("desktop runtime composite-variant symbols missing");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 517;
    }
    void *memory_probe = runtime_malloc(16U);
    char *string_probe = runtime_strdup("runtime memory");
    int memory_allocated = memory_probe != NULL;
    int string_allocated = string_probe != NULL;
    int memory_zero = memory_probe &&
        ((unsigned char *)memory_probe)[0] == 0U &&
        string_probe != NULL;
    int string_valid = string_probe && string_probe[0] == 'r';
    int memory_ok = memory_allocated && string_allocated && memory_zero &&
        string_valid;
    void *resized_probe = memory_ok ? runtime_realloc(memory_probe, 32U) : NULL;
    int resized = resized_probe != NULL;
    memory_ok = memory_ok && resized;
    runtime_free(resized_probe ? resized_probe : memory_probe);
    runtime_free(string_probe);
    if (!memory_ok) {
        printf("desktop runtime memory probe failed: alloc=%u string=%u zero=%u text=%u realloc=%u\n",
               memory_allocated, string_allocated, memory_zero, string_valid,
               resized);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 506;
    }
    uint32_t collection_values[2] = {17U, 29U};
    void *array = array_new(1, 1, (unsigned)sizeof(uint32_t));
    int array_ok = array && array_append(array, collection_values, 2U) == array &&
        array_length(array) == 2U &&
        array_element_size(array) == sizeof(uint32_t) &&
        array_set_size(array, 3U) == array;
    uint32_t *array_segment = array_ok ? (uint32_t *)array_free(array, 1) : NULL;
    array_ok = array_ok && array_segment && array_segment[0] == 17U &&
        array_segment[1] == 29U && array_segment[2] == 0U;
    if (array_segment) runtime_free(array_segment);
    static const char first_value[] = "first";
    static const char second_value[] = "second";
    void *ptr_array = ptr_array_new();
    int ptr_ok = ptr_array && ptr_array_add(ptr_array, (void *)first_value) &&
        ptr_array_add(ptr_array, (void *)second_value) &&
        ptr_array_length(ptr_array) == 2U &&
        ptr_array_index(ptr_array, 1U) == (void *)second_value;
    void **ptr_segment = ptr_array ? ptr_array_free(ptr_array, 1) : NULL;
    ptr_ok = ptr_ok && ptr_segment && ptr_segment[0] == (void *)first_value &&
        ptr_segment[1] == (void *)second_value;
    if (ptr_segment) runtime_free(ptr_segment);
    void *runtime_string = string_new("aka");
    int string_ok = runtime_string && string_append(runtime_string, "OS") == runtime_string &&
        string_append_len(runtime_string, "-native", 7U) == runtime_string &&
        string_truncate(runtime_string, 5U) == runtime_string;
    char *runtime_string_value = runtime_string ? string_free(runtime_string, 1) : NULL;
    string_ok = string_ok && runtime_string_value &&
        strcmp(runtime_string_value, "akaOS") == 0;
    if (runtime_string_value) runtime_free(runtime_string_value);
    if (!array_ok || !ptr_ok || !string_ok) {
        printf("desktop runtime GLib collections failed: array=%d ptr=%d string=%d\n",
               array_ok, ptr_ok, string_ok);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 508;
    }
    static const char hash_alpha[] = "alpha";
    static const char hash_beta[] = "beta";
    static const char hash_alpha_query[] = "alpha";
    static const char hash_one[] = "one";
    static const char hash_two[] = "two";
    static const char hash_updated[] = "updated";
    void *hash_table = hash_new(hash_string, hash_equal);
    int hash_ok = hash_table && hash_insert(hash_table, (void *)hash_alpha,
                                            (void *)hash_one) &&
        hash_insert(hash_table, (void *)hash_beta, (void *)hash_two) &&
        hash_size(hash_table) == 2U &&
        hash_contains(hash_table, hash_alpha_query) &&
        hash_lookup(hash_table, hash_alpha_query) == (void *)hash_one &&
        hash_insert(hash_table, (void *)hash_alpha, (void *)hash_updated) &&
        hash_size(hash_table) == 2U &&
        hash_lookup(hash_table, hash_alpha_query) == (void *)hash_updated &&
        hash_remove(hash_table, hash_alpha_query) &&
        !hash_contains(hash_table, hash_alpha_query) &&
        hash_size(hash_table) == 1U &&
        !hash_remove(hash_table, hash_alpha_query);
    if (hash_table) hash_destroy(hash_table);
    if (!hash_ok) {
        puts("desktop runtime hash-table probe failed");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 510;
    }
    int list_zero = 0;
    int list_one = 1;
    int list_two = 2;
    void *list = list_append(NULL, &list_one);
    list = list_append(list, &list_two);
    list = list_prepend(list, &list_zero);
    int list_ok = list && list_length(list) == 3U &&
        list_nth_data(list, 0U) == &list_zero &&
        list_nth_data(list, 2U) == &list_two;
    list = list_remove(list, &list_one);
    list_ok = list_ok && list_length(list) == 2U &&
        list_nth_data(list, 1U) == &list_two;
    if (list) list_free(list);
    void *slist = slist_prepend(NULL, &list_zero);
    slist = slist_append(slist, &list_one);
    slist = slist_append(slist, &list_two);
    int slist_ok = slist && slist_length(slist) == 3U &&
        slist_nth_data(slist, 1U) == &list_one;
    slist = slist_remove(slist, &list_zero);
    slist_ok = slist_ok && slist_length(slist) == 2U &&
        slist_nth_data(slist, 0U) == &list_one;
    if (slist) slist_free(slist);
    if (!list_ok || !slist_ok) {
        puts("desktop runtime list probe failed");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 512;
    }
    static const unsigned char bytes_payload[] = {1U, 2U, 3U, 5U};
    unsigned char bytes_source[sizeof(bytes_payload)];
    memcpy(bytes_source, bytes_payload, sizeof(bytes_source));
    void *bytes = bytes_new(bytes_source, sizeof(bytes_source));
    void *bytes_copy = bytes_new(bytes_payload, sizeof(bytes_payload));
    size_t bytes_length = 0U;
    const unsigned char *bytes_view = bytes
        ? (const unsigned char *)bytes_data(bytes, &bytes_length) : NULL;
    int bytes_ok = bytes && bytes_copy && bytes_view &&
        bytes_length == sizeof(bytes_payload) &&
        bytes_size(bytes) == sizeof(bytes_payload) && bytes_view[0] == 1U &&
        bytes_equal(bytes, bytes_copy) && bytes_ref(bytes) == bytes;
    bytes_source[0] = 9U;
    bytes_ok = bytes_ok && bytes_view[0] == 1U;
    if (bytes) {
        bytes_unref(bytes);
        bytes_unref(bytes);
    }
    if (bytes_copy) bytes_unref(bytes_copy);
    if (!bytes_ok) {
        puts("desktop runtime immutable-bytes probe failed");
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 514;
    }
    void *variant_string = variant_string_new("akaOS");
    void *variant_boolean = variant_boolean_new(1);
    void *variant_uint32 = variant_uint32_new(UINT32_C(0x12345678));
    void *variant_int32 = variant_int32_new(-42);
    void *variant_uint64 = variant_uint64_new(UINT64_C(0x1020304050607080));
    void *variant_int64 = variant_int64_new(INT64_C(-9000000000));
    size_t variant_length = 0U;
    const char *variant_text = variant_string
        ? variant_string_get(variant_string, &variant_length) : NULL;
    int variants_ok = variant_string && variant_boolean && variant_uint32 &&
        variant_int32 && variant_uint64 && variant_int64 && variant_text &&
        variant_length == 5U && strcmp(variant_text, "akaOS") == 0 &&
        variant_boolean_get(variant_boolean) == 1 &&
        variant_uint32_get(variant_uint32) == UINT32_C(0x12345678) &&
        variant_int32_get(variant_int32) == -42 &&
        variant_uint64_get(variant_uint64) == UINT64_C(0x1020304050607080) &&
        variant_int64_get(variant_int64) == INT64_C(-9000000000) &&
        variant_is_type(variant_string, "s") &&
        variant_is_type(variant_boolean, "b") &&
        variant_is_type(variant_uint32, "u") &&
        variant_is_type(variant_int32, "i") &&
        variant_is_type(variant_uint64, "t") &&
        variant_is_type(variant_int64, "x") &&
        variant_ref(variant_string) == variant_string;
    if (!variants_ok) {
        puts("desktop runtime typed-variant probe failed");
        if (variant_string) variant_unref(variant_string);
        if (variant_boolean) variant_unref(variant_boolean);
        if (variant_uint32) variant_unref(variant_uint32);
        if (variant_int32) variant_unref(variant_int32);
        if (variant_uint64) variant_unref(variant_uint64);
        if (variant_int64) variant_unref(variant_int64);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 516;
    }
    const void *variant_children[] = {variant_string, variant_uint32};
    void *variant_tuple = variant_new_tuple(variant_children, 2U);
    void *variant_array = variant_new_array(variant_children, 2U);
    void *tuple_child = variant_tuple ? variant_get_child(variant_tuple, 1U) : NULL;
    void *array_child = variant_array ? variant_get_child(variant_array, 0U) : NULL;
    int composite_ok = variant_tuple && variant_array &&
        variant_n_children(variant_tuple) == 2U &&
        variant_n_children(variant_array) == 2U && tuple_child && array_child &&
        variant_uint32_get(tuple_child) == UINT32_C(0x12345678) &&
        variant_is_type(array_child, "s");
    if (tuple_child) variant_unref(tuple_child);
    if (array_child) variant_unref(array_child);
    if (variant_tuple) variant_unref(variant_tuple);
    if (variant_array) variant_unref(variant_array);
    if (!composite_ok) {
        puts("desktop runtime composite-variant probe failed");
        if (variant_string) variant_unref(variant_string);
        if (variant_boolean) variant_unref(variant_boolean);
        if (variant_uint32) variant_unref(variant_uint32);
        if (variant_int32) variant_unref(variant_int32);
        if (variant_uint64) variant_unref(variant_uint64);
        if (variant_int64) variant_unref(variant_int64);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 518;
    }
    void *formatted = variant_new_format("(su)", "akaOS", (unsigned)7U);
    void *formatted_text = formatted ? variant_get_child(formatted, 0U) : NULL;
    void *formatted_number = formatted ? variant_get_child(formatted, 1U) : NULL;
    size_t formatted_length = 0U;
    const char *formatted_value = formatted_text
        ? variant_string_get(formatted_text, &formatted_length) : NULL;
    int format_ok = formatted && formatted_text && formatted_number &&
        variant_n_children(formatted) == 2U && formatted_value &&
        formatted_length == 5U && strcmp(formatted_value, "akaOS") == 0 &&
        variant_uint32_get(formatted_number) == 7U;
    if (formatted_text) variant_unref(formatted_text);
    if (formatted_number) variant_unref(formatted_number);
    if (formatted) variant_unref(formatted);
    if (!format_ok) {
        puts("desktop runtime variant-format probe failed");
        if (variant_string) variant_unref(variant_string);
        if (variant_boolean) variant_unref(variant_boolean);
        if (variant_uint32) variant_unref(variant_uint32);
        if (variant_int32) variant_unref(variant_int32);
        if (variant_uint64) variant_unref(variant_uint64);
        if (variant_int64) variant_unref(variant_int64);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 519;
    }
    void *dict_entry = variant_new_dict(variant_string, variant_uint32);
    void *dict_key = dict_entry ? variant_get_key(dict_entry) : NULL;
    void *dict_value = dict_entry ? variant_get_value(dict_entry) : NULL;
    size_t dict_key_length = 0U;
    const char *dict_key_text = dict_key
        ? variant_string_get(dict_key, &dict_key_length) : NULL;
    int dict_ok = dict_entry && dict_key && dict_value &&
        variant_n_children(dict_entry) == 2U && dict_key_text &&
        dict_key_length == 5U && strcmp(dict_key_text, "akaOS") == 0 &&
        variant_uint32_get(dict_value) == UINT32_C(0x12345678) &&
        variant_is_type(dict_entry, "{**}");
    if (dict_key) variant_unref(dict_key);
    if (dict_value) variant_unref(dict_value);
    const void *dict_children[] = {dict_entry};
    void *dict_array = dict_entry
        ? variant_new_array(dict_children, 1U) : NULL;
    void *dict_lookup = dict_array
        ? variant_lookup(dict_array, "akaOS", "u") : NULL;
    int lookup_ok = dict_array && dict_lookup &&
        variant_uint32_get(dict_lookup) == UINT32_C(0x12345678);
    if (dict_lookup) variant_unref(dict_lookup);
    if (dict_array) variant_unref(dict_array);
    void *wrapped = variant_new_variant(variant_uint32);
    void *unwrapped = wrapped ? variant_get_variant(wrapped) : NULL;
    int variant_wrap_ok = wrapped && unwrapped &&
        variant_is_type(wrapped, "v") &&
        variant_uint32_get(unwrapped) == UINT32_C(0x12345678);
    if (unwrapped) variant_unref(unwrapped);
    if (wrapped) variant_unref(wrapped);
    void *builder = variant_builder_new("a{sv}");
    int builder_value_ok = builder && dict_entry &&
        variant_builder_add_value(builder, dict_entry);
    int builder_format_ok = builder &&
        variant_builder_add(builder, "{sv}", "mode", variant_uint32);
    void *built = builder ? variant_builder_end(builder) : NULL;
    void *built_lookup = built ? variant_lookup(built, "mode", "u") : NULL;
    void *built_wrapped = built ? variant_lookup(built, "mode", "v") : NULL;
    void *built_value = built_wrapped
        ? variant_get_variant(built_wrapped) : NULL;
    int builder_ok = builder_value_ok && builder_format_ok && built &&
        variant_n_children(built) == 2U && built_lookup == NULL &&
        built_wrapped && built_value &&
        variant_uint32_get(built_value) == UINT32_C(0x12345678);
    if (built_lookup) variant_unref(built_lookup);
    if (built_value) variant_unref(built_value);
    if (built_wrapped) variant_unref(built_wrapped);
    if (built) variant_unref(built);
    if (!dict_ok || !lookup_ok || !variant_wrap_ok || !builder_ok) {
        puts("desktop runtime variant-dictionary/builder probe failed");
        if (dict_entry) variant_unref(dict_entry);
        if (variant_string) variant_unref(variant_string);
        if (variant_boolean) variant_unref(variant_boolean);
        if (variant_uint32) variant_unref(variant_uint32);
        if (variant_int32) variant_unref(variant_int32);
        if (variant_uint64) variant_unref(variant_uint64);
        if (variant_int64) variant_unref(variant_int64);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 521;
    }
    void *settings = settings_new("org.akaos.desktop");
    void *settings_refd = settings ? settings_ref(settings) : NULL;
    int settings_string_set = settings &&
        settings_set_string(settings, "theme", "Aurora");
    char *settings_string = settings_string_set
        ? settings_get_string(settings, "theme") : NULL;
    int settings_bool_set = settings &&
        settings_set_boolean(settings, "animations", 1);
    int settings_bool_ok = settings_bool_set &&
        settings_get_boolean(settings, "animations") == 1;
    int settings_value_set = settings &&
        settings_set_value(settings, "count", variant_uint32);
    void *settings_value = settings_value_set
        ? settings_get_value(settings, "count") : NULL;
    int settings_value_ok = settings_value && variant_is_type(settings_value, "u") &&
        variant_uint32_get(settings_value) == UINT32_C(0x12345678);
    int settings_ok = settings && settings_refd == settings &&
        settings_string && strcmp(settings_string, "Aurora") == 0 &&
        settings_bool_ok && settings_value_ok;
    if (settings_string) settings_free(settings_string);
    if (settings_value) settings_variant_unref(settings_value);
    if (settings_refd) settings_unref(settings_refd);
    if (settings) settings_unref(settings);
    if (!settings_ok) {
        puts("desktop runtime GSettings probe failed");
        if (dict_entry) variant_unref(dict_entry);
        if (variant_string) variant_unref(variant_string);
        if (variant_boolean) variant_unref(variant_boolean);
        if (variant_uint32) variant_unref(variant_uint32);
        if (variant_int32) variant_unref(variant_int32);
        if (variant_uint64) variant_unref(variant_uint64);
        if (variant_int64) variant_unref(variant_int64);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 524;
    }
    void *file = file_new_path("/etc/akaos.conf");
    void *file_uri = file_new_uri("file:///etc/akaos.conf");
    void *file_prefix = file_new_path("/etc");
    void *file_parent = file ? file_get_parent(file) : NULL;
    char *file_path_text = file ? file_get_path(file) : NULL;
    char *file_uri_text = file ? file_get_uri(file) : NULL;
    char *file_base_text = file ? file_get_basename(file) : NULL;
    char *parent_path_text = file_parent ? file_get_path(file_parent) : NULL;
    int file_ok = file && file_uri && file_prefix && file_parent &&
        file_path_text && file_uri_text && file_base_text && parent_path_text &&
        strcmp(file_path_text, "/etc/akaos.conf") == 0 &&
        strcmp(file_uri_text, "file:///etc/akaos.conf") == 0 &&
        strcmp(file_base_text, "akaos.conf") == 0 &&
        strcmp(parent_path_text, "/etc") == 0 &&
        file_equal(file, file_uri) && file_has_prefix(file, file_prefix) &&
        file_ref(file) == file;
    if (file_path_text) runtime_free(file_path_text);
    if (file_uri_text) runtime_free(file_uri_text);
    if (file_base_text) runtime_free(file_base_text);
    if (parent_path_text) runtime_free(parent_path_text);
    if (file_parent) file_unref(file_parent);
    if (file_prefix) file_unref(file_prefix);
    if (file_uri) file_unref(file_uri);
    if (file) {
        file_unref(file);
        file_unref(file);
    }
    if (!file_ok) {
        puts("desktop runtime GFile probe failed");
        if (dict_entry) variant_unref(dict_entry);
        if (variant_string) variant_unref(variant_string);
        if (variant_boolean) variant_unref(variant_boolean);
        if (variant_uint32) variant_unref(variant_uint32);
        if (variant_int32) variant_unref(variant_int32);
        if (variant_uint64) variant_unref(variant_uint64);
        if (variant_int64) variant_unref(variant_int64);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 526;
    }
    void *directory = file_new_path("/etc");
    void *enumerator_error = NULL;
    void *enumerator = directory
        ? file_enumerate(directory, "standard::name,standard::size", 0,
                         NULL, &enumerator_error) : NULL;
    int directory_ok = directory && enumerator &&
        file_enumerator_container(enumerator) == directory;
    void *enumerator_ref = enumerator && file_enumerator_ref
        ? file_enumerator_ref(enumerator) : NULL;
    directory_ok = directory_ok && enumerator_ref == enumerator;
    if (enumerator_ref) file_enumerator_unref(enumerator_ref);
    int saw_motd = 0;
    int saw_config = 0;
    size_t directory_entries = 0U;
    while (directory_ok && directory_entries < 64U) {
        void *info_error = NULL;
        void *info = file_enumerator_next(enumerator, NULL, &info_error);
        if (!info) {
            if (info_error) {
                directory_ok = 0;
                error_free(info_error);
            }
            break;
        }
        const char *name = file_info_name(info);
        const char *display_name = file_info_display_name(info);
        const char *attribute_name = file_info_attribute_string(
            info, "standard::name");
        uint64_t size = file_info_size(info);
        uint64_t attribute_size = file_info_attribute_uint64(
            info, "standard::size");
        directory_ok = directory_ok && name && display_name &&
            attribute_name && strcmp(name, display_name) == 0 &&
            strcmp(name, attribute_name) == 0 &&
            file_info_type(info) >= 1 && file_info_type(info) <= 4 &&
            size == attribute_size;
        if (name && strcmp(name, "motd") == 0) saw_motd = 1;
        if (name && strcmp(name, "akaos.conf") == 0) saw_config = 1;
        void *info_ref = file_info_ref(info);
        directory_ok = directory_ok && info_ref == info;
        if (info_ref) file_info_unref(info_ref);
        file_info_unref(info);
        ++directory_entries;
    }
    if (enumerator) {
        void *close_error = NULL;
        directory_ok = directory_ok &&
            file_enumerator_close(enumerator, NULL, &close_error) == 1;
        if (close_error) error_free(close_error);
        file_enumerator_unref(enumerator);
    }
    if (enumerator_error) error_free(enumerator_error);
    if (directory) file_unref(directory);
    directory_ok = directory_ok && saw_motd && saw_config &&
        directory_entries >= 3U;
    if (directory_ok) puts("GIO directory enumeration verified");
    static const char gio_test_path[] = "/root/.gio-runtime-test";
    static const char gio_test_payload[] = "gio-file-content";
    (void)unlink(gio_test_path);
    void *gio_error = NULL;
    char *motd_contents = NULL;
    size_t motd_length = 0U;
    int gio_motd_ok = file_get_contents("/etc/motd", &motd_contents,
                                       &motd_length, &gio_error);
    gio_motd_ok = gio_motd_ok && motd_contents &&
        motd_length == sizeof(expected_motd) - 1U &&
        strcmp(motd_contents, expected_motd) == 0;
    if (motd_contents) runtime_free(motd_contents);
    if (gio_error) {
        error_free(gio_error);
        gio_error = NULL;
    }
    int gio_write_ok = file_set_contents(gio_test_path, gio_test_payload, -1,
                                         &gio_error);
    if (gio_error) {
        error_free(gio_error);
        gio_error = NULL;
    }
    char *gio_contents = NULL;
    size_t gio_length = 0U;
    int gio_read_ok = file_get_contents(gio_test_path, &gio_contents,
                                        &gio_length, &gio_error);
    int gio_content_ok = gio_read_ok && gio_contents &&
        gio_length == sizeof(gio_test_payload) - 1U &&
        strcmp(gio_contents, gio_test_payload) == 0;
    if (gio_contents) runtime_free(gio_contents);
    if (gio_error) {
        error_free(gio_error);
        gio_error = NULL;
    }
    void *gio_file = file_new_path(gio_test_path);
    int gio_exists_ok = gio_file && file_query_exists(gio_file, NULL);
    struct runtime_file_async_probe gio_async_probe = {0, NULL};
    if (gio_file)
        file_load_async(gio_file, NULL, runtime_file_async_callback,
                        (void *)&gio_async_probe);
    char *gio_async_contents = NULL;
    size_t gio_async_length = 0U;
    char *gio_async_etag = NULL;
    void *gio_async_error = NULL;
    int gio_async_ok = gio_async_probe.callback_seen && gio_async_probe.result &&
        file_load_finish(gio_file, gio_async_probe.result,
                         &gio_async_contents, &gio_async_length,
                         &gio_async_etag, &gio_async_error);
    gio_async_ok = gio_async_ok && gio_async_contents &&
        gio_async_length == sizeof(gio_test_payload) - 1U &&
        strcmp(gio_async_contents, gio_test_payload) == 0 &&
        gio_async_etag == NULL;
    if (gio_async_contents) runtime_free(gio_async_contents);
    if (gio_async_etag) runtime_free(gio_async_etag);
    if (gio_async_error) error_free(gio_async_error);
    if (gio_file) file_unref(gio_file);
    (void)unlink(gio_test_path);
    int gio_file_ok = gio_motd_ok && gio_write_ok && gio_content_ok &&
        gio_exists_ok && gio_async_ok && directory_ok;
    if (gio_file_ok) puts("GIO file content I/O verified");
    if (dict_entry) variant_unref(dict_entry);
    if (variant_string) {
        variant_unref(variant_string);
        variant_unref(variant_string);
    }
    if (variant_boolean) variant_unref(variant_boolean);
    if (variant_uint32) variant_unref(variant_uint32);
    if (variant_int32) variant_unref(variant_int32);
    if (variant_uint64) variant_unref(variant_uint64);
    if (variant_int64) variant_unref(variant_int64);
    void *loop = loop_new();
    void *window = window_new(0);
    if (!loop || !window) {
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 502;
    }
    window_title(window, "akaOS runtime test");
    window_size(window, 320, 200);
    window_show(window);
    volatile int loop_callback_called = 0;
    struct runtime_loop_quit_probe loop_probe = {
        loop_quit, loop, &loop_callback_called
    };
    unsigned loop_source = idle_add(runtime_loop_quit_callback, &loop_probe);
    if (loop_source == 0U) {
        (void)window_destroy(window);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 502;
    }
    loop_run(loop);
    if (!loop_callback_called || loop_running(loop) != 0 ||
        !window_destroy(window)) {
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 503;
    }
    /* The GLib-shaped constructor must return before dispatch so a caller can
     * register its first source.  Reuse the quit probe to verify that wrapper
     * path and the requested-running state do not consume the handle early. */
    void *default_context = context_default();
    void *retained_context = default_context ? context_ref(default_context) : NULL;
    if (!default_context || retained_context != default_context ||
        context_iteration(default_context, 0) != 0) {
        if (retained_context) context_unref(retained_context);
        (void)window_destroy(window);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 504;
    }
    context_unref(retained_context);
    /* Sources must belong to the context selected by the caller.  Register a
     * descriptor while an explicit thread-default context is pushed, prove
     * that the process-wide default cannot consume it, then dispatch it from
     * the owning context. */
    void *isolated_context = context_new ? context_new() : NULL;
    int isolated_descriptors[2] = {-1, -1};
    struct runtime_fd_probe isolated_probe = {0, 0U};
    unsigned isolated_source = 0U;
    int isolated_default_dispatch = 0;
    int isolated_owner_dispatch = 0;
    char isolated_received = 0;
    const char isolated_marker = 'I';
    if (isolated_context && pipe(isolated_descriptors) == 0 &&
        write(isolated_descriptors[1], &isolated_marker, 1) == 1) {
        context_push(isolated_context);
        isolated_source = unix_fd_add(isolated_descriptors[0], POLLIN,
                                       runtime_fd_callback, &isolated_probe);
        context_pop(isolated_context);
        if (isolated_source)
            isolated_default_dispatch = context_iteration(default_context, 0);
        if (isolated_source && !isolated_probe.called)
            isolated_owner_dispatch = context_iteration(isolated_context, 1);
        if (isolated_descriptors[0] >= 0)
            (void)read(isolated_descriptors[0], &isolated_received, 1);
    }
    if (isolated_descriptors[0] >= 0) close(isolated_descriptors[0]);
    if (isolated_descriptors[1] >= 0) close(isolated_descriptors[1]);
    int isolated_context_ok = isolated_context && isolated_source &&
        isolated_default_dispatch == 0 && isolated_owner_dispatch == 1 &&
        isolated_probe.called && (isolated_probe.condition & POLLIN) != 0U &&
        isolated_received == isolated_marker;
    if (isolated_source && !isolated_context_ok && source_remove)
        (void)source_remove(isolated_source);
    if (isolated_context) context_unref(isolated_context);
    if (isolated_context_ok)
        puts("GLib per-context source ownership verified");
    if (!isolated_context_ok) {
        (void)window_destroy(window);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 511;
    }
    int fd_descriptors[2] = {-1, -1};
    struct runtime_fd_probe fd_probe = {0, 0U};
    const char fd_marker = 'F';
    unsigned fd_source = 0U;
    int fd_iteration = 0;
    if (pipe(fd_descriptors) == 0 &&
        write(fd_descriptors[1], &fd_marker, 1) == 1)
        fd_source = unix_fd_add(fd_descriptors[0], POLLIN,
                                runtime_fd_callback, &fd_probe);
    if (fd_source) fd_iteration = context_iteration(default_context, 1);
    char fd_received = 0;
    int fd_read_ok = fd_descriptors[0] >= 0 &&
        read(fd_descriptors[0], &fd_received, 1) == 1 &&
        fd_received == fd_marker;
    if (fd_descriptors[0] >= 0) close(fd_descriptors[0]);
    if (fd_descriptors[1] >= 0) close(fd_descriptors[1]);
    int fd_source_ok = fd_source && fd_iteration == 1 && fd_probe.called &&
                       (fd_probe.condition & POLLIN) != 0U && fd_read_ok;
    if (fd_source_ok) puts("GLib descriptor source readiness verified");
    if (!fd_source_ok) {
        (void)window_destroy(window);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 506;
    }
    int io_descriptors[2] = {-1, -1};
    struct runtime_io_probe io_probe = {0, 0U, NULL};
    const char io_marker = 'G';
    void *io_channel = NULL;
    void *io_retained = NULL;
    unsigned io_source = 0U;
    int io_iteration = 0;
    if (pipe(io_descriptors) == 0)
        io_channel = io_channel_unix_new(io_descriptors[0]);
    if (io_channel) io_retained = io_channel_ref(io_channel);
    if (io_channel && io_retained == io_channel &&
        io_channel_get_fd(io_channel) == io_descriptors[0] &&
        write(io_descriptors[1], &io_marker, 1) == 1) {
        io_source = io_add_watch(io_channel, POLLIN, runtime_io_callback,
                                 &io_probe);
        if (io_source) io_iteration = context_iteration(default_context, 1);
    }
    char io_received = 0;
    int io_read_ok = io_descriptors[0] >= 0 &&
        read(io_descriptors[0], &io_received, 1) == 1 &&
        io_received == io_marker;
    if (io_channel && io_retained) io_channel_unref(io_retained);
    if (io_channel) io_channel_unref(io_channel);
    if (io_descriptors[0] >= 0) close(io_descriptors[0]);
    if (io_descriptors[1] >= 0) close(io_descriptors[1]);
    int io_source_ok = io_source && io_iteration == 1 && io_probe.called &&
                       (io_probe.condition & POLLIN) != 0U && io_read_ok &&
                       io_probe.channel != NULL;
    if (io_source_ok) puts("GIOChannel descriptor watch verified");
    if (!io_source_ok) {
        (void)window_destroy(window);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 507;
    }
    /* Drive a real native D-Bus transaction through the same descriptor
     * source used by GLib/GIO clients.  The connect/bootstrap remains
     * synchronous, but request completion is entirely event-loop driven. */
    int async_dbus_descriptor = akaos_dbus_connect(0);
    struct runtime_dbus_async_probe async_dbus_probe;
    memset(&async_dbus_probe, 0, sizeof(async_dbus_probe));
    unsigned async_dbus_source = 0U;
    int async_dbus_iterations = 0;
    if (async_dbus_descriptor >= 0 &&
        akaos_dbus_async_ping_init(&async_dbus_probe.ping,
                                   async_dbus_descriptor) == 0) {
        async_dbus_source = unix_fd_add(async_dbus_descriptor, POLLIN,
                                        runtime_dbus_async_callback,
                                        &async_dbus_probe);
        for (int iteration = 0;
             async_dbus_source && iteration < 8 &&
             !akaos_dbus_async_ping_done(&async_dbus_probe.ping);
             ++iteration) {
            async_dbus_iterations += context_iteration(default_context, 1);
        }
    }
    int async_dbus_ok = async_dbus_source && async_dbus_iterations > 0 &&
        async_dbus_probe.called && async_dbus_probe.result == 1 &&
        akaos_dbus_async_ping_done(&async_dbus_probe.ping);
    if (async_dbus_source && !async_dbus_ok && source_remove)
        (void)source_remove(async_dbus_source);
    if (async_dbus_descriptor >= 0)
        (void)akaos_dbus_close(async_dbus_descriptor);
    if (async_dbus_ok) puts("D-Bus asynchronous descriptor watch verified");
    if (!async_dbus_ok) {
        printf("D-Bus async watch state: fd=%d source=%u iterations=%d "
               "called=%d result=%d done=%d errno=%d\n",
               async_dbus_descriptor, async_dbus_source,
               async_dbus_iterations, async_dbus_probe.called,
               async_dbus_probe.result,
               akaos_dbus_async_ping_done(&async_dbus_probe.ping), errno);
        (void)window_destroy(window);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 508;
    }
    int async_binary_descriptor = akaos_dbus_binary_connect();
    struct runtime_dbus_binary_async_probe async_binary_probe;
    memset(&async_binary_probe, 0, sizeof(async_binary_probe));
    unsigned async_binary_source = 0U;
    int async_binary_iterations = 0;
    if (async_binary_descriptor >= 0 &&
        akaos_dbus_async_binary_ping_init(&async_binary_probe.ping,
                                          async_binary_descriptor) == 0) {
        async_binary_source = unix_fd_add(async_binary_descriptor, POLLIN,
                                          runtime_dbus_binary_async_callback,
                                          &async_binary_probe);
        for (int iteration = 0;
             async_binary_source && iteration < 8 &&
             !akaos_dbus_async_binary_ping_done(&async_binary_probe.ping);
             ++iteration) {
            async_binary_iterations += context_iteration(default_context, 1);
        }
    }
    int async_binary_ok = async_binary_source && async_binary_iterations > 0 &&
        async_binary_probe.called && async_binary_probe.result == 1 &&
        akaos_dbus_async_binary_ping_done(&async_binary_probe.ping);
    if (async_binary_source && !async_binary_ok && source_remove)
        (void)source_remove(async_binary_source);
    if (async_binary_descriptor >= 0)
        (void)akaos_dbus_close(async_binary_descriptor);
    if (async_binary_ok)
        puts("D-Bus asynchronous binary descriptor watch verified");
    if (!async_binary_ok) {
        (void)window_destroy(window);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 509;
    }
    int async_queue_descriptor = akaos_dbus_binary_connect();
    struct runtime_dbus_queue_probe async_queue_probe;
    memset(&async_queue_probe, 0, sizeof(async_queue_probe));
    uint32_t async_queue_ping_serial = 0U;
    uint32_t async_queue_id_serial = 0U;
    unsigned async_queue_source = 0U;
    int async_queue_iterations = 0;
    int async_queue_setup = async_queue_descriptor >= 0 &&
        akaos_dbus_async_client_init(&async_queue_probe.client,
                                     async_queue_descriptor) == 0;
    if (async_queue_setup) {
        async_queue_setup =
            akaos_dbus_async_client_queue_call(
                &async_queue_probe.client, "org.freedesktop.DBus",
                "/org/freedesktop/DBus", "org.freedesktop.DBus", "Ping",
                NULL, NULL, 0U, &async_queue_ping_serial) == 0 &&
            akaos_dbus_async_client_queue_call(
                &async_queue_probe.client, "org.freedesktop.DBus",
                "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetId",
                NULL, NULL, 0U, &async_queue_id_serial) == 0;
    }
    if (async_queue_setup)
        async_queue_source = unix_fd_add(
            async_queue_descriptor, POLLIN, runtime_dbus_queue_callback,
            &async_queue_probe);
    for (int iteration = 0;
         async_queue_source && iteration < 12 &&
         akaos_dbus_async_client_pending(&async_queue_probe.client) != 0U;
         ++iteration)
        async_queue_iterations += context_iteration(default_context, 1);
    unsigned char async_queue_id_body[64];
    size_t async_queue_ping_length = 0U;
    size_t async_queue_id_length = 0U;
    int async_queue_ping_error = 0;
    int async_queue_id_error = 0;
    int async_queue_ping_ok = async_queue_setup &&
        akaos_dbus_async_client_take_reply(
            &async_queue_probe.client, async_queue_ping_serial, NULL, 0U,
            &async_queue_ping_length, &async_queue_ping_error) == 1 &&
        async_queue_ping_length == 0U && !async_queue_ping_error;
    int async_queue_id_ok = async_queue_setup &&
        akaos_dbus_async_client_take_reply(
            &async_queue_probe.client, async_queue_id_serial,
            async_queue_id_body, sizeof(async_queue_id_body),
            &async_queue_id_length, &async_queue_id_error) == 1 &&
        async_queue_id_length >= 5U && async_queue_id_body[4] != 0U &&
        async_queue_id_body[async_queue_id_length - 1U] == 0U &&
        !async_queue_id_error;
    int async_queue_ok = async_queue_source && async_queue_iterations > 0 &&
        async_queue_probe.called >= 2 && !async_queue_probe.failed &&
        async_queue_ping_ok && async_queue_id_ok &&
        akaos_dbus_async_client_pending(&async_queue_probe.client) == 0U;
    if (async_queue_source && !async_queue_ok && source_remove)
        (void)source_remove(async_queue_source);
    if (async_queue_ok)
        puts("D-Bus asynchronous request queue verified");
    if (!async_queue_ok) {
        if (async_queue_descriptor >= 0)
            (void)akaos_dbus_close(async_queue_descriptor);
        (void)window_destroy(window);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 510;
    }
    uint32_t async_cancel_serial = 0U;
    size_t async_cancel_length = 0U;
    int async_cancel_error = 0;
    int async_cancel_queued =
        akaos_dbus_async_client_queue_call(
            &async_queue_probe.client, "org.freedesktop.DBus",
            "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetId",
            NULL, NULL, 0U, &async_cancel_serial) == 0;
    int async_cancelled = async_cancel_queued &&
        akaos_dbus_async_client_cancel(
            &async_queue_probe.client, async_cancel_serial) == 1;
    errno = 0;
    int async_cancel_take = async_cancelled
        ? akaos_dbus_async_client_take_reply(
            &async_queue_probe.client, async_cancel_serial, NULL, 0U,
            &async_cancel_length, &async_cancel_error)
        : 0;
    int async_cancel_ok = async_cancelled && async_cancel_take == -1 &&
        errno == ECANCELED && async_cancel_length == 0U &&
        !async_cancel_error;
    if (async_cancel_ok)
        puts("D-Bus asynchronous cancellation lifecycle verified");
    if (!async_cancel_ok) {
        (void)akaos_dbus_async_client_close(&async_queue_probe.client);
        (void)window_destroy(window);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 512;
    }
    /* Reconnect is an explicit lifecycle boundary: old serials are dropped,
     * the authenticated descriptor is replaced, and a fresh watch must be
     * attached before new requests can complete. */
    uint32_t async_reconnect_serial = 0U;
    unsigned async_reconnect_source = 0U;
    int async_reconnect_iterations = 0;
    int async_reconnect_ok = 0;
    async_reconnect_ok =
        akaos_dbus_async_client_reconnect(&async_queue_probe.client) == 0;
    if (async_reconnect_ok) {
        async_queue_probe.called = 0;
        async_queue_probe.failed = 0;
        async_reconnect_ok =
            akaos_dbus_async_client_queue_call(
                &async_queue_probe.client, "org.freedesktop.DBus",
                "/org/freedesktop/DBus", "org.freedesktop.DBus", "Ping",
                NULL, NULL, 0U, &async_reconnect_serial) == 0;
    }
    if (async_reconnect_ok)
        async_reconnect_source = unix_fd_add(
            async_queue_probe.client.descriptor, POLLIN,
            runtime_dbus_queue_callback, &async_queue_probe);
    for (int iteration = 0;
         async_reconnect_source && iteration < 8 &&
         akaos_dbus_async_client_pending(&async_queue_probe.client) != 0U;
         ++iteration)
        async_reconnect_iterations += context_iteration(default_context, 1);
    size_t async_reconnect_length = 0U;
    int async_reconnect_error = 0;
    async_reconnect_ok = async_reconnect_ok && async_reconnect_source &&
        async_reconnect_iterations > 0 && !async_queue_probe.failed &&
        akaos_dbus_async_client_take_reply(
            &async_queue_probe.client, async_reconnect_serial, NULL, 0U,
            &async_reconnect_length, &async_reconnect_error) == 1 &&
        async_reconnect_length == 0U && !async_reconnect_error &&
        akaos_dbus_async_client_pending(&async_queue_probe.client) == 0U;
    if (async_reconnect_source && !async_reconnect_ok && source_remove)
        (void)source_remove(async_reconnect_source);
    if (async_reconnect_ok)
        puts("D-Bus asynchronous reconnect verified");
    if (!async_reconnect_ok) {
        (void)window_destroy(window);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 511;
    }
    /* Exercise the bounded retry policy from a deliberately closed client.
     * Retry re-authenticates a fresh descriptor and never resurrects the
     * previous connection's serial or reply state. */
    uint32_t async_retry_serial = 0U;
    unsigned async_retry_source = 0U;
    int async_retry_iterations = 0;
    int async_retry_ok =
        akaos_dbus_async_client_close(&async_queue_probe.client) == 0 &&
        akaos_dbus_async_client_retry(&async_queue_probe.client, 2U) == 0;
    if (async_retry_ok) {
        async_queue_probe.called = 0;
        async_queue_probe.failed = 0;
        async_retry_ok =
            akaos_dbus_async_client_queue_call(
                &async_queue_probe.client, "org.freedesktop.DBus",
                "/org/freedesktop/DBus", "org.freedesktop.DBus", "Ping",
                NULL, NULL, 0U, &async_retry_serial) == 0;
    }
    if (async_retry_ok)
        async_retry_source = unix_fd_add(
            async_queue_probe.client.descriptor, POLLIN,
            runtime_dbus_queue_callback, &async_queue_probe);
    for (int iteration = 0;
         async_retry_source && iteration < 8 &&
         akaos_dbus_async_client_pending(&async_queue_probe.client) != 0U;
         ++iteration)
        async_retry_iterations += context_iteration(default_context, 1);
    size_t async_retry_length = 0U;
    int async_retry_error = 0;
    async_retry_ok = async_retry_ok && async_retry_source &&
        async_retry_iterations > 0 && !async_queue_probe.failed &&
        akaos_dbus_async_client_take_reply(
            &async_queue_probe.client, async_retry_serial, NULL, 0U,
            &async_retry_length, &async_retry_error) == 1 &&
        async_retry_length == 0U && !async_retry_error &&
        akaos_dbus_async_client_pending(&async_queue_probe.client) == 0U;
    if (async_retry_source && !async_retry_ok && source_remove)
        (void)source_remove(async_retry_source);
    if (async_retry_ok)
        puts("D-Bus bounded retry policy verified");
    if (!async_retry_ok) {
        (void)akaos_dbus_async_client_close(&async_queue_probe.client);
        (void)window_destroy(window);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 514;
    }
    int async_close_ok =
        akaos_dbus_async_client_close(&async_queue_probe.client) == 0 &&
        async_queue_probe.client.descriptor < 0 &&
        akaos_dbus_async_client_pending(&async_queue_probe.client) == 0U;
    if (async_close_ok)
        puts("D-Bus asynchronous cancellation and close lifecycle verified");
    if (!async_close_ok) {
        (void)window_destroy(window);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 513;
    }
    void *gmain_loop = g_main_loop_new_api(default_context, 1);
    volatile int gmain_callback_called = 0;
    struct runtime_loop_quit_probe gmain_probe = {
        loop_quit, gmain_loop, &gmain_callback_called
    };
    unsigned gmain_source = gmain_loop && idle_add
        ? idle_add(runtime_loop_quit_callback, &gmain_probe) : 0U;
    if (!gmain_loop || !gmain_source || !loop_running(gmain_loop) ||
        !loop_run || (loop_run(gmain_loop), !gmain_callback_called) ||
        loop_running(gmain_loop) != 0) {
        if (gmain_loop) g_main_loop_unref_api(gmain_loop);
        (void)window_destroy(window);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 504;
    }
    g_main_loop_unref_api(gmain_loop);
    void *timed_loop = g_main_loop_new_api(default_context, 0);
    volatile int timeout_called = 0;
    struct runtime_loop_quit_probe timeout_probe = {
        loop_quit, timed_loop, &timeout_called
    };
    unsigned timeout_source = timed_loop
        ? timeout_add(3U, runtime_loop_quit_callback, &timeout_probe) : 0U;
    int timeout_first = context_iteration(NULL, 0);
    int timeout_before = timeout_called;
    int timeout_second = context_iteration(NULL, 0);
    int timeout_mid = timeout_called;
    int timeout_third = context_iteration(NULL, 0);
    if (!timed_loop || !timeout_source || timeout_first != 0 ||
        timeout_second != 0 || timeout_before != 0 || timeout_mid != 0 ||
        timeout_third == 0 || !timeout_called) {
        if (timed_loop) g_main_loop_unref_api(timed_loop);
        (void)window_destroy(window);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 505;
    }
    g_main_loop_unref_api(timed_loop);
    struct runtime_source_destroy_probe timeout_full_probe = {0, 0};
    unsigned timeout_full_source = timeout_add_full
        ? timeout_add_full(0, 0U, runtime_source_once_callback,
                           (void *)&timeout_full_probe,
                           runtime_source_destroy_callback)
        : 0U;
    int timeout_full_iteration = timeout_full_source && context_iteration
        ? context_iteration(NULL, 0) : 0;
    int timeout_full_ok = timeout_full_source && timeout_full_iteration == 1 &&
        timeout_full_probe.callback_called == 1 &&
        timeout_full_probe.destroy_called == 1;
    struct runtime_source_destroy_probe idle_full_probe = {0, 0};
    unsigned idle_full_source = idle_add_full
        ? idle_add_full(0, runtime_source_once_callback,
                        (void *)&idle_full_probe,
                        runtime_source_destroy_callback)
        : 0U;
    int idle_full_removed = idle_full_source && source_remove
        ? source_remove(idle_full_source) : 0;
    int idle_full_ok = idle_full_source && idle_full_removed == 1 &&
        idle_full_probe.callback_called == 0 &&
        idle_full_probe.destroy_called == 1;
    int source_full_ok = timeout_full_ok && idle_full_ok;
    if (source_full_ok) puts("GLib source destroy-notify lifecycle verified");
    void *box = widget_new(2U);
    void *label = widget_new(3U);
    void *button = widget_new(4U);
    int widget_width = 0;
    int widget_height = 0;
    runtime_signal_seen = 0;
    int have_widgets = box && label && button;
    int label_ok = have_widgets && widget_text(label, "Runtime label");
    int button_text_ok = have_widgets && widget_text(button, "Activate");
    int button_size_ok = have_widgets && widget_size(button, 160, 48);
    int label_add_ok = have_widgets && widget_add(box, label);
    int button_add_ok = have_widgets && widget_add(box, button);
    int parent_ok = have_widgets && widget_parent(button) == box;
    int visible_ok = have_widgets && widget_get_visible(button) == 0;
    int got_size = have_widgets && widget_get_size(button, &widget_width,
                                                    &widget_height);
    int dimensions_ok = got_size && widget_width == 160 && widget_height == 48;
    void *entry = widget_entry_new();
    void *check = widget_check_new("Accept");
    void *grid = widget_grid_new();
    void *grid_label = widget_new(3U);
    int entry_ok = entry && widget_entry_set_text && widget_entry_get_text &&
        (widget_entry_set_text(entry, "input"),
         widget_entry_get_text(entry) &&
         strcmp(widget_entry_get_text(entry), "input") == 0);
    int check_ok = check && widget_active_set && widget_active_get &&
        (widget_active_set(check, 1), widget_active_get(check) == 1);
    int extended_add_ok = have_widgets && entry && check &&
        widget_add(box, entry) && widget_add(box, check);
    int show_all_ok = have_widgets && extended_add_ok && widget_show_all &&
        (widget_show_all(box), widget_get_visible(box) == 1 &&
         widget_get_visible(label) == 1 && widget_get_visible(button) == 1 &&
         widget_get_visible(entry) == 1 && widget_get_visible(check) == 1);
    int remove_readd_ok = have_widgets && widget_remove && widget_parent &&
        widget_remove(box, label) == 1 && widget_parent(label) == NULL &&
        widget_add(box, label) == 1 && widget_parent(label) == box;
    if (remove_readd_ok) puts("GTK container detach/reparent lifecycle verified");
    int grid_ok = grid && grid_label;
    if (grid_ok) {
        widget_grid_attach(grid, grid_label, 0, 0, 1, 1);
        grid_ok = widget_parent(grid_label) == grid;
    }
    int visibility_ok = entry && widget_visible_set &&
        (widget_visible_set(entry, 1), widget_get_visible(entry) == 1);
    int sensitivity_ok = button && widget_sensitive_set &&
        widget_sensitive_get && (widget_sensitive_set(button, 0),
                                  widget_sensitive_get(button) == 0);
    if (button && widget_sensitive_set) widget_sensitive_set(button, 1);
    runtime_gvalue_probe property_title;
    runtime_gvalue_probe property_title_read;
    runtime_gvalue_probe property_count;
    runtime_gvalue_probe property_count_read;
    runtime_gvalue_probe property_enabled;
    runtime_gvalue_probe property_enabled_read;
    memset(&property_title, 0, sizeof(property_title));
    memset(&property_title_read, 0, sizeof(property_title_read));
    memset(&property_count, 0, sizeof(property_count));
    memset(&property_count_read, 0, sizeof(property_count_read));
    memset(&property_enabled, 0, sizeof(property_enabled));
    memset(&property_enabled_read, 0, sizeof(property_enabled_read));
    if (value_init && value_set_string) {
        value_init(&property_title, "s");
        value_set_string(&property_title, "Properties");
    }
    if (value_init && value_set_int) {
        value_init(&property_count, "i");
        value_set_int(&property_count, -7);
    }
    if (value_init && value_set_boolean) {
        value_init(&property_enabled, "b");
        value_set_boolean(&property_enabled, 1);
    }
    int property_title_ok = button && object_set_property && object_get_property &&
        object_set_property(button, "title", &property_title) &&
        object_get_property(button, "title", &property_title_read) &&
        value_get_string(&property_title_read) &&
        strcmp(value_get_string(&property_title_read), "Properties") == 0;
    int property_count_ok = button && object_set_property && object_get_property &&
        object_set_property(button, "count", &property_count) &&
        object_get_property(button, "count", &property_count_read) &&
        value_get_int(&property_count_read) == -7;
    int property_enabled_ok = button && object_set_property && object_get_property &&
        object_set_property(button, "enabled", &property_enabled) &&
        object_get_property(button, "enabled", &property_enabled_read) &&
        value_get_boolean(&property_enabled_read) == 1;
    int property_uint_ok = 0;
    if (button && value_init && value_set_uint && object_set_property &&
        object_get_property && value_get_uint) {
        runtime_gvalue_probe property_width;
        runtime_gvalue_probe property_width_read;
        memset(&property_width, 0, sizeof(property_width));
        memset(&property_width_read, 0, sizeof(property_width_read));
        value_init(&property_width, "u");
        value_set_uint(&property_width, 640U);
        property_uint_ok = object_set_property(button, "width", &property_width) &&
            object_get_property(button, "width", &property_width_read) &&
            value_get_uint(&property_width_read) == 640U;
        if (value_unset) {
            value_unset(&property_width);
            value_unset(&property_width_read);
        }
    }
    int properties_ok = property_title_ok && property_count_ok &&
        property_enabled_ok && property_uint_ok;
    runtime_gvalue_probe bound_read;
    runtime_gvalue_probe reverse_read;
    memset(&bound_read, 0, sizeof(bound_read));
    memset(&reverse_read, 0, sizeof(reverse_read));
    void *binding = button && label && binding_bind
        ? binding_bind(button, "title", label, "text", 3U) : NULL;
    void *binding_refd = binding && binding_ref ? binding_ref(binding) : NULL;
    int binding_sync_ok = binding && binding_refd == binding &&
        object_get_property(label, "text", &bound_read) &&
        value_get_string(&bound_read) &&
        strcmp(value_get_string(&bound_read), "Properties") == 0;
    if (binding_sync_ok && value_set_string && object_set_property) {
        value_set_string(&property_title, "Bound");
        binding_sync_ok = object_set_property(button, "title", &property_title) &&
            object_get_property(label, "text", &bound_read) &&
            value_get_string(&bound_read) &&
            strcmp(value_get_string(&bound_read), "Bound") == 0;
    }
    int binding_reverse_ok = binding_sync_ok && value_set_string &&
        object_set_property && object_get_property;
    if (binding_reverse_ok) {
        value_set_string(&bound_read, "Reverse");
        binding_reverse_ok = object_set_property(label, "text", &bound_read) &&
            object_get_property(button, "title", &reverse_read) &&
            value_get_string(&reverse_read) &&
            strcmp(value_get_string(&reverse_read), "Reverse") == 0;
    }
    int binding_ok = binding_sync_ok && binding_reverse_ok;
    if (binding && binding_unbind) {
        if (binding_refd) binding_unbind(binding_refd);
        binding_unbind(binding);
    }
    runtime_task_callback_seen = 0;
    void *cancellable = cancellable_new ? cancellable_new() : NULL;
    void *cancellable_refd = cancellable && cancellable_ref
        ? cancellable_ref(cancellable) : NULL;
    void *task = cancellable && task_new
        ? task_new(button, cancellable, runtime_task_callback,
                   (void *)&runtime_task_callback_seen) : NULL;
    void *task_refd = task && task_ref ? task_ref(task) : NULL;
    void *task_error = NULL;
    int task_ok = task && task_refd == task && task_source(task) == button &&
        !task_completed(task);
    if (task_ok) task_return_boolean(task, 1);
    task_ok = task_ok && runtime_task_callback_seen == 1 &&
        task_completed(task) && task_propagate_boolean(task, &task_error) == 1 &&
        task_error == NULL;
    if (task_refd) task_unref(task_refd);
    if (task) task_unref(task);
    if (cancellable_refd) cancellable_unref(cancellable_refd);
    if (cancellable) cancellable_unref(cancellable);
    void *cancel_token = cancellable_new ? cancellable_new() : NULL;
    void *cancel_task = cancel_token && task_new
        ? task_new(NULL, cancel_token, NULL, NULL) : NULL;
    int cancelled_ok = cancel_token && cancel_task && cancellable_cancel &&
        cancellable_is_cancelled && task_return_cancelled && task_completed &&
        task_propagate_boolean;
    if (cancelled_ok) {
        cancellable_cancel(cancel_token);
        cancelled_ok = cancellable_is_cancelled(cancel_token) &&
            task_return_cancelled(cancel_task) == 1 && task_completed(cancel_task) &&
            task_propagate_boolean(cancel_task, &task_error) == 0 &&
            task_error && error_matches(task_error, 1U, 125) &&
            error_domain(task_error) == 1U && error_code(task_error) == 125 &&
            error_message(task_error) &&
            strcmp(error_message(task_error), "Operation cancelled") == 0;
        void *error_clone = (cancelled_ok && error_copy)
            ? error_copy(task_error) : NULL;
        cancelled_ok = cancelled_ok && error_clone &&
            error_matches(error_clone, 1U, 125);
        if (error_clone && error_free) error_free(error_clone);
        if (task_error && error_free) error_free(task_error);
        task_error = NULL;
    }
    if (cancel_task) task_unref(cancel_task);
    if (cancel_token) cancellable_unref(cancel_token);
    int gio_task_ok = task_ok && cancelled_ok;
    if (gio_task_ok) puts("GIO error propagation verified");
    if (value_unset) {
        value_unset(&property_title);
        value_unset(&property_title_read);
        value_unset(&property_count);
        value_unset(&property_count_read);
        value_unset(&property_enabled);
        value_unset(&property_enabled_read);
        value_unset(&bound_read);
        value_unset(&reverse_read);
    }
    void *data_token = (void *)&widget_width;
    void *data_get = NULL;
    void *data_steal = NULL;
    void *data_after = NULL;
    if (button && object_set_data && object_get_data && object_steal_data) {
        object_set_data(button, "runtime-token", data_token);
        data_get = object_get_data(button, "runtime-token");
        data_steal = object_steal_data(button, "runtime-token");
        data_after = object_get_data(button, "runtime-token");
    }
    int object_data_ok = button && object_set_data && object_get_data &&
        object_steal_data && data_get == data_token &&
        data_steal == data_token && data_after == NULL;
    char *vararg_title = NULL;
    int vararg_visible = 0;
    int vararg_sensitive = 1;
    int object_varargs_ok = button && object_set && object_get;
    if (object_varargs_ok) {
        object_set(button, "title", "Varargs", "visible", 1,
                   "sensitive", 0, NULL);
        object_get(button, "title", &vararg_title,
                   "visible", &vararg_visible,
                   "sensitive", &vararg_sensitive, NULL);
        object_varargs_ok = vararg_title &&
            strcmp(vararg_title, "Varargs") == 0 && vararg_visible == 1 &&
            vararg_sensitive == 0 && widget_sensitive_get(button) == 0;
    }
    unsigned signal_id = have_widgets ? signal_connect(
        button, "clicked", runtime_button_callback,
        (void *)&runtime_signal_seen) : 0U;
    int emitted = signal_id ? signal_emit(button, "clicked") : 0;
    int disconnected = signal_id
        ? signal_disconnect(button, signal_id) : 0;
    int not_emitted = signal_id && signal_emit(button, "clicked") == 0 &&
        runtime_signal_seen == 1;
    unsigned second_signal_id = signal_connect(button, "clicked",
                                                runtime_button_callback,
                                                (void *)&runtime_signal_seen);
    unsigned by_func_removed = second_signal_id
        ? signal_disconnect_by_func(button, runtime_button_callback,
                                    (void *)&runtime_signal_seen) : 0U;
    int disconnect_by_func_ok = second_signal_id && by_func_removed == 1U &&
        signal_emit(button, "clicked") == 0 && runtime_signal_seen == 1;
    if (!have_widgets || !label_ok || !button_text_ok || !button_size_ok ||
        !label_add_ok || !button_add_ok || !parent_ok || !visible_ok ||
        !dimensions_ok || !entry_ok || !check_ok || !extended_add_ok ||
        !show_all_ok || !remove_readd_ok || !grid_ok || !visibility_ok ||
        !sensitivity_ok || !object_data_ok || !source_full_ok ||
        !properties_ok || !object_varargs_ok || !binding_ok || !gio_task_ok ||
        !gio_file_ok ||
        !application_lifecycle_ok || !window_state_ok || !qt_ok || !action_ok ||
        signal_id == 0U || emitted != 1 || !disconnected || !not_emitted ||
        !disconnect_by_func_ok ||
         runtime_signal_seen != 1 || !fd_source_ok || !io_source_ok) {
        printf("desktop widget tree probe failed: objects=%d label=%d text=%d size=%d "
               "add=%d/%d parent=%d visible=%d dims=%d entry=%d check=%d add2=%d "
               "grid=%d vis=%d sensitive=%d data=%d get=%p steal=%p after=%p "
               "varargs=%d title=%p title0=%d title1=%d visible=%d sens=%d signal=%u emitted=%d "
               "disconnected=%d quiet=%d byfunc=%d seen=%d\n",
               have_widgets, label_ok, button_text_ok, button_size_ok,
               label_add_ok, button_add_ok, parent_ok, visible_ok,
               dimensions_ok, entry_ok, check_ok, extended_add_ok, grid_ok,
               visibility_ok, sensitivity_ok, object_data_ok,
               data_get, data_steal, data_after,
               object_varargs_ok, vararg_title,
               vararg_title ? (unsigned char)vararg_title[0] : 0,
               vararg_title ? (unsigned char)vararg_title[1] : 0,
               vararg_visible,
               vararg_sensitive, signal_id, emitted, disconnected, not_emitted,
               disconnect_by_func_ok, runtime_signal_seen);
        if (vararg_title && runtime_free) runtime_free(vararg_title);
        if (grid_label) (void)window_destroy(grid_label);
        if (grid) (void)window_destroy(grid);
        if (check) (void)window_destroy(check);
        if (entry) (void)window_destroy(entry);
        if (button) (void)window_destroy(button);
        if (label) (void)window_destroy(label);
        if (box) (void)window_destroy(box);
        (void)dlclose(qt);
        (void)dlclose(gtk);
        (void)dlclose(glib);
        return 505;
    }
    if (vararg_title && runtime_free) runtime_free(vararg_title);
    (void)window_destroy(button);
    (void)window_destroy(label);
    (void)window_destroy(box);
    (void)window_destroy(check);
    (void)window_destroy(entry);
    (void)window_destroy(grid_label);
    (void)window_destroy(grid);
    if (dlclose(qt) != 0 || dlclose(gtk) != 0 || dlclose(glib) != 0) return 504;
    puts("desktop shared runtime ABI verified");
    puts("desktop runtime event-loop and widget lifecycle verified");
    puts("desktop widget tree and activation signals verified");
    puts("GIO cancellation and async task lifecycle verified");
    puts("GIO file content I/O verified");
    puts("GApplication lifecycle verified");
    puts("GTK window state and lifecycle verified");
    puts("Qt core and widget bridge verified");
    puts("GAction map and activation verified");
    return 0;
}

static void custom_sigusr1_handler(int sig) {
    received_signal = sig;
    signal_handler_counter++;
}

static int test_user_defined_signals(void) {
    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = custom_sigusr1_handler;
    sa.sa_flags = 0;
    sa.sa_mask = 0;

    if (sigaction(SIGUSR1, &sa, &old_sa) != 0) {
        printf("sigaction SIGUSR1 failed\n");
        return 301;
    }

    /* Send SIGUSR1 to self */
    received_signal = 0;
    signal_handler_counter = 0;
    if (kill((int)getpid(), SIGUSR1) != 0) {
        printf("kill SIGUSR1 failed\n");
        return 302;
    }

    if (signal_handler_counter != 1 || received_signal != SIGUSR1) {
        printf("signal handler not executed properly: counter=%d, sig=%d\n",
               signal_handler_counter, received_signal);
        return 303;
    }

    /* Test SIG_IGN for SIGUSR2 */
    struct sigaction sa_ign;
    memset(&sa_ign, 0, sizeof(sa_ign));
    sa_ign.sa_handler = SIG_IGN;
    if (sigaction(SIGUSR2, &sa_ign, NULL) != 0) {
        printf("sigaction SIG_IGN failed\n");
        return 304;
    }

    if (kill((int)getpid(), SIGUSR2) != 0) {
        printf("kill SIGUSR2 with SIG_IGN failed\n");
        return 305;
    }

    /* Test raise() helper */
    if (raise(SIGUSR1) != 0) {
        printf("raise SIGUSR1 failed\n");
        return 306;
    }

    if (signal_handler_counter != 2) {
        printf("raise handler not executed: counter=%d\n", signal_handler_counter);
        return 307;
    }

    puts("user-defined signal handling (sigaction) verified");
    return 0;
}

static int test_demand_paging_and_cow(void) {
    /* Reserve several pages but touch only the first and last. The kernel
     * must commit them lazily as zero-filled anonymous pages. */
    void *before_brk = sbrk(0);
    uint8_t *reserved = (uint8_t *)sbrk(4 * 4096);
    if (reserved == (void *)-1) {
        printf("vm brk failed at %p\n", before_brk);
        return 222;
    }
    reserved[0] = UINT8_C(0x5a);
    if (reserved[16383] != 0) return 223;
    reserved[16383] = UINT8_C(0xa5);

    cow_test_value = UINT32_C(0x11111111);
    pid_t child = fork();
    if (child < 0) return 224;

    if (child == 0) {
        cow_test_value = UINT32_C(0xc01dc01d);
        if (cow_test_value != UINT32_C(0xc01dc01d) ||
            reserved[0] != UINT8_C(0x5a) ||
            reserved[16383] != UINT8_C(0xa5)) {
            _exit(221);
        }
        puts("COW child isolation verified");
        puts("POSIX fork continuation verified");
        _exit(0);
    }

    cow_test_value = UINT32_C(0x22222222);
    int status = -1;
    if (waitpid(child, &status, 0) != child || status != 0) return 225;
    if (cow_test_value != UINT32_C(0x22222222)) return 226;
    if (reserved[0] != UINT8_C(0x5a)) return 227;
    if (reserved[16383] != UINT8_C(0xa5)) return 228;
    puts("demand paging and COW verified");
    return 0;
}

static int write_complete(int descriptor, const char *buffer, size_t length) {
    size_t written = 0;
    while (written < length) {
        ssize_t result = write(descriptor, buffer + written, length - written);
        if (result <= 0) return 0;
        written += (size_t)result;
    }
    return 1;
}

static int buffer_contains(const char *buffer, size_t length,
                           const char *needle) {
    size_t needle_length = strlen(needle);
    if (needle_length == 0) return 1;
    for (size_t start = 0; start + needle_length <= length; ++start) {
        if (memcmp(buffer + start, needle, needle_length) == 0) return 1;
    }
    return 0;
}

static int test_sessions_and_terminal_groups(void) {
    pid_t pid = getpid();
    pid_t group = getpgrp();
    pid_t session = getsid(0);
    pid_t foreground = tcgetpgrp(STDIN_FILENO);
    if (pid <= 0 || group <= 0 || session <= 0 || foreground != group) {
        return 501;
    }
    errno = 0;
    if (setsid() != -1 || errno != EPERM) return 502;
    puts("sessions, process groups, and controlling TTY verified");
    return 0;
}

static int test_pseudoterminal_pair(void) {
    int master = posix_openpt(O_RDWR);
    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0) return 511;
    char *name = ptsname(master);
    if (!name || strncmp(name, "/dev/pts/", 9) != 0) return 512;
    int slave = open(name, O_RDWR);
    if (slave < 0) return 513;
    const char to_slave[] = "pty-input\n";
    const char to_master[] = "pty-output";
    char received[32];
    struct termios settings;
    struct winsize size;
    if (tcgetattr(slave, &settings) != 0 ||
        (settings.c_lflag & (ICANON | ECHO | ISIG)) !=
            (ICANON | ECHO | ISIG)) return 514;
    struct pollfd readiness = {slave, POLLIN, 0};
    if (write(master, to_slave, sizeof(to_slave) - 2) !=
            (ssize_t)(sizeof(to_slave) - 2) ||
        poll(&readiness, 1, 0) != 0 ||
        write(master, "\n", 1) != 1 ||
        read(slave, received, sizeof(to_slave) - 1) !=
            (ssize_t)(sizeof(to_slave) - 1) ||
        memcmp(received, to_slave, sizeof(to_slave) - 1) != 0) return 514;
    if (read(master, received, sizeof(to_slave) - 1) !=
            (ssize_t)(sizeof(to_slave) - 1) ||
        memcmp(received, to_slave, sizeof(to_slave) - 1) != 0) return 515;
    cfmakeraw(&settings);
    if (tcsetattr(slave, TCSANOW, &settings) != 0 ||
        write(master, "R", 1) != 1 || read(slave, received, 1) != 1 ||
        received[0] != 'R') return 516;
    size.ws_row = 40;
    size.ws_col = 120;
    size.ws_xpixel = 960;
    size.ws_ypixel = 640;
    if (ioctl(slave, TIOCSWINSZ, &size) != 0) return 517;
    memset(&size, 0, sizeof(size));
    if (ioctl(master, TIOCGWINSZ, &size) != 0 || size.ws_row != 40 ||
        size.ws_col != 120) return 518;
    if (!ttyname(slave) || strncmp(ttyname(slave), "/dev/pts/", 9) != 0) {
        return 519;
    }
    if (write(slave, to_master, sizeof(to_master) - 1) !=
            (ssize_t)(sizeof(to_master) - 1) ||
        read(master, received, sizeof(to_master) - 1) !=
            (ssize_t)(sizeof(to_master) - 1) ||
        memcmp(received, to_master, sizeof(to_master) - 1) != 0) return 520;
    if (close(slave) != 0 || close(master) != 0) return 521;
    puts("PTY master/slave bidirectional I/O verified");
    puts("termios, ioctl, canonical/raw mode, and window size verified");
    return 0;
}

static int test_proc_namespace(void) {
    char buffer[512];
    int descriptor = open("/proc/self/status", O_RDONLY);
    if (descriptor < 0) return 521;
    ssize_t count = read(descriptor, buffer, sizeof(buffer));
    if (count <= 0 || close(descriptor) != 0 ||
        !buffer_contains(buffer, (size_t)count, "Pid:\t") ||
        !buffer_contains(buffer, (size_t)count, "Pgid:\t") ||
        !buffer_contains(buffer, (size_t)count, "Sid:\t") ||
        !buffer_contains(buffer, (size_t)count, "TtyPgrp:\t")) return 522;
    descriptor = open("/proc/processes", O_RDONLY);
    if (descriptor < 0) return 523;
    count = read(descriptor, buffer, sizeof(buffer));
    if (count <= 0 || close(descriptor) != 0 ||
        !buffer_contains(buffer, (size_t)count, "PID PPID PGID SID STATE")) {
        return 524;
    }

    /* Test dynamic /proc enumeration */
    int proc_dir = open("/proc", O_RDONLY);
    if (proc_dir < 0) return 527;
    struct akaos_dirent entries[16];
    int saw_self = 0, saw_processes = 0, saw_pid = 0;
    unsigned long my_pid = (unsigned long)getpid();
    char my_pid_str[16];
    snprintf(my_pid_str, sizeof(my_pid_str), "%lu", my_pid);

    ssize_t dcount;
    int got_any = 0;
    while ((dcount = getdents(proc_dir, entries, 16)) > 0) {
        got_any = 1;
        for (ssize_t i = 0; i < dcount; ++i) {
            if (strcmp(entries[i].name, "self") == 0) saw_self = 1;
            else if (strcmp(entries[i].name, "processes") == 0) saw_processes = 1;
            else if (strcmp(entries[i].name, my_pid_str) == 0) saw_pid = 1;
        }
    }
    if (!got_any || close(proc_dir) != 0) return 528;
    if (!saw_self || !saw_processes || !saw_pid) return 529;

    /* Test /proc/<pid> stat and directory listing */
    char pid_dir_path[32];
    snprintf(pid_dir_path, sizeof(pid_dir_path), "/proc/%lu", my_pid);
    struct akaos_stat st;
    if (stat(pid_dir_path, &st) != 0 || st.type != AKAOS_DT_DIRECTORY || !S_ISDIR(st.mode)) {
        return 530;
    }

    int my_proc_dir = open(pid_dir_path, O_RDONLY);
    if (my_proc_dir < 0) return 531;
    dcount = getdents(my_proc_dir, entries, 16);
    if (dcount <= 0 || close(my_proc_dir) != 0) return 532;
    int saw_status = 0;
    for (ssize_t i = 0; i < dcount; ++i) {
        if (strcmp(entries[i].name, "status") == 0) saw_status = 1;
    }
    if (!saw_status) return 533;

    char pid_path[32] = "/proc/";
    char digits[20];
    size_t used = 6;
    size_t digit_count = 0;
    unsigned long pid_value = my_pid;
    do {
        digits[digit_count++] = (char)('0' + pid_value % 10);
        pid_value /= 10;
    } while (pid_value && digit_count < sizeof(digits));
    while (digit_count) pid_path[used++] = digits[--digit_count];
    memcpy(pid_path + used, "/status", 8);
    descriptor = open(pid_path, O_RDONLY);
    if (descriptor < 0) return 525;
    count = read(descriptor, buffer, sizeof(buffer));
    if (count <= 0 || close(descriptor) != 0 ||
        !buffer_contains(buffer, (size_t)count, "Pid:\t")) return 526;
    puts("generated /proc process information verified");
    puts("dynamic /proc directory enumeration verified");
    puts("per-process /proc/<pid>/status lookup verified");
    return 0;
}

static void allow_timer_preemption(void) {
    volatile uint32_t count = 0x02000000;
    while (count > 0) {
        --count;
    }
}

static int test_heap_allocator(void) {
    /* 1. Verify sbrk initial address */
    void *initial_brk = sbrk(0);
    if (initial_brk == (void *)-1 || initial_brk == NULL) return 101;

    /* 2. Test malloc and memory write */
    uint8_t *p1 = (uint8_t *)malloc(128);
    if (!p1) return 102;
    for (int i = 0; i < 128; ++i) {
        p1[i] = (uint8_t)(i & 0xFF);
    }
    for (int i = 0; i < 128; ++i) {
        if (p1[i] != (uint8_t)(i & 0xFF)) return 103;
    }

    /* 3. Test calloc zero-initialization */
    uint32_t *p2 = (uint32_t *)calloc(64, sizeof(uint32_t));
    if (!p2) return 104;
    for (int i = 0; i < 64; ++i) {
        if (p2[i] != 0) return 105;
        p2[i] = (uint32_t)(0x12345678 + i);
    }

    /* 4. Test realloc expanding */
    p1 = (uint8_t *)realloc(p1, 512);
    if (!p1) return 106;
    for (int i = 0; i < 128; ++i) {
        if (p1[i] != (uint8_t)(i & 0xFF)) return 107;
    }

    /* 5. Test large allocation spanning multiple 4 KiB pages */
    char *large = (char *)malloc(8192);
    if (!large) return 108;
    memset(large, 'A', 8192);
    if (large[0] != 'A' || large[8191] != 'A') return 109;

    /* 6. Test formatted printf */
    char snbuf[64];
    int len = snprintf(snbuf, sizeof(snbuf), "Test %d 0x%X %s", 42, 0xABCD, "ok");
    if (len <= 0 || strcmp(snbuf, "Test 42 0xABCD ok") != 0) return 110;

    /* 7. Test free and coalescing */
    free(p1);
    free(p2);
    free(large);

    /* Allocate again after free to verify freelist reuse */
    void *reused = malloc(256);
    if (!reused) return 111;
    free(reused);

    /* Stress the allocator with fragmented, differently-sized blocks. */
    enum { STRESS_COUNT = 128 };
    void *stress_blocks[STRESS_COUNT] = {0};
    size_t stress_sizes[STRESS_COUNT];
    for (unsigned i = 0; i < STRESS_COUNT; ++i) {
        size_t block_size = (size_t)((i * 73U) % 127U) + 1U;
        uint8_t pattern = (uint8_t)(i ^ 0xA5U);
        stress_blocks[i] = malloc(block_size);
        if (!stress_blocks[i]) return 112;
        stress_sizes[i] = block_size;
        memset(stress_blocks[i], pattern, block_size);
    }
    for (unsigned i = 0; i < STRESS_COUNT; ++i) {
        unsigned index = (i * 73U) % STRESS_COUNT;
        uint8_t *block = (uint8_t *)stress_blocks[index];
        uint8_t pattern = (uint8_t)(index ^ 0xA5U);
        for (size_t byte = 0; byte < stress_sizes[index]; ++byte) {
            if (block[byte] != pattern) return 113;
        }
        free(block);
        stress_blocks[index] = NULL;
    }

    void *aligned = aligned_alloc(64U, 192U);
    if (!aligned || ((uintptr_t)aligned & 63U) != 0U) return 114;
    memset(aligned, 0x5A, 192U);
    if (((uint8_t *)aligned)[0] != 0x5A || ((uint8_t *)aligned)[191] != 0x5A)
        return 115;
    free(aligned);

    void *posix_aligned = NULL;
    int posix_result = posix_memalign(&posix_aligned, 128U, 73U);
    if (posix_result != 0 || !posix_aligned ||
        ((uintptr_t)posix_aligned & 127U) != 0U)
        return 116;
    memset(posix_aligned, 0x3C, 73U);
    if (((uint8_t *)posix_aligned)[0] != 0x3C ||
        ((uint8_t *)posix_aligned)[72] != 0x3C)
        return 117;
    free(posix_aligned);

    void *invalid_destination = (void *)(uintptr_t)0x1234U;
    int saved_errno = errno;
    errno = EINTR;
    int invalid_result = posix_memalign(&invalid_destination, 24U, 32U);
    if (invalid_result != EINVAL || errno != EINTR ||
        invalid_destination != (void *)(uintptr_t)0x1234U)
        return 118;
    errno = saved_errno;

    puts("malloc fragmentation, back-reference free, and aligned allocation verified");
    puts("posix_memalign alignment and error contract verified");

    return 0;
}

static int test_pipe_ipc(void) {
    int pfd[2];
    if (pipe(pfd) != 0) return 201;
    if (pfd[0] < 3 || pfd[1] < 3 || pfd[0] == pfd[1]) return 202;

    const char test_data[] = "hello pipe ipc\n";
    ssize_t w = write(pfd[1], test_data, sizeof(test_data) - 1);
    if (w != (ssize_t)(sizeof(test_data) - 1)) return 203;

    char read_buf[32];
    ssize_t r = read(pfd[0], read_buf, sizeof(test_data) - 1);
    if (r != (ssize_t)(sizeof(test_data) - 1)) return 204;
    if (memcmp(read_buf, test_data, sizeof(test_data) - 1) != 0) return 205;

    /* Test EOF when write end is closed */
    if (close(pfd[1]) != 0) return 206;
    ssize_t eof_r = read(pfd[0], read_buf, sizeof(read_buf));
    if (eof_r != 0) return 207;
    if (close(pfd[0]) != 0) return 208;

    return 0;
}

static int test_poll_and_input(void) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return 460;

    struct pollfd descriptors[3] = {
        {.fd = pipefd[0], .events = POLLIN, .revents = 0},
        {.fd = pipefd[1], .events = POLLOUT, .revents = 0},
        {.fd = 15, .events = POLLIN, .revents = 0},
    };
    int ready = poll(descriptors, 3, 0);
    if (ready != 2 || descriptors[0].revents != 0 ||
        (descriptors[1].revents & POLLOUT) == 0 ||
        (descriptors[2].revents & POLLNVAL) == 0) return 461;

    char marker = 'P';
    if (write(pipefd[1], &marker, 1) != 1) return 462;
    descriptors[0].revents = 0;
    if (poll(descriptors, 1, 0) != 1 ||
        (descriptors[0].revents & POLLIN) == 0) return 463;
    char result = 0;
    if (read(pipefd[0], &result, 1) != 1 || result != marker) return 464;

    if (close(pipefd[1]) != 0) return 465;
    descriptors[0].revents = 0;
    if (poll(descriptors, 1, 0) != 1 ||
        (descriptors[0].revents & POLLHUP) == 0) return 466;
    if (close(pipefd[0]) != 0) return 467;

    int input = open(AKAOS_INPUT_EVENT_PATH, O_RDONLY);
    if (input < 0) return 468;
    struct pollfd input_poll = {.fd = input, .events = POLLIN, .revents = 0};
    if (poll(&input_poll, 1, 0) < 0 ||
        (input_poll.revents & POLLNVAL) != 0) return 469;
    if (close(input) != 0) return 470;

    puts("poll readiness and input event descriptor verified");
    return 0;
}

static int test_framebuffer_device(void) {
    int descriptor = open(AKAOS_FRAMEBUFFER_PATH, O_RDONLY);
    if (descriptor < 0) return 471;
    struct pollfd readiness = {
        .fd = descriptor, .events = POLLIN, .revents = 0};
    if (poll(&readiness, 1, 0) != 1 ||
        (readiness.revents & POLLIN) == 0) return 472;
    struct akaos_display_info info;
    if (read(descriptor, &info, sizeof(info)) != (ssize_t)sizeof(info) ||
        info.abi_version != AKAOS_DISPLAY_ABI_VERSION ||
        info.width == 0 || info.height == 0 ||
        info.stride_pixels < info.width ||
        info.byte_size < (uint64_t)info.stride_pixels * info.height * 4) {
        return 473;
    }
    if (read(descriptor, &info, sizeof(info)) != 0 ||
        close(descriptor) != 0) return 474;
    puts("userspace framebuffer device verified");
    return 0;
}

static int test_device_filesystem(void) {
    struct akaos_stat status;
    if (stat("/dev", &status) != 0 || !S_ISDIR(status.mode)) return 475;
    if (stat("/dev/input", &status) != 0 || !S_ISDIR(status.mode)) return 476;

    for (size_t index = 0; index < 5U; ++index) {
        const char *path = index == 0 ? "/dev/tty1" :
                           index == 1 ? "/dev/null" :
                           index == 2 ? "/dev/zero" :
                           index == 3 ? "/dev/fb0" :
                                        "/dev/input/event0";
        if (stat(path, &status) != 0 ||
            status.type != AKAOS_DT_CHARACTER || !S_ISCHR(status.mode)) {
            return 477;
        }
    }

    int directory = open("/dev", O_RDONLY);
    if (directory < 0) return 478;
    struct akaos_dirent entries[16];
    ssize_t count = getdents(directory, entries, 16);
    if (count < 0 || close(directory) != 0) return 479;
    int saw_tty = 0, saw_null = 0, saw_zero = 0, saw_fb = 0, saw_input = 0;
    for (ssize_t index = 0; index < count; ++index) {
        if (strcmp(entries[index].name, "tty1") == 0) saw_tty = 1;
        else if (strcmp(entries[index].name, "null") == 0) saw_null = 1;
        else if (strcmp(entries[index].name, "zero") == 0) saw_zero = 1;
        else if (strcmp(entries[index].name, "fb0") == 0) saw_fb = 1;
        else if (strcmp(entries[index].name, "input") == 0) saw_input = 1;
    }
    if (!saw_tty || !saw_null || !saw_zero || !saw_fb || !saw_input) {
        return 480;
    }

    int null_device = open("/dev/null", O_RDWR);
    if (null_device < 0 || write(null_device, "discard", 7) != 7) return 481;
    char bytes[16];
    if (read(null_device, bytes, sizeof(bytes)) != 0 ||
        close(null_device) != 0) return 482;

    char saved_cwd[128];
    if (!getcwd(saved_cwd, sizeof(saved_cwd)) || chdir("/dev") != 0) {
        return 487;
    }
    int relative_null = open("null", O_WRONLY);
    if (relative_null < 0 || write(relative_null, "relative", 8) != 8 ||
        close(relative_null) != 0 || chdir(saved_cwd) != 0) return 488;

    int zero_device = open("/dev/zero", O_RDWR);
    if (zero_device < 0 || read(zero_device, bytes, sizeof(bytes)) !=
                               (ssize_t)sizeof(bytes)) return 483;
    for (size_t index = 0; index < sizeof(bytes); ++index) {
        if (bytes[index] != 0) return 484;
    }
    if (write(zero_device, "discard", 7) != 7 ||
        close(zero_device) != 0) return 485;

    int tty = open("/dev/tty1", O_WRONLY);
    struct pollfd ready = {.fd = tty, .events = POLLOUT, .revents = 0};
    if (tty < 0 || poll(&ready, 1, 0) != 1 ||
        (ready.revents & POLLOUT) == 0 || close(tty) != 0) return 486;

    puts("visible /dev character devices verified");
    return 0;
}

static int test_safe_usercopy(void) {
    errno = 0;
    if (write(STDOUT_FILENO, (const void *)(uintptr_t)1, 1) != -1 ||
        errno != EFAULT) return 231;

    int descriptor = open("/etc/motd", O_RDONLY);
    if (descriptor < 0) return 232;
    errno = 0;
    if (read(descriptor, (void *)(uintptr_t)1, 4) != -1 || errno != EFAULT) {
        return 233;
    }
    char normal[sizeof(expected_motd)];
    ssize_t count = read(descriptor, normal, sizeof(expected_motd) - 1);
    if (count != (ssize_t)(sizeof(expected_motd) - 1) ||
        memcmp(normal, expected_motd, sizeof(expected_motd) - 1) != 0) {
        return 234;
    }
    if (close(descriptor) != 0) return 235;

    uint8_t *reserved = (uint8_t *)sbrk(8192);
    if (reserved == (void *)-1) return 236;
    char *cross_page = (char *)(reserved + 4090);
    descriptor = open("/etc/motd", O_RDONLY);
    if (descriptor < 0) return 237;
    count = read(descriptor, cross_page, sizeof(expected_motd) - 1);
    if (close(descriptor) != 0 ||
        count != (ssize_t)(sizeof(expected_motd) - 1) ||
        memcmp(cross_page, expected_motd, sizeof(expected_motd) - 1) != 0) {
        return 238;
    }

    usercopy_cow_buffer[0] = '#';
    pid_t child = fork();
    if (child < 0) return 239;
    if (child == 0) {
        int child_fd = open("/etc/motd", O_RDONLY);
        if (child_fd < 0) _exit(240);
        ssize_t child_count = read(child_fd, (void *)usercopy_cow_buffer,
                                   sizeof(expected_motd) - 1);
        (void)close(child_fd);
        if (child_count != (ssize_t)(sizeof(expected_motd) - 1) ||
            memcmp((const void *)usercopy_cow_buffer, expected_motd,
                   sizeof(expected_motd) - 1) != 0) {
            _exit(241);
        }
        _exit(0);
    }
    int status = -1;
    if (waitpid(child, &status, 0) != child || status != 0) return 242;
    if (usercopy_cow_buffer[0] != '#') return 243;

    puts("safe user copy-in/copy-out verified");
    return 0;
}

__attribute__((noinline))
static uint32_t test_stack_growth_recursive(uint32_t depth) {
    volatile uint8_t stack_block[1024];
    for (size_t index = 0; index < sizeof(stack_block); ++index) {
        stack_block[index] = (uint8_t)(depth + index);
    }
    uint32_t result = stack_block[0] + stack_block[sizeof(stack_block) - 1];
    if (depth != 0) result += test_stack_growth_recursive(depth - 1);
    return result + stack_block[depth & (sizeof(stack_block) - 1)];
}

static int test_clean_page_cache(void) {
    const char *text_ptr = hello_message;
    if (text_ptr[0] != '[') return 401;

    pid_t child = fork();
    if (child < 0) return 402;

    if (child == 0) {
        if (hello_message[0] != '[' || expected_motd[0] != 'a') {
            _exit(403);
        }
        _exit(0);
    }

    int status = 0;
    if (waitpid(child, &status, 0) != child || status != 0) {
        printf("pagecache child failed: status=%d\n", status);
        return 404;
    }

    puts("clean file-page cache sharing verified");
    return 0;
}

static int test_execve_replacement(void) {
    pid_t child = fork();
    if (child < 0) return 410;
    if (child == 0) {
        char arg0[] = "hello";
        char arg1[] = "--exec-target";
        char environment[] = "AKAOS_EXEC_TEST=1";
        char *argv[] = {arg0, arg1, NULL};
        char *envp[] = {environment, NULL};
        if (execve("/bin/hello", argv, envp) < 0) {
            printf("execve child error: %d\n", errno);
        }
        _exit(411);
    }
    int status = -1;
    if (waitpid(child, &status, 0) != child || status != 0) {
        printf("execve child status: %d\n", status);
        return 412;
    }
    puts("POSIX fork-exec-wait lifecycle verified");
    return 0;
}

static int test_mmap_private(void) {
    uint8_t *mapping = mmap(NULL, 8192, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) return 420;
    if (mapping[0] != 0 || mapping[4096] != 0 || mapping[8191] != 0) {
        return 421;
    }
    mapping[0] = UINT8_C(0x31);
    mapping[4096] = UINT8_C(0x42);
    mapping[8191] = UINT8_C(0x53);

    pid_t child = fork();
    if (child < 0) return 422;
    if (child == 0) {
        if (mapping[0] != UINT8_C(0x31) ||
            mapping[8191] != UINT8_C(0x53)) _exit(423);
        mapping[0] = UINT8_C(0xa1);
        mapping[8191] = UINT8_C(0xb2);
        _exit(0);
    }
    int status = -1;
    if (waitpid(child, &status, 0) != child || status != 0) return 424;
    if (mapping[0] != UINT8_C(0x31) ||
        mapping[8191] != UINT8_C(0x53)) return 425;

    int descriptor = open("/etc/motd", O_RDONLY);
    if (descriptor < 0) return 426;
    const char *file_mapping = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE,
                                    descriptor, 0);
    if (close(descriptor) != 0 || file_mapping == MAP_FAILED) return 427;
    if (memcmp(file_mapping, expected_motd, sizeof(expected_motd) - 1) != 0) {
        return 428;
    }
    if (munmap((void *)file_mapping, 4096) != 0) return 429;

    descriptor = open("/etc/motd", O_RDONLY);
    if (descriptor < 0) return 432;
    const char *shared_file_mapping = mmap(NULL, 4096, PROT_READ,
                                           MAP_SHARED, descriptor, 0);
    if (shared_file_mapping == MAP_FAILED) {
        close(descriptor);
        return 433;
    }
    if (close(descriptor) != 0 ||
        memcmp(shared_file_mapping, expected_motd,
               sizeof(expected_motd) - 1) != 0) return 434;
    errno = 0;
    if (mprotect((void *)shared_file_mapping, 4096,
                 PROT_READ | PROT_WRITE) != -1 || errno != EACCES)
        return 435;
    if (munmap((void *)shared_file_mapping, 4096) != 0) return 436;

    if (munmap(mapping, 8192) != 0) return 430;

    errno = 0;
    if (mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, -1, 1) != MAP_FAILED ||
        errno != EINVAL) return 431;
    puts("POSIX mmap private and immutable shared-file mappings verified");
    return 0;
}

static int wait_shared_futex(volatile uint32_t *word, uint32_t expected) {
    for (;;) {
        if (__atomic_load_n(word, __ATOMIC_ACQUIRE) != expected) return 0;
        long result = akaos_futex(word, AKAOS_FUTEX_WAIT, expected);
        if (result < 0 && result != -EAGAIN) return -1;
    }
}

static int wait_shared_marker(volatile uint32_t *word) {
    const struct timespec pause = {0, 1000000L};
    for (uint32_t attempt = 0; attempt < 5000U; ++attempt) {
        if (__atomic_load_n(word, __ATOMIC_ACQUIRE) != 0U) return 0;
        (void)nanosleep(&pause, NULL);
    }
    return -1;
}

static int test_shared_memory(void) {
    static const char object_name[] = "/akaos-shm-test";
    int descriptor = shm_open(object_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (descriptor < 0) return 440;
    if (ftruncate(descriptor, 4096) != 0) return 441;

    uint32_t *mapping = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                             MAP_SHARED, descriptor, 0);
    if (mapping == MAP_FAILED) return 442;
    mapping[0] = UINT32_C(0x11223344);
    mapping[2] = 0U;
    mapping[1023] = UINT32_C(0x55667788);

    errno = 0;
    int duplicate = shm_open(object_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (duplicate != -1 || errno != EEXIST) return 443;

    pid_t child = fork();
    if (child < 0) return 444;
    if (child == 0) {
        int child_descriptor = shm_open(object_name, O_RDWR, 0);
        if (child_descriptor < 0) _exit(445);
        uint32_t *child_mapping = mmap(NULL, 4096,
                                       PROT_READ | PROT_WRITE,
                                       MAP_SHARED, child_descriptor, 0);
        if (child_mapping == MAP_FAILED ||
            child_mapping[0] != UINT32_C(0x11223344) ||
            child_mapping[1023] != UINT32_C(0x55667788)) _exit(446);
        struct timespec wake_delay = {0, 20000000L};
        if (nanosleep(&wake_delay, NULL) != 0) _exit(456);
        __atomic_store_n(&child_mapping[2], 1U, __ATOMIC_RELEASE);
        if (akaos_futex(&child_mapping[2], AKAOS_FUTEX_WAKE, 1U) < 0)
            _exit(457);
        child_mapping[0] = UINT32_C(0xaabbccdd);
        child_mapping[1023] = UINT32_C(0x13579bdf);
        if (munmap(child_mapping, 4096) != 0 ||
            close(child_descriptor) != 0) _exit(447);
        _exit(0);
    }
    if (wait_shared_futex(&mapping[2], 0U) != 0 || mapping[2] != 1U)
        return 458;
    int status = -1;
    if (waitpid(child, &status, 0) != child || status != 0) return 448;
    if (mapping[0] != UINT32_C(0xaabbccdd) ||
        mapping[1023] != UINT32_C(0x13579bdf)) return 449;
    puts("Process-shared futex blocking and wake verified");

    pthread_mutex_t *shared_mutex =
        (pthread_mutex_t *)(void *)&mapping[4];
    pthread_cond_t *shared_condition =
        (pthread_cond_t *)(void *)&mapping[16];
    pthread_rwlock_t *shared_rwlock =
        (pthread_rwlock_t *)(void *)&mapping[24];
    pthread_barrier_t *shared_barrier =
        (pthread_barrier_t *)(void *)&mapping[32];
    sem_t *shared_semaphore = (sem_t *)(void *)&mapping[40];
    for (uint32_t index = 50U; index < 56U; ++index) mapping[index] = 0U;

    pthread_mutexattr_t mutex_attributes;
    if (pthread_mutexattr_init(&mutex_attributes) != 0 ||
        pthread_mutexattr_setpshared(&mutex_attributes,
                                     PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_mutex_init(shared_mutex, &mutex_attributes) != 0)
        return 460;
    (void)pthread_mutexattr_destroy(&mutex_attributes);
    pthread_condattr_t condition_attributes;
    if (pthread_condattr_init(&condition_attributes) != 0 ||
        pthread_condattr_setpshared(&condition_attributes,
                                    PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_cond_init(shared_condition, &condition_attributes) != 0)
        return 461;
    (void)pthread_condattr_destroy(&condition_attributes);

    if (pthread_mutex_lock(shared_mutex) != 0) return 462;
    pid_t pthread_child = fork();
    if (pthread_child < 0) return 463;
    if (pthread_child == 0) {
        int child_descriptor = shm_open(object_name, O_RDWR, 0);
        if (child_descriptor < 0) _exit(464);
        uint32_t *child_mapping = mmap(NULL, 4096,
                                       PROT_READ | PROT_WRITE,
                                       MAP_SHARED, child_descriptor, 0);
        if (child_mapping == MAP_FAILED) _exit(465);
        pthread_mutex_t *child_mutex =
            (pthread_mutex_t *)(void *)&child_mapping[4];
        pthread_cond_t *child_condition =
            (pthread_cond_t *)(void *)&child_mapping[16];
        __atomic_store_n(&child_mapping[50], 1U, __ATOMIC_RELEASE);
        if (pthread_mutex_lock(child_mutex) != 0) _exit(466);
        __atomic_store_n(&child_mapping[51], 1U, __ATOMIC_RELEASE);
        if (pthread_cond_broadcast(child_condition) != 0 ||
            pthread_mutex_unlock(child_mutex) != 0) _exit(467);
        if (munmap(child_mapping, 4096) != 0 ||
            close(child_descriptor) != 0) _exit(468);
        _exit(0);
    }
    if (wait_shared_marker(&mapping[50]) != 0) {
        (void)pthread_mutex_unlock(shared_mutex);
        (void)waitpid(pthread_child, &status, 0);
        return 469;
    }
    if (pthread_cond_wait(shared_condition, shared_mutex) != 0 ||
        __atomic_load_n(&mapping[51], __ATOMIC_ACQUIRE) != 1U) {
        (void)pthread_mutex_unlock(shared_mutex);
        (void)waitpid(pthread_child, &status, 0);
        return 470;
    }
    if (pthread_mutex_unlock(shared_mutex) != 0 ||
        waitpid(pthread_child, &status, 0) != pthread_child || status != 0)
        return 471;

    pthread_rwlockattr_t rwlock_attributes;
    if (pthread_rwlockattr_init(&rwlock_attributes) != 0 ||
        pthread_rwlockattr_setpshared(&rwlock_attributes,
                                      PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_rwlock_init(shared_rwlock, &rwlock_attributes) != 0)
        return 472;
    (void)pthread_rwlockattr_destroy(&rwlock_attributes);
    if (pthread_rwlock_rdlock(shared_rwlock) != 0) return 473;
    pid_t rwlock_child = fork();
    if (rwlock_child < 0) return 474;
    if (rwlock_child == 0) {
        int child_descriptor = shm_open(object_name, O_RDWR, 0);
        if (child_descriptor < 0) _exit(475);
        uint32_t *child_mapping = mmap(NULL, 4096,
                                       PROT_READ | PROT_WRITE,
                                       MAP_SHARED, child_descriptor, 0);
        if (child_mapping == MAP_FAILED) _exit(476);
        pthread_rwlock_t *child_rwlock =
            (pthread_rwlock_t *)(void *)&child_mapping[24];
        __atomic_store_n(&child_mapping[52], 1U, __ATOMIC_RELEASE);
        if (pthread_rwlock_wrlock(child_rwlock) != 0) _exit(477);
        __atomic_store_n(&child_mapping[53], 1U, __ATOMIC_RELEASE);
        if (pthread_rwlock_unlock(child_rwlock) != 0) _exit(478);
        if (munmap(child_mapping, 4096) != 0 ||
            close(child_descriptor) != 0) _exit(479);
        _exit(0);
    }
    if (wait_shared_marker(&mapping[52]) != 0) {
        (void)pthread_rwlock_unlock(shared_rwlock);
        (void)waitpid(rwlock_child, &status, 0);
        return 480;
    }
    if (pthread_rwlock_unlock(shared_rwlock) != 0 ||
        waitpid(rwlock_child, &status, 0) != rwlock_child || status != 0 ||
        __atomic_load_n(&mapping[53], __ATOMIC_ACQUIRE) != 1U)
        return 481;

    pthread_barrierattr_t barrier_attributes;
    if (pthread_barrierattr_init(&barrier_attributes) != 0 ||
        pthread_barrierattr_setpshared(&barrier_attributes,
                                       PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_barrier_init(shared_barrier, &barrier_attributes, 2U) != 0)
        return 482;
    (void)pthread_barrierattr_destroy(&barrier_attributes);
    pid_t barrier_child = fork();
    if (barrier_child < 0) return 483;
    if (barrier_child == 0) {
        int child_descriptor = shm_open(object_name, O_RDWR, 0);
        if (child_descriptor < 0) _exit(484);
        uint32_t *child_mapping = mmap(NULL, 4096,
                                       PROT_READ | PROT_WRITE,
                                       MAP_SHARED, child_descriptor, 0);
        if (child_mapping == MAP_FAILED) _exit(485);
        pthread_barrier_t *child_barrier =
            (pthread_barrier_t *)(void *)&child_mapping[32];
        int result = pthread_barrier_wait(child_barrier);
        if (result != 0 && result != PTHREAD_BARRIER_SERIAL_THREAD)
            _exit(486);
        __atomic_store_n(&child_mapping[54], 1U, __ATOMIC_RELEASE);
        if (munmap(child_mapping, 4096) != 0 ||
            close(child_descriptor) != 0) _exit(487);
        _exit(0);
    }
    int barrier_result = pthread_barrier_wait(shared_barrier);
    if ((barrier_result != 0 &&
         barrier_result != PTHREAD_BARRIER_SERIAL_THREAD) ||
        waitpid(barrier_child, &status, 0) != barrier_child || status != 0 ||
        __atomic_load_n(&mapping[54], __ATOMIC_ACQUIRE) != 1U)
        return 488;

    if (sem_init(shared_semaphore, 1, 0U) != 0) return 489;
    pid_t semaphore_child = fork();
    if (semaphore_child < 0) return 490;
    if (semaphore_child == 0) {
        int child_descriptor = shm_open(object_name, O_RDWR, 0);
        if (child_descriptor < 0) _exit(491);
        uint32_t *child_mapping = mmap(NULL, 4096,
                                       PROT_READ | PROT_WRITE,
                                       MAP_SHARED, child_descriptor, 0);
        if (child_mapping == MAP_FAILED) _exit(492);
        sem_t *child_semaphore = (sem_t *)(void *)&child_mapping[40];
        const struct timespec post_delay = {0, 20000000L};
        if (nanosleep(&post_delay, NULL) != 0 ||
            sem_post(child_semaphore) != 0) _exit(493);
        if (munmap(child_mapping, 4096) != 0 ||
            close(child_descriptor) != 0) _exit(494);
        _exit(0);
    }
    if (sem_wait(shared_semaphore) != 0 ||
        waitpid(semaphore_child, &status, 0) != semaphore_child || status != 0)
        return 495;
    if (sem_destroy(shared_semaphore) != 0 ||
        pthread_barrier_destroy(shared_barrier) != 0 ||
        pthread_rwlock_destroy(shared_rwlock) != 0 ||
        pthread_cond_destroy(shared_condition) != 0 ||
        pthread_mutex_destroy(shared_mutex) != 0)
        return 496;
    puts("Process-shared pthread sync verified");

    if (mprotect(mapping, 4096, PROT_NONE) != 0 ||
        mprotect(mapping, 4096, PROT_READ | PROT_WRITE) != 0) return 450;
    if (mapping[0] != UINT32_C(0xaabbccdd)) return 451;
    mapping[1] = UINT32_C(0xcafef00d);

    if (shm_unlink(object_name) != 0 || close(descriptor) != 0) return 452;
    errno = 0;
    if (shm_open(object_name, O_RDWR, 0) != -1 || errno != ENOENT) return 453;
    if (mapping[1] != UINT32_C(0xcafef00d) ||
        munmap(mapping, 4096) != 0) return 454;

    puts("POSIX shared memory and mprotect verified");
    return 0;
}

int main(int argument_count, char **arguments) {
    if (argument_count == 2 &&
        (strcmp(arguments[1], "--help") == 0 ||
         strcmp(arguments[1], "-h") == 0)) {
        puts("Usage: hello [SELF-TEST-OPTION]\nRun the akaOS userspace validation program.");
        return 0;
    }
    if (argument_count == 3 &&
        strcmp(arguments[1], "--cloexec-check") == 0) {
        char *end = NULL;
        long descriptor = strtol(arguments[2], &end, 10);
        errno = 0;
        if (!end || *end != '\0' || descriptor < 0 || descriptor > INT32_MAX ||
            fcntl((int)descriptor, F_GETFD) != -1 || errno != EBADF) {
            puts("POSIX close-on-exec child failed");
            return 510;
        }
        puts("POSIX close-on-exec child verified");
        return 0;
    }
    if (argument_count == 2 &&
        strcmp(arguments[1], "--spawn-environment-check") == 0) {
        const char *value = getenv("AKAOS_TEST_SPAWN_ENV");
        if (!value || strcmp(value, "inherited") != 0) {
            puts("akaOS spawn() child environment was not inherited");
            return 530;
        }
        puts("akaOS spawn() child environment verified");
        return 0;
    }
    if (argument_count == 2 &&
        strcmp(arguments[1], "--spawn-signal-check") == 0) {
        sigset_t current_mask;
        struct sigaction current_action;
        if (sigprocmask(SIG_SETMASK, NULL, &current_mask) != 0 ||
            sigismember(&current_mask, SIGUSR2) != 1 ||
            sigaction(SIGUSR1, NULL, &current_action) != 0 ||
            current_action.sa_handler != SIG_DFL) {
            printf("POSIX spawn child signal state failed mask=%x "
                   "blocked=%d handler=%p errno=%d\n", current_mask,
                   sigismember(&current_mask, SIGUSR2),
                   (void *)current_action.sa_handler, errno);
            return 527;
        }
        puts("POSIX spawn child signal state verified");
        return 0;
    }
    for (int option_index = 1; option_index < argument_count; ++option_index) {
        if (!is_properties_signal_option(arguments[option_index])) continue;
        int result = akaos_dbus_binary_properties_signal_test();
        if (result != 0)
            printf("D-Bus PropertiesChanged signal test failed (errno=%d)\n",
                   errno);
        else
            puts("D-Bus PropertiesChanged signals verified");
        return result;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--exec-target") == 0) {
        if (!environ || !environ[0] || environ[1] != NULL ||
            strcmp(environ[0], "AKAOS_EXEC_TEST=1") != 0) {
            puts("execve environment propagation failed");
            return 413;
        }
        puts("POSIX execve userspace verified");
        return 0;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--pagecache-test") == 0) {
        int result = test_clean_page_cache();
        if (result != 0) printf("pagecache test failed: %d\n", result);
        return result;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--stack-growth-test") == 0) {
        uint32_t result = test_stack_growth_recursive(12);
        if (result == 0) return 90;
        puts("stack growth verified");
        return 0;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--vm-test") == 0) {
        int vm_result = test_demand_paging_and_cow();
        if (vm_result != 0) printf("vm test failed: %d\n", vm_result);
        return vm_result;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--usercopy-test") == 0) {
        int result = test_safe_usercopy();
        if (result != 0) printf("usercopy test failed: %d\n", result);
        return result;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--signal-test") == 0) {
        int result = test_user_defined_signals();
        if (result != 0) printf("signal test failed: %d\n", result);
        return result;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--exec-test") == 0) {
        int result = test_execve_replacement();
        if (result != 0) printf("execve test failed: %d\n", result);
        return result;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--mmap-test") == 0) {
        int result = test_mmap_private();
        if (result != 0) printf("mmap test failed: %d\n", result);
        return result;
    }
    if (argument_count == 2 &&
        strcmp(arguments[1], "--shared-memory-test") == 0) {
        int result = test_shared_memory();
        if (result != 0) printf("shared memory test failed: %d\n", result);
        return result;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--poll-test") == 0) {
        int result = test_poll_and_input();
        if (result != 0) printf("poll/input test failed: %d\n", result);
        return result;
    }
    if (argument_count == 2 &&
        strcmp(arguments[1], "--framebuffer-test") == 0) {
        int result = test_framebuffer_device();
        if (result != 0) printf("framebuffer test failed: %d\n", result);
        return result;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--devfs-test") == 0) {
        int result = test_device_filesystem();
        if (result != 0) printf("devfs test failed: %d\n", result);
        return result;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--session-test") == 0) {
        int result = test_sessions_and_terminal_groups();
        if (result != 0) printf("session test failed: %d\n", result);
        return result;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--pty-test") == 0) {
        int result = test_pseudoterminal_pair();
        if (result != 0) printf("pty test failed: %d\n", result);
        return result;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--proc-test") == 0) {
        int result = test_proc_namespace();
        if (result != 0) printf("proc test failed: %d\n", result);
        return result;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--pthread-test") == 0) {
        int result = test_pthread_compatibility();
        if (result != 0) printf("pthread compatibility test failed: %d\n", result);
        return result;
    }
    if (argument_count == 2 &&
        strcmp(arguments[1], "--desktop-runtime-test") == 0) {
        int result = test_desktop_runtime();
        if (result != 0)
            printf("desktop runtime test failed (stage=%d)\n", result);
        return result;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--dbus-test") == 0) {
        return test_dbus_bus(0);
    }
    if (argument_count == 2 && strcmp(arguments[1], "--dbus-system-test") == 0) {
        return test_dbus_bus(1);
    }
    if (argument_count == 2 && strcmp(arguments[1], "--dbus-binary-test") == 0) {
        return test_dbus_binary_bus();
    }
    if (argument_count == 2 && strcmp(arguments[1], "--dbus-names-test") == 0) {
        return test_dbus_names();
    }
    if (argument_count == 2 &&
        strcmp(arguments[1], "--dbus-standard-test") == 0) {
        int result = akaos_dbus_binary_standard_test();
        if (result != 0)
            printf("D-Bus standard broker test failed (errno=%d)\n", errno);
        else
            puts("D-Bus standard broker methods verified");
        return result;
    }
    if (argument_count == 2 &&
        strcmp(arguments[1], "--dbus-endian-test") == 0) {
        int result = akaos_dbus_binary_byte_order_test();
        if (result != 0)
            printf("D-Bus big-endian frame test failed (errno=%d)\n", errno);
        else
            puts("D-Bus big-endian frame interoperability verified");
        return result;
    }
    if (argument_count == 2 &&
        strcmp(arguments[1], "--dbus-marshaller-test") == 0) {
        int result = akaos_dbus_marshaller_self_test();
        if (result != 0)
            printf("D-Bus generic signature codec failed (errno=%d)\n", errno);
        else
            puts("D-Bus generic signature marshaller verified");
        return result;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--dbus-match-test") == 0) {
        return test_dbus_matches();
    }
    if (argument_count == 2 && strcmp(arguments[1], "--dbus-route-test") == 0) {
        return test_dbus_routing();
    }
    if (argument_count == 2 &&
        strcmp(arguments[1], "--dbus-activation-test") == 0) {
        int result = akaos_dbus_binary_activation_test();
        if (result != 0)
            printf("D-Bus service activation test failed (errno=%d)\n", errno);
        else
            puts("D-Bus service activation verified");
        return result;
    }
    if (argument_count == 2 &&
        strcmp(arguments[1], "--dbus-activation-service") == 0) {
        return akaos_dbus_binary_activation_service();
    }
    if (argument_count == 2 &&
        strcmp(arguments[1], "--dbus-registration-test") == 0) {
        int result = akaos_dbus_binary_registration_test();
        if (result != 0)
            printf("D-Bus service registration test failed (errno=%d)\n", errno);
        else
            puts("D-Bus per-user service registration verified");
        return result;
    }
    if (argument_count == 2 &&
        strcmp(arguments[1], "--dbus-desktop-service-test") == 0) {
        int result = akaos_dbus_binary_desktop_service_test();
        if (result != 0)
            printf("D-Bus desktop service routing failed (errno=%d)\n", errno);
        else
            puts("D-Bus desktop service routing verified");
        return result;
    }
    if (argument_count == 2 &&
        strcmp(arguments[1], "--dbus-owner-signal-test") == 0) {
        int result = akaos_dbus_binary_owner_signal_test();
        if (result != 0)
            printf("D-Bus owner signal test failed (errno=%d)\n", errno);
        else
            puts("D-Bus owner-change signals verified");
        return result;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--toolkit-test") == 0) {
        return test_toolkit_bridge();
    }
    if (argument_count == 2 && strcmp(arguments[1], "--unix-socket-test") == 0) {
        return test_unix_stream_socket();
    }
    if (argument_count == 2 && strcmp(arguments[1], "--select-test") == 0) {
        return test_select_compatibility();
    }
    if (argument_count == 2 && strcmp(arguments[1], "--epoll-test") == 0) {
        return test_epoll_compatibility();
    }
    if (argument_count == 2 && strcmp(arguments[1], "--exit-group-test") == 0) {
        return test_exit_group_compatibility();
    }
    if (argument_count == 2 && strcmp(arguments[1], "--eventfd-test") == 0) {
        return test_eventfd_compatibility();
    }
    if (argument_count == 2 && strcmp(arguments[1], "--timerfd-test") == 0) {
        int result = test_timerfd_compatibility();
        if (result != 0)
            printf("timerfd compatibility test failed (stage=%d errno=%d)\n",
                   result, errno);
        return result;
    }
    if (argument_count == 2 &&
        strcmp(arguments[1], "--getaddrinfo-test") == 0) {
        return test_getaddrinfo_compatibility();
    }
    if (argument_count == 2 && strcmp(arguments[1], "--locale-test") == 0) {
        return test_locale_utf8_compatibility();
    }
    if (argument_count == 2 && strcmp(arguments[1], "--posix-test") == 0) {
        return test_posix_process_compatibility();
    }
    if (argument_count == 2 && strcmp(arguments[1], "--display-protocol-test") == 0) {
        return test_display_protocols();
    }
    if (argument_count == 2 && strcmp(arguments[1], "--heap-test") == 0) {
        int result = test_heap_allocator();
        if (result == 0) puts("heap allocator self-test passed");
        return result;
    }
    if (argument_count != 1) {
        puts("hello: invalid option or extra operand");
        return 1;
    }

    allow_timer_preemption();

    char buffer[64];
    int descriptor = open("/etc/motd", O_RDONLY);
    if (descriptor != 3) return 10;
    ssize_t count = read(descriptor, buffer, sizeof(buffer));
    if (count != (ssize_t)(sizeof(expected_motd) - 1) ||
        memcmp(buffer, expected_motd, sizeof(expected_motd) - 1) != 0) return 11;
    if (!write_complete(STDOUT_FILENO, buffer, (size_t)count)) return 12;
    if (read(descriptor, buffer, 1) != 0) return 13;
    if (close(descriptor) != 0) return 14;

    errno = 0;
    if (open("/does/not/exist", O_RDONLY) != -1 || errno != ENOENT) return 15;
    errno = 0;
    if (open("/etc/motd", O_WRONLY) != -1 || errno != EROFS) return 16;

    if (!write_complete(STDOUT_FILENO, hello_message,
                        sizeof(hello_message) - 1)) return 17;

    int duplicate = dup(STDOUT_FILENO);
    if (duplicate != 3 || close(duplicate) != 0) return 18;
    if (dup2(STDOUT_FILENO, 4) != 4 || close(4) != 0) return 19;
    errno = 0;
    if (write(4, hello_message, sizeof(hello_message) - 1) != -1 ||
        errno != EBADF) return 20;

    /* Execute heap & formatted I/O validation */
    int heap_err = test_heap_allocator();
    if (heap_err != 0) return heap_err;

    /* Execute pipe IPC validation */
    int pipe_err = test_pipe_ipc();
    if (pipe_err != 0) return pipe_err;

    int cache_err = test_clean_page_cache();
    if (cache_err != 0) return cache_err;

    if (test_stack_growth_recursive(12) == 0) return 91;
    puts("stack growth verified");

    return 0;
}
