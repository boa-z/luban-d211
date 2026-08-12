/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  Author: <che.jiang@artinchip.com>
 *  Desc: Storage manager -- database + file management for recorder
 */

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>
#include "aic_storage.h"
#include "mpp_log.h"
#include "mpp_mem.h"
#include "mpp_time.h"

#define STORAGE_DB_NAME       "records.db"
#define STORAGE_TABLE_RECORD  "record"
#define STORAGE_TABLE_PICTURE "picture"
#define DEFAULT_KEEP_COUNT    100

struct aic_storage {
	sqlite3 *db;
	char base_path[AIC_STORAGE_PATH_LEN];
	char filter_ext[16];
	int64_t max_mb;
	int keep_count;

	pthread_t tid;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool running;
	bool need_cleanup;
	bool lock_pending;
	char pending_user_data[AIC_STORAGE_USER_DATA_LEN];
};

/* ======================== file helpers ======================== */

static int ensure_dir(const char *path)
{
	struct stat st;

	if (stat(path, &st) == 0)
		return S_ISDIR(st.st_mode) ? 0 : -1;
	if (mkdir(path, 0755) == 0)
		return 0;
	loge("[storage] mkdir %s failed: %s", path, strerror(errno));
	return -1;
}

static int filter_by_ext(const char *name, const char *ext)
{
	const char *dot;

	if (!ext || ext[0] == '\0')
		return 1;
	dot = strrchr(name, '.');
	return (dot && strncmp(dot, ext, strlen(ext) + 1) == 0) ? 1 : 0;
}

static int cmp_name_desc(const struct dirent **a, const struct dirent **b)
{
	return strncmp((*b)->d_name, (*a)->d_name, NAME_MAX);
}

static void free_namelist(struct dirent **namelist, int n)
{
	for (int i = 0; i < n; i++)
		free(namelist[i]);
	free(namelist);
}

static int file_get_list(struct aic_storage *s,
			 char (*list)[AIC_STORAGE_PATH_LEN], int max)
{
	struct dirent **namelist;
	int n, count = 0;

	n = scandir(s->base_path, &namelist, NULL, cmp_name_desc);
	if (n <= 0)
		return 0;

	for (int i = 0; i < n && count < max; i++) {
		if (filter_by_ext(namelist[i]->d_name, s->filter_ext)) {
			int written = snprintf(list[count], AIC_STORAGE_PATH_LEN, "%s/%s",
					 s->base_path, namelist[i]->d_name);
			if (written >= AIC_STORAGE_PATH_LEN)
				list[count][AIC_STORAGE_PATH_LEN - 1] = '\0';
			count++;
		}
	}
	free_namelist(namelist, n);
	return count;
}

static int file_delete(struct aic_storage *s, const char *file_path)
{
	(void)s;
	if (!file_path)
		return -1;
	return unlink(file_path);
}

/* ======================== database helpers ======================== */

/* escape single quotes for SQL string literals: ' → '' */
static void sql_escape(const char *src, char *dst, int dst_len)
{
	int j = 0;

	if (!src || !dst || dst_len <= 0)
		return;
	for (int i = 0; src[i] && j < dst_len - 3; i++) {
		if (src[i] == '\'') {
			dst[j++] = '\'';
			dst[j++] = '\'';
		} else {
			dst[j++] = src[i];
		}
	}
	dst[j] = '\0';
}

static int db_exec(struct aic_storage *s, const char *sql)
{
	char *err = NULL;
	int rc;

	if (!s->db || !sql)
		return -1;

	rc = sqlite3_exec(s->db, sql, NULL, NULL, &err);
	if (rc != SQLITE_OK) {
		if (err) {
			loge("[storage] exec failed: %s (SQL: %.100s)", err, sql);
			sqlite3_free(err);
		}
		return rc;
	}
	return SQLITE_OK;
}

