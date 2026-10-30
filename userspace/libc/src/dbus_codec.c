#include <akaos/dbus.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

/*
 * Little-endian D-Bus body codec.
 *
 * The broker's policy layer intentionally remains separate from this file:
 * this code knows how to walk and encode the wire type system, while the bus
 * decides which interfaces and credentials a caller may use.  Keeping the
 * codec independent means the same implementation is available to GTK/GIO,
 * Qt, and native services on every XSCore architecture.
 */

struct dbus_codec_writer {
    unsigned char *bytes;
    size_t capacity;
    size_t offset;
    int big_endian;
};

struct dbus_codec_reader {
    const unsigned char *bytes;
    size_t length;
    size_t offset;
    int big_endian;
};

static int codec_byte_order_valid(int byte_order) {
    return byte_order == AKAOS_DBUS_BYTE_ORDER_LITTLE_ENDIAN ||
           byte_order == AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN;
}

static int codec_signature_length(const char *signature, size_t *length_out) {
    if (!signature || !length_out) return 0;
    size_t length = 0U;
    while (length <= AKAOS_DBUS_CODEC_MAX_SIGNATURE &&
           signature[length] != '\0')
        ++length;
    if (length > AKAOS_DBUS_CODEC_MAX_SIGNATURE) return 0;
    *length_out = length;
    return 1;
}

static int codec_basic_type(char type) {
    return type == 'y' || type == 'b' || type == 'n' || type == 'q' ||
           type == 'i' || type == 'u' || type == 'x' || type == 't' ||
           type == 'd' || type == 's' || type == 'o' || type == 'g' ||
           type == 'h';
}

static int codec_type_alignment(char type) {
    switch (type) {
    case 'y':
    case 'g':
    case 'v':
        return 1;
    case 'n':
    case 'q':
        return 2;
    case 'b':
    case 'i':
    case 'u':
    case 'h':
    case 's':
    case 'o':
    case 'a':
        return 4;
    case 'x':
    case 't':
    case 'd':
    case '(':
    case '{':
        return 8;
    default:
        return 0;
    }
}

static int codec_parse_type(const char *signature, size_t length,
                            size_t *cursor, unsigned depth,
                            int dictionary_allowed) {
    if (!signature || !cursor || *cursor >= length ||
        depth > AKAOS_DBUS_CODEC_MAX_DEPTH)
        return 0;
    char type = signature[(*cursor)++];
    if (codec_basic_type(type) || type == 'v') return 1;
    if (type == 'a') {
        return codec_parse_type(signature, length, cursor, depth + 1U, 1);
    }
    if (type == '(') {
        size_t children = 0U;
        while (*cursor < length && signature[*cursor] != ')') {
            if (!codec_parse_type(signature, length, cursor, depth + 1U, 0))
                return 0;
            ++children;
        }
        if (*cursor >= length || signature[*cursor] != ')' || children == 0U)
            return 0;
        ++*cursor;
        return 1;
    }
    if (type == '{') {
        if (!dictionary_allowed || *cursor >= length) return 0;
        char key = signature[*cursor];
        if (!codec_basic_type(key) || key == 'v') return 0;
        ++*cursor;
        if (!codec_parse_type(signature, length, cursor, depth + 1U, 0) ||
            *cursor >= length || signature[*cursor] != '}')
            return 0;
        ++*cursor;
        return 1;
    }
    return 0;
}

static int codec_next_type(const char *signature, size_t length,
                           size_t start, int dictionary_allowed,
                           size_t *end_out) {
    size_t cursor = start;
    if (!codec_parse_type(signature, length, &cursor, 0U,
                          dictionary_allowed)) return 0;
    if (end_out) *end_out = cursor;
    return 1;
}

int akaos_dbus_signature_validate(const char *signature,
                                  size_t *type_count) {
    size_t length = 0U;
    if (!codec_signature_length(signature, &length)) {
        errno = EINVAL;
        return -1;
    }
    size_t cursor = 0U;
    size_t count = 0U;
    while (cursor < length) {
        size_t next = cursor;
        if (!codec_next_type(signature, length, cursor, 0, &next) ||
            next <= cursor) {
            errno = EINVAL;
            return -1;
        }
        cursor = next;
        ++count;
    }
    if (type_count) *type_count = count;
    return 0;
}

/* D-Bus object paths are slash-separated, non-empty components.  The root
 * path is the one exception and is represented by a single '/'. */
static int codec_valid_object_path(const char *path, size_t length) {
    if (!path || length == 0U || path[0] != '/') return 0;
    if (length == 1U) return 1;
    size_t component_start = 1U;
    for (size_t index = 1U; index < length; ++index) {
        unsigned char character = (unsigned char)path[index];
        if (character == '/') {
            if (index == component_start) return 0;
            component_start = index + 1U;
            continue;
        }
        if (!((character >= 'A' && character <= 'Z') ||
              (character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') ||
              character == '_'))
            return 0;
    }
    return component_start < length;
}

/* D-Bus strings must be valid UTF-8.  Decode code points explicitly so
 * overlong encodings, UTF-16 surrogates, truncated sequences, and values
 * beyond U+10FFFF are rejected consistently on every target ABI. */
static int codec_valid_utf8(const unsigned char *text, size_t length) {
    if (length != 0U && !text) return 0;
    size_t offset = 0U;
    while (offset < length) {
        uint8_t lead = text[offset++];
        if (lead <= 0x7fU) continue;

        uint32_t codepoint;
        uint32_t minimum;
        unsigned continuation_count;
        if (lead >= 0xc2U && lead <= 0xdfU) {
            codepoint = lead & 0x1fU;
            minimum = 0x80U;
            continuation_count = 1U;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            codepoint = lead & 0x0fU;
            minimum = 0x800U;
            continuation_count = 2U;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            codepoint = lead & 0x07U;
            minimum = 0x10000U;
            continuation_count = 3U;
        } else {
            return 0;
        }
        if ((size_t)continuation_count > length - offset) return 0;
        for (unsigned index = 0U; index < continuation_count; ++index) {
            uint8_t continuation = text[offset++];
            if ((continuation & 0xc0U) != 0x80U) return 0;
            codepoint = (codepoint << 6U) | (continuation & 0x3fU);
        }
        if (codepoint < minimum || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU))
            return 0;
    }
    return 1;
}

int akaos_dbus_object_path_validate(const char *path) {
    return path && codec_valid_object_path(path, strlen(path)) ? 0 : -1;
}

static int codec_align_writer(struct dbus_codec_writer *writer, int alignment) {
    if (!writer || alignment <= 0) return 0;
    size_t mask = (size_t)alignment - 1U;
    size_t aligned = (writer->offset + mask) & ~mask;
    if (aligned < writer->offset || aligned > writer->capacity) return 0;
    if (aligned > writer->offset)
        memset(writer->bytes + writer->offset, 0, aligned - writer->offset);
    writer->offset = aligned;
    return 1;
}

static int codec_write_bytes(struct dbus_codec_writer *writer,
                             const void *bytes, size_t length) {
    if (!writer || (length != 0U && !bytes) ||
        length > writer->capacity - writer->offset)
        return 0;
    if (length != 0U) memcpy(writer->bytes + writer->offset, bytes, length);
    writer->offset += length;
    return 1;
}

static int codec_write_u8(struct dbus_codec_writer *writer, uint8_t value) {
    return codec_write_bytes(writer, &value, sizeof(value));
}

static int codec_write_u16(struct dbus_codec_writer *writer, uint16_t value) {
    unsigned char bytes[2];
    if (writer->big_endian) {
        bytes[0] = (unsigned char)(value >> 8U);
        bytes[1] = (unsigned char)value;
    } else {
        bytes[0] = (unsigned char)value;
        bytes[1] = (unsigned char)(value >> 8U);
    }
    return codec_write_bytes(writer, bytes, sizeof(bytes));
}

static void codec_store_u32(unsigned char *bytes, uint32_t value,
                            int big_endian) {
    if (big_endian) {
        bytes[0] = (unsigned char)(value >> 24U);
        bytes[1] = (unsigned char)(value >> 16U);
        bytes[2] = (unsigned char)(value >> 8U);
        bytes[3] = (unsigned char)value;
    } else {
        bytes[0] = (unsigned char)value;
        bytes[1] = (unsigned char)(value >> 8U);
        bytes[2] = (unsigned char)(value >> 16U);
        bytes[3] = (unsigned char)(value >> 24U);
    }
}

static int codec_write_u32(struct dbus_codec_writer *writer, uint32_t value) {
    unsigned char bytes[4];
    codec_store_u32(bytes, value, writer->big_endian);
    return codec_write_bytes(writer, bytes, sizeof(bytes));
}

static int codec_write_u64(struct dbus_codec_writer *writer, uint64_t value) {
    unsigned char bytes[8];
    for (unsigned index = 0U; index < 8U; ++index) {
        unsigned shift_index = writer->big_endian ? 7U - index : index;
        bytes[index] = (unsigned char)(value >> (shift_index * 8U));
    }
    return codec_write_bytes(writer, bytes, sizeof(bytes));
}

