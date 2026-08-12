/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <math.h>
#include <getopt.h>
#include "artvg.h"
#include "public/vg_drm.h"

#define ARRAY_SIZE(arr)    (sizeof(arr) / sizeof((arr)[0]))

#define COLOR_RED          0xFFFF0000
#define COLOR_GREEN        0xFF00FF00
#define COLOR_BLUE         0xFF0000FF
#define COLOR_YELLOW       0xFFFFFF00
#define COLOR_CYAN         0xFF00FFFF
#define COLOR_MAGENTA      0xFFFF00FF
#define COLOR_WHITE        0xFFFFFFFF
#define COLOR_BLACK        0xFF000000
#define COLOR_ORANGE       0xFFFFA500
#define COLOR_PURPLE       0xFF800080

#define VLC_OP_MOVE         0x02
#define VLC_OP_LINE         0x04
#define VLC_OP_QUAD         0x06
#define VLC_OP_CUBIC        0x08

#define VECTOR_SECTION_SIZE 4088  /* 4KB - 8 bytes header */

#define VECTOR_POOL_DEFAULT_SIZE  (1 * 1024 * 1024)  /* 1MB total pool */
#define VECTOR_POOL_DEFAULT_BLOCK 512                  /* 512 bytes per block */

typedef enum {
    MODE_UNKNOWN = 0,
    MODE_MULTI_TRIANGLES,
    MODE_MULTI_RECTANGLES,
    MODE_MULTI_CIRCLES,
    MODE_MULTI_MIXED,
    /* Large data modes */
    MODE_LARGE_DENSE_LINES,
    MODE_LARGE_DENSE_QUADS,
    MODE_LARGE_STAR_BURST,
    MODE_LARGE_SPIRAL,
    MODE_LARGE_GRID,
    /* Extreme modes */
    MODE_EXTREME_LINES,
    MODE_EXTREME_SPIRAL,
    MODE_EXTREME_FRACTAL,
    MODE_COUNT
} test_mode_t;

/* Size presets for extreme mode */
typedef enum {
    SIZE_100K = 100 * 1024,
    SIZE_500K = 500 * 1024,
    SIZE_1M = 1024 * 1024,
} size_preset_t;

/* Path builder for dynamic path construction */
typedef struct {
    float *data;
    size_t capacity;
    size_t count;
} path_builder_t;

/* Mode info structure */
typedef struct {
    const char *name;
    test_mode_t mode;
    const char *description;
} mode_info_t;

static mode_info_t g_mode_info[] = {
    {"multi-triangles", MODE_MULTI_TRIANGLES, "Multiple triangles (10 shapes)"},
    {"multi-rectangles", MODE_MULTI_RECTANGLES, "Multiple rectangles (8 shapes)"},
    {"multi-circles", MODE_MULTI_CIRCLES, "Multiple circles (6 shapes, 32 segments each)"},
    {"multi-mixed", MODE_MULTI_MIXED, "Mixed shapes (triangles + rectangles + circles)"},
    /* Large data modes */
    {"large-dense-lines", MODE_LARGE_DENSE_LINES, "600 dense lines (>4KB)"},
    {"large-dense-quads", MODE_LARGE_DENSE_QUADS, "300 quadratic bezier curves (>4KB)"},
    {"large-star-burst", MODE_LARGE_STAR_BURST, "Star burst pattern (100 lines from center)"},
    {"large-spiral", MODE_LARGE_SPIRAL, "Spiral pattern (500 segments)"},
    {"large-grid", MODE_LARGE_GRID, "Grid pattern (20x20 lines)"},
    /* Extreme modes */
    {"extreme-lines", MODE_EXTREME_LINES, "Extreme dense lines (100KB-1MB data)"},
    {"extreme-spiral", MODE_EXTREME_SPIRAL, "Deep spiral with many segments"},
    {"extreme-fractal", MODE_EXTREME_FRACTAL, "Recursive fractal pattern"},
};

/* Path builder functions */
static int builder_init(path_builder_t *builder, size_t initial_capacity)
{
    builder->data = (float *)malloc(initial_capacity * sizeof(float));
    if (!builder->data) return -1;
    builder->capacity = initial_capacity;
    builder->count = 0;
    return 0;
}

static void builder_free(path_builder_t *builder)
{
    if (builder->data) {
        free(builder->data);
        builder->data = NULL;
    }
    builder->capacity = 0;
    builder->count = 0;
}

static int builder_ensure_capacity(path_builder_t *builder, size_t needed)
{
    if (builder->count + needed > builder->capacity) {
        size_t new_capacity = builder->capacity * 2;
        while (new_capacity < builder->count + needed) {
            new_capacity *= 2;
        }
        float *new_data = (float *)realloc(builder->data, new_capacity * sizeof(float));
        if (!new_data) return -1;
        builder->data = new_data;
        builder->capacity = new_capacity;
    }
    return 0;
}

static int builder_add_move(path_builder_t *builder, float x, float y)
{
    if (builder_ensure_capacity(builder, 3) != 0) return -1;
    builder->data[builder->count++] = (float)VLC_OP_MOVE;
    builder->data[builder->count++] = x;
    builder->data[builder->count++] = y;
    return 0;
}

static int builder_add_line(path_builder_t *builder, float x, float y)
{
    if (builder_ensure_capacity(builder, 3) != 0) return -1;
    builder->data[builder->count++] = (float)VLC_OP_LINE;
    builder->data[builder->count++] = x;
    builder->data[builder->count++] = y;
    return 0;
}

static int builder_add_quad(path_builder_t *builder, float cx, float cy, float x, float y)
{
    if (builder_ensure_capacity(builder, 5) != 0) return -1;
    builder->data[builder->count++] = (float)VLC_OP_QUAD;
    builder->data[builder->count++] = cx;
    builder->data[builder->count++] = cy;
    builder->data[builder->count++] = x;
    builder->data[builder->count++] = y;
    return 0;
}


