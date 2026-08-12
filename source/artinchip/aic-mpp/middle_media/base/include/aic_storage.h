/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  Author: <che.jiang@artinchip.com>
 *  Desc: Storage manager -- database + file management for recorder
 */

#ifndef __AIC_STORAGE_H__
#define __AIC_STORAGE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define AIC_STORAGE_PATH_LEN   	    256
#define AIC_STORAGE_USER_DATA_LEN   128

struct aic_storage;

struct aic_storage_record {
	char file_path[AIC_STORAGE_PATH_LEN];
	char thumbnail_path[AIC_STORAGE_PATH_LEN];
	int64_t file_size;
	int duration;
	int64_t start_time;
	int64_t end_time;
	int locked;
	char user_data[AIC_STORAGE_USER_DATA_LEN];
	char created_at[32];
};

struct aic_storage_picture {
	char file_path[AIC_STORAGE_PATH_LEN];
	char thumbnail_path[AIC_STORAGE_PATH_LEN];
	int64_t file_size;
	int64_t timestamp;
	char created_at[32];
};

/**
 * Create storage for one recorder output directory.
 * max_mb is auto-detected from card capacity; keep_count defaults to 100.
 */
struct aic_storage *aic_storage_create(const char *base_path);
void aic_storage_destroy(struct aic_storage *s);

/* ======================== video record API ======================== */

/**
 * Record a completed video file into the database.
 * Automatically triggers cleanup if max_mb / keep_count policy is configured.
 */
int aic_storage_record_file(struct aic_storage *s,
			    const char *file_path,
			    const char *thumbnail_path,
			    int64_t start_time, int64_t end_time,
			    int duration_s, int64_t file_size,
			    const char *user_data);

/** Sync: scan disk for existing .mp4/.jpg files and insert missing ones into DB.
 *  Call after aic_storage_create to recover from a lost/corrupted database. */
int aic_storage_sync(struct aic_storage *s);

/** Manual cleanup: delete oldest files exceeding max_mb, keeping keep_count. */
int aic_storage_cleanup(struct aic_storage *s,
			int64_t max_mb, int keep_count);

/** Lock/unlock a video file (locked files are skipped by cleanup). */
int aic_storage_lock_file(struct aic_storage *s, const char *file_path, int locked);

/**
 * Mark that the next completed recording should be locked.
 * Call during recording — takes effect when the file finishes.
 */
int aic_storage_set_lock_pending(struct aic_storage *s, int pending);

/**
 * Set user data for a specific file (retroactive update).
 * Returns SQLITE_OK on success.
 */
int aic_storage_set_user_data(struct aic_storage *s,
			      const char *file_path, const char *data);

/**
 * Set pending user data — applied to the next completed recording.
 * Pass NULL or "" to clear.
 */
int aic_storage_set_pending_user_data(struct aic_storage *s, const char *data);

/** Delete a single video file by path (both on disk and from database). */
int aic_storage_delete_file(struct aic_storage *s, const char *file_path);

/** Query: get total video record count. */
int aic_storage_get_count(struct aic_storage *s);

/** Query: get video record list (newest first), returns actual count (<= max). */
int aic_storage_get_list(struct aic_storage *s,
			 struct aic_storage_record *records, int max);

/** Query: get a single video record by file path. Returns 0 on success, -1 if not found. */
int aic_storage_get_record(struct aic_storage *s, const char *file_path,
			   struct aic_storage_record *record);

/** Query: get video records by date (YYYY-MM-DD), newest first. */
int aic_storage_get_list_by_date(struct aic_storage *s, const char *date,
				 struct aic_storage_record *records, int max);

/** Query: count locked video records. */
int aic_storage_get_locked_count(struct aic_storage *s);

/** Query: get locked video records, newest first. */
int aic_storage_get_locked_list(struct aic_storage *s,
				struct aic_storage_record *records, int max);

/** Query: count video records by date (YYYY-MM-DD). */
int aic_storage_get_count_by_date(struct aic_storage *s, const char *date);

/* ======================== picture API ======================== */

/** Record a snapshot picture into the database. */
int aic_storage_record_picture(struct aic_storage *s,
			       const char *file_path,
			       const char *thumbnail_path,
			       int64_t timestamp, int64_t file_size);

/** Query: get total picture count. */
int aic_storage_get_picture_count(struct aic_storage *s);

/** Query: count pictures by date (YYYY-MM-DD). */
int aic_storage_get_picture_count_by_date(struct aic_storage *s, const char *date);

/** Query: get picture list (newest first), returns actual count (<= max). */
int aic_storage_get_picture_list(struct aic_storage *s,
				 struct aic_storage_picture *pictures, int max);

/** Query: get pictures by date (YYYY-MM-DD), newest first. */
int aic_storage_get_picture_list_by_date(struct aic_storage *s, const char *date,
					 struct aic_storage_picture *pictures, int max);

/** Delete a single picture by path (both on disk and from database). */
int aic_storage_delete_picture(struct aic_storage *s, const char *file_path);

/* ======================== disk utils ======================== */

/** Get partition total / used / free space in MB (any path on the filesystem). */
int64_t aic_storage_get_total_mb(const char *path);
int64_t aic_storage_get_free_mb(const char *path);
int64_t aic_storage_get_used_mb(const char *path);
/** Check whether a path is mounted. Returns 1 if mounted, 0 if not, -1 on error. */
int aic_storage_is_mounted(const char *path);


/** Format: remove all files under path. Returns deleted count or -1. */
int aic_storage_format(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* __AIC_STORAGE_H__ */