static int codec_write_string(struct dbus_codec_writer *writer,
                              const char *value, size_t length) {
    if (!value || length > UINT32_MAX ||
        !codec_valid_utf8((const unsigned char *)value, length) ||
        !codec_align_writer(writer, 4))
        return 0;
    return codec_write_u32(writer, (uint32_t)length) &&
           codec_write_bytes(writer, value, length) &&
           codec_write_u8(writer, 0U);
}

static int codec_write_signature(struct dbus_codec_writer *writer,
                                 const char *signature) {
    size_t length = 0U;
    if (!codec_signature_length(signature, &length) || length > 255U ||
        !codec_write_u8(writer, (uint8_t)length) ||
        !codec_write_bytes(writer, signature, length))
        return 0;
    return codec_write_u8(writer, 0U);
}

static int codec_value_matches_basic(char type,
                                     const struct akaos_dbus_value *value) {
    if (!value) return 0;
    switch (type) {
    case 'y': return value->kind == AKAOS_DBUS_VALUE_BYTE;
    case 'b': return value->kind == AKAOS_DBUS_VALUE_BOOLEAN;
    case 'n': return value->kind == AKAOS_DBUS_VALUE_INT16;
    case 'q': return value->kind == AKAOS_DBUS_VALUE_UINT16;
    case 'i': return value->kind == AKAOS_DBUS_VALUE_INT32;
    case 'u': return value->kind == AKAOS_DBUS_VALUE_UINT32;
    case 'x': return value->kind == AKAOS_DBUS_VALUE_INT64;
    case 't': return value->kind == AKAOS_DBUS_VALUE_UINT64;
    case 'd': return value->kind == AKAOS_DBUS_VALUE_DOUBLE_BITS;
    case 's': return value->kind == AKAOS_DBUS_VALUE_STRING;
    case 'o': return value->kind == AKAOS_DBUS_VALUE_OBJECT_PATH;
    case 'g': return value->kind == AKAOS_DBUS_VALUE_SIGNATURE;
    case 'h': return value->kind == AKAOS_DBUS_VALUE_UNIX_FD;
    default: return 0;
    }
}

static int codec_marshal_type(const char *signature, size_t length,
                              size_t *cursor,
                              const struct akaos_dbus_value *value,
                              struct dbus_codec_writer *writer,
                              unsigned depth) {
    if (!signature || !cursor || !value || !writer || *cursor >= length ||
        depth > AKAOS_DBUS_CODEC_MAX_DEPTH)
        return 0;
    char type = signature[(*cursor)++];
    if (codec_basic_type(type)) {
        if (!codec_value_matches_basic(type, value)) return 0;
        switch (type) {
        case 'y': return codec_write_u8(writer, value->scalar.byte_value);
        case 'b':
            return codec_align_writer(writer, 4) &&
                   codec_write_u32(writer,
                                   value->scalar.boolean_value ? 1U : 0U);
        case 'n':
            return codec_align_writer(writer, 2) &&
                   codec_write_u16(writer, (uint16_t)value->scalar.int16_value);
        case 'q':
            return codec_align_writer(writer, 2) &&
                   codec_write_u16(writer, value->scalar.uint16_value);
        case 'i':
            return codec_align_writer(writer, 4) &&
                   codec_write_u32(writer, (uint32_t)value->scalar.int32_value);
        case 'u':
        case 'h':
            return codec_align_writer(writer, 4) &&
                   codec_write_u32(writer, value->scalar.uint32_value);
        case 'x':
            return codec_align_writer(writer, 8) &&
                   codec_write_u64(writer, (uint64_t)value->scalar.int64_value);
        case 't':
            return codec_align_writer(writer, 8) &&
                   codec_write_u64(writer, value->scalar.uint64_value);
        case 'd':
            return codec_align_writer(writer, 8) &&
                   codec_write_u64(writer, value->scalar.double_bits);
        case 's':
        case 'o': {
            const char *text = value->scalar.string_value;
            if (!text) return 0;
            size_t text_length = strlen(text);
            if (type == 'o' && !codec_valid_object_path(text, text_length))
                return 0;
            return codec_write_string(writer, text, text_length);
        }
        case 'g':
            if (!value->scalar.string_value ||
                akaos_dbus_signature_validate(value->scalar.string_value,
                                              NULL) != 0)
                return 0;
            return codec_write_signature(writer, value->scalar.string_value);
        default:
            return 0;
        }
    }
    if (type == 'v') {
        if (value->kind != AKAOS_DBUS_VALUE_VARIANT ||
            value->child_count != 1U || !value->children ||
            !value->variant_signature)
            return 0;
        size_t variant_length = 0U;
        size_t variant_count = 0U;
        if (!codec_signature_length(value->variant_signature, &variant_length) ||
            akaos_dbus_signature_validate(value->variant_signature,
                                           &variant_count) != 0 ||
            variant_count != 1U ||
            !codec_write_signature(writer, value->variant_signature))
            return 0;
        size_t variant_cursor = 0U;
        (void)variant_length;
        return codec_marshal_type(value->variant_signature, variant_length,
                                  &variant_cursor, value->children, writer,
                                  depth + 1U) &&
               variant_cursor == variant_length;
    }
    if (type == 'a') {
        if (value->kind != AKAOS_DBUS_VALUE_ARRAY || *cursor >= length)
            return 0;
        size_t child_start = *cursor;
        size_t child_end = child_start;
        if (!codec_next_type(signature, length, child_start, 1,
                             &child_end) ||
            !codec_align_writer(writer, 4))
            return 0;
        size_t length_offset = writer->offset;
        if (!codec_write_u32(writer, 0U)) return 0;
        int child_alignment = codec_type_alignment(signature[child_start]);
        if (child_alignment == 0 || !codec_align_writer(writer, child_alignment))
            return 0;
        /* The array length excludes the alignment padding after the length
         * word; it counts only bytes beginning at the first aligned element. */
        size_t payload_offset = writer->offset;
        for (size_t index = 0U; index < value->child_count; ++index) {
            size_t child_cursor = child_start;
            if (!codec_marshal_type(signature, length, &child_cursor,
                                    &value->children[index], writer,
                                    depth + 1U) || child_cursor != child_end)
                return 0;
        }
        if (writer->offset < payload_offset ||
            writer->offset - payload_offset > UINT32_MAX)
            return 0;
        uint32_t payload_length = (uint32_t)(writer->offset - payload_offset);
        unsigned char *length_bytes = writer->bytes + length_offset;
        codec_store_u32(length_bytes, payload_length, writer->big_endian);
        *cursor = child_end;
        return 1;
    }
    if (type == '(') {
        if (value->kind != AKAOS_DBUS_VALUE_STRUCT) return 0;
        if (!codec_align_writer(writer, 8)) return 0;
        size_t child_index = 0U;
        while (*cursor < length && signature[*cursor] != ')') {
            if (child_index >= value->child_count ||
                !codec_marshal_type(signature, length, cursor,
                                    &value->children[child_index], writer,
                                    depth + 1U))
                return 0;
            ++child_index;
        }
        if (*cursor >= length || signature[*cursor] != ')' ||
            child_index != value->child_count)
            return 0;
        ++*cursor;
        return 1;
    }
    if (type == '{') {
        if (value->kind != AKAOS_DBUS_VALUE_DICT_ENTRY ||
            value->child_count != 2U || !codec_align_writer(writer, 8))
            return 0;
        if (!codec_marshal_type(signature, length, cursor,
                                &value->children[0], writer, depth + 1U) ||
            !codec_marshal_type(signature, length, cursor,
                                &value->children[1], writer, depth + 1U) ||
            *cursor >= length || signature[*cursor] != '}')
            return 0;
        ++*cursor;
        return 1;
    }
    return 0;
}

int akaos_dbus_marshal_with_byte_order(
    const char *signature, const struct akaos_dbus_value *values,
    size_t value_count, void *body, size_t body_capacity, int byte_order,
    size_t *body_length) {
    size_t signature_length = 0U;
    size_t type_count = 0U;
    if (!codec_byte_order_valid(byte_order) ||
        (body_capacity != 0U && !body) ||
        !codec_signature_length(signature, &signature_length) ||
        akaos_dbus_signature_validate(signature, &type_count) != 0 ||
        (type_count != value_count && !(type_count == 0U && value_count == 0U)) ||
        (value_count != 0U && !values)) {
        errno = EINVAL;
        return -1;
    }
    struct dbus_codec_writer writer = {
        (unsigned char *)body, body_capacity, 0U,
        byte_order == AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN
    };
    size_t cursor = 0U;
    for (size_t index = 0U; index < value_count; ++index) {
        if (!codec_marshal_type(signature, signature_length, &cursor,
                                &values[index], &writer, 0U)) {
            errno = writer.offset >= body_capacity ? ENOSPC : EINVAL;
            return -1;
        }
    }
    if (cursor != signature_length) {
        errno = EINVAL;
        return -1;
    }
    if (body_length) *body_length = writer.offset;
    return 0;
}