static int do_cleanup(struct aic_storage *s, int64_t max_mb, int keep_count)
{
	char files[128][AIC_STORAGE_PATH_LEN];
	char sql[512];
	int64_t total_mb, free_mb;
	int total, deleted = 0;

	if (max_mb <= 0 || keep_count < 0)
		return -1;

	/* only clean when free space drops below 10% */
	total_mb = aic_storage_get_total_mb(s->base_path);
	free_mb = aic_storage_get_free_mb(s->base_path);
	if (total_mb <= 0)
		return -1;
	if (free_mb > total_mb / 10)
		return 0;

	snprintf(s->filter_ext, sizeof(s->filter_ext), ".mp4");
	total = file_get_list(s, files, 128);
	s->filter_ext[0] = '\0';

	if (total <= keep_count)
		return 0;

	/* delete oldest first, skip locked, stop when free > 20% or keep_count reached */
	for (int i = total - 1; i >= 0 && deleted < total - keep_count; i--) {
		snprintf(sql, sizeof(sql),
			 "SELECT locked FROM %s WHERE file_path = '%s';",
			 STORAGE_TABLE_RECORD, files[i]);
		sqlite3_stmt *lock_stmt = NULL;
		int locked = 0;
		if (sqlite3_prepare_v2(s->db, sql, -1, &lock_stmt, NULL) == SQLITE_OK) {
			if (sqlite3_step(lock_stmt) == SQLITE_ROW)
				locked = sqlite3_column_int(lock_stmt, 0);
			sqlite3_finalize(lock_stmt);
		}
		if (locked)
			continue;

		if (file_delete(s, files[i]) == 0) {
			snprintf(sql, sizeof(sql),
				 "DELETE FROM %s WHERE file_path = '%s';",
				 STORAGE_TABLE_RECORD, files[i]);
			db_exec(s, sql);
			deleted++;
		}
		free_mb = aic_storage_get_free_mb(s->base_path);
		if (free_mb > total_mb / 5)
			break;
	}

	if (deleted > 0)
		logi("[storage] cleanup deleted %d files, free %lld/%lld MB",
		     deleted, (long long)free_mb, (long long)total_mb);

	return deleted;
}

/* ====================== cleanup thread ====================== */

static void *cleanup_thread(void *arg)
{
	struct aic_storage *s = (struct aic_storage *)arg;

	pthread_mutex_lock(&s->lock);
	while (s->running) {
		if (!s->need_cleanup) {
			pthread_cond_wait(&s->cond, &s->lock);
			continue;
		}
		s->need_cleanup = false;
		pthread_mutex_unlock(&s->lock);

		do_cleanup(s, s->max_mb, s->keep_count);

		pthread_mutex_lock(&s->lock);
	}
	pthread_mutex_unlock(&s->lock);
	return NULL;
}

/* ======================== public API ======================== */

struct aic_storage *aic_storage_create(const char *base_path)
{
	struct aic_storage *s;
	char db_path[AIC_STORAGE_PATH_LEN];
	int rc;

	if (!base_path || base_path[0] == '\0') {
		loge("[storage] invalid base_path");
		return NULL;
	}

	s = (struct aic_storage *)mpp_alloc(sizeof(*s));
	if (!s) {
		loge("[storage] mpp_alloc failed");
		return NULL;
	}
	memset(s, 0, sizeof(*s));
	strncpy(s->base_path, base_path, AIC_STORAGE_PATH_LEN - 1);
	s->base_path[AIC_STORAGE_PATH_LEN - 1] = '\0';
	/* strip trailing slashes to avoid // in generated paths */
	int sl = strlen(s->base_path);
	while (sl > 1 && s->base_path[sl - 1] == '/')
		s->base_path[--sl] = '\0';

	if (ensure_dir(s->base_path) != 0)
		goto err_free;

	s->max_mb = aic_storage_get_total_mb(s->base_path);
	s->keep_count = DEFAULT_KEEP_COUNT;

	int db_len = snprintf(db_path, sizeof(db_path), "%s/%s",
			      s->base_path, STORAGE_DB_NAME);
	if (db_len >= (int)sizeof(db_path))
		db_path[sizeof(db_path) - 1] = '\0';

	rc = sqlite3_open(db_path, &s->db);
	if (rc != SQLITE_OK) {
		loge("[storage] open %s failed: %s", db_path,
		     sqlite3_errmsg(s->db));
		goto err_close_db;
	}

