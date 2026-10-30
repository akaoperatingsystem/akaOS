#ifndef AKAOS_DBUS_H
#define AKAOS_DBUS_H

#include <stddef.h>
#include <stdint.h>

/* Native session-bus bootstrap contract.  Line framing remains available for
 * tiny early-boot clients, while the daemon also accepts the standard D-Bus
 * SASL EXTERNAL/BEGIN handshake and bounded little-endian method frames. */
#define AKAOS_DBUS_ABI_VERSION UINT32_C(1)
#define AKAOS_DBUS_SESSION_ENDPOINT "/run/user/0/bus"
#define AKAOS_DBUS_SYSTEM_ENDPOINT "/run/dbus/system_bus_socket"
#define AKAOS_DBUS_SESSION_PORT UINT16_C(8765)
#define AKAOS_DBUS_SYSTEM_PORT UINT16_C(8766)
#define AKAOS_DBUS_GREETING "AKAOS-DBUS 1\n"
#define AKAOS_DBUS_NATIVE_MARKER "NATIVE 1\n"

/* A small asynchronous native-bus transaction that can be driven by the
 * GLib-shaped descriptor sources in desktop_runtime.h.  The object is
 * caller-owned and deliberately contains only fixed-size state, so it is
 * safe to keep it in a service's event-loop context without heap ownership
 * or a hidden thread. */
#define AKAOS_DBUS_ASYNC_PING_REQUEST_SIZE 5U
#define AKAOS_DBUS_ASYNC_PING_RESPONSE_SIZE 5U
struct akaos_dbus_async_ping {
    int descriptor;
    uint32_t state;
    size_t received;
    unsigned char response[AKAOS_DBUS_ASYNC_PING_RESPONSE_SIZE];
};

/* Start a native PING transaction on an already handshaken connection.
 * The request is sent before returning; subsequent progress is nonblocking
 * and must be driven from a POLLIN/G_IO_IN descriptor callback. */
int akaos_dbus_async_ping_init(struct akaos_dbus_async_ping *ping,
                               int descriptor);
/* Drive one nonblocking read step.  Returns 1 when PONG is complete, 0 while
 * waiting for more input, and -1 on EOF, a protocol mismatch, or an error. */
int akaos_dbus_async_ping_pump(struct akaos_dbus_async_ping *ping,
                               short revents);
int akaos_dbus_async_ping_done(const struct akaos_dbus_async_ping *ping);

/* Bounded standard D-Bus method-call transport.  The connection returned by
 * akaos_dbus_binary_connect() has completed SASL EXTERNAL/BEGIN; the async
 * object then sends org.freedesktop.DBus.Ping and parses one METHOD_RETURN
 * frame without blocking. */
#define AKAOS_DBUS_ASYNC_BINARY_FRAME_CAPACITY 512U
struct akaos_dbus_async_binary_ping {
    int descriptor;
    uint32_t state;
    size_t request_length;
    size_t response_received;
    size_t response_expected;
    unsigned char request[AKAOS_DBUS_ASYNC_BINARY_FRAME_CAPACITY];
    unsigned char response[AKAOS_DBUS_ASYNC_BINARY_FRAME_CAPACITY];
};

int akaos_dbus_binary_connect(void);
int akaos_dbus_async_binary_ping_init(
    struct akaos_dbus_async_binary_ping *ping, int descriptor);
int akaos_dbus_async_binary_ping_pump(
    struct akaos_dbus_async_binary_ping *ping, short revents);
int akaos_dbus_async_binary_ping_done(
    const struct akaos_dbus_async_binary_ping *ping);

/* Bounded reusable asynchronous request queue.  This is intentionally
 * caller-owned: desktop services keep the object with their main-context
 * state and drive it from a POLLIN/G_IO_IN watch. */
#define AKAOS_DBUS_ASYNC_CLIENT_FRAME_CAPACITY 2048U
#define AKAOS_DBUS_ASYNC_CLIENT_REPLY_CAPACITY 256U
#define AKAOS_DBUS_ASYNC_CLIENT_MAX_PENDING 4U
#define AKAOS_DBUS_ASYNC_CLIENT_MAX_RETRIES 3U
#define AKAOS_DBUS_ASYNC_CLIENT_STRING_CAPACITY 96U
#define AKAOS_DBUS_MAX_UNIX_FDS 4U
struct akaos_dbus_async_reply {
    uint32_t used;
    uint32_t serial;
    uint32_t state;
    size_t body_length;
    unsigned char body[AKAOS_DBUS_ASYNC_CLIENT_REPLY_CAPACITY];
    size_t fd_count;
    int fds[AKAOS_DBUS_MAX_UNIX_FDS];
};
struct akaos_dbus_async_client {
    int descriptor;
    uint32_t state;
    uint32_t next_serial;
    size_t response_received;
    size_t response_expected;
    unsigned char response[AKAOS_DBUS_ASYNC_CLIENT_FRAME_CAPACITY];
    size_t response_fd_count;
    int response_fds[AKAOS_DBUS_MAX_UNIX_FDS];
    struct akaos_dbus_async_reply replies[AKAOS_DBUS_ASYNC_CLIENT_MAX_PENDING];
};

