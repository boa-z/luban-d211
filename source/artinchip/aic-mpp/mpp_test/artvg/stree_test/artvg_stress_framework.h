/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 */

#ifndef ARTVG_STRESS_FRAMEWORK_H
#define ARTVG_STRESS_FRAMEWORK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <stdatomic.h>

#include <getopt.h>
#include "artvg.h"
#include "vg_drm.h"

/***********************
 *  DEFAULT CONSTANTS
 **********************/
#define STRESS_DEFAULT_THREADS        1
#define STRESS_DEFAULT_ITERATIONS     50
#define STRESS_MAX_THREADS            10

/***********************
 *  FAILURE REASONS
 **********************/
typedef enum {
    STRESS_FAIL_REASON_NONE = 0,
    STRESS_FAIL_REASON_NO_BUFFER,
    STRESS_FAIL_REASON_INVALID_CROP,
    STRESS_FAIL_REASON_OP_FAILED,     /* Specific operation failed */
    STRESS_FAIL_REASON_MAX
} stress_fail_reason_t;

/***********************
 *  CROP CONFIG
 **********************/
typedef struct {
    int crop_en;
    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
} stress_crop_config_t;

/***********************
 *  FORWARD DECLARATIONS
 **********************/
typedef struct stress_thread_context_t stress_thread_context_t;

/***********************
 *  RENDER CONTEXT STRUCTURE
 **********************/
typedef struct stress_render_context_t {
    drm_dev_t *drm_dev;
    struct mpp_buf draw_buffers[2];
    atomic_int display_buffer_index;
    volatile int buffer_drawing_count[2];
    pthread_mutex_t buffer_mutex;
    volatile int running;
    int num_threads;
    int total_iterations;
    uint64_t total_frames;
    double start_time_ms;
    int display_wait_vg_complete;  /* Runtime flag for wait mode */
    int shared_vg_mode;            /* 0: multi-instance (default), 1: single-instance shared VG */
    struct artvg *shared_vg;       /* Shared VG instance (single-instance mode only) */
} stress_render_context_t;

/***********************
 *  THREAD CONTEXT
 **********************/
struct stress_thread_context_t {
    int thread_id;
    stress_render_context_t *ctx;
    struct artvg *vg;
    uint64_t draw_count;
    uint64_t fail_count;
    uint64_t fail_reasons[STRESS_FAIL_REASON_MAX];
    double total_time_ms;
    int current_param_index;
    void *user_data;  /* Thread-specific user data (e.g., per-thread source buffer) */
    struct mpp_buf private_buf;  /* Private buffer copy for thread-safe parameter modification */
};

typedef struct stress_thread_context_t stress_thread_context_t;

/***********************
 *  TEST OPERATIONS (FUNCTION POINTERS)
 **********************/
typedef struct {
    const char *test_name;                        /* Test name */

    /* Initialize test-specific resources (e.g., source buffer for blit) */
    /* This is called before threads are created, and can be used to add */
    /* extra DMA fds to the render context for later use in draw_callback */
    int (*init_test_resources)(void *render_ctx);

    /* Initialize thread-specific resources (e.g., per-thread source buffer) */
    /* Called after VG is created, before test loop, once per thread */
    int (*init_thread_resources)(void *thread_ctx);

    /* Cleanup thread-specific resources */
    /* Called after test loop, before VG destroy, once per thread */
    void (*cleanup_thread_resources)(void *thread_ctx);

    /* Cleanup test-specific resources */
    void (*cleanup_test_resources)(void *render_ctx);

    /*
     * Single draw callback - manages parameters internally.
     *
     * Thread Safety: Each thread has a private_buf copy of the draw buffer.
     * Pattern:
     *   1. Copy shared buffer to private_buf
     *   2. Modify private_buf parameters (crop, etc.)
     *   3. Execute VG operation with private_buf
     *
     * This ensures thread safety WITHOUT locks - each VG operation uses
     * its own isolated buffer copy.
     *
     * Returns: 0 on success, -1 on failure
     */
    int (*draw_callback)(void *thread_ctx, int buffer_idx);

    /* (Optional) Collect and print test results */
    /* If NULL, framework will use default result printing */
    void (*collect_and_print_results)(void *render_ctx,
                                     stress_thread_context_t *thread_contexts,
                                     int num_threads);

    /* (Optional) Convert failure reason to string */
    const char *(*fail_reason_to_string)(int reason);

    /* Custom long options for this test (must end with {0,0,0,0}) */
    const struct option *custom_long_options;

    /*
     * Callback for handling custom options.
     * val: the 'val' field from the matched custom_long_options entry
     * optarg: argument value (NULL if no_argument)
     * Returns 0 on success, -1 on error (causes parse failure).
     * If NULL, any unrecognized option triggers default error handling.
     */
    int (*handle_custom_option)(int val, const char *optarg);

    /* (Optional) Custom usage text printed after framework options on --help.
     * Should describe test-specific options. NULL if not needed. */
    const char *custom_usage_text;
} stress_test_ops_t;

/***********************
 *  PUBLIC HELPER FUNCTIONS
 **********************/
/* Record a failure in the thread context */
void stress_record_failure(stress_thread_context_t *thread_ctx, int reason);

/***********************
 *  MAIN TEST ENTRY POINT
 **********************/
/* Run the stress test with given operations */
int stress_run_test(int argc, char **argv, const stress_test_ops_t *ops);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ARTVG_STRESS_FRAMEWORK_H */