	sqlite3_busy_timeout(s->db, 3000);
	sqlite3_exec(s->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
	sqlite3_exec(s->db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);

	rc = db_exec(s,
		     "CREATE TABLE IF NOT EXISTS " STORAGE_TABLE_RECORD " ("
		     "id INTEGER PRIMARY KEY AUTOINCREMENT,"
		     "file_path TEXT NOT NULL UNIQUE,"
		     "thumbnail_path TEXT DEFAULT '',"
		     "file_size INTEGER DEFAULT 0,"
		     "duration INTEGER DEFAULT 0,"
		     "start_time INTEGER NOT NULL,"
		     "end_time INTEGER DEFAULT 0,"
		     "locked INTEGER DEFAULT 0,"
		     "user_data TEXT DEFAULT '',"
		     "created_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
	if (rc != SQLITE_OK)
		goto err_close_db;

	rc = db_exec(s,
		     "CREATE TABLE IF NOT EXISTS " STORAGE_TABLE_PICTURE " ("
		     "id INTEGER PRIMARY KEY AUTOINCREMENT,"
		     "file_path TEXT NOT NULL UNIQUE,"
		     "thumbnail_path TEXT DEFAULT '',"
		     "file_size INTEGER DEFAULT 0,"
		     "timestamp INTEGER NOT NULL,"
		     "created_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
	if (rc != SQLITE_OK)
		goto err_close_db;

	/* start cleanup thread */
	pthread_mutex_init(&s->lock, NULL);
	pthread_cond_init(&s->cond, NULL);
	s->running = true;
	if (pthread_create(&s->tid, NULL, cleanup_thread, s) != 0) {
		loge("[storage] cleanup thread create failed");
		pthread_cond_destroy(&s->cond);
		pthread_mutex_destroy(&s->lock);
		goto err_close_db;
	}
	logi("[storage] cleanup thread started, auto max_mb=%lld, keep=%d",
	     (long long)s->max_mb, s->keep_count);

	/* recover any files that exist on disk but not in the database */
	aic_storage_sync(s);

	logi("[storage] created, base_path: %s", base_path);
	return s;

err_close_db:
	if (s->db)
		sqlite3_close(s->db);
err_free:
	mpp_free(s);
	return NULL;
}

void aic_storage_destroy(struct aic_storage *s)
{
	if (!s)
		return;

	if (s->running) {
		pthread_mutex_lock(&s->lock);
		s->running = false;
		pthread_cond_signal(&s->cond);
		pthread_mutex_unlock(&s->lock);
		pthread_join(s->tid, NULL);
		pthread_cond_destroy(&s->cond);
		pthread_mutex_destroy(&s->lock);
	}

	if (s->db) {
		sqlite3_close(s->db);
		s->db = NULL;
	}
	mpp_free(s);
}

int aic_storage_record_file(struct aic_storage *s,
			    const char *file_path,
			    const char *thumbnail_path,
			    int64_t start_time, int64_t end_time,
			    int duration_s, int64_t file_size,
			    const char *user_data)
{
	char sql[1024];
	char esc_ud[384];
	struct tm tm;
	char creat[32];
	int ret;

	if (!s || !file_path)
		return -1;

	sql_escape(user_data ? user_data : "", esc_ud, sizeof(esc_ud));

	mpp_get_local_tm(&tm);
	snprintf(creat, sizeof(creat), "%04d-%02d-%02d %02d:%02d:%02d",
		 1900 + tm.tm_year, 1 + tm.tm_mon, tm.tm_mday,
		 tm.tm_hour, tm.tm_min, tm.tm_sec);

	snprintf(sql, sizeof(sql),
		 "INSERT INTO %s "
		 "(file_path, thumbnail_path, file_size, duration, "
		 "start_time, end_time, user_data, created_at) "
		 "VALUES ('%s', '%s', %lld, %d, %lld, %lld, '%s', '%s');",
		 STORAGE_TABLE_RECORD,
		 file_path, thumbnail_path ? thumbnail_path : "",
		 (long long)file_size, duration_s,
		 (long long)start_time, (long long)end_time, esc_ud, creat);

	ret = db_exec(s, sql);
	if (ret != SQLITE_OK)
		return ret;

	/* apply pending lock flag */
	if (s->lock_pending) {
		s->lock_pending = false;
		snprintf(sql, sizeof(sql),
			 "UPDATE %s SET locked = 1 WHERE file_path = '%s';",
			 STORAGE_TABLE_RECORD, file_path);
		db_exec(s, sql);
	}

	/* apply pending user data */
	if (s->pending_user_data[0] != '\0') {
		sql_escape(s->pending_user_data, esc_ud, sizeof(esc_ud));
		snprintf(sql, sizeof(sql),
			 "UPDATE %s SET user_data = '%s' WHERE file_path = '%s';",
			 STORAGE_TABLE_RECORD, esc_ud, file_path);
		db_exec(s, sql);
		s->pending_user_data[0] = '\0';
	}

	/* signal cleanup thread */
	if (s->running) {
		pthread_mutex_lock(&s->lock);
		s->need_cleanup = true;
		pthread_cond_signal(&s->cond);
		pthread_mutex_unlock(&s->lock);
	}

	return ret;
}

int aic_storage_set_lock_pending(struct aic_storage *s, int pending)
{
	if (!s)
		return -1;
	s->lock_pending = (pending != 0);
	return 0;
}

int aic_storage_set_pending_user_data(struct aic_storage *s, const char *data)
{
	if (!s)
		return -1;
	if (data) {
		strncpy(s->pending_user_data, data, sizeof(s->pending_user_data) - 1);
		s->pending_user_data[sizeof(s->pending_user_data) - 1] = '\0';
	} else {
		s->pending_user_data[0] = '\0';
	}
	return 0;
}

int aic_storage_set_user_data(struct aic_storage *s,
			      const char *file_path, const char *data)
{
	char sql[1024];
	char esc[384];

	if (!s || !file_path || !data)
		return -1;

	sql_escape(data, esc, sizeof(esc));
	snprintf(sql, sizeof(sql),
		 "UPDATE %s SET user_data = '%s' WHERE file_path = '%s';",
		 STORAGE_TABLE_RECORD, esc, file_path);
	return db_exec(s, sql);
}

int aic_storage_cleanup(struct aic_storage *s,
			int64_t max_mb, int keep_count)
{
	if (!s)
		return -1;
	return do_cleanup(s, max_mb, keep_count);
}

int aic_storage_delete_file(struct aic_storage *s, const char *file_path)
{
	char sql[512];

	if (!s || !file_path)
		return -1;

	if (unlink(file_path) != 0) {
		loge("[storage] unlink %s failed: %s", file_path, strerror(errno));
		return -1;
	}

	snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE file_path = '%s';",
		 STORAGE_TABLE_RECORD, file_path);
	db_exec(s, sql);

	logi("[storage] deleted file: %s", file_path);
	return 0;
}

int aic_storage_lock_file(struct aic_storage *s, const char *file_path, int locked)
{
	char sql[512];

	if (!s || !file_path)
		return -1;

	snprintf(sql, sizeof(sql),
		 "UPDATE %s SET locked = %d WHERE file_path = '%s';",
		 STORAGE_TABLE_RECORD, locked ? 1 : 0, file_path);
	return db_exec(s, sql);
}

static void bind_record(sqlite3_stmt *stmt, struct aic_storage_record *r)
{
	snprintf(r->file_path, sizeof(r->file_path), "%s",
		 (const char *)sqlite3_column_text(stmt, 0));
	snprintf(r->thumbnail_path, sizeof(r->thumbnail_path), "%s",
		 (const char *)sqlite3_column_text(stmt, 1));
	r->file_size  = sqlite3_column_int64(stmt, 2);
	r->duration   = sqlite3_column_int(stmt, 3);
	r->start_time = sqlite3_column_int64(stmt, 4);
	r->end_time   = sqlite3_column_int64(stmt, 5);
	r->locked     = sqlite3_column_int(stmt, 6);
	snprintf(r->user_data, sizeof(r->user_data), "%s",
		 (const char *)sqlite3_column_text(stmt, 7));
	snprintf(r->created_at, sizeof(r->created_at), "%s",
		 (const char *)sqlite3_column_text(stmt, 8));
}

int aic_storage_get_count(struct aic_storage *s)
{
	sqlite3_stmt *stmt = NULL;
	int count = 0;
	char sql[128];

	if (!s || !s->db)
		return 0;

	snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s;", STORAGE_TABLE_RECORD);
	if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK)
		return 0;
	if (sqlite3_step(stmt) == SQLITE_ROW)
		count = sqlite3_column_int(stmt, 0);
	sqlite3_finalize(stmt);
	return count;
}

int aic_storage_get_list(struct aic_storage *s,
			 struct aic_storage_record *records, int max)
{
	sqlite3_stmt *stmt = NULL;
	char sql[256];
	int count = 0;