int akaos_dbus_async_client_init(struct akaos_dbus_async_client *client,
                                 int descriptor);
/* Explicitly release the descriptor and all queued/reply state.  The object
 * remains reusable after a new init, reconnect, or bounded retry operation. */
int akaos_dbus_async_client_close(struct akaos_dbus_async_client *client);
/* Drop all outstanding requests, close the current authenticated connection,
 * and establish a fresh binary session-bus connection.  Callers must remove
 * the old descriptor watch before reconnecting, then register the returned
 * descriptor again.  Old serials are invalid after a successful reconnect. */
int akaos_dbus_async_client_reconnect(struct akaos_dbus_async_client *client);
/* Retry a closed/error client with a bounded number of fresh authenticated
 * connections.  A successful attempt resets serial state; callers must attach
 * the newly returned descriptor to their event-loop context. */
int akaos_dbus_async_client_retry(struct akaos_dbus_async_client *client,
                                  unsigned max_attempts);
int akaos_dbus_async_client_queue_call(
    struct akaos_dbus_async_client *client, const char *destination,
    const char *path, const char *interface_name, const char *member,
    const char *signature, const unsigned char *body, size_t body_length,
    uint32_t *serial_out);
/* Like queue_call(), but attaches a bounded SCM_RIGHTS fd list.  D-Bus `h`
 * values in the body are zero-based indices into `fds`; both the header's
 * UNIX_FDS count and every handle index are checked before sending. */
int akaos_dbus_async_client_queue_call_with_fds(
    struct akaos_dbus_async_client *client, const char *destination,
    const char *path, const char *interface_name, const char *member,
    const char *signature, const unsigned char *body, size_t body_length,
    const int *fds, size_t fd_count, uint32_t *serial_out);
int akaos_dbus_async_client_pump(struct akaos_dbus_async_client *client,
                                 short revents);
size_t akaos_dbus_async_client_pending(
    const struct akaos_dbus_async_client *client);
int akaos_dbus_async_client_take_reply(struct akaos_dbus_async_client *client,
                                       uint32_t serial, void *body,
                                       size_t body_capacity,
                                       size_t *body_length, int *is_error);
/* Transfers reply descriptors to the caller on success.  The legacy
 * take_reply() wrapper closes any reply descriptors it cannot return. */
int akaos_dbus_async_client_take_reply_with_fds(
    struct akaos_dbus_async_client *client, uint32_t serial, void *body,
    size_t body_capacity, size_t *body_length, int *fds,
    size_t fd_capacity, size_t *fd_count, int *is_error);
/* Mark a pending serial cancelled.  The slot remains consumable exactly once;
 * take_reply() returns -1 with ECANCELED so callers can distinguish it from a
 * missing serial and release the bounded slot. */
int akaos_dbus_async_client_cancel(struct akaos_dbus_async_client *client,
                                   uint32_t serial);

int akaos_dbus_connect(int system_bus);
int akaos_dbus_ping(int descriptor);
int akaos_dbus_binary_ping(void);
int akaos_dbus_binary_bus_test(void);
int akaos_dbus_binary_standard_test(void);
int akaos_dbus_binary_byte_order_test(void);
int akaos_dbus_binary_match_test(void);
int akaos_dbus_binary_route_test(void);
int akaos_dbus_binary_activation_test(void);
int akaos_dbus_binary_activation_service(void);
/* Session-bus service activation registration.  The broker accepts the
 * bounded native registration ABI and persists definitions under /run; the
 * executable must be an akaOS path and is started on first addressed call. */
int akaos_dbus_register_service(const char *name, const char *path,
                                const char *argument, int restart,
                                uint32_t timeout_ms);
int akaos_dbus_unregister_service(const char *name);
/* Claim a well-known session-bus name and keep the returned binary bus
 * connection open for the lifetime of the owner.  Release sends the matching
 * ReleaseName call and closes that connection. */
int akaos_dbus_claim_name(const char *name);
int akaos_dbus_release_name(int descriptor, const char *name);
/* Poll a claimed binary bus connection and answer one routed method call with
 * a bounded string status.  Returns 1 when a call was handled, 0 on timeout,
 * and -1 when the connection or frame is unusable. */
int akaos_dbus_service_dispatch(int descriptor, const char *status,
                                int timeout_ms);
/* Emit the bounded desktop PropertiesChanged signal used by session roles
 * when their published state changes.  The native boundary publishes the
 * complete typed desktop property set (Status, Ready, Capabilities,
 * DisplayServer, Toolkit, and SessionType) at /org/akaos/Desktop; callers
 * keep ownership of the claimed bus descriptor. */
int akaos_dbus_emit_desktop_properties_changed(int descriptor,
                                               const char *status);
int akaos_dbus_binary_desktop_service_test(void);
int akaos_dbus_binary_properties_signal_test(void);
int akaos_dbus_binary_registration_test(void);
int akaos_dbus_binary_owner_signal_test(void);
int akaos_dbus_close(int descriptor);

