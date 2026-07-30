#include "spi_stats.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int compare_double(const void *left, const void *right) {
  double a = *(const double *)left;
  double b = *(const double *)right;

  return (a > b) - (a < b);
}

void spi_stats_init(spi_stats_t *stats) {
  memset(stats, 0, sizeof(*stats));
}

int spi_stats_add(spi_stats_t *stats, double value) {
  double delta;

  if (stats->count >= SPI_STATS_MAX_SAMPLES)
    return -1;
  stats->values[stats->count++] = value;
  if (stats->count == 1) {
    stats->min = value;
    stats->max = value;
    stats->mean = value;
    return 0;
  }
  if (value < stats->min)
    stats->min = value;
  if (value > stats->max)
    stats->max = value;
  delta = value - stats->mean;
  stats->mean += delta / stats->count;
  stats->m2 += delta * (value - stats->mean);
  return 0;
}

double spi_stats_min(const spi_stats_t *stats) {
  return stats->count ? stats->min : 0.0;
}

double spi_stats_max(const spi_stats_t *stats) {
  return stats->count ? stats->max : 0.0;
}

double spi_stats_mean(const spi_stats_t *stats) {
  return stats->count ? stats->mean : 0.0;
}

double spi_stats_median(const spi_stats_t *stats) {
  double sorted[SPI_STATS_MAX_SAMPLES];
  size_t middle;

  if (!stats->count)
    return 0.0;
  memcpy(sorted, stats->values, stats->count * sizeof(sorted[0]));
  qsort(sorted, stats->count, sizeof(sorted[0]), compare_double);
  middle = stats->count / 2;
  if (stats->count % 2)
    return sorted[middle];
  return (sorted[middle - 1] + sorted[middle]) / 2.0;
}

double spi_stats_sample_stddev(const spi_stats_t *stats) {
  if (stats->count < 2)
    return 0.0;
  return sqrt(stats->m2 / (stats->count - 1));
}
