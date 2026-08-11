#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../pattern.h"

static uint8_t reverse_bits(uint8_t value)
{
    uint8_t result = 0;
    unsigned int bit;

    for (bit = 0; bit < 8; bit++)
        result = (uint8_t)((result << 1) | ((value >> bit) & 1U));
    return result;
}

static void test_pattern_widths(void)
{
    uint8_t buffer[16];

    assert(et_word_bytes(8) == 1U);
    assert(et_word_bytes(16) == 2U);
    assert(et_word_bytes(32) == 4U);
    assert(et_build_pattern(8, buffer, 2) == 2U);
    assert(et_build_pattern(16, buffer, 2) == 4U);
    assert(et_build_pattern(32, buffer, 2) == 8U);
}

static void test_sequence_and_stream_identity(void)
{
    uint8_t master_first[8];
    uint8_t master_next[8];
    uint8_t slave_first[8];

    assert(et_build_pattern_sequence(8, master_first, 8, 0, ET_STREAM_MASTER) == 8U);
    assert(et_build_pattern_sequence(8, master_next, 8, 1, ET_STREAM_MASTER) == 8U);
    assert(et_build_pattern_sequence(8, slave_first, 8, 0, ET_STREAM_SLAVE) == 8U);
    assert(memcmp(master_first, master_next, sizeof(master_first)) != 0);
    assert(memcmp(master_first, slave_first, sizeof(master_first)) != 0);
}

static void test_classification(void)
{
    static const uint8_t expected16[] = { 0x12, 0x34, 0xa5, 0x5a };
    static const uint8_t swapped16[] = { 0x34, 0x12, 0x5a, 0xa5 };
    static const uint8_t swapped_reversed16[] = { 0x2c, 0x48, 0x5a, 0xa5 };
    uint8_t reversed8[sizeof(expected16)];
    size_t i;

    for (i = 0; i < sizeof(expected16); i++)
        reversed8[i] = reverse_bits(expected16[i]);

    assert(et_compare(16, expected16, expected16, 2) == ET_MATCH);
    assert(et_compare(16, expected16, swapped16, 2) == ET_WORD_SWAP);
    assert(et_compare(8, expected16, reversed8, sizeof(expected16)) == ET_BIT_REVERSE);
    assert(et_compare(16, expected16, reversed8, 2) == ET_BIT_REVERSE);
    assert(et_compare(16, expected16, swapped_reversed16, 2) == ET_WORD_SWAP_BIT_REVERSE);
}

int main(void)
{
    test_pattern_widths();
    test_sequence_and_stream_identity();
    test_classification();
    puts("pattern tests passed");
    return 0;
}
