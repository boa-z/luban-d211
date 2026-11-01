/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 */

#include "artvg_stress_framework.h"
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include <pthread.h>

/***********************
 *  CONSTANTS
 **********************/
#define STRESS_DRM_DEVICE_PATH "/dev/dri/card0"

/***********************
 *  UTILITY FUNCTIONS
 **********************/
double stress_get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

void stress_show_usage(const char *program_name)
{
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -t, --threads <num>       Number of threads (default: %d, max: %d)\n",
           STRESS_DEFAULT_THREADS, STRESS_MAX_THREADS);
    printf("  -i, --iterations <num>    Iterations per thread (default: %d)\n",
           STRESS_DEFAULT_ITERATIONS);
    printf("  -w, --wait-vg             Display waits for VG to complete (default: disabled)\n");
    printf("  -s, --shared-vg           Use shared VG instance for all threads (single-instance mode)\n");
    printf("  --help                    Show this help message\n");
}

/***********************
 *  BUFFER MANAGEMENT
 **********************/
void stress_wait_for_buffer_completion(stress_render_context_t *ctx, int buffer_index)
{
    if (ctx->display_wait_vg_complete) {
        pthread_mutex_lock(&ctx->buffer_mutex);
        while (ctx->buffer_drawing_count[buffer_index] > 0) {
            pthread_mutex_unlock(&ctx->buffer_mutex);
            usleep(10);
            pthread_mutex_lock(&ctx->buffer_mutex);
        }
        pthread_mutex_unlock(&ctx->buffer_mutex);
    }
}

int stress_display_buffer(stress_render_context_t *ctx, int buffer_index)
{
    if (drm_buffer_flush(ctx->drm_dev, buffer_index) != 0) {
        printf("[DRM] Failed to flush buffer %d\n", buffer_index);
        return -1;
    }

    if (drm_wait_vsync(ctx->drm_dev) != 0) {
        printf("[DRM] Failed to wait for vsync\n");
        return -1;
    }

    return 0;
}

void stress_mark_buffer_drawing_start(stress_render_context_t *ctx, int buffer_index)
{
    if (ctx->display_wait_vg_complete) {
        pthread_mutex_lock(&ctx->buffer_mutex);
        ctx->buffer_drawing_count[buffer_index]++;
        pthread_mutex_unlock(&ctx->buffer_mutex);
    }
}

void stress_mark_buffer_drawing_end(stress_render_context_t *ctx, int buffer_index)
{
    if (ctx->display_wait_vg_complete) {
        pthread_mutex_lock(&ctx->buffer_mutex);
        ctx->buffer_drawing_count[buffer_index]--;
        pthread_mutex_unlock(&ctx->buffer_mutex);
    }
}

/***********************
 *  THREAD MANAGEMENT
 **********************/
void *stress_drm_manager_thread(void *arg)
{
    stress_render_context_t *ctx = (stress_render_context_t *)arg;
    printf("[DRM] Manager thread started (pthread_id=%lu)\n", (unsigned long)pthread_self());

    while (ctx->running) {
        int next_buffer = 1 - atomic_load(&ctx->display_buffer_index);

        stress_wait_for_buffer_completion(ctx, next_buffer);
        atomic_store(&ctx->display_buffer_index, next_buffer);
        if (stress_display_buffer(ctx, next_buffer) == 0) {
            ctx->total_frames++;
        }
    }

    printf("[DRM] Manager thread exiting\n");
    return NULL;
}

void stress_record_failure(stress_thread_context_t *thread_ctx, int reason)
{
    thread_ctx->fail_count++;
    if (reason >= 0 && reason < STRESS_FAIL_REASON_MAX) {
        thread_ctx->fail_reasons[reason]++;
    }
}

int stress_allocate_thread_resources(stress_thread_context_t **thread_contexts,
                                     pthread_t **vg_threads,
                                     pthread_t **drm_thread,
                                     int num_threads)
{
    *thread_contexts = calloc(num_threads, sizeof(stress_thread_context_t));
    *vg_threads = calloc(num_threads, sizeof(pthread_t));
    *drm_thread = calloc(1, sizeof(pthread_t));

    if (!*thread_contexts || !*vg_threads || !*drm_thread) {
        printf("Error: Failed to allocate memory\n");
        free(*thread_contexts);
        free(*vg_threads);
        free(*drm_thread);
        return -1;
    }
    return 0;
}