	if (!s || !s->db || !records)
		return 0;

	snprintf(sql, sizeof(sql),
		 "SELECT file_path, thumbnail_path, file_size, duration, "
		 "start_time, end_time, locked, user_data, created_at "
		 "FROM %s ORDER BY created_at DESC LIMIT %d;",
		 STORAGE_TABLE_RECORD, max > 0 ? max : -1);

	if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK)
		return 0;

	while (sqlite3_step(stmt) == SQLITE_ROW && count < max) {
		bind_record(stmt, &records[count]);
		count++;
	}
	sqlite3_finalize(stmt);
	return count;
}

int aic_storage_get_record(struct aic_storage *s, const char *file_path,
			   struct aic_storage_record *record)
{
	sqlite3_stmt *stmt = NULL;
	char sql[512];
	int rc;

	if (!s || !s->db || !file_path || !record)
		return -1;

	snprintf(sql, sizeof(sql),
		 "SELECT file_path, thumbnail_path, file_size, duration, "
		 "start_time, end_time, locked, user_data, created_at "
		 "FROM %s WHERE file_path = '%s';",
		 STORAGE_TABLE_RECORD, file_path);

	if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK)
		return -1;

	rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW)
		bind_record(stmt, record);
	sqlite3_finalize(stmt);
	return (rc == SQLITE_ROW) ? 0 : -1;
}

