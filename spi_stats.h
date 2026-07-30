#ifndef SPI_STATS_H
#define SPI_STATS_H

#include <stddef.h>

#define SPI_STATS_MAX_SAMPLES 1000U

typedef struct {
  double values[SPI_STATS_MAX_SAMPLES];
  size_t count;
  double min;
  double max;
  double mean;
  double m2;
} spi_stats_t;

void spi_stats_init(spi_stats_t *stats);
int spi_stats_add(spi_stats_t *stats, double value);
double spi_stats_min(const spi_stats_t *stats);
double spi_stats_max(const spi_stats_t *stats);
double spi_stats_mean(const spi_stats_t *stats);
double spi_stats_median(const spi_stats_t *stats);
double spi_stats_sample_stddev(const spi_stats_t *stats);

#endif