int stress_create_vg_worker_threads(pthread_t *vg_threads,
                                    stress_thread_context_t *thread_contexts,
                                    void *(*worker_func)(void *),
                                    int num_threads)
{
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&vg_threads[i], NULL, worker_func, &thread_contexts[i]) != 0) {
            printf("Error: Failed to create VG thread %d\n", i);
            for (int j = 0; j < i; j++) {
                pthread_join(vg_threads[j], NULL);
            }
            return -1;
        }
    }
    return 0;
}

void stress_wait_for_vg_threads(pthread_t *vg_threads, int num_threads)
{
    for (int i = 0; i < num_threads; i++) {
        pthread_join(vg_threads[i], NULL);
    }
}

/***********************
 *  INITIALIZATION AND CLEANUP
 **********************/
int stress_init_render_context(stress_render_context_t *ctx, int num_threads,
                                int iterations, int display_wait_vg, int shared_vg_mode)
{
    memset(ctx, 0, sizeof(stress_render_context_t));
    ctx->num_threads = num_threads;
    ctx->total_iterations = iterations;
    ctx->running = 1;
    ctx->start_time_ms = stress_get_time_ms();
    ctx->display_wait_vg_complete = display_wait_vg;
    ctx->shared_vg_mode = shared_vg_mode;
    ctx->shared_vg = NULL;
    return 0;
}

int stress_init_drm_device(stress_render_context_t *ctx)
{
    ctx->drm_dev = calloc(1, sizeof(drm_dev_t));
    if (!ctx->drm_dev) {
        printf("Error: Failed to allocate drm_dev_t\n");
        return -1;
    }

    if (drm_device_open(ctx->drm_dev, STRESS_DRM_DEVICE_PATH, -1) != 0) {
        printf("Error: Failed to open DRM device: %s\n", STRESS_DRM_DEVICE_PATH);
        free(ctx->drm_dev);
        ctx->drm_dev = NULL;
        return -1;
    }

    printf("DRM screen size: %ux%u\n", ctx->drm_dev->width, ctx->drm_dev->height);
    return 0;
}

int stress_init_draw_buffers(stress_render_context_t *ctx)
{
    for (int i = 0; i < 2; i++) {
        if (drm_buffer_to_mpp_buffer(ctx->drm_dev, i, &ctx->draw_buffers[i]) != 0) {
            printf("Error: Failed to convert DRM buffer %d\n", i);
            return -1;
        }
    }

    /* Initialize display buffer index atomically */
    atomic_init(&ctx->display_buffer_index, 0);

    printf("Double buffers initialized\n");
    return 0;
}

int stress_init_synchronization(stress_render_context_t *ctx)
{
    /* Initialize drawing count to 0 */
    ctx->buffer_drawing_count[0] = 0;
    ctx->buffer_drawing_count[1] = 0;

    if (pthread_mutex_init(&ctx->buffer_mutex, NULL) != 0) {
        printf("Error: Failed to initialize buffer mutex\n");
        return -1;
    }

    printf("Synchronization primitives initialized\n");
    return 0;
}

void stress_stop_drm_thread(stress_render_context_t *ctx, pthread_t *drm_thread)
{
    if (ctx && ctx->running && drm_thread) {
        ctx->running = 0;
        pthread_join(*drm_thread, NULL);
    }
}

void stress_cleanup_drm_resources(stress_render_context_t *ctx)
{
    if (ctx) {
        pthread_mutex_destroy(&ctx->buffer_mutex);
    }

    if (ctx && ctx->drm_dev) {
        drm_device_close(ctx->drm_dev);
        free(ctx->drm_dev);
        ctx->drm_dev = NULL;
    }
}

void stress_cleanup_resources(stress_render_context_t *ctx,
                              stress_thread_context_t *thread_contexts,
                              pthread_t *vg_threads,
                              pthread_t *drm_thread)
{
    if (!ctx) {
        return;
    }

    stress_stop_drm_thread(ctx, drm_thread);
    stress_cleanup_drm_resources(ctx);

    free(thread_contexts);
    free(vg_threads);
    free(drm_thread);
}

void stress_init_thread_context(stress_thread_context_t *ctx, int thread_id,
                                stress_render_context_t *render_ctx)
{
    ctx->thread_id = thread_id;
    ctx->ctx = render_ctx;
    ctx->draw_count = 0;
    ctx->fail_count = 0;
    ctx->total_time_ms = 0.0;
    memset(ctx->fail_reasons, 0, sizeof(ctx->fail_reasons));
    ctx->user_data = NULL;  /* Initialize user_data to NULL */
}