int aic_storage_get_locked_list(struct aic_storage *s,
				struct aic_storage_record *records, int max)
{
	sqlite3_stmt *stmt = NULL;
	char sql[256];
	int count = 0;

	if (!s || !s->db || !records)
		return 0;

	snprintf(sql, sizeof(sql),
		 "SELECT file_path, thumbnail_path, file_size, duration, "
		 "start_time, end_time, locked, user_data, created_at "
		 "FROM %s WHERE locked = 1 "
		 "ORDER BY created_at DESC LIMIT %d;",
		 STORAGE_TABLE_RECORD, max > 0 ? max : -1);

	if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK)
		return 0;

	while (sqlite3_step(stmt) == SQLITE_ROW && count < max) {
		bind_record(stmt, &records[count]);
		count++;
	}
	sqlite3_finalize(stmt);
	return count;
}

int aic_storage_get_locked_count(struct aic_storage *s)
{
	sqlite3_stmt *stmt = NULL;
	int c = 0;
	char sql[128];

	if (!s || !s->db)
		return 0;

	snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE locked = 1;",
		 STORAGE_TABLE_RECORD);
	if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK)
		return 0;
	if (sqlite3_step(stmt) == SQLITE_ROW)
		c = sqlite3_column_int(stmt, 0);
	sqlite3_finalize(stmt);
	return c;
}

int aic_storage_get_count_by_date(struct aic_storage *s, const char *date)
{
	sqlite3_stmt *stmt = NULL;
	int c = 0;
	char sql[256];

	if (!s || !s->db || !date)
		return 0;

	snprintf(sql, sizeof(sql),
		 "SELECT COUNT(*) FROM %s WHERE date(start_time, 'unixepoch') = '%s';",
		 STORAGE_TABLE_RECORD, date);
	if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK)
		return 0;
	if (sqlite3_step(stmt) == SQLITE_ROW)
		c = sqlite3_column_int(stmt, 0);
	sqlite3_finalize(stmt);
	return c;
}

int aic_storage_get_list_by_date(struct aic_storage *s, const char *date,
				 struct aic_storage_record *records, int max)
{
	sqlite3_stmt *stmt = NULL;
	char sql[256];
	int count = 0;

	if (!s || !s->db || !date || !records || max <= 0)
		return 0;

	snprintf(sql, sizeof(sql),
		 "SELECT file_path, thumbnail_path, file_size, duration, "
		 "start_time, end_time, locked, user_data, created_at "
		 "FROM %s WHERE date(start_time, 'unixepoch') = '%s' "
		 "ORDER BY created_at DESC LIMIT %d;",
		 STORAGE_TABLE_RECORD, date, max > 0 ? max : -1);

