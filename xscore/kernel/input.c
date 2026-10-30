#include <xscore/input.h>
#include <xscore/process.h>
#include <xscore/thread.h>
#include <xscore/tty.h>
#include <akaos/errno.h>
#include <stdint.h>

#define INPUT_EVENT_CAPACITY UINT64_C(128)

static struct akaos_input_event input_events[INPUT_EVENT_CAPACITY];
static volatile uint64_t input_next_sequence;
static volatile uint32_t input_clients;

void xscore_input_initialize(void) {
    input_next_sequence = 0;
    input_clients = 0;
}

void xscore_input_client_open(void) {
    __atomic_add_fetch(&input_clients, 1U, __ATOMIC_ACQ_REL);
}

void xscore_input_client_close(void) {
    uint32_t current = __atomic_load_n(&input_clients, __ATOMIC_ACQUIRE);
    while (current != 0U &&
           !__atomic_compare_exchange_n(&input_clients, &current, current - 1U,
                                        0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) { }
}

int xscore_input_is_grabbed(void) {
    return __atomic_load_n(&input_clients, __ATOMIC_ACQUIRE) != 0U;
}

void xscore_input_publish(uint16_t type, uint16_t code, int32_t value) {
    uint64_t sequence = input_next_sequence;
    input_events[sequence % INPUT_EVENT_CAPACITY].type = type;
    input_events[sequence % INPUT_EVENT_CAPACITY].code = code;
    input_events[sequence % INPUT_EVENT_CAPACITY].value = value;
    __asm__ volatile("" ::: "memory");
    input_next_sequence = sequence + 1;
}

uint64_t xscore_input_open_cursor(void) {
    return input_next_sequence;
}

int xscore_input_ready(uint64_t cursor) {
    xscore_tty_poll_keyboard();
    return cursor != input_next_sequence;
}

int64_t xscore_input_read(uint64_t *cursor, void *buffer, uint64_t length) {
    if (!cursor || !buffer) return -AKAOS_EFAULT;
    if (length < sizeof(struct akaos_input_event)) return -AKAOS_EINVAL;

    while (!xscore_input_ready(*cursor)) {
        if (xscore_process_signal_pending_current()) return -AKAOS_EINTR;
        xscore_thread_yield();
    }

    uint64_t next = input_next_sequence;
    uint64_t oldest = next > INPUT_EVENT_CAPACITY
                          ? next - INPUT_EVENT_CAPACITY
                          : 0;
    if (*cursor < oldest) *cursor = oldest;

    uint64_t capacity = length / sizeof(struct akaos_input_event);
    uint64_t available = next - *cursor;
    uint64_t count = available < capacity ? available : capacity;
    struct akaos_input_event *destination = buffer;
    for (uint64_t index = 0; index < count; ++index) {
        destination[index] =
            input_events[(*cursor + index) % INPUT_EVENT_CAPACITY];
    }
    *cursor += count;
    return (int64_t)(count * sizeof(struct akaos_input_event));
}