/* Build multiple triangles */
static int build_multi_triangles(path_builder_t *builder, int count)
{
    int shapes_per_row = 3;
    float shape_size = 80.0f;
    float spacing = 120.0f;

    for (int i = 0; i < count; i++) {
        float offset_x = (i % shapes_per_row) * spacing + 50.0f;
        float offset_y = (i / shapes_per_row) * spacing + 50.0f;

        /* Triangle: 3 points (MOVE + 2 LINE to points, last LINE to close) */
        if (builder_add_move(builder, offset_x + shape_size / 2, offset_y) != 0) return -1;
        if (builder_add_line(builder, offset_x + shape_size, offset_y + shape_size) != 0) return -1;
        if (builder_add_line(builder, offset_x, offset_y + shape_size) != 0) return -1;
        if (builder_add_line(builder, offset_x + shape_size / 2, offset_y) != 0) return -1;
    }
    return 0;
}

/* Build multiple rectangles */
static int build_multi_rectangles(path_builder_t *builder, int count)
{
    int shapes_per_row = 4;
    float width = 70.0f;
    float height = 50.0f;
    float spacing_x = 100.0f;
    float spacing_y = 80.0f;

    for (int i = 0; i < count; i++) {
        float offset_x = (i % shapes_per_row) * spacing_x + 50.0f;
        float offset_y = (i / shapes_per_row) * spacing_y + 50.0f;

        /* Rectangle: MOVE + 3 LINE + LINE to close */
        if (builder_add_move(builder, offset_x, offset_y) != 0) return -1;
        if (builder_add_line(builder, offset_x + width, offset_y) != 0) return -1;
        if (builder_add_line(builder, offset_x + width, offset_y + height) != 0) return -1;
        if (builder_add_line(builder, offset_x, offset_y + height) != 0) return -1;
        if (builder_add_line(builder, offset_x, offset_y) != 0) return -1;
    }
    return 0;
}

/* Build multiple circles (approximated with line segments) */
static int build_multi_circles(path_builder_t *builder, int count, int segments)
{
    int shapes_per_row = 3;
    float radius = 50.0f;
    float spacing = 140.0f;

    for (int i = 0; i < count; i++) {
        float center_x = (i % shapes_per_row) * spacing + radius + 50.0f;
        float center_y = (i / shapes_per_row) * spacing + radius + 50.0f;

        /* Circle: MOVE to first point + segments-1 LINE + LINE to close */
        float start_x = center_x + radius;
        float start_y = center_y;

        if (builder_add_move(builder, start_x, start_y) != 0) return -1;

        for (int j = 1; j < segments; j++) {
            float angle = 2.0f * M_PI * j / segments;
            float x = center_x + radius * cosf(angle);
            float y = center_y + radius * sinf(angle);
            if (builder_add_line(builder, x, y) != 0) return -1;
        }

        /* Close the circle */
        if (builder_add_line(builder, start_x, start_y) != 0) return -1;
    }
    return 0;
}

/* Build mixed shapes */
static int build_multi_mixed(path_builder_t *builder, int triangle_count, int rect_count, int circle_count)
{
    float offset_y = 50.0f;
    float shape_size, offset_x;

    /* Triangles row */
    shape_size = 60.0f;
    for (int i = 0; i < triangle_count; i++) {
        offset_x = 50.0f + i * 100.0f;
        if (builder_add_move(builder, offset_x + shape_size / 2, offset_y) != 0) return -1;
        if (builder_add_line(builder, offset_x + shape_size, offset_y + shape_size) != 0) return -1;
        if (builder_add_line(builder, offset_x, offset_y + shape_size) != 0) return -1;
        if (builder_add_line(builder, offset_x + shape_size / 2, offset_y) != 0) return -1;
    }

    /* Rectangles row */
    offset_y += 100.0f;
    for (int i = 0; i < rect_count; i++) {
        offset_x = 50.0f + i * 100.0f;
        if (builder_add_move(builder, offset_x, offset_y) != 0) return -1;
        if (builder_add_line(builder, offset_x + 60.0f, offset_y) != 0) return -1;
        if (builder_add_line(builder, offset_x + 60.0f, offset_y + 40.0f) != 0) return -1;
        if (builder_add_line(builder, offset_x, offset_y + 40.0f) != 0) return -1;
        if (builder_add_line(builder, offset_x, offset_y) != 0) return -1;
    }

    /* Circles row */
    offset_y += 100.0f;
    float radius = 30.0f;
    int segments = 20;
    for (int i = 0; i < circle_count; i++) {
        float center_x = 80.0f + i * 100.0f;
        float center_y = offset_y + radius;
        float start_x = center_x + radius;
        float start_y = center_y;

        if (builder_add_move(builder, start_x, start_y) != 0) return -1;
        for (int j = 1; j < segments; j++) {
            float angle = 2.0f * M_PI * j / segments;
            float x = center_x + radius * cosf(angle);
            float y = center_y + radius * sinf(angle);
            if (builder_add_line(builder, x, y) != 0) return -1;
        }
        if (builder_add_line(builder, start_x, start_y) != 0) return -1;
    }

    return 0;
}

/* Build dense small rectangles (closed shapes, large data test) */
static int build_large_dense_lines(path_builder_t *builder, int count,
                                   float screen_width, float screen_height)
{
    /* Use actual screen size with margin */
    float margin = 50.0f;
    float max_width = screen_width - 2 * margin;
    float max_height = screen_height - 2 * margin;
    float start_x = margin;
    float start_y = margin;
    float rect_size = 8.0f;

    /* Calculate grid dimensions to fit all rectangles */
    float rects_per_row_f = sqrtf((float)count * max_width / max_height);
    int rects_per_row = (int)(rects_per_row_f + 0.5f);
    if (rects_per_row < 1) rects_per_row = 1;

    int num_rows = (count + rects_per_row - 1) / rects_per_row;
    float spacing_x = max_width / rects_per_row;
    float spacing_y = max_height / num_rows;

    for (int i = 0; i < count; i++) {
        int row = i / rects_per_row;
        int col = i % rects_per_row;
        float x = start_x + col * spacing_x;
        float y = start_y + row * spacing_y;

        /* Draw small closed rectangle: MOVE + 3 LINE + LINE to close */
        if (builder_add_move(builder, x, y) != 0) return -1;
        if (builder_add_line(builder, x + rect_size, y) != 0) return -1;
        if (builder_add_line(builder, x + rect_size, y + rect_size) != 0) return -1;
        if (builder_add_line(builder, x, y + rect_size) != 0) return -1;
        if (builder_add_line(builder, x, y) != 0) return -1;  /* Close */
    }
    return 0;
}

