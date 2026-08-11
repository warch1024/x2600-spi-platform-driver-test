#include <libhardware2/sslv.h>

#include "pattern.h"

static unsigned int bits_per_word = 8U;
static int busy;
static int info_required;
static int first_enable_seen;
static unsigned int receive_sequence;
static unsigned int send_calls;

int sslv_open(char *sslv_dev_path)
{
    (void)sslv_dev_path;
    return 1;
}

void sslv_close(int fd)
{
    (void)fd;
}

int sslv_enable(int fd)
{
    (void)fd;
    if (busy || (!first_enable_seen && info_required))
        return -1;
    busy = 1;
    if (!first_enable_seen) {
        first_enable_seen = 1;
        info_required = 1;
    }
    return 0;
}

int sslv_disable(int fd)
{
    (void)fd;
    if (!busy)
        return -1;
    busy = 0;
    return 0;
}

int sslv_receive(int fd, void *buf, int size)
{
    size_t words;

    (void)fd;
    words = (size_t)size / et_word_bytes(bits_per_word);
    if (et_build_pattern_sequence(bits_per_word, buf, words, receive_sequence,
                                  ET_STREAM_MASTER) != (size_t)size)
        return -1;
    receive_sequence++;
    return size;
}

int sslv_send(int fd, void *buf, int size, char add_zero)
{
    int expected_add_zero = send_calls >= 9U && send_calls < 12U;

    (void)fd;
    (void)buf;
    send_calls++;
    if (!!add_zero != expected_add_zero)
        return -1;
    return size;
}

void sslv_get_info(int fd, struct sslv_config_data *info)
{
    (void)fd;
    info->id = 0U;
    info->name = 0;
    info->bits_per_word = bits_per_word;
    info->sslv_pol = 1U;
    info->sslv_pha = 1U;
    info->loop_mode = 0U;
    info_required = 0;
}

int sslv_set_mode(int fd, int mode)
{
    (void)fd;
    if (busy || mode != 3)
        return -1;
    info_required = 1;
    return 0;
}

int sslv_set_bits(int fd, int bits)
{
    (void)fd;
    if (busy || bits < 4 || bits > 32)
        return -1;
    bits_per_word = (unsigned int)bits;
    info_required = 1;
    return 0;
}