	if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK)
		return 0;

	while (sqlite3_step(stmt) == SQLITE_ROW && count < max) {
		bind_record(stmt, &records[count]);
		count++;
	}
	sqlite3_finalize(stmt);
	return count;
}

/* ======================== picture API ======================== */

int aic_storage_record_picture(struct aic_storage *s,
			       const char *file_path,
			       const char *thumbnail_path,
			       int64_t timestamp, int64_t file_size)
{
	char sql[512];
	int ret;

	if (!s || !file_path)
		return -1;

	snprintf(sql, sizeof(sql),
		 "INSERT INTO %s "
		 "(file_path, thumbnail_path, file_size, timestamp) "
		 "VALUES ('%s', '%s', %lld, %lld);",
		 STORAGE_TABLE_PICTURE,
		 file_path, thumbnail_path ? thumbnail_path : "",
		 (long long)file_size, (long long)timestamp);

	ret = db_exec(s, sql);

	/* signal cleanup thread */
	if (s->running && ret == SQLITE_OK) {
		pthread_mutex_lock(&s->lock);
		s->need_cleanup = true;
		pthread_cond_signal(&s->cond);
		pthread_mutex_unlock(&s->lock);
	}

	return ret;
}

int aic_storage_get_picture_count(struct aic_storage *s)
{
	sqlite3_stmt *stmt = NULL;
	int count = 0;
	char sql[128];

	if (!s || !s->db)
		return 0;

	snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s;",
		 STORAGE_TABLE_PICTURE);
	if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK)
		return 0;
	if (sqlite3_step(stmt) == SQLITE_ROW)
		count = sqlite3_column_int(stmt, 0);
	sqlite3_finalize(stmt);
	return count;
}

static void bind_picture(sqlite3_stmt *stmt, struct aic_storage_picture *p)
{
	snprintf(p->file_path, sizeof(p->file_path), "%s",
		 (const char *)sqlite3_column_text(stmt, 0));
	snprintf(p->thumbnail_path, sizeof(p->thumbnail_path), "%s",
		 (const char *)sqlite3_column_text(stmt, 1));
	p->file_size = sqlite3_column_int64(stmt, 2);
	p->timestamp = sqlite3_column_int64(stmt, 3);
	snprintf(p->created_at, sizeof(p->created_at), "%s",
		 (const char *)sqlite3_column_text(stmt, 4));
}

int aic_storage_get_picture_list(struct aic_storage *s,
				 struct aic_storage_picture *pictures, int max)
{
	sqlite3_stmt *stmt = NULL;
	char sql[256];
	int count = 0;

	if (!s || !s->db || !pictures)
		return 0;

	snprintf(sql, sizeof(sql),
		 "SELECT file_path, thumbnail_path, file_size, "
		 "timestamp, created_at "
		 "FROM %s ORDER BY timestamp DESC LIMIT %d;",
		 STORAGE_TABLE_PICTURE, max > 0 ? max : -1);

	if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK)
		return 0;

	while (sqlite3_step(stmt) == SQLITE_ROW && count < max) {
		bind_picture(stmt, &pictures[count]);
		count++;
	}
	sqlite3_finalize(stmt);
	return count;
}

int aic_storage_get_picture_list_by_date(struct aic_storage *s, const char *date,
					 struct aic_storage_picture *pictures, int max)
{
	sqlite3_stmt *stmt = NULL;
	char sql[256];
	int count = 0;

	if (!s || !s->db || !date || !pictures || max <= 0)
		return 0;

	snprintf(sql, sizeof(sql),
		 "SELECT file_path, thumbnail_path, file_size, "
		 "timestamp, created_at "
		 "FROM %s WHERE date(timestamp, 'unixepoch') = '%s' "
		 "ORDER BY timestamp DESC LIMIT %d;",
		 STORAGE_TABLE_PICTURE, date, max > 0 ? max : -1);

	if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK)
		return 0;

	while (sqlite3_step(stmt) == SQLITE_ROW && count < max) {
		bind_picture(stmt, &pictures[count]);
		count++;
	}
	sqlite3_finalize(stmt);
	return count;
}