void stress_init_thread_contexts(stress_thread_context_t *contexts,
                                 stress_render_context_t *ctx, int num_threads)
{
    if (!contexts || !ctx) {
        return;
    }

    for (int i = 0; i < num_threads; i++) {
        stress_init_thread_context(&contexts[i], i, ctx);
    }
}

/***********************
 *  TEST FLOW
 **********************/
int stress_parse_command_line_args(int argc, char **argv, int *num_threads,
                                   int *iterations, int *display_wait_vg, int *shared_vg_mode,
                                   const stress_test_ops_t *ops)
{
    static struct option base_options[] = {
        {"threads", required_argument, 0, 't'},
        {"iterations", required_argument, 0, 'i'},
        {"wait-vg", no_argument, 0, 'w'},
        {"shared-vg", no_argument, 0, 's'},
        {"help", no_argument, 0, '?'},
        {0, 0, 0, 0}
    };

    /* Count base options */
    int base_count = 0;
    while (base_options[base_count].name != NULL)
        base_count++;

    /* Count custom options */
    int custom_count = 0;
    const struct option *custom_opts = ops ? ops->custom_long_options : NULL;
    if (custom_opts) {
        while (custom_opts[custom_count].name != NULL)
            custom_count++;
    }

    /* Allocate merged options array (base + custom + terminator) */
    int total_count = base_count + custom_count + 1;
    struct option *merged = malloc(total_count * sizeof(struct option));
    if (!merged) {
        printf("Error: Failed to allocate memory for options\n");
        return -1;
    }

    memcpy(merged, base_options, base_count * sizeof(struct option));
    if (custom_opts && custom_count > 0)
        memcpy(merged + base_count, custom_opts, custom_count * sizeof(struct option));
    memset(merged + base_count + custom_count, 0, sizeof(struct option));

    int opt;
    while ((opt = getopt_long(argc, argv, "t:i:ws", merged, NULL)) != -1) {
        switch (opt) {
        case 't':
            *num_threads = atoi(optarg);
            if (*num_threads <= 0 || *num_threads > STRESS_MAX_THREADS) {
                printf("Error: Thread count must be between 1 and %d\n", STRESS_MAX_THREADS);
                free(merged);
                return -1;
            }
            break;
        case 'i':
            *iterations = atoi(optarg);
            if (*iterations <= 0) {
                printf("Error: Iterations must be positive\n");
                free(merged);
                return -1;
            }
            break;
        case 'w':
            *display_wait_vg = 1;
            break;
        case 's':
            *shared_vg_mode = 1;
            break;
        case '?':
            stress_show_usage(argv[0]);
            if (ops && ops->custom_usage_text)
                printf("%s", ops->custom_usage_text);
            free(merged);
            return 1;
        default:
            /* Custom option: delegate to test's handler */
            if (ops && ops->handle_custom_option) {
                if (ops->handle_custom_option(opt, optarg) == 0)
                    break;
            }
            stress_show_usage(argv[0]);
            if (ops && ops->custom_usage_text)
                printf("%s", ops->custom_usage_text);
            free(merged);
            return 1;
        }
    }

    free(merged);
    return 0;
}

void stress_print_test_configuration(const char *test_name, int num_threads,
                                    int iterations, int display_wait_vg, int shared_vg_mode)
{
    printf("=== %s ===\n", test_name);
    printf("Device: %s\n", STRESS_DRM_DEVICE_PATH);
    printf("Threads: %d\n", num_threads);
    printf("Iterations per thread: %d\n", iterations);
    printf("Buffer strategy: Minimal double buffer (no state machine)\n");
    if (shared_vg_mode) {
        printf("VG mode: Single-instance (shared VG)\n");
    } else {
        printf("VG mode: Multi-instance (per-thread VG)\n");
    }
    if (display_wait_vg) {
        printf("Display wait for VG: Enabled\n");
    } else {
        printf("Display wait for VG: Disabled (concurrent access)\n");
    }
    printf("=============================================\n\n");
}

