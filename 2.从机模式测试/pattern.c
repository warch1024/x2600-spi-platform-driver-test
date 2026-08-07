#include "pattern.h"

#include <string.h>

static const uint8_t pattern8[] = { 0x12, 0x34, 0xa5, 0x5a, 0x81, 0x7e, 0x01, 0xfe };
static const uint16_t pattern16[] = { 0x1234, 0xa55a, 0x8001, 0x0f70, 0x5aa5, 0x7e81, 0x0102, 0xfe7d };
static const uint32_t pattern32[] = {
    UINT32_C(0x12345678), UINT32_C(0xa55ac33c), UINT32_C(0x8001f00d), UINT32_C(0x0f1e2d3c),
    UINT32_C(0x5aa5c33c), UINT32_C(0x7e81b42d), UINT32_C(0x01020304), UINT32_C(0xfe7d6c5b),
};

size_t et_word_bytes(unsigned int bits)
{
    if (bits == 8U)
        return 1U;
    if (bits == 16U)
        return 2U;
    if (bits == 32U)
        return 4U;
    return 0U;
}
static uint32_t sequence_mask(unsigned int bits, uint32_t sequence, et_stream_t stream)
{
    uint32_t stream_mask = stream == ET_STREAM_SLAVE ? UINT32_C(0xa55a3cc3) : 0U;
    uint32_t value = sequence * UINT32_C(0x9e3779b9) ^ stream_mask;

    if (bits <= 8U)
        return value & UINT32_C(0xff);
    if (bits <= 16U)
        return value & UINT32_C(0xffff);
    return value;
}

size_t et_build_pattern_sequence(unsigned int bits,
                                 uint8_t *buf,
                                 size_t words,
                                 uint32_t sequence,
                                 et_stream_t stream)
{
    size_t i;
    size_t bytes = et_word_bytes(bits);
    uint32_t mask = sequence_mask(bits, sequence, stream);

    if (!buf || !bytes)
        return 0U;
    for (i = 0; i < words; i++) {
        size_t index = i % 8U;
        if (bits == 8U) {
            buf[i] = pattern8[index] ^ (uint8_t)mask;
        } else if (bits == 16U) {
            uint16_t value = pattern16[index] ^ (uint16_t)mask;
            buf[i * bytes] = (uint8_t)(value >> 8);
            buf[i * bytes + 1U] = (uint8_t)value;
        } else {
            uint32_t value = pattern32[index] ^ mask;
            buf[i * bytes] = (uint8_t)(value >> 24);
            buf[i * bytes + 1U] = (uint8_t)(value >> 16);
            buf[i * bytes + 2U] = (uint8_t)(value >> 8);
            buf[i * bytes + 3U] = (uint8_t)value;
        }
    }
    return words * bytes;
}

//返回实际写入的字节数
size_t et_build_pattern(unsigned int bits, uint8_t *buf, size_t words)
{
    return et_build_pattern_sequence(bits, buf, words, 0, ET_STREAM_MASTER);
}

static uint8_t reverse_bits(uint8_t value)
{
    uint8_t result = 0;
    unsigned int bit;

    for (bit = 0; bit < 8U; bit++)
        result = (uint8_t)((result << 1) | ((value >> bit) & 1U));
    return result;
}

static int is_word_swap(unsigned int bits, const uint8_t *expected, const uint8_t *actual, size_t words)
{
    size_t bytes = et_word_bytes(bits);
    size_t i;

    if (bytes < 2U)
        return 0;
    for (i = 0; i < words; i++) {
        size_t j;
        for (j = 0; j < bytes; j++) {
            if (actual[i * bytes + j] != expected[i * bytes + bytes - 1U - j])
                return 0;
        }
    }
    return 1;
}

static int is_bit_reverse(const uint8_t *expected, const uint8_t *actual, size_t bytes)
{
    size_t i;

    for (i = 0; i < bytes; i++) {
        if (actual[i] != reverse_bits(expected[i]))
            return 0;
    }
    return 1;
}

static int is_word_swap_bit_reverse(unsigned int bits,
                                    const uint8_t *expected,
                                    const uint8_t *actual,
                                    size_t words)
{
    size_t bytes = et_word_bytes(bits);
    size_t i;

    if (bytes < 2U)
        return 0;
    for (i = 0; i < words; i++) {
        size_t j;
        for (j = 0; j < bytes; j++) {
            if (actual[i * bytes + j] != reverse_bits(expected[i * bytes + bytes - 1U - j]))
                return 0;
        }
    }
    return 1;
}

et_result_t et_compare(unsigned int bits,
                        const uint8_t *expected,
                        const uint8_t *actual,
                        size_t words)
{
    size_t bytes = et_word_bytes(bits);
    int swapped;
    int reversed;

    if (!bytes || !expected || !actual)
        return ET_MISMATCH;
    if (!memcmp(expected, actual, words * bytes))
        return ET_MATCH;
    swapped = is_word_swap(bits, expected, actual, words);
    reversed = is_bit_reverse(expected, actual, words * bytes);
    if (is_word_swap_bit_reverse(bits, expected, actual, words))
        return ET_WORD_SWAP_BIT_REVERSE;
    if (swapped)
        return ET_WORD_SWAP;
    if (reversed)
        return ET_BIT_REVERSE;
    return ET_MISMATCH;
}

const char *et_result_name(et_result_t result)
{
    switch (result) {
    case ET_MATCH:
        return "match";
    case ET_WORD_SWAP:
        return "word-byte-swap";
    case ET_BIT_REVERSE:
        return "bit-reverse";
    case ET_WORD_SWAP_BIT_REVERSE:
        return "word-byte-swap+bit-reverse";
    default:
        return "mismatch";
    }
}