/* Build closed quadratic bezier shapes (petal/leaf shapes, large data test) */
static int build_large_dense_quads(path_builder_t *builder, int count,
                                   float screen_width, float screen_height)
{
    /* Use actual screen size with margin */
    float margin = 50.0f;
    float max_width = screen_width - 2 * margin;
    float max_height = screen_height - 2 * margin;
    float start_x = margin;
    float start_y = margin;

    /* Calculate grid dimensions to fit all shapes */
    float shapes_per_row_f = sqrtf((float)count * max_width / max_height);
    int shapes_per_row = (int)(shapes_per_row_f + 0.5f);
    if (shapes_per_row < 1) shapes_per_row = 1;

    int num_rows = (count + shapes_per_row - 1) / shapes_per_row;
    float spacing_x = max_width / shapes_per_row;
    float spacing_y = max_height / num_rows;

    for (int i = 0; i < count; i++) {
        int row = i / shapes_per_row;
        int col = i % shapes_per_row;
        float cx = start_x + col * spacing_x + spacing_x * 0.5f;
        float cy = start_y + row * spacing_y + spacing_y * 0.5f;
        float w = spacing_x * 0.35f;
        float h = spacing_y * 0.35f;

        /* Draw closed petal shape using quadratic bezier curves */
        /* Start at bottom point */
        if (builder_add_move(builder, cx, cy + h) != 0) return -1;
        /* Left curve to top point */
        if (builder_add_quad(builder, cx - w * 1.5f, cy, cx, cy - h) != 0) return -1;
        /* Right curve back to bottom point */
        if (builder_add_quad(builder, cx + w * 1.5f, cy, cx, cy + h) != 0) return -1;
    }
    return 0;
}

/* Build star shape (closed polygon, adaptive to screen size) */
static int build_large_star_burst(path_builder_t *builder, int count,
                                  float screen_width, float screen_height)
{
    float center_x = screen_width / 2.0f;
    float center_y = screen_height / 2.0f;
    float max_radius = fminf(screen_width, screen_height) * 0.45f;
    float inner_radius = max_radius * 0.4f;
    float outer_radius = max_radius;

    /* Draw closed star polygon */
    int points = count * 2;  /* Each ray has outer and inner point */
    if (points < 10) points = 10;

    /* Start at first outer point */
    float first_x = center_x + outer_radius;
    float first_y = center_y;
    if (builder_add_move(builder, first_x, first_y) != 0) return -1;

    for (int i = 1; i <= points; i++) {
        float angle = M_PI * i / count;  /* Half step for alternating */
        float radius = (i % 2 == 0) ? outer_radius : inner_radius;
        float x = center_x + radius * cosf(angle);
        float y = center_y + radius * sinf(angle);
        if (builder_add_line(builder, x, y) != 0) return -1;
    }

    printf("  Star shape: %d points, center (%.0f, %.0f)\n", count, center_x, center_y);
    return 0;
}

/* Build concentric circles (closed, adaptive to screen size) */
static int build_large_spiral(path_builder_t *builder, int count,
                              float screen_width, float screen_height)
{
    float center_x = screen_width / 2.0f;
    float center_y = screen_height / 2.0f;
    float max_radius = fminf(screen_width, screen_height) * 0.45f;

    /* Distribute count across multiple concentric circles */
    int num_rings = (count + 63) / 64;  /* Each ring has ~64 segments */
    if (num_rings < 1) num_rings = 1;
    if (num_rings > count) num_rings = count;

    int segments_per_ring = count / num_rings;
    if (segments_per_ring < 8) segments_per_ring = 8;

    for (int r = 0; r < num_rings; r++) {
        float radius = max_radius * (r + 1) / num_rings;

        /* Draw closed circle with line segments */
        float start_x = center_x + radius;
        float start_y = center_y;

        if (builder_add_move(builder, start_x, start_y) != 0) return -1;

        for (int i = 1; i <= segments_per_ring; i++) {
            float angle = 2.0f * M_PI * i / segments_per_ring;
            float x = center_x + radius * cosf(angle);
            float y = center_y + radius * sinf(angle);
            if (builder_add_line(builder, x, y) != 0) return -1;
        }
    }

    printf("  Concentric circles: %d rings, %d segs/ring, center (%.0f, %.0f)\n",
           num_rings, segments_per_ring, center_x, center_y);
    return 0;
}

/* Build grid of closed rectangles (filled cells) */
static int build_large_grid(path_builder_t *builder, int rows, int cols,
                            float screen_width, float screen_height)
{
    float margin = 50.0f;
    float start_x = margin;
    float start_y = margin;
    float max_width = screen_width - 2 * margin;
    float max_height = screen_height - 2 * margin;
    float cell_w = max_width / cols;
    float cell_h = max_height / rows;

    /* Draw each grid cell as a closed rectangle */
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            float x = start_x + c * cell_w;
            float y = start_y + r * cell_h;

            /* Draw closed rectangle: MOVE + 3 LINE + LINE to close */
            if (builder_add_move(builder, x, y) != 0) return -1;
            if (builder_add_line(builder, x + cell_w, y) != 0) return -1;
            if (builder_add_line(builder, x + cell_w, y + cell_h) != 0) return -1;
            if (builder_add_line(builder, x, y + cell_h) != 0) return -1;
            if (builder_add_line(builder, x, y) != 0) return -1;  /* Close */
        }
    }

    printf("  Grid: %dx%d closed rectangles, %d total cells\n", cols, rows, cols * rows);
    return 0;
}

/* Build extreme dense concentric circles (100KB - 1MB+ data)
 * Visual design: Many closed concentric circles with increasing density
 * More data = more circles = denser appearance
 */