void stress_print_test_results(stress_render_context_t *ctx,
                               stress_thread_context_t *thread_contexts,
                               const char *(*fail_reason_to_string)(int))
{
    if (!ctx || !thread_contexts) {
        return;
    }

    uint64_t total_draws = 0;
    uint64_t total_failed = 0;
    double elapsed_ms = stress_get_time_ms() - ctx->start_time_ms;

    for (int i = 0; i < ctx->num_threads; i++) {
        total_draws += thread_contexts[i].draw_count;
        total_failed += thread_contexts[i].fail_count;
    }

    printf("\n=== Stress Test Results ===\n");
    printf("Threads: %d\n", ctx->num_threads);
    printf("Iterations per thread: %d\n", ctx->total_iterations);
    printf("Total draws: %" PRIu64 "\n", total_draws);
    printf("Total failed: %" PRIu64 "\n", total_failed);
    printf("DRM frames submitted: %" PRIu64 "\n", ctx->total_frames);
    printf("Total time: %.2f seconds\n", elapsed_ms / 1000.0);

    if (total_draws > 0) {
        printf("Average time per draw: %.2f ms\n", elapsed_ms / total_draws);
        printf("Throughput: %.2f draws/sec\n", (total_draws * 1000.0) / elapsed_ms);
    }

    printf("\nPer-thread statistics:\n");
    for (int i = 0; i < ctx->num_threads; i++) {
        printf("  Thread %d: Draws=%" PRIu64 ", Failed=%" PRIu64 ", Time=%.2fs\n",
               i, thread_contexts[i].draw_count, thread_contexts[i].fail_count,
               thread_contexts[i].total_time_ms / 1000.0);
    }

    /* Aggregate and print failure reasons breakdown */
    if (total_failed > 0 && fail_reason_to_string) {
        uint64_t total_fail_reasons[STRESS_FAIL_REASON_MAX] = {0};
        for (int i = 0; i < ctx->num_threads; i++) {
            for (int j = 0; j < STRESS_FAIL_REASON_MAX; j++) {
                total_fail_reasons[j] += thread_contexts[i].fail_reasons[j];
            }
        }

        printf("\nFailure breakdown:\n");
        for (int j = 0; j < STRESS_FAIL_REASON_MAX; j++) {
            if (total_fail_reasons[j] > 0) {
                printf("  %s: %" PRIu64 "\n",
                       fail_reason_to_string(j),
                       total_fail_reasons[j]);
            }
        }
    }
    printf("============================\n");
}

int stress_check_test_results(stress_thread_context_t *thread_contexts, int num_threads)
{
    for (int i = 0; i < num_threads; i++) {
        if (thread_contexts[i].fail_count > 0) {
            return -1;
        }
    }
    return 0;
}

/***********************
 *  VG WORKER THREAD (GENERIC)
 **********************/
static int add_dma_fds_to_vg(struct artvg *vg, struct mpp_buf *buffers, int instance_id)
{
    for (int i = 0; i < 2; i++) {
        if (buffers[i].fd[0] < 0) {
            printf("Error: Invalid DMA FD for buffer %d in VG %d\n", i, instance_id);
            return -1;
        }
        if (artvg_add_dma_fd(vg, buffers[i].fd[0]) != ARTVG_SUCCESS) {
            printf("Error: Failed to add DMA FD for buffer %d to VG %d\n", i, instance_id);
            return -1;
        }
    }
    return 0;
}

static void remove_dma_fds_from_vg(struct artvg *vg, struct mpp_buf *buffers)
{
    for (int i = 0; i < 2; i++) {
        if (buffers[i].fd[0] >= 0) {
            artvg_remove_dma_fd(vg, buffers[i].fd[0]);
        }
    }
}

