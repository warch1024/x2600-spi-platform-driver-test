#include "../spi_stats.h"

#include <assert.h>
#include <math.h>

static void close_to(double actual, double expected) {
  assert(fabs(actual - expected) < 0.000001);
}

int main(void) {
  spi_stats_t stats;

  spi_stats_init(&stats);
  spi_stats_add(&stats, 1.0);
  spi_stats_add(&stats, 2.0);
  spi_stats_add(&stats, 3.0);
  spi_stats_add(&stats, 4.0);
  assert(stats.count == 4);
  close_to(spi_stats_min(&stats), 1.0);
  close_to(spi_stats_max(&stats), 4.0);
  close_to(spi_stats_mean(&stats), 2.5);
  close_to(spi_stats_median(&stats), 2.5);
  close_to(spi_stats_sample_stddev(&stats), 1.2909944487358056);

  spi_stats_init(&stats);
  spi_stats_add(&stats, 9.0);
  spi_stats_add(&stats, 1.0);
  spi_stats_add(&stats, 5.0);
  close_to(spi_stats_median(&stats), 5.0);
  close_to(spi_stats_sample_stddev(&stats), 4.0);
  return 0;
}