static int build_extreme_lines(path_builder_t *builder, size_t target_bytes,
                               float screen_width, float screen_height)
{
    /* Each circle: MOVE (3 floats) + N * LINE (3 floats each)
     * For ~64 segments per circle: 3 + 64*3 = 195 floats = 780 bytes
     */
    size_t bytes_per_circle = 780;
    int num_circles = (int)(target_bytes / bytes_per_circle);
    if (num_circles < 10) num_circles = 10;
    if (num_circles > 2000) num_circles = 2000;

    /* Adaptive screen parameters - center based, proportional sizing */
    float cx = screen_width / 2.0f;
    float cy = screen_height / 2.0f;
    float max_radius = fminf(screen_width, screen_height) * 0.48f;

    /* Generate concentric circles */
    for (int i = 0; i < num_circles; i++) {
        float radius = (i + 1) * max_radius / num_circles;
        /* More segments for larger circles */
        int segments = 32 + (i * 64 / num_circles);
        if (segments > 128) segments = 128;

        /* Draw closed circle */
        float start_x = cx + radius;
        float start_y = cy;

        if (builder_add_move(builder, start_x, start_y) != 0) return -1;

        for (int j = 1; j <= segments; j++) {
            float angle = 2.0f * M_PI * j / segments;
            float x = cx + radius * cosf(angle);
            float y = cy + radius * sinf(angle);
            if (builder_add_line(builder, x, y) != 0) return -1;
        }
    }

    printf("  Generated %d closed concentric circles\n", num_circles);
    printf("  Target: %d circles (~%zu bytes), Screen: %.0fx%.0f\n",
           num_circles, num_circles * bytes_per_circle, screen_width, screen_height);

    return 0;
}

/* Build extreme rose/flower pattern (closed shapes)
 * Multiple overlapping closed petals forming a rose pattern
 */
static int build_extreme_spiral(path_builder_t *builder, int num_segments,
                                float screen_width, float screen_height)
{
    /* Adaptive center and radius based on screen size */
    float center_x = screen_width / 2.0f;
    float center_y = screen_height / 2.0f;
    float max_radius = fminf(screen_width, screen_height) * 0.48f;

    /* Create multiple rose curves with different parameters */
    int num_roses = (num_segments + 499) / 500;  /* Each rose ~500 segments */
    if (num_roses < 3) num_roses = 3;
    if (num_roses > 50) num_roses = 50;

    for (int r = 0; r < num_roses; r++) {
        /* Rose curve: r = a * cos(k * theta)
         * k = n/d determines the petal structure
         */
        int petals = 3 + (r % 8);  /* Vary petal count */
        float k = (petals % 2 == 0) ? petals / 2.0f : petals;
        float a = max_radius * (0.3f + 0.7f * r / num_roses);
        int points = 100 + (r * 20);

        /* Draw closed rose curve */
        float first_x = center_x + a * cosf(0);
        float first_y = center_y + a * sinf(0);
        if (builder_add_move(builder, first_x, first_y) != 0) return -1;

        for (int i = 1; i <= points; i++) {
            float theta = 2.0f * M_PI * i / points;
            float radius = a * fabsf(cosf(k * theta));
            float x = center_x + radius * cosf(theta);
            float y = center_y + radius * sinf(theta);
            if (builder_add_line(builder, x, y) != 0) return -1;
        }
    }

    printf("  Generated %d closed rose curves\n", num_roses);
    printf("  Center: (%.0f, %.0f), Max radius: %.0f\n", center_x, center_y, max_radius);

    return 0;
}

/* Recursive fractal: Koch snowflake segments */
static int build_fractal_koch(path_builder_t *builder, float x1, float y1,
                               float x2, float y2, int depth)
{
    if (depth == 0) {
        if (builder_add_move(builder, x1, y1) != 0) return -1;
        if (builder_add_line(builder, x2, y2) != 0) return -1;
        return 0;
    }

    /* Divide line into 3 parts, create Koch curve bump */
    float dx = (x2 - x1) / 3.0f;
    float dy = (y2 - y1) / 3.0f;

    float xa = x1 + dx;
    float ya = y1 + dy;
    float xb = x1 + 2.0f * dx;
    float yb = y1 + 2.0f * dy;

    /* Peak of equilateral triangle */
    float xc = (xa + xb) / 2.0f - dy * 0.866f;
    float yc = (ya + yb) / 2.0f + dx * 0.866f;

    /* Recursively build 4 segments */
    if (build_fractal_koch(builder, x1, y1, xa, ya, depth - 1) != 0) return -1;
    if (build_fractal_koch(builder, xa, ya, xc, yc, depth - 1) != 0) return -1;
    if (build_fractal_koch(builder, xc, yc, xb, yb, depth - 1) != 0) return -1;
    if (build_fractal_koch(builder, xb, yb, x2, y2, depth - 1) != 0) return -1;

    return 0;
}

/* Build extreme fractal pattern */
static int build_extreme_fractal(path_builder_t *builder, int depth,
                                  float screen_width, float screen_height)
{
    float margin = 100.0f;
    float size = (screen_width < screen_height ? screen_width : screen_height) - 2 * margin;
    if (size < 200.0f) size = 200.0f;

    float cx = screen_width / 2.0f;
    float cy = screen_height / 2.0f;

    /* Triangle vertices */
    float x1 = cx;
    float y1 = cy - size / 2.0f;
    float x2 = cx - size * 0.866f / 2.0f;
    float y2 = cy + size / 4.0f;
    float x3 = cx + size * 0.866f / 2.0f;
    float y3 = cy + size / 4.0f;

    printf("  Generating Koch snowflake fractal (depth=%d)\n", depth);

    /* Three sides of Koch snowflake */
    if (build_fractal_koch(builder, x1, y1, x2, y2, depth) != 0) return -1;
    if (build_fractal_koch(builder, x2, y2, x3, y3, depth) != 0) return -1;
    if (build_fractal_koch(builder, x3, y3, x1, y1, depth) != 0) return -1;

    /* Calculate approximate segments: 3 * 4^depth lines */
    int approx_lines = 3;
    for (int i = 0; i < depth; i++) approx_lines *= 4;
    printf("  Approximate segments: %d\n", approx_lines);

    return 0;
}

