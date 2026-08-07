#ifndef SPI_ENDIAN_PATTERN_H
#define SPI_ENDIAN_PATTERN_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    ET_MATCH = 0,
    ET_WORD_SWAP,
    ET_BIT_REVERSE,
    ET_WORD_SWAP_BIT_REVERSE,
    ET_MISMATCH,
} et_result_t;

typedef enum {
    ET_STREAM_MASTER = 0,
    ET_STREAM_SLAVE = 1,
} et_stream_t;

size_t et_word_bytes(unsigned int bits);
size_t et_build_pattern(unsigned int bits, uint8_t *buf, size_t words);
size_t et_build_pattern_sequence(unsigned int bits,
                                 uint8_t *buf,
                                 size_t words,
                                 uint32_t sequence,
                                 et_stream_t stream);
et_result_t et_compare(unsigned int bits,
                        const uint8_t *expected,
                        const uint8_t *actual,
                        size_t words);
const char *et_result_name(et_result_t result);

#endif