int akaos_dbus_marshal(const char *signature,
                       const struct akaos_dbus_value *values,
                       size_t value_count, void *body, size_t body_capacity,
                       size_t *body_length) {
    return akaos_dbus_marshal_with_byte_order(
        signature, values, value_count, body, body_capacity,
        AKAOS_DBUS_BYTE_ORDER_LITTLE_ENDIAN, body_length);
}

static int codec_align_reader(struct dbus_codec_reader *reader, int alignment) {
    if (!reader || alignment <= 0) return 0;
    size_t mask = (size_t)alignment - 1U;
    size_t aligned = (reader->offset + mask) & ~mask;
    if (aligned < reader->offset || aligned > reader->length) return 0;
    reader->offset = aligned;
    return 1;
}

static int codec_read_bytes(struct dbus_codec_reader *reader, void *out,
                            size_t length) {
    if (!reader || (length != 0U && !out) ||
        length > reader->length - reader->offset)
        return 0;
    if (length != 0U) memcpy(out, reader->bytes + reader->offset, length);
    reader->offset += length;
    return 1;
}

static int codec_read_u8(struct dbus_codec_reader *reader, uint8_t *value) {
    return codec_read_bytes(reader, value, sizeof(*value));
}

static int codec_read_u16(struct dbus_codec_reader *reader, uint16_t *value) {
    unsigned char bytes[2];
    if (!codec_read_bytes(reader, bytes, sizeof(bytes))) return 0;
    *value = reader->big_endian
        ? ((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]
        : (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
    return 1;
}

static int codec_read_u32(struct dbus_codec_reader *reader, uint32_t *value) {
    unsigned char bytes[4];
    if (!codec_read_bytes(reader, bytes, sizeof(bytes))) return 0;
    *value = reader->big_endian
        ? ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
              ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3]
        : (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
              ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
    return 1;
}

static int codec_read_u64(struct dbus_codec_reader *reader, uint64_t *value) {
    unsigned char bytes[8];
    if (!codec_read_bytes(reader, bytes, sizeof(bytes))) return 0;
    uint64_t result = 0U;
    for (unsigned index = 0U; index < 8U; ++index) {
        unsigned shift_index = reader->big_endian ? 7U - index : index;
        result |= (uint64_t)bytes[index] << (shift_index * 8U);
    }
    *value = result;
    return 1;
}

static int codec_validate_signature_value(const unsigned char *bytes,
                                          size_t length) {
    if (!bytes || length > 255U) return 0;
    char signature[AKAOS_DBUS_CODEC_MAX_SIGNATURE + 1U];
    if (length > AKAOS_DBUS_CODEC_MAX_SIGNATURE) return 0;
    memcpy(signature, bytes, length);
    signature[length] = '\0';
    return akaos_dbus_signature_validate(signature, NULL) == 0;
}

static int codec_validate_type(const char *signature, size_t length,
                               size_t *cursor, struct dbus_codec_reader *reader,
                               unsigned depth) {
    if (!signature || !cursor || !reader || *cursor >= length ||
        depth > AKAOS_DBUS_CODEC_MAX_DEPTH)
        return 0;
    char type = signature[(*cursor)++];
    if (codec_basic_type(type)) {
        if (type == 'y') {
            uint8_t value;
            return codec_read_u8(reader, &value);
        }
        if (type == 'n' || type == 'q') {
            uint16_t value;
            return codec_align_reader(reader, 2) && codec_read_u16(reader, &value);
        }
        if (type == 'b' || type == 'i' || type == 'u' || type == 'h') {
            uint32_t value;
            if (!codec_align_reader(reader, 4) ||
                !codec_read_u32(reader, &value)) return 0;
            return type != 'b' || value == 0U || value == 1U;
        }
        if (type == 'x' || type == 't' || type == 'd') {
            uint64_t value;
            return codec_align_reader(reader, 8) && codec_read_u64(reader, &value);
        }
        if (type == 'g') {
            uint8_t length_byte;
            if (!codec_read_u8(reader, &length_byte) ||
                (size_t)length_byte > reader->length - reader->offset)
                return 0;
            const unsigned char *bytes = reader->bytes + reader->offset;
            if ((size_t)length_byte + 1U > reader->length - reader->offset ||
                bytes[length_byte] != 0U ||
                !codec_validate_signature_value(bytes, length_byte))
                return 0;
            reader->offset += (size_t)length_byte + 1U;
            return 1;
        }
        if (!codec_align_reader(reader, 4)) return 0;
        uint32_t string_length;
        if (!codec_read_u32(reader, &string_length) ||
            (size_t)string_length + 1U > reader->length - reader->offset)
            return 0;
        const unsigned char *text = reader->bytes + reader->offset;
        if (text[string_length] != 0U ||
            memchr(text, 0, (size_t)string_length) != NULL ||
            !codec_valid_utf8(text, (size_t)string_length))
            return 0;
        if (type == 'o' &&
            !codec_valid_object_path((const char *)text, string_length))
            return 0;
        reader->offset += (size_t)string_length + 1U;
        return 1;
    }
    if (type == 'v') {
        uint8_t signature_length;
        if (!codec_read_u8(reader, &signature_length) ||
            (size_t)signature_length + 1U > reader->length - reader->offset)
            return 0;
        const unsigned char *bytes = reader->bytes + reader->offset;
        if (bytes[signature_length] != 0U || signature_length == 0U ||
            !codec_validate_signature_value(bytes, signature_length))
            return 0;
        char child_signature[AKAOS_DBUS_CODEC_MAX_SIGNATURE + 1U];
        memcpy(child_signature, bytes, signature_length);
        child_signature[signature_length] = '\0';
        size_t child_count = 0U;
        if (akaos_dbus_signature_validate(child_signature, &child_count) != 0 ||
            child_count != 1U)
            return 0;
        reader->offset += (size_t)signature_length + 1U;
        size_t child_cursor = 0U;
        return codec_validate_type(child_signature, signature_length,
                                   &child_cursor, reader, depth + 1U) &&
               child_cursor == (size_t)signature_length;
    }
    if (type == 'a') {
        if (*cursor >= length) return 0;
        size_t child_start = *cursor;
        size_t child_end = child_start;
        if (!codec_next_type(signature, length, child_start, 1,
                             &child_end) ||
            !codec_align_reader(reader, 4))
            return 0;
        uint32_t payload_length;
        if (!codec_read_u32(reader, &payload_length) ||
            (size_t)payload_length > reader->length - reader->offset)
            return 0;
        int child_alignment = codec_type_alignment(signature[child_start]);
        if (child_alignment == 0 || !codec_align_reader(reader, child_alignment) ||
            (size_t)payload_length > reader->length - reader->offset)
            return 0;
        size_t payload_end = reader->offset + (size_t)payload_length;
        while (reader->offset < payload_end) {
            size_t child_cursor = child_start;
            if (!codec_validate_type(signature, length, &child_cursor, reader,
                                     depth + 1U) || child_cursor != child_end ||
                reader->offset > payload_end)
                return 0;
        }
        if (reader->offset != payload_end) return 0;
        *cursor = child_end;
        return 1;
    }
    if (type == '(') {
        if (!codec_align_reader(reader, 8)) return 0;
        while (*cursor < length && signature[*cursor] != ')') {
            if (!codec_validate_type(signature, length, cursor, reader,
                                     depth + 1U))
                return 0;
        }
        if (*cursor >= length || signature[*cursor] != ')') return 0;
        ++*cursor;
        return 1;
    }
    if (type == '{') {
        if (!codec_align_reader(reader, 8) ||
            !codec_validate_type(signature, length, cursor, reader, depth + 1U) ||
            !codec_validate_type(signature, length, cursor, reader, depth + 1U) ||
            *cursor >= length || signature[*cursor] != '}')
            return 0;
        ++*cursor;
        return 1;
    }
    return 0;
}

int akaos_dbus_body_validate_with_byte_order(
    const char *signature, const void *body, size_t body_length,
    int byte_order, size_t *consumed_length) {
    size_t signature_length = 0U;
    if (!codec_byte_order_valid(byte_order) ||
        (!body && body_length != 0U) ||
        !codec_signature_length(signature, &signature_length) ||
        akaos_dbus_signature_validate(signature, NULL) != 0) {
        errno = EINVAL;
        return -1;
    }
    struct dbus_codec_reader reader = {
        (const unsigned char *)body, body_length, 0U,
        byte_order == AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN
    };
    size_t cursor = 0U;
    while (cursor < signature_length) {
        if (!codec_validate_type(signature, signature_length, &cursor, &reader,
                                 0U)) {
            errno = EPROTO;
            return -1;
        }
    }
    if (reader.offset != body_length) {
        errno = EPROTO;
        return -1;
    }
    if (consumed_length) *consumed_length = reader.offset;
    return 0;
}

int akaos_dbus_body_validate(const char *signature, const void *body,
                             size_t body_length, size_t *consumed_length) {
    return akaos_dbus_body_validate_with_byte_order(
        signature, body, body_length, AKAOS_DBUS_BYTE_ORDER_LITTLE_ENDIAN,
        consumed_length);
}

static uint32_t codec_load_u32(const unsigned char *bytes, int big_endian) {
    if (big_endian)
        return ((uint32_t)bytes[0] << 24U) |
               ((uint32_t)bytes[1] << 16U) |
               ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static int codec_align_offset(size_t *offset, size_t limit, int alignment) {
    if (!offset || alignment <= 0 || *offset > limit) return 0;
    size_t mask = (size_t)alignment - 1U;
    if (*offset > SIZE_MAX - mask) return 0;
    size_t aligned = (*offset + mask) & ~mask;
    if (aligned < *offset || aligned > limit) return 0;
    *offset = aligned;
    return 1;
}

static void codec_reverse_bytes(unsigned char *bytes, size_t length) {
    for (size_t left = 0U; left < length / 2U; ++left) {
        size_t right = length - left - 1U;
        unsigned char swap = bytes[left];
        bytes[left] = bytes[right];
        bytes[right] = swap;
    }
}

static int codec_convert_value(const char *signature, size_t signature_length,
                               size_t *signature_cursor,
                               unsigned char *body, size_t body_length,
                               size_t *body_cursor, int source_big_endian,
                               int destination_big_endian, unsigned depth) {
    if (!signature || !signature_cursor || !body_cursor ||
        *signature_cursor >= signature_length || depth > AKAOS_DBUS_CODEC_MAX_DEPTH)
        return 0;
    char type = signature[(*signature_cursor)++];
    if (type == 'a') {
        size_t child_start = *signature_cursor;
        size_t child_end = child_start;
        if (!codec_next_type(signature, signature_length, child_start, 1,
                             &child_end) ||
            !codec_align_offset(body_cursor, body_length, 4) ||
            body_length - *body_cursor < 4U)
            return 0;
        unsigned char *length_bytes = body + *body_cursor;
        uint32_t payload_length = codec_load_u32(length_bytes,
                                                  source_big_endian);
        *body_cursor += 4U;
        int child_alignment = codec_type_alignment(signature[child_start]);
        if (!codec_align_offset(body_cursor, body_length, child_alignment) ||
            (size_t)payload_length > body_length - *body_cursor)
            return 0;
        size_t payload_end = *body_cursor + (size_t)payload_length;
        if (source_big_endian != destination_big_endian)
            codec_reverse_bytes(length_bytes, 4U);
        while (*body_cursor < payload_end) {
            size_t child_cursor = child_start;
            size_t before = *body_cursor;
            if (!codec_convert_value(signature, signature_length,
                                     &child_cursor, body, payload_end,
                                     body_cursor, source_big_endian,
                                     destination_big_endian, depth + 1U) ||
                child_cursor != child_end || *body_cursor <= before ||
                *body_cursor > payload_end)
                return 0;
        }
        if (*body_cursor != payload_end) return 0;
        *signature_cursor = child_end;
        return 1;
    }
    if (type == '(') {
        if (!codec_align_offset(body_cursor, body_length, 8)) return 0;
        while (*signature_cursor < signature_length &&
               signature[*signature_cursor] != ')') {
            if (!codec_convert_value(signature, signature_length,
                                     signature_cursor, body, body_length,
                                     body_cursor, source_big_endian,
                                     destination_big_endian, depth + 1U))
                return 0;
        }
        if (*signature_cursor >= signature_length ||
            signature[*signature_cursor] != ')') return 0;
        ++*signature_cursor;
        return 1;
    }
    if (type == '{') {
        if (!codec_align_offset(body_cursor, body_length, 8) ||
            !codec_convert_value(signature, signature_length,
                                 signature_cursor, body, body_length,
                                 body_cursor, source_big_endian,
                                 destination_big_endian, depth + 1U) ||
            !codec_convert_value(signature, signature_length,
                                 signature_cursor, body, body_length,
                                 body_cursor, source_big_endian,
                                 destination_big_endian, depth + 1U) ||
            *signature_cursor >= signature_length ||
            signature[*signature_cursor] != '}')
            return 0;
        ++*signature_cursor;
        return 1;
    }
    if (type == 'v') {
        if (*body_cursor >= body_length) return 0;
        unsigned char variant_length = body[(*body_cursor)++];
        if ((size_t)variant_length + 1U > body_length - *body_cursor)
            return 0;
        char variant_signature[AKAOS_DBUS_CODEC_MAX_SIGNATURE + 1U];
        memcpy(variant_signature, body + *body_cursor, variant_length);
        variant_signature[variant_length] = '\0';
        *body_cursor += (size_t)variant_length + 1U;
        size_t variant_type_count = 0U;
        size_t variant_cursor = 0U;
        if (variant_length == 0U ||
            akaos_dbus_signature_validate(variant_signature,
                                          &variant_type_count) != 0 ||
            variant_type_count != 1U ||
            !codec_convert_value(variant_signature, variant_length,
                                 &variant_cursor, body, body_length,
                                 body_cursor, source_big_endian,
                                 destination_big_endian, depth + 1U) ||
            variant_cursor != variant_length)
            return 0;
        return 1;
    }
    if (!codec_basic_type(type)) return 0;
    int alignment = codec_type_alignment(type);
    if (!codec_align_offset(body_cursor, body_length, alignment)) return 0;
    if (type == 's' || type == 'o') {
        if (body_length - *body_cursor < 4U) return 0;
        unsigned char *length_bytes = body + *body_cursor;
        uint32_t string_length = codec_load_u32(length_bytes,
                                                 source_big_endian);
        *body_cursor += 4U;
        if ((size_t)string_length + 1U > body_length - *body_cursor)
            return 0;
        if (source_big_endian != destination_big_endian)
            codec_reverse_bytes(length_bytes, 4U);
        *body_cursor += (size_t)string_length + 1U;
        return 1;
    }
    if (type == 'g') {
        if (*body_cursor >= body_length) return 0;
        unsigned char signature_value_length = body[(*body_cursor)++];
        if ((size_t)signature_value_length + 1U >
            body_length - *body_cursor)
            return 0;
        *body_cursor += (size_t)signature_value_length + 1U;
        return 1;
    }
    size_t width = type == 'y' ? 1U :
                   (type == 'n' || type == 'q') ? 2U :
                   (type == 'x' || type == 't' || type == 'd') ? 8U : 4U;
    if (width > body_length - *body_cursor) return 0;
    if (source_big_endian != destination_big_endian && width > 1U)
        codec_reverse_bytes(body + *body_cursor, width);
    *body_cursor += width;
    return 1;
}

int akaos_dbus_body_convert_byte_order(const char *signature, void *body,
                                       size_t body_length,
                                       int source_byte_order,
                                       int destination_byte_order) {
    if (!codec_byte_order_valid(source_byte_order) ||
        !codec_byte_order_valid(destination_byte_order) ||
        (!body && body_length != 0U) ||
        akaos_dbus_body_validate_with_byte_order(
            signature, body, body_length, source_byte_order, NULL) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (source_byte_order == destination_byte_order) return 0;
    size_t signature_length = 0U;
    if (!codec_signature_length(signature, &signature_length)) {
        errno = EINVAL;
        return -1;
    }
    size_t signature_cursor = 0U;
    size_t body_cursor = 0U;
    int source_big_endian =
        source_byte_order == AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN;
    int destination_big_endian =
        destination_byte_order == AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN;
    while (signature_cursor < signature_length) {
        if (!codec_convert_value(signature, signature_length,
                                 &signature_cursor,
                                 (unsigned char *)body, body_length,
                                 &body_cursor, source_big_endian,
                                 destination_big_endian, 0U)) {
            errno = EPROTO;
            return -1;
        }
    }
    if (body_cursor != body_length) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static size_t codec_align8_size(size_t value) {
    if (value > SIZE_MAX - 7U) return SIZE_MAX;
    return (value + 7U) & ~(size_t)7U;
}

static int codec_header_field_type(unsigned char code, char type) {
    switch (code) {
    case 1U: return type == 'o';
    case 2U:
    case 3U:
    case 4U:
    case 6U:
    case 7U: return type == 's';
    case 5U:
    case 9U: return type == 'u';
    case 8U: return type == 'g';
    default: return 1;
    }
}

static int codec_normalize_header_fields(unsigned char *fields,
                                         size_t fields_length,
                                         int source_big_endian,
                                         char *body_signature,
                                         size_t signature_capacity) {
    if ((!fields && fields_length != 0U) || !body_signature ||
        signature_capacity == 0U)
        return 0;
    size_t offset = 0U;
    unsigned seen_fields = 0U;
    body_signature[0] = '\0';
    while (offset < fields_length) {
        size_t cursor = codec_align8_size(offset);
        if (cursor > fields_length) return 0;
        offset = cursor;
        if (cursor == fields_length) break;
        if (fields_length - cursor < 4U) return 0;
        unsigned char code = fields[cursor++];
        unsigned char signature_length = fields[cursor++];
        if (signature_length == 0U ||
            (size_t)signature_length + 1U > fields_length - cursor)
            return 0;
        char field_signature[AKAOS_DBUS_CODEC_MAX_SIGNATURE + 1U];
        memcpy(field_signature, fields + cursor, signature_length);
        field_signature[signature_length] = '\0';
        if (fields[cursor + signature_length] != 0U) return 0;
        cursor += (size_t)signature_length + 1U;
        size_t field_type_count = 0U;
        if (akaos_dbus_signature_validate(field_signature,
                                          &field_type_count) != 0 ||
            field_type_count != 1U || field_signature[1] != '\0' ||
            !codec_basic_type(field_signature[0]) ||
            !codec_header_field_type(code, field_signature[0]))
            return 0;
        if (code >= 1U && code <= 9U) {
            unsigned bit = 1U << code;
            if ((seen_fields & bit) != 0U) return 0;
            seen_fields |= bit;
        }
        char type = field_signature[0];
        if (!codec_align_offset(&cursor, fields_length,
                                codec_type_alignment(type)))
            return 0;
        if (type == 's' || type == 'o') {
            if (fields_length - cursor < 4U) return 0;
            unsigned char *length_bytes = fields + cursor;
            uint32_t length = codec_load_u32(length_bytes,
                                              source_big_endian);
            cursor += 4U;
            if ((size_t)length + 1U > fields_length - cursor ||
                fields[cursor + length] != 0U ||
                !codec_valid_utf8(fields + cursor, length) ||
                (type == 'o' && !codec_valid_object_path(
                    (const char *)fields + cursor, length)))
                return 0;
            if (source_big_endian) codec_reverse_bytes(length_bytes, 4U);
            cursor += (size_t)length + 1U;
        } else if (type == 'g') {
            if (cursor >= fields_length) return 0;
            unsigned char length = fields[cursor++];
            if ((size_t)length + 1U > fields_length - cursor ||
                fields[cursor + length] != 0U ||
                (size_t)length >= signature_capacity)
                return 0;
            memcpy(body_signature, fields + cursor, length);
            body_signature[length] = '\0';
            if (akaos_dbus_signature_validate(body_signature, NULL) != 0)
                return 0;
            cursor += (size_t)length + 1U;
        } else {
            size_t width = (type == 'y') ? 1U :
                           (type == 'n' || type == 'q') ? 2U :
                           (type == 'x' || type == 't' || type == 'd')
                               ? 8U : 4U;
            if (width > fields_length - cursor) return 0;
            if (source_big_endian && width > 1U)
                codec_reverse_bytes(fields + cursor, width);
            cursor += width;
        }
        if (cursor <= offset) return 0;
        offset = cursor;
    }
    return 1;
}

int akaos_dbus_message_normalize_little_endian(void *frame,
                                               size_t frame_length) {
    if (!frame || frame_length < 16U) {
        errno = EINVAL;
        return -1;
    }
    unsigned char *bytes = (unsigned char *)frame;
    int byte_order = bytes[0];
    if (!codec_byte_order_valid(byte_order) || bytes[3] != 1U) {
        errno = EPROTO;
        return -1;
    }
    int source_big_endian = byte_order == AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN;
    uint32_t body_length = codec_load_u32(bytes + 4U, source_big_endian);
    uint32_t serial = codec_load_u32(bytes + 8U, source_big_endian);
    uint32_t fields_length = codec_load_u32(bytes + 12U, source_big_endian);
    size_t padded_fields = codec_align8_size((size_t)fields_length);
    if ((size_t)fields_length > frame_length - 16U ||
        padded_fields > frame_length - 16U ||
        (size_t)body_length > frame_length - 16U - padded_fields ||
        16U + padded_fields + (size_t)body_length != frame_length) {
        errno = EPROTO;
        return -1;
    }
    char signature[AKAOS_DBUS_CODEC_MAX_SIGNATURE + 1U];
    if (!codec_normalize_header_fields(bytes + 16U, fields_length,
                                       source_big_endian, signature,
                                       sizeof(signature))) {
        errno = EPROTO;
        return -1;
    }
    unsigned char *body = bytes + 16U + padded_fields;
    if (akaos_dbus_body_convert_byte_order(
            signature, body, (size_t)body_length, byte_order,
            AKAOS_DBUS_BYTE_ORDER_LITTLE_ENDIAN) != 0) {
        errno = EPROTO;
        return -1;
    }
    codec_store_u32(bytes + 4U, body_length, 0);
    codec_store_u32(bytes + 8U, serial, 0);
    codec_store_u32(bytes + 12U, fields_length, 0);
    bytes[0] = AKAOS_DBUS_BYTE_ORDER_LITTLE_ENDIAN;
    return 0;
}

static struct akaos_dbus_value *codec_decode_alloc_values(
    struct akaos_dbus_decode_context *context, size_t count) {
    if (!context) {
        errno = EINVAL;
        return NULL;
    }
    if (count == 0U)
        return context->values ? context->values + context->value_used : NULL;
    if (!context->values || context->value_used > context->value_capacity ||
        count > context->value_capacity - context->value_used) {
        errno = ENOSPC;
        return NULL;
    }
    struct akaos_dbus_value *values = context->values + context->value_used;
    if (count != 0U) memset(values, 0, count * sizeof(*values));
    context->value_used += count;
    return values;
}

static const char *codec_decode_store_text(
    struct akaos_dbus_decode_context *context, const unsigned char *bytes,
    size_t length) {
    if (!context || !bytes || length > SIZE_MAX - 1U) return NULL;
    if (!context->string_storage) return (const char *)bytes;
    if (context->string_used > context->string_capacity ||
        length + 1U > context->string_capacity - context->string_used) {
        errno = ENOSPC;
        return NULL;
    }
    char *destination = context->string_storage + context->string_used;
    memcpy(destination, bytes, length);
    destination[length] = '\0';
    context->string_used += length + 1U;
    return destination;
}

static int codec_decode_string_value(
    struct dbus_codec_reader *reader,
    struct akaos_dbus_decode_context *context, uint32_t kind,
    struct akaos_dbus_value *value, int object_path) {
    if (!reader || !context || !value || !codec_align_reader(reader, 4))
        return 0;
    uint32_t string_length = 0U;
    if (!codec_read_u32(reader, &string_length) ||
        (size_t)string_length + 1U > reader->length - reader->offset)
        return 0;
    const unsigned char *bytes = reader->bytes + reader->offset;
    if (bytes[string_length] != 0U ||
        memchr(bytes, 0, (size_t)string_length) != NULL ||
        !codec_valid_utf8(bytes, (size_t)string_length))
        return 0;
    if (object_path &&
        !codec_valid_object_path((const char *)bytes, string_length))
        return 0;
    const char *stored = codec_decode_store_text(
        context, bytes, (size_t)string_length);
    if (!stored) return 0;
    value->kind = kind;
    value->scalar.string_value = stored;
    reader->offset += (size_t)string_length + 1U;
    return 1;
}

static int codec_decode_signature_value(
    struct dbus_codec_reader *reader,
    struct akaos_dbus_decode_context *context, const char **signature_out) {
    if (!reader || !context || !signature_out) return 0;
    uint8_t signature_length = 0U;
    if (!codec_read_u8(reader, &signature_length) ||
        (size_t)signature_length + 1U > reader->length - reader->offset)
        return 0;
    const unsigned char *bytes = reader->bytes + reader->offset;
    if (bytes[signature_length] != 0U ||
        !codec_validate_signature_value(bytes, signature_length))
        return 0;
    const char *stored = codec_decode_store_text(
        context, bytes, (size_t)signature_length);
    if (!stored) return 0;
    *signature_out = stored;
    reader->offset += (size_t)signature_length + 1U;
    return 1;
}

static int codec_decode_type(
    const char *signature, size_t length, size_t *cursor,
    struct dbus_codec_reader *reader,
    struct akaos_dbus_decode_context *context,
    struct akaos_dbus_value *value, unsigned depth);

static int codec_decode_array(
    const char *signature, size_t length, size_t *cursor,
    struct dbus_codec_reader *reader,
    struct akaos_dbus_decode_context *context,
    struct akaos_dbus_value *value, unsigned depth) {
    if (*cursor >= length || depth > AKAOS_DBUS_CODEC_MAX_DEPTH) return 0;
    size_t child_start = *cursor;
    size_t child_end = child_start;
    if (!codec_next_type(signature, length, child_start, 1, &child_end) ||
        !codec_align_reader(reader, 4))
        return 0;
    uint32_t payload_length = 0U;
    if (!codec_read_u32(reader, &payload_length)) return 0;
    int child_alignment = codec_type_alignment(signature[child_start]);
    if (child_alignment == 0 || !codec_align_reader(reader, child_alignment) ||
        (size_t)payload_length > reader->length - reader->offset)
        return 0;
    size_t payload_end = reader->offset + (size_t)payload_length;

    /* Count elements with the validating walker first so the child values can
     * be reserved as one contiguous caller-owned block. */
    struct dbus_codec_reader probe = *reader;
    size_t child_count = 0U;
    while (probe.offset < payload_end) {
        size_t probe_cursor = child_start;
        if (!codec_validate_type(signature, length, &probe_cursor, &probe,
                                 depth + 1U) || probe_cursor != child_end ||
            probe.offset > payload_end)
            return 0;
        ++child_count;
    }
    if (probe.offset != payload_end) return 0;
    struct akaos_dbus_value *children =
        codec_decode_alloc_values(context, child_count);
    if (child_count != 0U && !children) return 0;
    value->kind = AKAOS_DBUS_VALUE_ARRAY;
    value->children = children;
    value->child_count = child_count;
    for (size_t index = 0U; index < child_count; ++index) {
        size_t child_cursor = child_start;
        if (!codec_decode_type(signature, length, &child_cursor, reader,
                               context, &children[index], depth + 1U) ||
            child_cursor != child_end)
            return 0;
    }
    if (reader->offset != payload_end) return 0;
    *cursor = child_end;
    return 1;
}

static int codec_decode_type(
    const char *signature, size_t length, size_t *cursor,
    struct dbus_codec_reader *reader,
    struct akaos_dbus_decode_context *context,
    struct akaos_dbus_value *value, unsigned depth) {
    if (!signature || !cursor || !reader || !context || !value ||
        *cursor >= length || depth > AKAOS_DBUS_CODEC_MAX_DEPTH)
        return 0;
    char type = signature[(*cursor)++];
    if (codec_basic_type(type)) {
        value->kind = 0U;
        if (type == 'y') {
            uint8_t scalar = 0U;
            if (!codec_read_u8(reader, &scalar)) return 0;
            value->kind = AKAOS_DBUS_VALUE_BYTE;
            value->scalar.byte_value = scalar;
            return 1;
        }
        if (type == 'n' || type == 'q') {
            uint16_t scalar = 0U;
            if (!codec_align_reader(reader, 2) ||
                !codec_read_u16(reader, &scalar)) return 0;
            if (type == 'n') {
                value->kind = AKAOS_DBUS_VALUE_INT16;
                value->scalar.int16_value = (int16_t)scalar;
            } else {
                value->kind = AKAOS_DBUS_VALUE_UINT16;
                value->scalar.uint16_value = scalar;
            }
            return 1;
        }
        if (type == 'b' || type == 'i' || type == 'u' || type == 'h') {
            uint32_t scalar = 0U;
            if (!codec_align_reader(reader, 4) ||
                !codec_read_u32(reader, &scalar)) return 0;
            if (type == 'b') {
                if (scalar > 1U) return 0;
                value->kind = AKAOS_DBUS_VALUE_BOOLEAN;
                value->scalar.boolean_value = scalar != 0U;
            } else if (type == 'i') {
                value->kind = AKAOS_DBUS_VALUE_INT32;
                value->scalar.int32_value = (int32_t)scalar;
            } else if (type == 'u') {
                value->kind = AKAOS_DBUS_VALUE_UINT32;
                value->scalar.uint32_value = scalar;
            } else {
                value->kind = AKAOS_DBUS_VALUE_UNIX_FD;
                value->scalar.uint32_value = scalar;
            }
            return 1;
        }
        if (type == 'x' || type == 't' || type == 'd') {
            uint64_t scalar = 0U;
            if (!codec_align_reader(reader, 8) ||
                !codec_read_u64(reader, &scalar)) return 0;
            if (type == 'x') {
                value->kind = AKAOS_DBUS_VALUE_INT64;
                value->scalar.int64_value = (int64_t)scalar;
            } else if (type == 't') {
                value->kind = AKAOS_DBUS_VALUE_UINT64;
                value->scalar.uint64_value = scalar;
            } else {
                value->kind = AKAOS_DBUS_VALUE_DOUBLE_BITS;
                value->scalar.double_bits = scalar;
            }
            return 1;
        }
        if (type == 's' || type == 'o')
            return codec_decode_string_value(
                reader, context,
                type == 's' ? AKAOS_DBUS_VALUE_STRING :
                              AKAOS_DBUS_VALUE_OBJECT_PATH,
                value, type == 'o');
        if (type == 'g') {
            const char *stored = NULL;
            if (!codec_decode_signature_value(reader, context, &stored))
                return 0;
            value->kind = AKAOS_DBUS_VALUE_SIGNATURE;
            value->scalar.string_value = stored;
            return 1;
        }
        return 0;
    }
    if (type == 'v') {
        const char *variant_signature = NULL;
        if (!codec_decode_signature_value(reader, context, &variant_signature))
            return 0;
        size_t variant_length = strlen(variant_signature);
        size_t variant_count = 0U;
        if (akaos_dbus_signature_validate(variant_signature,
                                          &variant_count) != 0 ||
            variant_count != 1U)
            return 0;
        struct akaos_dbus_value *child =
            codec_decode_alloc_values(context, 1U);
        if (!child) return 0;
        size_t variant_cursor = 0U;
        if (!codec_decode_type(variant_signature, variant_length,
                               &variant_cursor, reader, context, child,
                               depth + 1U) ||
            variant_cursor != variant_length)
            return 0;
        value->kind = AKAOS_DBUS_VALUE_VARIANT;
        value->children = child;
        value->child_count = 1U;
        value->variant_signature = variant_signature;
        return 1;
    }
    if (type == 'a') {
        value->children = NULL;
        value->child_count = 0U;
        return codec_decode_array(signature, length, cursor, reader, context,
                                  value, depth);
    }
    if (type == '(') {
        if (!codec_align_reader(reader, 8)) return 0;
        size_t child_cursor = *cursor;
        size_t child_count = 0U;
        while (child_cursor < length && signature[child_cursor] != ')') {
            size_t child_end = child_cursor;
            if (!codec_next_type(signature, length, child_cursor, 0,
                                 &child_end))
                return 0;
            child_cursor = child_end;
            ++child_count;
        }
        if (child_cursor >= length || signature[child_cursor] != ')') return 0;
        struct akaos_dbus_value *children =
            codec_decode_alloc_values(context, child_count);
        if (child_count != 0U && !children) return 0;
        value->kind = AKAOS_DBUS_VALUE_STRUCT;
        value->children = children;
        value->child_count = child_count;
        for (size_t index = 0U; index < child_count; ++index) {
            if (!codec_decode_type(signature, length, cursor, reader, context,
                                   &children[index], depth + 1U))
                return 0;
        }
        if (*cursor >= length || signature[*cursor] != ')') return 0;
        ++*cursor;
        return 1;
    }
    if (type == '{') {
        if (!codec_align_reader(reader, 8)) return 0;
        struct akaos_dbus_value *children =
            codec_decode_alloc_values(context, 2U);
        if (!children ||
            !codec_decode_type(signature, length, cursor, reader, context,
                               &children[0], depth + 1U) ||
            !codec_decode_type(signature, length, cursor, reader, context,
                               &children[1], depth + 1U) ||
            *cursor >= length || signature[*cursor] != '}')
            return 0;
        ++*cursor;
        value->kind = AKAOS_DBUS_VALUE_DICT_ENTRY;
        value->children = children;
        value->child_count = 2U;
        return 1;
    }
    return 0;
}

int akaos_dbus_unmarshal_with_byte_order(
    const char *signature, const void *body, size_t body_length,
    int byte_order, struct akaos_dbus_decode_context *context,
    size_t *value_count) {
    size_t signature_length = 0U;
    size_t type_count = 0U;
    if (!codec_byte_order_valid(byte_order)) {
        errno = EINVAL;
        return -1;
    }
    if (!context || !codec_signature_length(signature, &signature_length) ||
        akaos_dbus_signature_validate(signature, &type_count) != 0 ||
        akaos_dbus_body_validate_with_byte_order(
            signature, body, body_length, byte_order, NULL) != 0 ||
        (type_count != 0U && !context->values)) {
        if (errno == 0) errno = EINVAL;
        return -1;
    }
    context->value_used = 0U;
    context->string_used = 0U;
    struct akaos_dbus_value *values =
        codec_decode_alloc_values(context, type_count);
    if (type_count != 0U && !values) return -1;
    struct dbus_codec_reader reader = {
        (const unsigned char *)body, body_length, 0U,
        byte_order == AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN
    };
    size_t cursor = 0U;
    for (size_t index = 0U; index < type_count; ++index) {
        if (!codec_decode_type(signature, signature_length, &cursor, &reader,
                               context, &values[index], 0U)) {
            if (errno == 0) errno = EPROTO;
            return -1;
        }
    }
    if (cursor != signature_length || reader.offset != body_length) {
        errno = EPROTO;
        return -1;
    }
    if (value_count) *value_count = type_count;
    return 0;
}

int akaos_dbus_unmarshal(const char *signature, const void *body,
                         size_t body_length,
                         struct akaos_dbus_decode_context *context,
                         size_t *value_count) {
    return akaos_dbus_unmarshal_with_byte_order(
        signature, body, body_length, AKAOS_DBUS_BYTE_ORDER_LITTLE_ENDIAN,
        context, value_count);
}

static struct akaos_dbus_value codec_string_value(uint32_t kind,
                                                  const char *value) {
    struct akaos_dbus_value result;
    memset(&result, 0, sizeof(result));
    result.kind = kind;
    result.scalar.string_value = value;
    return result;
}

int akaos_dbus_marshaller_self_test(void) {
    unsigned char body[2048];
    size_t body_length = 0U;
    size_t consumed = 0U;

    if (akaos_dbus_marshal("", NULL, 0U, NULL, 0U, &body_length) != 0 ||
        body_length != 0U ||
        akaos_dbus_body_validate("", NULL, 0U, &consumed) != 0 ||
        consumed != 0U ||
        akaos_dbus_object_path_validate("/") != 0 ||
        akaos_dbus_object_path_validate("/org/akaos/Desktop") != 0 ||
        akaos_dbus_object_path_validate("relative") == 0 ||
        akaos_dbus_object_path_validate("/org//Desktop") == 0 ||
        akaos_dbus_object_path_validate("/org/Desktop/") == 0)
        goto fail;

    char oversized_signature[AKAOS_DBUS_CODEC_MAX_SIGNATURE + 2U];
    memset(oversized_signature, 'y', sizeof(oversized_signature) - 1U);
    oversized_signature[sizeof(oversized_signature) - 1U] = '\0';
    if (akaos_dbus_signature_validate(oversized_signature, NULL) == 0)
        goto fail;

    const char valid_utf8[] = {(char)0xc3U, (char)0xb8U, '\0'};
    const char invalid_utf8[] = {(char)0xc0U, (char)0xafU, '\0'};
    struct akaos_dbus_value utf8_value =
        codec_string_value(AKAOS_DBUS_VALUE_STRING, valid_utf8);
    struct akaos_dbus_value invalid_utf8_value =
        codec_string_value(AKAOS_DBUS_VALUE_STRING, invalid_utf8);
    if (akaos_dbus_marshal("s", &utf8_value, 1U, body, sizeof(body),
                           &body_length) != 0 ||
        akaos_dbus_body_validate("s", body, body_length, &consumed) != 0 ||
        consumed != body_length ||
        akaos_dbus_marshal("s", &invalid_utf8_value, 1U, body, sizeof(body),
                           &body_length) == 0)
        goto fail;

    const unsigned char embedded_nul_body[] = {
        2U, 0U, 0U, 0U, (unsigned char)'A', 0U, 0U
    };
    const unsigned char invalid_utf8_body[] = {
        2U, 0U, 0U, 0U, 0xc0U, 0xafU, 0U
    };
    const unsigned char valid_utf8_body[] = {
        2U, 0U, 0U, 0U, 0xc3U, 0xb8U, 0U
    };
    if (akaos_dbus_body_validate("s", embedded_nul_body,
                                 sizeof(embedded_nul_body), NULL) == 0 ||
        akaos_dbus_body_validate("s", invalid_utf8_body,
                                 sizeof(invalid_utf8_body), NULL) == 0 ||
        akaos_dbus_body_validate("s", valid_utf8_body,
                                 sizeof(valid_utf8_body), &consumed) != 0 ||
        consumed != sizeof(valid_utf8_body))
        goto fail;

    struct akaos_dbus_value utf8_decoded[1];
    char utf8_storage[8];
    struct akaos_dbus_decode_context utf8_context = {
        utf8_decoded, 1U, 0U, utf8_storage, sizeof(utf8_storage), 0U
    };
    size_t utf8_count = 0U;
    if (akaos_dbus_unmarshal("s", valid_utf8_body,
                             sizeof(valid_utf8_body), &utf8_context,
                             &utf8_count) != 0 || utf8_count != 1U ||
        utf8_decoded[0].kind != AKAOS_DBUS_VALUE_STRING ||
        memcmp(utf8_decoded[0].scalar.string_value, valid_utf8,
               sizeof(valid_utf8)) != 0)
        goto fail;

    struct akaos_dbus_value primitives[7];
    memset(primitives, 0, sizeof(primitives));
    primitives[0].kind = AKAOS_DBUS_VALUE_BYTE;
    primitives[0].scalar.byte_value = 0x7fU;
    primitives[1].kind = AKAOS_DBUS_VALUE_BOOLEAN;
    primitives[1].scalar.boolean_value = 1;
    primitives[2].kind = AKAOS_DBUS_VALUE_INT16;
    primitives[2].scalar.int16_value = -1234;
    primitives[3].kind = AKAOS_DBUS_VALUE_UINT16;
    primitives[3].scalar.uint16_value = 54321U;
    primitives[4].kind = AKAOS_DBUS_VALUE_INT32;
    primitives[4].scalar.int32_value = -1234567;
    primitives[5].kind = AKAOS_DBUS_VALUE_UINT32;
    primitives[5].scalar.uint32_value = UINT32_C(0xfeedbeef);
    primitives[6].kind = AKAOS_DBUS_VALUE_INT64;
    primitives[6].scalar.int64_value = INT64_C(-1234567890123);
    if (akaos_dbus_marshal("ybnqiux", primitives, 7U, body, sizeof(body),
                           &body_length) != 0 ||
        akaos_dbus_body_validate("ybnqiux", body, body_length, &consumed) != 0 ||
        consumed != body_length)
        goto fail;

    struct akaos_dbus_value wide[5];
    memset(wide, 0, sizeof(wide));
    wide[0].kind = AKAOS_DBUS_VALUE_UINT64;
    wide[0].scalar.uint64_value = UINT64_C(0x0123456789abcdef);
    wide[1].kind = AKAOS_DBUS_VALUE_DOUBLE_BITS;
    wide[1].scalar.double_bits = UINT64_C(0x3ff0000000000000);
    wide[2] = codec_string_value(AKAOS_DBUS_VALUE_STRING, "hello");
    wide[3] = codec_string_value(AKAOS_DBUS_VALUE_OBJECT_PATH,
                                  "/org/akaos/Test");
    wide[4] = codec_string_value(AKAOS_DBUS_VALUE_SIGNATURE, "a{sv}");
    if (akaos_dbus_marshal("tdsog", wide, 5U, body, sizeof(body),
                           &body_length) != 0 ||
        akaos_dbus_body_validate("tdsog", body, body_length, &consumed) != 0 ||
        consumed != body_length)
        goto fail;

    struct akaos_dbus_value decoded_wide[32];
    char decoded_strings[128];
    struct akaos_dbus_decode_context decoded_context = {
        decoded_wide, 32U, 0U, decoded_strings, sizeof(decoded_strings), 0U
    };
    size_t decoded_count = 0U;
    if (akaos_dbus_unmarshal("tdsog", body, body_length, &decoded_context,
                             &decoded_count) != 0 ||
        decoded_count != 5U ||
        decoded_wide[0].kind != AKAOS_DBUS_VALUE_UINT64 ||
        decoded_wide[1].kind != AKAOS_DBUS_VALUE_DOUBLE_BITS ||
        decoded_wide[2].kind != AKAOS_DBUS_VALUE_STRING ||
        decoded_wide[3].kind != AKAOS_DBUS_VALUE_OBJECT_PATH ||
        strcmp(decoded_wide[3].scalar.string_value, "/org/akaos/Test") != 0 ||
        decoded_wide[4].kind != AKAOS_DBUS_VALUE_SIGNATURE ||
        strcmp(decoded_wide[4].scalar.string_value, "a{sv}") != 0)
        goto fail;

    struct akaos_dbus_value big_endian_elements[2];
    memset(big_endian_elements, 0, sizeof(big_endian_elements));
    big_endian_elements[0].kind = AKAOS_DBUS_VALUE_UINT32;
    big_endian_elements[0].scalar.uint32_value = UINT32_C(0x01020304);
    big_endian_elements[1].kind = AKAOS_DBUS_VALUE_UINT32;
    big_endian_elements[1].scalar.uint32_value = UINT32_C(0x11223344);
    struct akaos_dbus_value big_endian_array;
    memset(&big_endian_array, 0, sizeof(big_endian_array));
    big_endian_array.kind = AKAOS_DBUS_VALUE_ARRAY;
    big_endian_array.children = big_endian_elements;
    big_endian_array.child_count = 2U;
    static const unsigned char expected_big_endian_array[] = {
        0x00U, 0x00U, 0x00U, 0x08U,
        0x01U, 0x02U, 0x03U, 0x04U,
        0x11U, 0x22U, 0x33U, 0x44U
    };
    if (akaos_dbus_marshal_with_byte_order(
            "au", &big_endian_array, 1U, body, sizeof(body),
            AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN, &body_length) != 0 ||
        body_length != sizeof(expected_big_endian_array) ||
        memcmp(body, expected_big_endian_array,
               sizeof(expected_big_endian_array)) != 0 ||
        akaos_dbus_body_validate_with_byte_order(
            "au", body, body_length, AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN,
            &consumed) != 0 || consumed != body_length)
        goto fail;
    memset(decoded_wide, 0, sizeof(decoded_wide));
    decoded_context.value_used = 0U;
    decoded_context.string_used = 0U;
    if (akaos_dbus_unmarshal_with_byte_order(
            "au", body, body_length, AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN,
            &decoded_context, &decoded_count) != 0 || decoded_count != 1U ||
        decoded_wide[0].kind != AKAOS_DBUS_VALUE_ARRAY ||
        decoded_wide[0].child_count != 2U ||
        decoded_wide[0].children[0].scalar.uint32_value !=
            UINT32_C(0x01020304) ||
        decoded_wide[0].children[1].scalar.uint32_value !=
            UINT32_C(0x11223344) ||
        akaos_dbus_body_validate_with_byte_order(
            "au", body, body_length, 'x', NULL) == 0)
        goto fail;

    struct akaos_dbus_value big_endian_scalars[4];
    memset(big_endian_scalars, 0, sizeof(big_endian_scalars));
    big_endian_scalars[0].kind = AKAOS_DBUS_VALUE_UINT16;
    big_endian_scalars[0].scalar.uint16_value = UINT16_C(0x1234);
    big_endian_scalars[1].kind = AKAOS_DBUS_VALUE_UINT32;
    big_endian_scalars[1].scalar.uint32_value = UINT32_C(0x01020304);
    big_endian_scalars[2].kind = AKAOS_DBUS_VALUE_UINT64;
    big_endian_scalars[2].scalar.uint64_value =
        UINT64_C(0x0123456789abcdef);
    big_endian_scalars[3].kind = AKAOS_DBUS_VALUE_DOUBLE_BITS;
    big_endian_scalars[3].scalar.double_bits =
        UINT64_C(0x3ff0000000000000);
    static const unsigned char expected_big_endian_scalars[] = {
        0x12U, 0x34U, 0x00U, 0x00U,
        0x01U, 0x02U, 0x03U, 0x04U,
        0x01U, 0x23U, 0x45U, 0x67U, 0x89U, 0xabU, 0xcdU, 0xefU,
        0x3fU, 0xf0U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    static const unsigned char expected_little_endian_scalars[] = {
        0x34U, 0x12U, 0x00U, 0x00U,
        0x04U, 0x03U, 0x02U, 0x01U,
        0xefU, 0xcdU, 0xabU, 0x89U, 0x67U, 0x45U, 0x23U, 0x01U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xf0U, 0x3fU
    };
    if (akaos_dbus_marshal_with_byte_order(
            "qutd", big_endian_scalars, 4U, body, sizeof(body),
            AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN, &body_length) != 0 ||
        body_length != sizeof(expected_big_endian_scalars) ||
        memcmp(body, expected_big_endian_scalars,
               sizeof(expected_big_endian_scalars)) != 0 ||
        akaos_dbus_body_validate_with_byte_order(
            "qutd", body, body_length, AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN,
            &consumed) != 0 || consumed != body_length)
        goto fail;
    decoded_context.value_used = 0U;
    decoded_context.string_used = 0U;
    if (akaos_dbus_unmarshal_with_byte_order(
            "qutd", body, body_length, AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN,
            &decoded_context, &decoded_count) != 0 || decoded_count != 4U ||
        decoded_wide[0].scalar.uint16_value != UINT16_C(0x1234) ||
        decoded_wide[1].scalar.uint32_value != UINT32_C(0x01020304) ||
        decoded_wide[2].scalar.uint64_value !=
            UINT64_C(0x0123456789abcdef) ||
        decoded_wide[3].scalar.double_bits !=
            UINT64_C(0x3ff0000000000000))
        goto fail;
    unsigned char converted_scalars[sizeof(expected_big_endian_scalars)];
    memcpy(converted_scalars, body, sizeof(converted_scalars));
    if (akaos_dbus_body_convert_byte_order(
            "qutd", converted_scalars, sizeof(converted_scalars),
            AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN,
            AKAOS_DBUS_BYTE_ORDER_LITTLE_ENDIAN) != 0 ||
        memcmp(converted_scalars, expected_little_endian_scalars,
               sizeof(expected_little_endian_scalars)) != 0 ||
        akaos_dbus_body_convert_byte_order(
            "qutd", converted_scalars, sizeof(converted_scalars),
            AKAOS_DBUS_BYTE_ORDER_LITTLE_ENDIAN,
            AKAOS_DBUS_BYTE_ORDER_BIG_ENDIAN) != 0 ||
        memcmp(converted_scalars, expected_big_endian_scalars,
               sizeof(expected_big_endian_scalars)) != 0)
        goto fail;

    struct akaos_dbus_value invalid_path =
        codec_string_value(AKAOS_DBUS_VALUE_OBJECT_PATH, "not/a/path");
    if (akaos_dbus_marshal("o", &invalid_path, 1U, body, sizeof(body),
                           &body_length) == 0)
        goto fail;

    struct akaos_dbus_value status_key =
        codec_string_value(AKAOS_DBUS_VALUE_STRING, "Status");
    struct akaos_dbus_value status_text =
        codec_string_value(AKAOS_DBUS_VALUE_STRING, "ready");
    struct akaos_dbus_value ready_key =
        codec_string_value(AKAOS_DBUS_VALUE_STRING, "Ready");
    struct akaos_dbus_value ready_bool;
    memset(&ready_bool, 0, sizeof(ready_bool));
    ready_bool.kind = AKAOS_DBUS_VALUE_BOOLEAN;
    ready_bool.scalar.boolean_value = 1;
    struct akaos_dbus_value status_variant;
    memset(&status_variant, 0, sizeof(status_variant));
    status_variant.kind = AKAOS_DBUS_VALUE_VARIANT;
    status_variant.children = &status_text;
    status_variant.child_count = 1U;
    status_variant.variant_signature = "s";
    struct akaos_dbus_value ready_variant;
    memset(&ready_variant, 0, sizeof(ready_variant));
    ready_variant.kind = AKAOS_DBUS_VALUE_VARIANT;
    ready_variant.children = &ready_bool;
    ready_variant.child_count = 1U;
    ready_variant.variant_signature = "b";
    struct akaos_dbus_value status_entry_children[2] = {
        status_key, status_variant
    };
    struct akaos_dbus_value ready_entry_children[2] = {
        ready_key, ready_variant
    };
    struct akaos_dbus_value entries[2];
    memset(entries, 0, sizeof(entries));
    entries[0].kind = AKAOS_DBUS_VALUE_DICT_ENTRY;
    entries[0].children = status_entry_children;
    entries[0].child_count = 2U;
    entries[1].kind = AKAOS_DBUS_VALUE_DICT_ENTRY;
    entries[1].children = ready_entry_children;
    entries[1].child_count = 2U;
    struct akaos_dbus_value properties;
    memset(&properties, 0, sizeof(properties));
    properties.kind = AKAOS_DBUS_VALUE_ARRAY;
    properties.children = entries;
    properties.child_count = 2U;
    if (akaos_dbus_marshal("a{sv}", &properties, 1U, body, sizeof(body),
                           &body_length) != 0 ||
        akaos_dbus_body_validate("a{sv}", body, body_length, &consumed) != 0 ||
        consumed != body_length)
        goto fail;
    memset(decoded_wide, 0, sizeof(decoded_wide));
    memset(decoded_strings, 0, sizeof(decoded_strings));
    decoded_context.value_used = 0U;
    decoded_context.string_used = 0U;
    if (akaos_dbus_unmarshal("a{sv}", body, body_length, &decoded_context,
                             &decoded_count) != 0 ||
        decoded_count != 1U || decoded_wide[0].kind != AKAOS_DBUS_VALUE_ARRAY ||
        decoded_wide[0].child_count != 2U ||
        decoded_wide[0].children[0].kind != AKAOS_DBUS_VALUE_DICT_ENTRY ||
        decoded_wide[0].children[0].children[1].kind !=
            AKAOS_DBUS_VALUE_VARIANT)
        goto fail;

    struct akaos_dbus_value names[2] = {
        codec_string_value(AKAOS_DBUS_VALUE_STRING, "one"),
        codec_string_value(AKAOS_DBUS_VALUE_STRING, "two")
    };
    struct akaos_dbus_value name_array;
    memset(&name_array, 0, sizeof(name_array));
    name_array.kind = AKAOS_DBUS_VALUE_ARRAY;
    name_array.children = names;
    name_array.child_count = 2U;
    struct akaos_dbus_value tuple_children[2] = {properties, name_array};
    struct akaos_dbus_value tuple;
    memset(&tuple, 0, sizeof(tuple));
    tuple.kind = AKAOS_DBUS_VALUE_STRUCT;
    tuple.children = tuple_children;
    tuple.child_count = 2U;
    if (akaos_dbus_marshal("(a{sv}as)", &tuple, 1U, body, sizeof(body),
                           &body_length) != 0 ||
        akaos_dbus_body_validate("(a{sv}as)", body, body_length,
                                 &consumed) != 0 || consumed != body_length)
        goto fail;

    struct akaos_dbus_value nested_variant;
    memset(&nested_variant, 0, sizeof(nested_variant));
    nested_variant.kind = AKAOS_DBUS_VALUE_VARIANT;
    nested_variant.children = &tuple;
    nested_variant.child_count = 1U;
    nested_variant.variant_signature = "(a{sv}as)";
    if (akaos_dbus_marshal("v", &nested_variant, 1U, body, sizeof(body),
                           &body_length) != 0 ||
        akaos_dbus_body_validate("v", body, body_length, &consumed) != 0 ||
        consumed != body_length ||
        akaos_dbus_signature_validate("a{", NULL) == 0 ||
        akaos_dbus_body_validate("u", body, 3U, NULL) == 0)
        goto fail;

    return 0;

fail:
    errno = EIO;
    return -1;
}