static void *vg_worker_thread_generic(void *arg)
{
    struct {
        stress_thread_context_t *thread_ctx;
        const stress_test_ops_t *ops;
    } *wrapper = arg;

    stress_thread_context_t *thread_ctx = wrapper->thread_ctx;
    const stress_test_ops_t *ops = wrapper->ops;
    stress_render_context_t *ctx = thread_ctx->ctx;
    double thread_start = stress_get_time_ms();
    int need_cleanup_dma = 0;

    printf("[Thread %d] Started (pthread_id=%lu)\n", thread_ctx->thread_id, (unsigned long)pthread_self());
    thread_ctx->current_param_index = 0;

    if (ctx->shared_vg_mode) {
        /* Single-instance mode: use shared VG */
        thread_ctx->vg = ctx->shared_vg;
        if (!thread_ctx->vg) {
            printf("[Thread %d] Shared VG instance is NULL\n", thread_ctx->thread_id);
            free(wrapper);
            return NULL;
        }
    } else {
        /* Multi-instance mode: create VG instance for this thread */
        thread_ctx->vg = artvg_create();
        if (!thread_ctx->vg) {
            printf("[Thread %d] Failed to create VG instance\n", thread_ctx->thread_id);
            free(wrapper);
            return NULL;
        }

        /* Add DMA buffers to VG instance */
        if (add_dma_fds_to_vg(thread_ctx->vg, ctx->draw_buffers, thread_ctx->thread_id) != 0) {
            printf("[Thread %d] Failed to add DMA buffers\n", thread_ctx->thread_id);
            artvg_destroy(thread_ctx->vg);
            thread_ctx->vg = NULL;
            free(wrapper);
            return NULL;
        }
        need_cleanup_dma = 1;
    }

    /* Initialize thread-specific resources (e.g., per-thread source buffer) */
    if (ops->init_thread_resources) {
        if (ops->init_thread_resources(thread_ctx) != 0) {
            printf("[Thread %d] Failed to initialize thread resources\n", thread_ctx->thread_id);
            if (need_cleanup_dma) {
                remove_dma_fds_from_vg(thread_ctx->vg, ctx->draw_buffers);
                artvg_destroy(thread_ctx->vg);
            }
            thread_ctx->vg = NULL;
            free(wrapper);
            return NULL;
        }
    }

    /* Execute test loop */
    for (int iter = 0; iter < ctx->total_iterations; iter++) {
        int buffer_idx = 1 - atomic_load(&ctx->display_buffer_index);

        stress_mark_buffer_drawing_start(ctx, buffer_idx);

        /* Call the simplified draw callback */
        int result = ops->draw_callback(thread_ctx, buffer_idx);

        /* Increment param index to cycle through test configurations */
        thread_ctx->current_param_index++;

        if (result == 0) {
            thread_ctx->draw_count++;
        } else {
            /* Error handling - draw_callback should call stress_record_failure internally */
        }

        stress_mark_buffer_drawing_end(ctx, buffer_idx);
        if (ctx->display_wait_vg_complete) {
            usleep(50);
        }
    }

    /* Cleanup thread-specific resources before VG destroy */
    if (ops->cleanup_thread_resources) {
        ops->cleanup_thread_resources(thread_ctx);
    }

    /* Cleanup VG instance (only in multi-instance mode) */
    if (need_cleanup_dma) {
        remove_dma_fds_from_vg(thread_ctx->vg, ctx->draw_buffers);
        artvg_destroy(thread_ctx->vg);
    }
    thread_ctx->vg = NULL;

    thread_ctx->total_time_ms = stress_get_time_ms() - thread_start;
    printf("[Thread %d] Completed: draws=%" PRIu64 ", fails=%" PRIu64 ", time=%.2fs\n",
           thread_ctx->thread_id, thread_ctx->draw_count, thread_ctx->fail_count,
           thread_ctx->total_time_ms / 1000.0);

    free(wrapper);
    return NULL;
}

/***********************
 *  MAIN TEST ENTRY POINT
 **********************/