/* Parse size parameter for extreme modes */
static size_t parse_size(const char *size_str)
{
    if (size_str == NULL) return SIZE_100K;

    char *endptr;
    long value = strtol(size_str, &endptr, 10);

    if (endptr && *endptr != '\0') {
        if (strcasecmp(endptr, "k") == 0 || strcasecmp(endptr, "kb") == 0) {
            value *= 1024;
        } else if (strcasecmp(endptr, "m") == 0 || strcasecmp(endptr, "mb") == 0) {
            value *= 1024 * 1024;
        }
    }

    if (value < 4096) value = 4096;  /* Minimum 4KB */
    if (value > SIZE_1M) value = SIZE_1M;  /* Maximum 1MB */

    return (size_t)value;
}

/* Parse test mode */
static test_mode_t parse_mode(const char *name)
{
    for (size_t i = 0; i < ARRAY_SIZE(g_mode_info); i++) {
        if (strncmp(name, g_mode_info[i].name, strlen(g_mode_info[i].name) + 1) == 0) {
            return g_mode_info[i].mode;
        }
    }
    return MODE_UNKNOWN;
}

/* Show usage */
static void show_usage(const char *program_name)
{
    printf("Usage: %s --mode <mode> [options]\n", program_name);
    printf("\nArtVG Vector Chain Test - Tests multiple shapes and large data scenarios\n");
    printf("\n=== Multi-Shape Modes (test vector list with multiple shapes) ===\n");
    printf("  multi-triangles       10 independent triangles (~320 bytes)\n");
    printf("  multi-rectangles      8 independent rectangles (~512 bytes)\n");
    printf("  multi-circles         6 circles (32 segments each, ~1560 bytes)\n");
    printf("  multi-mixed           Mixed shapes (triangles + rectangles + circles)\n");
    printf("\n=== Large Data Modes (test vector chain with >4KB data) ===\n");
    printf("  large-dense-lines     600 small closed rectangles (~4800 bytes, 2 sections)\n");
    printf("  large-dense-quads     300 closed quadratic bezier petals (~4800 bytes, 2 sections)\n");
    printf("  large-star-burst      Star shape with N points (closed polygon)\n");
    printf("  large-spiral          Concentric circles (500 segments, closed)\n");
    printf("  large-grid            20x20 closed rectangle cells (~6400 bytes, 2+ sections)\n");
    printf("\n=== Extreme Modes (test vector chain with 100KB-1MB data) ===\n");
    printf("  extreme-lines         Dense concentric circles: 100KB-1MB (use --size)\n");
    printf("  extreme-spiral        Rose/flower patterns: many closed curves (use --segments)\n");
    printf("  extreme-fractal       Koch snowflake fractal (use --depth)\n");
    printf("\n=== Options ===\n");
    printf("  --mode <mode>         Test mode (required)\n");
    printf("  --count <n>           Number of shapes (for multi-* modes, default varies)\n");
    printf("  --segments <n>        Segments per circle (for multi-circles, default: 32)\n");
    printf("  --lines <n>           Number of lines (for large-dense-lines, default: 600)\n");
    printf("  --quads <n>           Number of quads (for large-dense-quads, default: 300)\n");
    printf("  --rows <n>            Grid rows (for large-grid, default: 20)\n");
    printf("  --cols <n>            Grid columns (for large-grid, default: 20)\n");
    printf("  --size <100k|500k|1m> Target data size for extreme-lines (default: 100k)\n");
    printf("  --depth <n>           Recursion depth for extreme-fractal (default: 6)\n");
    printf("  --fill-color <0xRRGGBB>  Fill color (default: 0xFF0000 red)\n");
    printf("  --fill-rule <0|1>     Fill rule: 0=NON_ZERO (default), 1=EVEN_ODD\n");
    printf("  --pool-enable <0|1>   Enable vector pool (default: 1)\n");
    printf("  --pool-size <size>    Total pool size (default: 1m, e.g. 500k, 2m)\n");
    printf("  --pool-block <bytes>  Pool block size, min 256 (default: 512)\n");
    printf("  --help                Show this help message\n");
    printf("\n=== Examples ===\n");
    printf("  %s --mode multi-triangles --count 10\n", program_name);
    printf("  %s --mode multi-circles --count 6 --segments 32\n", program_name);
    printf("  %s --mode large-dense-lines --lines 600\n", program_name);
    printf("  %s --mode large-grid --rows 25 --cols 25\n", program_name);
    printf("  %s --mode extreme-lines --size 500k\n", program_name);
    printf("  %s --mode extreme-spiral --segments 20000\n", program_name);
    printf("  %s --mode extreme-fractal --depth 7\n", program_name);
    printf("  %s --mode large-dense-lines --pool-enable 1 --pool-size 1m --pool-block 512\n", program_name);
    printf("  %s --mode multi-mixed --pool-enable 0\n", program_name);
}

/* Build path from mode */
static int build_path_for_mode(path_builder_t *builder, test_mode_t mode,
                               int shape_count, int segments, int lines, int quads,
                               int grid_rows, int grid_cols,
                               size_t target_size, int depth,
                               float screen_width, float screen_height)
{
    switch (mode) {
    case MODE_MULTI_TRIANGLES:
        return build_multi_triangles(builder, shape_count > 0 ? shape_count : 10);
    case MODE_MULTI_RECTANGLES:
        return build_multi_rectangles(builder, shape_count > 0 ? shape_count : 8);
    case MODE_MULTI_CIRCLES:
        return build_multi_circles(builder, shape_count > 0 ? shape_count : 6,
                                   segments > 0 ? segments : 32);
    case MODE_MULTI_MIXED:
        return build_multi_mixed(builder,
                                 shape_count > 0 ? shape_count / 3 + 1 : 4,
                                 shape_count > 0 ? shape_count / 3 + 1 : 3,
                                 shape_count > 0 ? shape_count / 3 : 3);
    case MODE_LARGE_DENSE_LINES:
        return build_large_dense_lines(builder, lines > 0 ? lines : 600,
                                       screen_width, screen_height);
    case MODE_LARGE_DENSE_QUADS:
        return build_large_dense_quads(builder, quads > 0 ? quads : 300,
                                       screen_width, screen_height);
    case MODE_LARGE_STAR_BURST:
        return build_large_star_burst(builder, shape_count > 0 ? shape_count : 100,
                                      screen_width, screen_height);
    case MODE_LARGE_SPIRAL:
        return build_large_spiral(builder, shape_count > 0 ? shape_count : 500,
                                  screen_width, screen_height);
    case MODE_LARGE_GRID:
        return build_large_grid(builder,
                                grid_rows > 0 ? grid_rows : 20,
                                grid_cols > 0 ? grid_cols : 20,
                                screen_width, screen_height);
    case MODE_EXTREME_LINES:
        return build_extreme_lines(builder, target_size > 0 ? target_size : SIZE_100K,
                                   screen_width, screen_height);
    case MODE_EXTREME_SPIRAL:
        return build_extreme_spiral(builder, segments > 0 ? segments : 20000,
                                    screen_width, screen_height);
    case MODE_EXTREME_FRACTAL:
        return build_extreme_fractal(builder, depth > 0 ? depth : 6,
                                     screen_width, screen_height);
    default:
        return -1;
    }
}