int aic_storage_get_picture_count_by_date(struct aic_storage *s, const char *date)
{
	sqlite3_stmt *stmt = NULL;
	int c = 0;
	char sql[256];

	if (!s || !s->db || !date)
		return 0;

	snprintf(sql, sizeof(sql),
		 "SELECT COUNT(*) FROM %s WHERE date(timestamp, 'unixepoch') = '%s';",
		 STORAGE_TABLE_PICTURE, date);
	if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK)
		return 0;
	if (sqlite3_step(stmt) == SQLITE_ROW)
		c = sqlite3_column_int(stmt, 0);
	sqlite3_finalize(stmt);
	return c;
}

int aic_storage_delete_picture(struct aic_storage *s, const char *file_path)
{
	char sql[512];

	if (!s || !file_path)
		return -1;

	if (unlink(file_path) != 0) {
		loge("[storage] unlink %s failed: %s", file_path,
		     strerror(errno));
		return -1;
	}

	snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE file_path = '%s';",
		 STORAGE_TABLE_PICTURE, file_path);
	db_exec(s, sql);

	logi("[storage] deleted picture: %s", file_path);
	return 0;
}

/* ======================== disk sync ======================== */

/* parse YYYYMMDD_HHMMSS.ext into "YYYY-MM-DD HH:MM:SS", return 0 on success */
static int parse_time_str(const char *name, const char *ext, char *out, int out_len)
{
	char prefix[16];
	char tmp[5];
	const char *dot;
	int year, mon, day, hour, min, sec;

	dot = strrchr(name, '.');
	if (!dot || strncmp(dot, ext, strlen(ext) + 1) != 0)
		return -1;

	if (dot - name != 15 || dot - name > (int)sizeof(prefix) - 1)
		return -1;
	memcpy(prefix, name, 15);
	prefix[15] = '\0';

	/* YYYY MM DD _ HH MM SS → 15 chars total */
	tmp[0] = prefix[0]; tmp[1] = prefix[1]; tmp[2] = prefix[2]; tmp[3] = prefix[3]; tmp[4] = '\0';
	tmp[0] = prefix[0]; tmp[1] = prefix[1]; tmp[2] = prefix[2]; tmp[3] = prefix[3]; tmp[4] = '\0';
	year = atoi(tmp);
	tmp[0] = prefix[4]; tmp[1] = prefix[5]; tmp[2] = '\0';
	mon  = atoi(tmp);
	tmp[0] = prefix[6]; tmp[1] = prefix[7]; tmp[2] = '\0';
	day  = atoi(tmp);
	tmp[0] = prefix[9]; tmp[1] = prefix[10]; tmp[2] = '\0';
	hour = atoi(tmp);
	tmp[0] = prefix[11]; tmp[1] = prefix[12]; tmp[2] = '\0';
	min  = atoi(tmp);
	tmp[0] = prefix[13]; tmp[1] = prefix[14]; tmp[2] = '\0';
	sec  = atoi(tmp);

	snprintf(out, out_len, "%04d-%02d-%02d %02d:%02d:%02d",
		 year, mon, day, hour, min, sec);
	return 0;
}

static int db_file_exists(struct aic_storage *s, const char *table,
			  const char *file_path)
{
	sqlite3_stmt *stmt = NULL;
	char sql[512];
	int exists = 0;

	snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE file_path = '%s';",
		 table, file_path);
	if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK)
		return 0;
	if (sqlite3_step(stmt) == SQLITE_ROW)
		exists = sqlite3_column_int(stmt, 0) > 0;
	sqlite3_finalize(stmt);
	return exists;
}

