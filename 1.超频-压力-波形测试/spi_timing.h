#ifndef SPI_TIMING_H
#define SPI_TIMING_H

#include <stdint.h>
#include <string.h>

typedef struct {
    uint64_t bytes;
    uint64_t ioctl_count;
    double theoretical_seconds;
    double ioctl_seconds;
} spi_timing_t;

static inline void spi_timing_init(spi_timing_t *timing)
{
    memset(timing, 0, sizeof(*timing));
}

static inline int spi_timing_record(spi_timing_t *timing, uint64_t bytes, uint64_t sclk_hz, double ioctl_seconds)
{
    if (!timing || !bytes || !sclk_hz || ioctl_seconds < 0.0 || UINT64_MAX - timing->bytes < bytes ||
        timing->ioctl_count == UINT64_MAX)
        return -1;
    timing->bytes += bytes;
    timing->ioctl_count++;
    timing->theoretical_seconds += (double)bytes * 8.0 / (double)sclk_hz;
    timing->ioctl_seconds += ioctl_seconds;
    return 0;
}

static inline double spi_timing_ioctl_duty_percent(const spi_timing_t *timing)
{
    return timing && timing->ioctl_seconds > 0.0 ? timing->theoretical_seconds * 100.0 / timing->ioctl_seconds : 0.0;
}

static inline double spi_timing_non_data_seconds(const spi_timing_t *timing)
{
    return timing ? timing->ioctl_seconds - timing->theoretical_seconds : 0.0;
}

static inline double spi_timing_average_ioctl_seconds(const spi_timing_t *timing)
{
    return timing && timing->ioctl_count ? timing->ioctl_seconds / (double)timing->ioctl_count : 0.0;
}

static inline double spi_timing_average_theoretical_seconds(const spi_timing_t *timing)
{
    return timing && timing->ioctl_count ? timing->theoretical_seconds / (double)timing->ioctl_count : 0.0;
}

static inline double spi_timing_wall_duty_percent(const spi_timing_t *timing, double wall_seconds)
{
    return timing && wall_seconds > 0.0 ? timing->theoretical_seconds * 100.0 / wall_seconds : 0.0;
}

#endif
