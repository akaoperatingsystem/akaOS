/* Wire helpers for the akaOS X11 display server.
 *
 * The protocol layouts come from the vendored upstream X11 protocol header so
 * that the server and real X clients agree on every field offset.  Payloads
 * are decoded through explicit accessors instead of casting to packed
 * structures, which keeps the server independent of host alignment rules.
 */
#ifndef AKAOS_XSERVER_PROTO_H
#define AKAOS_XSERVER_PROTO_H

#include "xbase.h"

#include <X11/Xproto.h>

#include <stdint.h>
#include <stddef.h>

/* The server identifies every client resource with a 32-bit identifier. */
typedef uint32_t xid_t;

/* A bounds-checked reader over a request or reply payload. */
struct x_reader {
    const uint8_t *data;
    size_t length;
    size_t position;
};

/* A bounds-checked writer used to build reply payloads. */
struct x_writer {
    uint8_t *data;
    size_t capacity;
    size_t position;
};

static inline void x_reader_init(struct x_reader *reader, const void *data,
                                 size_t length) {
    reader->data = (const uint8_t *)data;
    reader->length = length;
    reader->position = 0U;
}

static inline int x_reader_has(const struct x_reader *reader, size_t count) {
    return reader->position + count <= reader->length;
}

static inline uint8_t x_reader_u8(struct x_reader *reader) {
    if (!x_reader_has(reader, 1U)) return 0U;
    return reader->data[reader->position++];
}

static inline uint16_t x_reader_u16(struct x_reader *reader) {
    if (!x_reader_has(reader, 2U)) { reader->position = reader->length; return 0U; }
    uint16_t value = (uint16_t)reader->data[reader->position] |
                     (uint16_t)((uint16_t)reader->data[reader->position + 1U] << 8);
    reader->position += 2U;
    return value;
}

static inline uint32_t x_reader_u32(struct x_reader *reader) {
    if (!x_reader_has(reader, 4U)) { reader->position = reader->length; return 0U; }
    uint32_t value = (uint32_t)reader->data[reader->position] |
                     ((uint32_t)reader->data[reader->position + 1U] << 8) |
                     ((uint32_t)reader->data[reader->position + 2U] << 16) |
                     ((uint32_t)reader->data[reader->position + 3U] << 24);
    reader->position += 4U;
    return value;
}

static inline int16_t x_reader_i8(struct x_reader *reader) {
    return (int8_t)x_reader_u8(reader);
}

static inline int32_t x_reader_i16(struct x_reader *reader) {
    return (int16_t)x_reader_u16(reader);
}

static inline void x_reader_skip(struct x_reader *reader, size_t count) {
    reader->position += count;
    if (reader->position > reader->length) reader->position = reader->length;
}

static inline const uint8_t *x_reader_bytes(struct x_reader *reader,
                                           size_t count) {
    if (!x_reader_has(reader, count)) {
        reader->position = reader->length;
        return NULL;
    }
    const uint8_t *base = reader->data + reader->position;
    reader->position += count;
    return base;
}

static inline void x_writer_init(struct x_writer *writer, void *data,
                                 size_t capacity) {
    writer->data = (uint8_t *)data;
    writer->capacity = capacity;
    writer->position = 0U;
}

static inline int x_writer_has(const struct x_writer *writer, size_t count) {
    return writer->position + count <= writer->capacity;
}

static inline void x_writer_u8(struct x_writer *writer, uint8_t value) {
    if (!x_writer_has(writer, 1U)) return;
    writer->data[writer->position++] = value;
}

static inline void x_writer_u16(struct x_writer *writer, uint16_t value) {
    if (!x_writer_has(writer, 2U)) return;
    writer->data[writer->position++] = (uint8_t)(value & 0xffU);
    writer->data[writer->position++] = (uint8_t)((value >> 8) & 0xffU);
}

static inline void x_writer_u32(struct x_writer *writer, uint32_t value) {
    if (!x_writer_has(writer, 4U)) return;
    writer->data[writer->position++] = (uint8_t)(value & 0xffU);
    writer->data[writer->position++] = (uint8_t)((value >> 8) & 0xffU);
    writer->data[writer->position++] = (uint8_t)((value >> 16) & 0xffU);
    writer->data[writer->position++] = (uint8_t)((value >> 24) & 0xffU);
}

static inline void x_writer_pad(struct x_writer *writer, size_t alignment) {
    size_t target = (writer->position + alignment - 1U) & ~(alignment - 1U);
    while (writer->position < target) x_writer_u8(writer, 0U);
}

#endif
