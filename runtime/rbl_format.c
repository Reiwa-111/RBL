#include "rbl_abi.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *fill;
    size_t fill_bytes;
    size_t width;
    int precision;
    char align;
    int align_explicit;
    char type;
} RBLFormatSpec;

static size_t utf8_codepoint_bytes(const unsigned char *s, size_t remaining)
{
    unsigned char c;
    size_t n;
    uint32_t value;
    if (!remaining)
        return 0;
    c = s[0];
    if (c < 0x80)
        return 1;
    if (c >= 0xc2 && c <= 0xdf) {
        n = 2;
        value = c & 0x1f;
    } else if (c >= 0xe0 && c <= 0xef) {
        n = 3;
        value = c & 0x0f;
    } else if (c >= 0xf0 && c <= 0xf4) {
        n = 4;
        value = c & 0x07;
    } else {
        return 0;
    }
    if (remaining < n)
        return 0;
    for (size_t i = 1; i < n; ++i) {
        if ((s[i] & 0xc0) != 0x80)
            return 0;
        value = (value << 6) | (uint32_t)(s[i] & 0x3f);
    }
    if ((n == 3 && value < 0x800) || (n == 4 && value < 0x10000) ||
        (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff)
        return 0;
    return n;
}

static size_t utf8_count(const char *s)
{
    size_t bytes = strlen(s);
    size_t offset = 0;
    size_t count = 0;
    while (offset < bytes) {
        size_t n = utf8_codepoint_bytes((const unsigned char *)s + offset,
                                        bytes - offset);
        if (!n)
            rbl_fail("formatting requires valid UTF-8 text");
        offset += n;
        ++count;
    }
    return count;
}

static int is_align(char c)
{
    return c == '<' || c == '>' || c == '^';
}

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static void parse_spec(const char *text, RBLFormatSpec *spec)
{
    const unsigned char *p = (const unsigned char *)text;
    memset(spec, 0, sizeof(*spec));
    spec->align = '>';
    spec->fill = " ";
    spec->fill_bytes = 1;
    spec->precision = -1;
    if (!text || !*text)
        return;
    if (strchr(text, '{'))
        rbl_fail("dynamic or nested f-string format specifications are not supported");

    {
        size_t n = utf8_codepoint_bytes(p, strlen((const char *)p));
        if (n && is_align((char)p[n])) {
            spec->fill = (const char *)p;
            spec->fill_bytes = n;
            p += n;
            spec->align = (char)*p++;
            spec->align_explicit = 1;
        } else if (is_align((char)p[0])) {
            spec->align = (char)*p++;
            spec->align_explicit = 1;
        }
    }
    if (is_digit((char)*p)) {
        size_t width = 0;
        do {
            unsigned digit = (unsigned)(*p - '0');
            if (width > (1000000u - digit) / 10u)
                rbl_fail("f-string width exceeds 1000000");
            width = width * 10u + digit;
            ++p;
        } while (is_digit((char)*p));
        spec->width = width;
    }
    if (*p == '.') {
        size_t precision = 0;
        ++p;
        if (!is_digit((char)*p))
            rbl_fail("f-string precision must be an integer from 0 to 17");
        do {
            unsigned digit = (unsigned)(*p - '0');
            if (precision > (17u - digit) / 10u)
                rbl_fail("f-string precision must be from 0 to 17");
            precision = precision * 10u + digit;
            ++p;
        } while (is_digit((char)*p));
        spec->precision = (int)precision;
    }
    if (*p)
        spec->type = (char)*p++;
    if (*p)
        rbl_fail("invalid f-string format specification");
    if (spec->type && !strchr("dboxXfFeEgGs", spec->type))
        rbl_fail("unsupported f-string format type");
}

static size_t format_integer(uint64_t payload, char type, char *buffer,
                             size_t capacity)
{
    int64_t value;
    memcpy(&value, &payload, sizeof(value));
    if (!type || type == 'd') {
        int n = snprintf(buffer, capacity, "%lld", (long long)value);
        if (n < 0 || (size_t)n >= capacity)
            rbl_fail("integer formatting failed");
        return (size_t)n;
    }
    {
        static const char digits[] = "0123456789abcdef";
        unsigned base = (type == 'b') ? 2u : ((type == 'o') ? 8u : 16u);
        uint64_t magnitude = value < 0 ? (uint64_t)(-(value + 1)) + 1u
                                      : (uint64_t)value;
        char reversed[65];
        size_t used = 0;
        size_t out = 0;
        do {
            unsigned digit = (unsigned)(magnitude % base);
            reversed[used++] = type == 'X' ? "0123456789ABCDEF"[digit]
                                             : digits[digit];
            magnitude /= base;
        } while (magnitude);
        if (value < 0)
            buffer[out++] = '-';
        while (used)
            buffer[out++] = reversed[--used];
        if (out >= capacity)
            rbl_fail("integer formatting failed");
        buffer[out] = '\0';
        return out;
    }
}

static size_t format_float(uint64_t payload, char type, int precision,
                           char *buffer, size_t capacity)
{
    double value;
    int n;
    memcpy(&value, &payload, sizeof(value));
    if (!type && precision < 0) {
        n = snprintf(buffer, capacity, "%.17g", value);
    } else {
        if (!type)
            type = 'g';
        if (precision < 0) {
            switch (type) {
            case 'f': n = snprintf(buffer, capacity, "%f", value); break;
            case 'F': n = snprintf(buffer, capacity, "%F", value); break;
            case 'e': n = snprintf(buffer, capacity, "%e", value); break;
            case 'E': n = snprintf(buffer, capacity, "%E", value); break;
            case 'g': n = snprintf(buffer, capacity, "%g", value); break;
            default:  n = snprintf(buffer, capacity, "%G", value); break;
            }
        } else {
            switch (type) {
            case 'f': n = snprintf(buffer, capacity, "%.*f", precision, value); break;
            case 'F': n = snprintf(buffer, capacity, "%.*F", precision, value); break;
            case 'e': n = snprintf(buffer, capacity, "%.*e", precision, value); break;
            case 'E': n = snprintf(buffer, capacity, "%.*E", precision, value); break;
            case 'g': n = snprintf(buffer, capacity, "%.*g", precision, value); break;
            default:  n = snprintf(buffer, capacity, "%.*G", precision, value); break;
            }
        }
    }
    if (n < 0 || (size_t)n >= capacity)
        rbl_fail("float formatting failed");
    return (size_t)n;
}

RBL_ABI_CALL RBLRuntimeValue rbl_format_value(uint64_t tag, uint64_t payload,
                                               const char *format_spec)
{
    RBLFormatSpec spec;
    char number[512];
    const char *source;
    size_t source_bytes;
    size_t source_chars;
    size_t left = 0;
    size_t right = 0;
    size_t output_bytes;
    size_t cursor = 0;
    char *output;
    RBLRuntimeValue converted;

    parse_spec(format_spec, &spec);
    if (spec.precision >= 0 && tag != RBL_TAG_FLOAT)
        rbl_fail("precision is only supported for floating-point f-string values");
    if (spec.precision >= 0 && spec.type == 's')
        rbl_fail("precision cannot be used with the string f-string format type");

    if ((!spec.type && spec.precision < 0) || spec.type == 's') {
        if (spec.type == 's' && tag != RBL_TAG_INT && tag != RBL_TAG_FLOAT &&
            tag != RBL_TAG_STRING && tag != RBL_TAG_BOOL && tag != RBL_TAG_LIST &&
            tag != RBL_TAG_DICTIONARY && tag != RBL_TAG_TUPLE &&
            tag != RBL_TAG_UNIT && tag != RBL_TAG_NULL)
            rbl_fail("str() cannot convert this value");
        converted = rbl_str(tag, payload);
        if (converted.tag != RBL_TAG_STRING)
            rbl_fail("string conversion failed during formatting");
        source = (const char *)(uintptr_t)converted.payload;
        if (!source)
            rbl_fail("invalid string value during formatting");
        source_bytes = strlen(source);
        if (!spec.align_explicit)
            spec.align = (!spec.type &&
                          (tag == RBL_TAG_INT || tag == RBL_TAG_FLOAT)) ? '>' : '<';
    } else if (spec.type && strchr("dboxX", spec.type)) {
        if (tag != RBL_TAG_INT)
            rbl_fail("integer f-string format type requires an int value");
        source_bytes = format_integer(payload, spec.type, number, sizeof(number));
        source = number;
    } else {
        if (tag != RBL_TAG_FLOAT)
            rbl_fail("floating-point f-string format type requires a float value");
        source_bytes = format_float(payload, spec.type, spec.precision,
                                    number, sizeof(number));
        source = number;
    }

    /* Empty/no-width specs must preserve arbitrary string bytes exactly like
       ordinary str() interpolation. Validate/count UTF-8 only when width
       semantics actually require a code-point length. */
    source_chars = spec.width ? utf8_count(source) : 0;
    if (spec.width > source_chars) {
        size_t padding = spec.width - source_chars;
        if (spec.align == '<')
            right = padding;
        else if (spec.align == '^') {
            left = padding / 2;
            right = padding - left;
        } else {
            left = padding;
        }
    }
    if (left > (SIZE_MAX - source_bytes - 1) / spec.fill_bytes ||
        right > (SIZE_MAX - source_bytes - 1 - left * spec.fill_bytes) /
                    spec.fill_bytes)
        rbl_fail("formatted string is too large");
    output_bytes = source_bytes + (left + right) * spec.fill_bytes + 1;
    output = rbl_string_alloc((uint64_t)output_bytes);
    if (!output)
        rbl_fail("out of memory while formatting a string");
    for (size_t i = 0; i < left; ++i) {
        memcpy(output + cursor, spec.fill, spec.fill_bytes);
        cursor += spec.fill_bytes;
    }
    memcpy(output + cursor, source, source_bytes);
    cursor += source_bytes;
    for (size_t i = 0; i < right; ++i) {
        memcpy(output + cursor, spec.fill, spec.fill_bytes);
        cursor += spec.fill_bytes;
    }
    output[cursor] = '\0';
    return (RBLRuntimeValue){RBL_TAG_STRING, (uint64_t)(uintptr_t)output};
}