/* Parse and build path using ArtVG API */
static artvg_path_t *parse_and_build_path(struct artvg *vg, const float *data, size_t count)
{
    artvg_error_t ret;
    size_t i = 0;

    artvg_path_t *path = artvg_path_allocate(vg);
    if (!path) return NULL;

    while (i < count) {
        uint8_t opcode = (uint8_t)data[i];
        switch (opcode) {
        case VLC_OP_MOVE:
            ret = artvg_path_move_to(vg, path, data[i+1], data[i+2]);
            i += 3;
            break;
        case VLC_OP_LINE:
            ret = artvg_path_line_to(vg, path, data[i+1], data[i+2]);
            i += 3;
            break;
        case VLC_OP_QUAD:
            ret = artvg_path_quad_to(vg, path, data[i+1], data[i+2], data[i+3], data[i+4]);
            i += 5;
            break;
        case VLC_OP_CUBIC:
            ret = artvg_path_cubic_to(vg, path, data[i+1], data[i+2], data[i+3],
                                      data[i+4], data[i+5], data[i+6]);
            i += 7;
            break;
        default:
            i++;
            ret = ARTVG_SUCCESS;
            break;
        }
        if (ret != ARTVG_SUCCESS) {
            artvg_path_free(vg, path);
            return NULL;
        }
    }

    if (artvg_path_finish(vg, path) != ARTVG_SUCCESS) {
        artvg_path_free(vg, path);
        return NULL;
    }

    return path;
}

/* Hardware next field format: bits[63:24]=next_phys_addr, bits[23:0]=section_size
 * These match the definitions in artvg_context.h
 */
#define DUMP_VECTOR_NEXT_FIELD_SIZE 8
#define DUMP_VECTOR_NEXT_SIZE_MASK  0xFFFFFF
#define DUMP_VECTOR_NEXT_ADDR_SHIFT 24

/* Parse and dump hardware next field from section memory */
static void dump_hardware_next_field(void *section_addr, uint32_t section_used, int section_idx)
{
    if (!section_addr || section_used == 0) {
        return;
    }

    /* Next field is located at the end of used data (8 bytes) */
    uint8_t *next_field_pos = (uint8_t *)section_addr + section_used;
    uint64_t next_field = *(uint64_t *)next_field_pos;

    printf("    Section %d HW Header (at offset %u):\n", section_idx, section_used);
    printf("      Raw next_field: 0x%016llX\n", (unsigned long long)next_field);

    if (next_field == 0) {
        printf("      -> Next phys addr: NONE (0x0, end of chain)\n");
        printf("      -> Next size: 0\n");
    } else {
        uint64_t next_phys = next_field >> DUMP_VECTOR_NEXT_ADDR_SHIFT;
        uint32_t next_size = (uint32_t)(next_field & DUMP_VECTOR_NEXT_SIZE_MASK);

        printf("      -> Next phys addr: 0x%010llX (bits[63:24])\n", (unsigned long long)next_phys);
        printf("      -> Next size: %u bytes (bits[23:0], raw=0x%06X)\n",
               next_size, next_size);

        if (next_size >= DUMP_VECTOR_NEXT_FIELD_SIZE) {
            printf("      -> Next data size: %u bytes (excl. 8B header)\n",
                   next_size - DUMP_VECTOR_NEXT_FIELD_SIZE);
        }
    }
}

/* Debug function to dump vector chain information */
static void dump_vector_chain_info(artvg_path_t *path)
{
    if (!path) {
        printf("  Path is NULL\n");
        return;
    }

    printf("\n=== Vector Chain Debug Info ===\n");
    printf("  Active vector: %p\n", (void *)path->active_vector);

    /* Dump vector list info */
    artvg_path_buffer_t *vector = &path->vector_list;
    int section_count = 0;
    size_t total_vector_size = 0;

    printf("\n  Vector Sections:\n");

    /* First section (embedded in path) */
    if (vector->size > 0) {
        printf("    Section %d: addr=%p, phy_addr=0x%llX, size=%u, used=%u, flag=0x%X%s\n",
               section_count,
               vector->addr,
               (unsigned long long)vector->buffer.phy_addr,
               vector->size,
               vector->used,
               vector->flag,
               (vector->flag & 0x2) ? " (pool)" : "");
        total_vector_size += vector->used;

        /* Dump hardware next field from memory */
        dump_hardware_next_field(vector->addr, vector->used, section_count);
        section_count++;

        /* Walk through additional sections in the list */
        artvg_path_buffer_t *buf = vector->next;
        while (buf != vector) {
            artvg_path_buffer_t *next_buf = buf->next;
            printf("\n    Section %d: addr=%p, phy_addr=0x%llX, size=%u, used=%u, flag=0x%X%s\n",
                   section_count,
                   buf->addr,
                   (unsigned long long)buf->buffer.phy_addr,
                   buf->size,
                   buf->used,
                   buf->flag,
                   (buf->flag & 0x2) ? " (pool)" : "");
            total_vector_size += buf->used;

            /* Dump hardware next field from memory */
            dump_hardware_next_field(buf->addr, buf->used, section_count);
            section_count++;
            buf = next_buf;
        }
    }

    printf("\n  Summary:\n");
    printf("    Total sections: %d\n", section_count);
    printf("    Total vector data: %zu bytes\n", total_vector_size);
    printf("    Estimated sections (4KB each): %zu\n",
           (total_vector_size + VECTOR_SECTION_SIZE - 1) / VECTOR_SECTION_SIZE);

    printf("================================\n\n");
}