/* Generic, caller-owned D-Bus body codec.  The native broker still exposes
 * bounded policy methods, but desktop libraries need one reusable wire
 * implementation for nested arrays, structs, dictionary entries, and
 * variants instead of a collection of method-specific encoders.  Values do
 * not own their child storage; callers keep the value tree alive for the
 * duration of akaos_dbus_marshal(). */
#define AKAOS_DBUS_CODEC_MAX_SIGNATURE 255U
#define AKAOS_DBUS_CODEC_MAX_DEPTH 32U
#define AKAOS_DBUS_BYTE_ORDER_LITTLE_ENDIAN 'l'
#define AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN 'B'
enum akaos_dbus_value_kind {
    AKAOS_DBUS_VALUE_BYTE = 1,
    AKAOS_DBUS_VALUE_BOOLEAN,
    AKAOS_DBUS_VALUE_INT16,
    AKAOS_DBUS_VALUE_UINT16,
    AKAOS_DBUS_VALUE_INT32,
    AKAOS_DBUS_VALUE_UINT32,
    AKAOS_DBUS_VALUE_INT64,
    AKAOS_DBUS_VALUE_UINT64,
    AKAOS_DBUS_VALUE_DOUBLE_BITS,
    AKAOS_DBUS_VALUE_STRING,
    AKAOS_DBUS_VALUE_OBJECT_PATH,
    AKAOS_DBUS_VALUE_SIGNATURE,
    AKAOS_DBUS_VALUE_ARRAY,
    AKAOS_DBUS_VALUE_STRUCT,
    AKAOS_DBUS_VALUE_DICT_ENTRY,
    AKAOS_DBUS_VALUE_VARIANT,
    AKAOS_DBUS_VALUE_UNIX_FD,
};

struct akaos_dbus_value {
    uint32_t kind;
    union {
        uint8_t byte_value;
        int boolean_value;
        int16_t int16_value;
        uint16_t uint16_value;
        int32_t int32_value;
        uint32_t uint32_value;
        int64_t int64_value;
        uint64_t uint64_value;
        uint64_t double_bits;
        const char *string_value;
    } scalar;
    const struct akaos_dbus_value *children;
    size_t child_count;
    const char *variant_signature;
};

/* Caller-owned storage for akaos_dbus_unmarshal().  The decoder resets the
 * used counters on each call; child pointers in the resulting value tree
 * refer into `values`, and scalar strings refer into `string_storage` when it
 * is supplied (otherwise they point into the input body). */
struct akaos_dbus_decode_context {
    struct akaos_dbus_value *values;
    size_t value_capacity;
    size_t value_used;
    char *string_storage;
    size_t string_capacity;
    size_t string_used;
};

/* Validate a complete D-Bus signature.  type_count receives its number of
 * top-level complete types when non-NULL. */
int akaos_dbus_signature_validate(const char *signature,
                                  size_t *type_count);
/* Validate the slash-separated D-Bus object-path grammar. */
int akaos_dbus_object_path_validate(const char *path);
/* Validate a body against a complete signature, including all alignment,
 * length, NUL-termination, and nested-container boundaries. */
int akaos_dbus_body_validate(const char *signature, const void *body,
                             size_t body_length, size_t *consumed_length);
/* Explicit-byte-order variants for messages received from peers using either
 * D-Bus wire byte order.  The legacy entry points above/below stay
 * little-endian for source and ABI compatibility. */
int akaos_dbus_body_validate_with_byte_order(
    const char *signature, const void *body, size_t body_length,
    int byte_order, size_t *consumed_length);
/* Convert a validated body in place between D-Bus byte orders.  Its byte
 * length is unchanged; strings/signatures remain byte sequences while
 * numeric fields and length prefixes are rewritten. */
int akaos_dbus_body_convert_byte_order(const char *signature, void *body,
                                       size_t body_length,
                                       int source_byte_order,
                                       int destination_byte_order);
/* Validate and normalize one complete D-Bus frame in place to little-endian.
 * This includes fixed header words, standard basic-valued header fields, and
 * the signed body. */
int akaos_dbus_message_normalize_little_endian(void *frame,
                                               size_t frame_length);
/* Encode a value tree against a complete signature. */
int akaos_dbus_marshal(const char *signature,
                       const struct akaos_dbus_value *values,
                       size_t value_count, void *body, size_t body_capacity,
                       size_t *body_length);
int akaos_dbus_marshal_with_byte_order(
    const char *signature, const struct akaos_dbus_value *values,
    size_t value_count, void *body, size_t body_capacity, int byte_order,
    size_t *body_length);
/* Decode a complete body into caller-owned value and string arenas. */
int akaos_dbus_unmarshal(const char *signature, const void *body,
                         size_t body_length,
                         struct akaos_dbus_decode_context *context,
                         size_t *value_count);
int akaos_dbus_unmarshal_with_byte_order(
    const char *signature, const void *body, size_t body_length,
    int byte_order, struct akaos_dbus_decode_context *context,
    size_t *value_count);
/* Cross-architecture codec regression covering primitive and nested types. */
int akaos_dbus_marshaller_self_test(void);

#endif