int stress_run_test(int argc, char **argv, const stress_test_ops_t *ops)
{
    stress_render_context_t ctx;
    stress_thread_context_t *thread_contexts = NULL;
    pthread_t *vg_threads = NULL;
    pthread_t *drm_thread = NULL;
    int num_threads = STRESS_DEFAULT_THREADS;
    int iterations = STRESS_DEFAULT_ITERATIONS;
    int display_wait_vg = 0;  /* Default: disabled */
    int shared_vg_mode = 0;   /* Default: multi-instance mode */

    int parse_result = stress_parse_command_line_args(argc, argv, &num_threads,
                                                       &iterations, &display_wait_vg, &shared_vg_mode, ops);
    if (parse_result != 0) {
        return parse_result;
    }

    /* Print test configuration */
    stress_print_test_configuration(ops->test_name, num_threads, iterations, display_wait_vg, shared_vg_mode);

    /* Initialize base framework resources */
    if (stress_init_render_context(&ctx, num_threads, iterations, display_wait_vg, shared_vg_mode) != 0 ||
        stress_init_drm_device(&ctx) != 0 ||
        stress_init_draw_buffers(&ctx) != 0) {
        printf("Error: Base initialization failed\n");
        stress_cleanup_resources(&ctx, NULL, NULL, NULL);
        return -1;
    }

    /* Single-instance mode: create shared VG instance */
    if (ctx.shared_vg_mode) {
        ctx.shared_vg = artvg_create();
        if (!ctx.shared_vg) {
            printf("Error: Failed to create shared VG instance\n");
            stress_cleanup_resources(&ctx, NULL, NULL, NULL);
            return -1;
        }
        if (add_dma_fds_to_vg(ctx.shared_vg, ctx.draw_buffers, 0) != 0) {
            printf("Error: Failed to add DMA buffers to shared VG\n");
            artvg_destroy(ctx.shared_vg);
            stress_cleanup_resources(&ctx, NULL, NULL, NULL);
            return -1;
        }
        printf("Shared VG instance created for single-instance mode\n\n");
    }

    /* Initialize test-specific resources (e.g., source buffer for blit) */
    if (ops->init_test_resources) {
        if (ops->init_test_resources(&ctx) != 0) {
            printf("Error: Test resources initialization failed\n");
            stress_cleanup_resources(&ctx, NULL, NULL, NULL);
            return -1;
        }
    }

    /* Initialize synchronization and allocate thread resources */
    if (stress_init_synchronization(&ctx) != 0 ||
        stress_allocate_thread_resources(&thread_contexts, &vg_threads, &drm_thread, num_threads) != 0) {
        printf("Error: Synchronization or thread allocation failed\n");
        if (ops->cleanup_test_resources) {
            ops->cleanup_test_resources(&ctx);
        }
        stress_cleanup_resources(&ctx, NULL, NULL, NULL);
        return -1;
    }

    stress_init_thread_contexts(thread_contexts, &ctx, num_threads);

    printf("Starting concurrent stress test with %d threads...\n\n", num_threads);

    /* Create DRM manager thread */
    if (pthread_create(drm_thread, NULL, stress_drm_manager_thread, &ctx) != 0) {
        printf("Error: Failed to create DRM thread\n");
        if (ops->cleanup_test_resources) {
            ops->cleanup_test_resources(&ctx);
        }
        stress_cleanup_resources(&ctx, thread_contexts, vg_threads, drm_thread);
        return -1;
    }

    /* Create wrapper structures for VG worker threads */
    for (int i = 0; i < num_threads; i++) {
        struct {
            stress_thread_context_t *thread_ctx;
            const stress_test_ops_t *ops;
        } *wrapper = malloc(sizeof(*wrapper));
        if (!wrapper) {
            printf("Error: Failed to allocate wrapper for thread %d\n", i);
            ctx.running = 0;
            pthread_join(*drm_thread, NULL);
            if (ops->cleanup_test_resources) {
                ops->cleanup_test_resources(&ctx);
            }
            stress_cleanup_resources(&ctx, thread_contexts, vg_threads, drm_thread);
            return -1;
        }
        wrapper->thread_ctx = &thread_contexts[i];
        wrapper->ops = ops;

        if (pthread_create(&vg_threads[i], NULL, vg_worker_thread_generic, wrapper) != 0) {
            printf("Error: Failed to create VG thread %d\n", i);
            free(wrapper);
            for (int j = 0; j < i; j++) {
                pthread_join(vg_threads[j], NULL);
            }
            ctx.running = 0;
            pthread_join(*drm_thread, NULL);
            if (ops->cleanup_test_resources) {
                ops->cleanup_test_resources(&ctx);
            }
            stress_cleanup_resources(&ctx, thread_contexts, vg_threads, drm_thread);
            return -1;
        }
    }

    /* Wait for all VG threads to complete */
    stress_wait_for_vg_threads(vg_threads, num_threads);

    /* Stop DRM thread */
    ctx.running = 0;
    pthread_join(*drm_thread, NULL);

    printf("\nAll threads completed\n");

    /* Collect and print test results */
    if (ops->collect_and_print_results) {
        ops->collect_and_print_results(&ctx, thread_contexts, num_threads);
    } else {
        stress_print_test_results(&ctx, thread_contexts, ops->fail_reason_to_string);
    }

    /* Cleanup test-specific resources */
    if (ops->cleanup_test_resources) {
        ops->cleanup_test_resources(&ctx);
    }

    stress_cleanup_resources(&ctx, thread_contexts, vg_threads, drm_thread);

    /* Single-instance mode: cleanup shared VG instance */
    if (ctx.shared_vg_mode && ctx.shared_vg) {
        remove_dma_fds_from_vg(ctx.shared_vg, ctx.draw_buffers);
        artvg_destroy(ctx.shared_vg);
        ctx.shared_vg = NULL;
    }

    return stress_check_test_results(thread_contexts, num_threads);
}
