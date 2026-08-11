#include <linux/spi/spidev.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pattern.h"

static unsigned int sequence;

void *__real_malloc(size_t size);

void *__wrap_malloc(size_t size)
{
    void *ptr = __real_malloc(size);

    if (ptr)
        memset(ptr, 0xa5, size);
    return ptr;
}

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    void *arg;

    (void)fd;
    va_start(args, request);
    arg = va_arg(args, void *);
    va_end(args);
    if (request == SPI_IOC_MESSAGE(1)) {
        struct spi_ioc_transfer *transfer = arg;
        size_t word_bytes = et_word_bytes(transfer->bits_per_word);
        unsigned int wire_words = transfer->len / word_bytes;

        if (transfer->rx_buf) {
            uint8_t *rx = (uint8_t *)(uintptr_t)transfer->rx_buf;
            unsigned int payload_words = wire_words;

            if (sequence >= 33U && sequence <= 35U)
                payload_words--;
            if (et_build_pattern_sequence(transfer->bits_per_word, rx, payload_words,
                                          sequence, ET_STREAM_SLAVE) != payload_words * word_bytes)
                return -1;
            if (payload_words != wire_words)
                memset(rx + payload_words * word_bytes, 0, word_bytes);
        }
        sequence++;
        return (int)transfer->len;
    }
    return 0;
}
