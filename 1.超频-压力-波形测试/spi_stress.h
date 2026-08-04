#ifndef SPI_STRESS_H
#define SPI_STRESS_H

#define SPI_STRESS_DEFAULT_SECONDS 5400U

static inline int spi_stress_seconds_valid(unsigned int seconds)
{
    return seconds != 0U;
}

static inline const char *spi_stress_default_cs(unsigned int bus)
{
    if (bus == 0U)
        return "pc09";
    if (bus == 1U)
        return "pc30";
    return 0;
}

#endif
