/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Author: <che.jiang@artinchip.com>
 * Desc: mpp time interface
 */

#ifndef MPP_TIME_H
#define MPP_TIME_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Monotonic clock — unaffected by system time changes,
 * use for duration/elapsed measurement */
static inline int64_t mpp_get_time_us(void)
{
	struct timespec ts = { 0 };

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}

static inline int64_t mpp_get_time_ms(void)
{
	struct timespec ts = { 0 };

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static inline int64_t mpp_get_time_s(void)
{
	struct timespec ts = { 0 };

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec;
}

/* UTC time via time() — seconds since epoch,
 * use for timestamp/wall-clock */
static inline time_t mpp_get_utc_time(void)
{
	return time(NULL);
}

/* Broken-down UTC time via gmtime_r(), unaffected by TZ */
static inline void mpp_get_utc_tm(struct tm *utc_tm)
{
	time_t t = time(NULL);

	gmtime_r(&t, utc_tm);
}

/* Broken-down local time via localtime_r(),
 * needs setenv("TZ", ...) + tzset() first */
static inline void mpp_get_local_tm(struct tm *local_tm)
{
	time_t t = time(NULL);

	localtime_r(&t, local_tm);
}

/* Set system UTC time via clock_settime(), requires root privilege */
static inline int mpp_set_utc_time(time_t t)
{
	struct timespec ts = {
		.tv_sec  = t,
		.tv_nsec = 0,
	};

	return clock_settime(CLOCK_REALTIME, &ts);
}


#ifdef __cplusplus
}
#endif

#endif /* MPP_TIME_H */
