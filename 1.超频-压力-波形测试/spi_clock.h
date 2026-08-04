#ifndef SPI_CLOCK_H
#define SPI_CLOCK_H

#include <stdint.h>

#define SPI_MPLL_HZ UINT64_C(1800000000)
#define SPI_SSICDR_MIN_DIVISOR 1U
#define SPI_SSICDR_MAX_DIVISOR 256U

typedef struct {
    uint64_t requested_div_ssi_hz;
    uint32_t ssicdr_divisor;
    uint64_t actual_div_ssi_hz;
    uint64_t actual_sclk_hz;
} spi_clock_plan_t;

static inline int spi_clock_plan_from_mpll(uint64_t requested_div_ssi_hz, spi_clock_plan_t *plan)
{
    uint64_t divisor;

    if (!requested_div_ssi_hz || !plan || requested_div_ssi_hz > SPI_MPLL_HZ)
        return -1;
    divisor = (SPI_MPLL_HZ + requested_div_ssi_hz / 2U) / requested_div_ssi_hz;
    if (divisor < SPI_SSICDR_MIN_DIVISOR || divisor > SPI_SSICDR_MAX_DIVISOR)
        return -1;
    plan->requested_div_ssi_hz = requested_div_ssi_hz;
    plan->ssicdr_divisor = (uint32_t)divisor;
    plan->actual_div_ssi_hz = SPI_MPLL_HZ / divisor;
    plan->actual_sclk_hz = plan->actual_div_ssi_hz / 2U;
    return 0;
}

#endif
