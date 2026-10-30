#ifndef XSCORE_TIME_H
#define XSCORE_TIME_H

#include <stdint.h>

/* Seconds since 1970-01-01 00:00:00 UTC from the platform wall clock.
 * Returns zero when the platform has no readable real-time clock. */
uint64_t xscore_time_epoch_seconds(void);

#endif