int main(int argc, char **argv)
{
    struct artvg *vg = NULL;
    struct mpp_buf buf = {0};
    drm_dev_t drm_dev = {0};
    path_builder_t builder = {0};
    artvg_path_t *path = NULL;
    artvg_matrix_t matrix;
    artvg_ctrl_t ctrl;
    artvg_vector_ctl_t vector_ctl;
    artvg_gradient_t gradient = {0};
    artvg_error_t ret;
    int result = 0;

    /* Command line parameters */
    test_mode_t mode = MODE_UNKNOWN;
    int shape_count = 0;
    int segments = 0;
    int lines = 0;
    int quads = 0;
    int grid_rows = 0;
    int grid_cols = 0;
    size_t target_size = 0;
    int depth = 0;
    uint32_t fill_color = COLOR_RED;
    int fill_rule = ARTVG_FILL_NON_ZERO;
    int pool_enable = 1;
    uint32_t pool_size = VECTOR_POOL_DEFAULT_SIZE;
    uint32_t pool_block = VECTOR_POOL_DEFAULT_BLOCK;

    printf("=== ArtVG Vector Chain Test ===\n\n");

    static struct option long_options[] = {
        {"mode", required_argument, 0, 0},
        {"count", required_argument, 0, 0},
        {"segments", required_argument, 0, 0},
        {"lines", required_argument, 0, 0},
        {"quads", required_argument, 0, 0},
        {"rows", required_argument, 0, 0},
        {"cols", required_argument, 0, 0},
        {"size", required_argument, 0, 0},
        {"depth", required_argument, 0, 0},
        {"fill-color", required_argument, 0, 0},
        {"fill-rule", required_argument, 0, 0},
        {"pool-enable", required_argument, 0, 0},
        {"pool-size", required_argument, 0, 0},
        {"pool-block", required_argument, 0, 0},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while (1) {
        opt = getopt_long(argc, argv, "h", long_options, &option_index);
        if (opt == -1) break;

        switch (opt) {
        case 0: {
            const char *name = long_options[option_index].name;
            if (strncmp(name, "mode", strlen("mode")) == 0) {
                mode = parse_mode(optarg);
                if (mode == MODE_UNKNOWN) {
                    printf("Error: Unknown mode '%s'\n", optarg);
                    show_usage(argv[0]);
                    return -1;
                }
            } else if (strncmp(name, "count", strlen("count")) == 0) {
                shape_count = atoi(optarg);
            } else if (strncmp(name, "segments", strlen("segments")) == 0) {
                segments = atoi(optarg);
            } else if (strncmp(name, "lines", strlen("lines")) == 0) {
                lines = atoi(optarg);
            } else if (strncmp(name, "quads", strlen("quads")) == 0) {
                quads = atoi(optarg);
            } else if (strncmp(name, "rows", strlen("rows")) == 0) {
                grid_rows = atoi(optarg);
            } else if (strncmp(name, "cols", strlen("cols")) == 0) {
                grid_cols = atoi(optarg);
            } else if (strncmp(name, "size", strlen("size")) == 0) {
                target_size = parse_size(optarg);
            } else if (strncmp(name, "depth", strlen("depth")) == 0) {
                depth = atoi(optarg);
            } else if (strncmp(name, "fill-color", strlen("fill-color")) == 0) {
                fill_color = strtoul(optarg, NULL, 0);
                if ((fill_color & 0xFF000000) == 0) {
                    fill_color |= 0xFF000000;  /* Add alpha if not specified */
                }
            } else if (strncmp(name, "fill-rule", strlen("fill-rule")) == 0) {
                fill_rule = atoi(optarg);
            } else if (strncmp(name, "pool-enable", strlen("pool-enable")) == 0) {
                pool_enable = atoi(optarg);
            } else if (strncmp(name, "pool-size", strlen("pool-size")) == 0) {
                char *ep;
                unsigned long v = strtoul(optarg, &ep, 0);
                if (ep && (*ep == 'k' || *ep == 'K')) v *= 1024;
                else if (ep && (*ep == 'm' || *ep == 'M')) v *= 1024 * 1024;
                pool_size = (uint32_t)(v > 0 ? v : VECTOR_POOL_DEFAULT_SIZE);
            } else if (strncmp(name, "pool-block", strlen("pool-block")) == 0) {
                pool_block = atoi(optarg);
                if (pool_block < 256) pool_block = 256;
            }
            break;
        }
        case 'h':
            show_usage(argv[0]);
            return 0;
        default:
            show_usage(argv[0]);
            return -1;
        }
    }

    /* Validate mode */
    if (mode == MODE_UNKNOWN) {
        printf("Error: Mode is required\n");
        show_usage(argv[0]);
        return -1;
    }

    /* Create ArtVG context */
    printf("Opening ArtVG device...\n");
    vg = artvg_create();
    if (!vg) {
        printf("Failed to create ArtVG context\n");
        result = -1;
        goto cleanup;
    }

    /* Enable vector pool if requested */
    if (pool_enable) {
        uint32_t pool_blocks = pool_size / pool_block;
        if (pool_blocks < 2) pool_blocks = 2;
        printf("Enabling vector pool: %u blocks x %u bytes = %u bytes total\n",
               pool_blocks, pool_block, pool_blocks * pool_block);
        ret = artvg_enable_vector_buffer_pool(vg, pool_blocks, pool_block);
        if (ret != ARTVG_SUCCESS) {
            printf("Warning: Failed to enable vector pool (%d), falling back to heap\n", ret);
        } else {
            printf("Vector pool enabled successfully\n");
        }
    } else {
        printf("Vector pool disabled, using per-section heap allocation\n");
    }

    /* Open DRM device */
    printf("Opening DRM device (/dev/dri/card0)...\n");
    if (drm_device_open(&drm_dev, "/dev/dri/card0", -1) != 0) {
        printf("Failed to open DRM device\n");
        result = -1;
        goto cleanup;
    }

    printf("Display resolution: %ux%u\n", drm_dev.width, drm_dev.height);

    /* For extreme modes, calculate required initial capacity */
    size_t initial_capacity = 4096;
    if (mode == MODE_EXTREME_LINES || mode == MODE_EXTREME_SPIRAL ||
        mode == MODE_EXTREME_FRACTAL) {
        if (target_size > 0) {
            initial_capacity = (target_size / sizeof(float)) + 1024;
        } else {
            initial_capacity = (SIZE_100K / sizeof(float)) + 1024;
        }
    }

    /* Initialize path builder after getting screen resolution */
    printf("Initializing path builder (capacity: %zu floats)...\n", initial_capacity);
    if (builder_init(&builder, initial_capacity) != 0) {
        printf("Failed to initialize path builder\n");
        result = -1;
        goto cleanup;
    }

    /* Build path data with actual screen dimensions */
    printf("Building path for mode: %s\n", g_mode_info[mode - 1].name);
    if (build_path_for_mode(&builder, mode, shape_count, segments, lines, quads,
                            grid_rows, grid_cols, target_size, depth,
                            (float)drm_dev.width, (float)drm_dev.height) != 0) {
        printf("Failed to build path\n");
        builder_free(&builder);
        result = -1;
        goto cleanup;
    }

    /* Calculate statistics */
    size_t total_bytes = builder.count * sizeof(float);
    size_t section_data_size = pool_enable ? (pool_block - 8) : VECTOR_SECTION_SIZE;
    size_t estimated_sections = (total_bytes + section_data_size - 1) / section_data_size;

    printf("\nTest Configuration:\n");
    printf("  Mode: %s\n", g_mode_info[mode - 1].name);
    printf("  Description: %s\n", g_mode_info[mode - 1].description);
    printf("  Vector pool: %s\n", pool_enable ? "enabled" : "disabled");
    if (pool_enable) {
        printf("  Pool config: %u total, %u bytes/block\n", pool_size, pool_block);
    }
    printf("  Total commands: %zu\n", builder.count / 3);  /* Approximate */
    printf("  Total bytes: %zu\n", total_bytes);
    printf("  Estimated sections: %zu", estimated_sections);
    if (pool_enable) {
        printf(" (block data = %zu bytes)", section_data_size);
    }
    printf("\n");
    if (estimated_sections > 1) {
        printf("  Note: Data exceeds single section, vector chain will be used\n");
    }
    printf("\n");

    /* Initialize control structures */
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.alpha_en = 0;
    ctrl.alpha_rules = 0;


    memset(&vector_ctl, 0, sizeof(vector_ctl));
    vector_ctl.fill_rule = fill_rule;
    vector_ctl.curve_flat_limit = 10;

    /* Configure gradient for solid fill */
    gradient.colors[0] = fill_color;
    gradient.count = 1;
    gradient.stops[0] = 0;
    gradient.precision = ARTVG_GRADIENT_PRECISION_NONE;
    gradient.spread = ARTVG_GRADIENT_SPREAD_NONE;
    if (drm_buffer_to_mpp_buffer(&drm_dev, 0, &buf) != 0) {
        printf("Failed to convert DRM buffer\n");
        result = -1;
        goto cleanup;
    }

    if (artvg_add_dma_fd(vg, buf.fd[0]) != ARTVG_SUCCESS) {
        printf("Failed to add DMA FD\n");
        result = -1;
        goto cleanup;
    }

    /* Clear screen first */
    printf("Clearing screen...\n");
    ret = artvg_clear(vg, &buf, COLOR_BLACK);
    if (ret != ARTVG_SUCCESS) {
        printf("Failed to clear screen: %d\n", ret);
    }

    /* Build ArtVG path from builder data */
    printf("Creating path with %zu floats...\n", builder.count);
    path = parse_and_build_path(vg, builder.data, builder.count);
    if (!path) {
        printf("Failed to create path\n");
        result = -1;
        goto cleanup;
    }

    /* Dump vector chain debug info */
    dump_vector_chain_info(path);

    /* Setup identity matrix (no transformation for basic test) */
    artvg_matrix_identity(&matrix);

    /* Draw path */
    printf("Drawing path (fill color: 0x%08X, fill rule: %s)...\n",
           fill_color, fill_rule == ARTVG_FILL_EVEN_ODD ? "EVEN_ODD" : "NON_ZERO");
    ret = artvg_draw_path(vg, &buf, path, &matrix, &ctrl,
                          &gradient, &vector_ctl);
    if (ret != ARTVG_SUCCESS) {
        printf("Failed to draw path: %d\n", ret);
        result = -1;
        goto cleanup;
    }

    /* Dump commands for debugging */
    artvg_dumping_cmd(vg);

    /* Flush operations */
    printf("Flushing operations...\n");
    ret = artvg_flush(vg);
    if (ret != ARTVG_SUCCESS) {
        printf("Failed to flush: %d\n", ret);
        result = -1;
        goto cleanup;
    }

    /* Wait for completion */
    printf("Waiting for hardware completion...\n");
    ret = artvg_wait_finish(vg);
    if (ret != ARTVG_SUCCESS) {
        printf("Failed to wait finish: %d\n", ret);
        result = -1;
        goto cleanup;
    }

    /* Flush to display */
    if (drm_buffer_flush(&drm_dev, 0) != 0) {
        printf("Failed to flush DRM buffer\n");
    }

    if (drm_wait_vsync(&drm_dev) != 0) {
        printf("Failed to wait for vsync\n");
    }

    printf("\n=== Test Completed Successfully ===\n");
    printf("Press Enter to exit...\n");
    getchar();

cleanup:
    if (vg && path) {
        artvg_path_free(vg, path);
    }
    artvg_remove_dma_fd(vg, buf.fd[0]);
    if (vg) {
        if (pool_enable) {
            ret = artvg_disable_vector_buffer_pool(vg);
            if (ret != ARTVG_SUCCESS) {
                printf("Warning: Failed to disable vector pool (%d)\n", ret);
            } else {
                printf("Vector pool disabled\n");
            }
        }
        artvg_destroy(vg);
    }
    drm_device_close(&drm_dev);
    builder_free(&builder);

    return result;
}