static int do_sync_table(struct aic_storage *s, const char *table,
			 const char *ext, int is_video)
{
	struct dirent **namelist;
	int n, synced = 0;

	n = scandir(s->base_path, &namelist, NULL, NULL);
	if (n <= 0)
		return 0;

	for (int i = 0; i < n; i++) {
		char file_path[AIC_STORAGE_PATH_LEN * 2];
		char time_str[32];
		struct stat st;

		if (namelist[i]->d_type != DT_REG)
			continue;

		if (parse_time_str(namelist[i]->d_name, ext, time_str, sizeof(time_str)) != 0)
			continue;

		snprintf(file_path, sizeof(file_path), "%s/%s",
			 s->base_path, namelist[i]->d_name);

		if (db_file_exists(s, table, file_path))
			continue;

		if (stat(file_path, &st) != 0)
			continue;

		if (is_video) {
			char sql[1024];
			snprintf(sql, sizeof(sql),
				 "INSERT INTO %s "
				 "(file_path, thumbnail_path, file_size, duration, "
				 "start_time, end_time, user_data, created_at) "
				 "VALUES ('%s', '', %lld, 0, "
				 "strftime('%%s','%s'), strftime('%%s','%s'), "
				 "'', '%s');",
				 table, file_path,
				 (long long)st.st_size,
				 time_str, time_str, time_str);
			db_exec(s, sql);
		} else {
			char sql[1024];
			snprintf(sql, sizeof(sql),
				 "INSERT INTO %s "
				 "(file_path, thumbnail_path, file_size, "
				 "timestamp, created_at) "
				 "VALUES ('%s', '', %lld, "
				 "strftime('%%s','%s'), '%s');",
				 table, file_path,
				 (long long)st.st_size,
				 time_str, time_str);
			db_exec(s, sql);
		}
		synced++;
	}
	free_namelist(namelist, n);
	return synced;
}

int aic_storage_sync(struct aic_storage *s)
{
	int total = 0;

	if (!s || !s->db)
		return -1;

	total += do_sync_table(s, STORAGE_TABLE_RECORD, ".mp4", 1);
	total += do_sync_table(s, STORAGE_TABLE_PICTURE, ".jpg", 0);

	if (total > 0)
		logi("[storage] synced %d files from disk to database", total);
	return total;
}

int64_t aic_storage_get_total_mb(const char *path)
{
	struct statfs st;

	if (!path)
		return -1;

	if (statfs(path, &st) < 0) {
		loge("[storage] statfs %s failed: %s", path, strerror(errno));
		return -1;
	}

	return (int64_t)st.f_blocks * st.f_bsize / (1024 * 1024);
}

int64_t aic_storage_get_free_mb(const char *path)
{
	struct statfs st;

	if (!path)
		return -1;

	if (statfs(path, &st) < 0) {
		loge("[storage] statfs %s failed: %s", path, strerror(errno));
		return -1;
	}

	return (int64_t)st.f_bfree * st.f_bsize / (1024 * 1024);
}

int64_t aic_storage_get_used_mb(const char *path)
{
	int64_t total = aic_storage_get_total_mb(path);
	int64_t free_mb = aic_storage_get_free_mb(path);

	if (total < 0 || free_mb < 0)
		return -1;

	return total - free_mb;
}

int aic_storage_is_mounted(const char *path)
{
	FILE *fp;
	char line[512];
	char clean_path[256];
	size_t len;

	if (!path)
		return -1;

	/* strip trailing slashes */
	strncpy(clean_path, path, sizeof(clean_path) - 1);
	clean_path[sizeof(clean_path) - 1] = '\0';
	len = strlen(clean_path);
	while (len > 1 && clean_path[len - 1] == '/')
		clean_path[--len] = '\0';

	fp = fopen("/proc/mounts", "r");
	if (!fp)
		return -1;

	while (fgets(line, sizeof(line), fp)) {
		char *saveptr, *token;
		token = strtok_r(line, " \t", &saveptr);
		if (!token)
			continue;
		token = strtok_r(NULL, " \t", &saveptr);
		if (!token)
			continue;
		/* also strip trailing slashes from token for comparison */
		size_t tlen = strlen(token);
		while (tlen > 1 && token[tlen - 1] == '/')
			token[--tlen] = '\0';
		if (strncmp(token, clean_path, len + 1) == 0) {
			fclose(fp);
			return 1;
		}
	}

	fclose(fp);
	return 0;
}

int aic_storage_format(const char *path)
{
	struct dirent **namelist;
	int n, deleted = 0;

	if (!path)
		return -1;

	n = scandir(path, &namelist, NULL, NULL);
	if (n <= 0)
		return 0;

	for (int i = 0; i < n; i++) {
		char fp[AIC_STORAGE_PATH_LEN * 2];
		snprintf(fp, sizeof(fp), "%s/%s", path, namelist[i]->d_name);
		if (unlink(fp) == 0)
			deleted++;
	}
	free_namelist(namelist, n);

	logi("[storage] format %s, deleted %d files", path, deleted);
	return deleted;
}
