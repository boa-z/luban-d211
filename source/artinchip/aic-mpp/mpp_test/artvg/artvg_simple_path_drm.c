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
#include <stdint.h>
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

typedef enum {
    PATH_TYPE_TRIANGLE = 0,
    PATH_TYPE_QUAD_BEZIER,
    PATH_TYPE_CUBIC_BEZIER,
    PATH_TYPE_HEART,
    PATH_TYPE_WAVE,
    PATH_TYPE_FILL_RULE,
    PATH_TYPE_OFFSCREEN_TRIANGLE,
    PATH_TYPE_OFFSCREEN_HEART,
    PATH_TYPE_COUNT
} path_type_t;

typedef enum {
    FILL_MODE_SOLID = 0,
    FILL_MODE_GRADIENT,
    FILL_MODE_COUNT
} fill_mode_t;

/* Preset gradient types */
typedef enum {
    PRESET_GRADIENT_RED_BLUE = 0,
    PRESET_GRADIENT_BLUE_GREEN,
    PRESET_GRADIENT_RAINBOW,
    PRESET_GRADIENT_FIRE,
    PRESET_GRADIENT_OCEAN,
    PRESET_GRADIENT_COUNT
} preset_gradient_t;

/* Preset color types */
typedef enum {
    PRESET_COLOR_RED = 0,
    PRESET_COLOR_BLUE,
    PRESET_COLOR_GREEN,
    PRESET_COLOR_YELLOW,
    PRESET_COLOR_CYAN,
    PRESET_COLOR_MAGENTA,
    PRESET_COLOR_WHITE,
    PRESET_COLOR_BLACK,
    PRESET_COLOR_ORANGE,
    PRESET_COLOR_PURPLE,
    PRESET_COLOR_COUNT
} preset_color_t;

/* Path data for triangle: MOVE + LINE commands */
static float g_path_triangle[] = {
    (float)(VLC_OP_MOVE), 0.0f, 400.0f,    /* Move to (0, 400) */
    (float)(VLC_OP_LINE), 400.0f, 400.0f,  /* Line to (400, 400) */
    (float)(VLC_OP_LINE), 200.0f, 0.0f,    /* Line to (200, 0) */
    (float)(VLC_OP_LINE), 0.0f, 400.0f,    /* Line to (0, 400) */
};

/* Path data for quadratic bezier curves (reference NXP QuadraticCurves.c) */
static float g_path_quad_bezier[] = {
    (float)(VLC_OP_MOVE), 0.0f, 400.0f,         /* Move to (0, 400) */
    (float)(VLC_OP_LINE), 400.0f, 400.0f,       /* Line to (400, 400) */
    (float)(VLC_OP_QUAD), 400.0f, 200.0f, 200.0f, 0.0f,   /* Quad: control(400,200), end(200,0) */
    (float)(VLC_OP_QUAD), 0.0f, 200.0f, 0.0f, 400.0f,     /* Quad: control(0,200), end(0,400) */
};

/* Path data for cubic bezier curves (reference NXP CubicCurves.c)
 * This path draws a teardrop/leaf shape using cubic bezier curves.
 * Path coordinates: based on NXP's pathData (S32 format converted to FP32)
 */
static float g_path_cubic_bezier[] = {
    (float)(VLC_OP_MOVE), 200.0f, 400.0f,         /* Move to (200, 400) */
    (float)(VLC_OP_LINE), 300.0f, 300.0f,         /* Line to (300, 300) */
    (float)(VLC_OP_CUBIC), 254.0f, 228.0f, 365.0f, 190.0f, 300.0f, 100.0f,  /* Cubic: ctrl1(254,228), ctrl2(365,190), end(300,100) */
    (float)(VLC_OP_CUBIC), 300.0f, 197.0f, 200.0f, 106.0f, 200.0f, 0.0f,    /* Cubic: ctrl1(300,197), ctrl2(200,106), end(200,0) */
    (float)(VLC_OP_CUBIC), 132.0f, 0.0f, 158.0f, 187.0f, 100.0f, 100.0f,    /* Cubic: ctrl1(132,0), ctrl2(158,187), end(100,100) */
    (float)(VLC_OP_CUBIC), 0.0f, 100.0f, 200.0f, 300.0f, 100.0f, 300.0f,    /* Cubic: ctrl1(0,100), ctrl2(200,300), end(100,300) */
    (float)(VLC_OP_LINE), 200.0f, 400.0f,         /* Line to (200, 400) - close the path */
};

/* Path data for heart shape using bezier curves */
static float g_path_heart[] = {
    (float)(VLC_OP_MOVE), 200.0f, 100.0f,       /* Move to (200, 100) */
    (float)(VLC_OP_CUBIC), 200.0f, 50.0f, 150.0f, 0.0f, 100.0f, 0.0f,       /* Left top curve */
    (float)(VLC_OP_CUBIC), 50.0f, 0.0f, 0.0f, 50.0f, 0.0f, 100.0f,          /* Left bottom curve */
    (float)(VLC_OP_CUBIC), 0.0f, 180.0f, 100.0f, 280.0f, 200.0f, 380.0f,    /* Left side to bottom */
    (float)(VLC_OP_CUBIC), 300.0f, 280.0f, 400.0f, 180.0f, 400.0f, 100.0f,  /* Right side */
    (float)(VLC_OP_CUBIC), 400.0f, 50.0f, 350.0f, 0.0f, 300.0f, 0.0f,       /* Right bottom curve */
    (float)(VLC_OP_CUBIC), 250.0f, 0.0f, 200.0f, 50.0f, 200.0f, 100.0f,     /* Right top curve */
};

/* Path data for wave shape using multiple quadratic bezier curves */
static float g_path_wave[] = {
    (float)(VLC_OP_MOVE), 0.0f, 200.0f,         /* Move to (0, 200) */
    (float)(VLC_OP_QUAD), 50.0f, 100.0f, 100.0f, 200.0f,    /* Wave 1 up */
    (float)(VLC_OP_QUAD), 150.0f, 300.0f, 200.0f, 200.0f,   /* Wave 1 down */
    (float)(VLC_OP_QUAD), 250.0f, 100.0f, 300.0f, 200.0f,   /* Wave 2 up */
    (float)(VLC_OP_QUAD), 350.0f, 300.0f, 400.0f, 200.0f,   /* Wave 2 down */
    (float)(VLC_OP_LINE), 400.0f, 400.0f,       /* Line to bottom right */
    (float)(VLC_OP_LINE), 0.0f, 400.0f,         /* Line to bottom left */
    (float)(VLC_OP_LINE), 0.0f, 200.0f,         /* Close path */
};

/* Path data for fill rule testing (two intersecting triangles)
 * This path demonstrates the difference between NON_ZERO and EVEN_ODD fill rules.
 * First triangle: (0,400) -> (200,0) -> (400,400) -> (0,400)
 * Second triangle: (0,0) -> (400,0) -> (200,400) -> (0,0)
 * The intersection area will be filled differently based on fill rule.
 */
static float g_path_fill_rule[] = {
    /* First triangle */
    (float)(VLC_OP_MOVE), 0.0f, 400.0f,         /* Move to (0, 400) */
    (float)(VLC_OP_LINE), 200.0f, 0.0f,         /* Line to (200, 0) */
    (float)(VLC_OP_LINE), 400.0f, 400.0f,       /* Line to (400, 400) */
    (float)(VLC_OP_LINE), 0.0f, 400.0f,         /* Close first triangle */
    /* Second triangle (intersecting with first) */
    (float)(VLC_OP_MOVE), 0.0f, 0.0f,           /* Move to (0, 0) */
    (float)(VLC_OP_LINE), 400.0f, 0.0f,         /* Line to (400, 0) */
    (float)(VLC_OP_LINE), 200.0f, 400.0f,       /* Line to (200, 400) */
    (float)(VLC_OP_LINE), 0.0f, 0.0f,           /* Close second triangle */
};

/* Offscreen triangle: starts at negative coordinates, partially outside top-left */
static float g_path_offscreen_triangle[] = {
    (float)(VLC_OP_MOVE), -100.0f, 300.0f,
    (float)(VLC_OP_LINE), 300.0f, 300.0f,
    (float)(VLC_OP_LINE), 100.0f, -100.0f,
    (float)(VLC_OP_LINE), -100.0f, 300.0f,
};

/* Offscreen heart: shifted top-left by 100px, partially outside screen */
static float g_path_offscreen_heart[] = {
    (float)(VLC_OP_MOVE), 100.0f, 0.0f,
    (float)(VLC_OP_CUBIC), 100.0f, -50.0f, 50.0f, -100.0f, 0.0f, -100.0f,
    (float)(VLC_OP_CUBIC), -50.0f, -100.0f, -100.0f, -50.0f, -100.0f, 0.0f,
    (float)(VLC_OP_CUBIC), -100.0f, 80.0f, 0.0f, 180.0f, 100.0f, 280.0f,
    (float)(VLC_OP_CUBIC), 200.0f, 180.0f, 300.0f, 80.0f, 300.0f, 0.0f,
    (float)(VLC_OP_CUBIC), 300.0f, -50.0f, 250.0f, -100.0f, 200.0f, -100.0f,
    (float)(VLC_OP_CUBIC), 150.0f, -100.0f, 100.0f, -50.0f, 100.0f, 0.0f,
};

/* Path info structure */
typedef struct {
    const char *name;
    float *data;
    size_t size;
    float min_x, min_y, max_x, max_y;
    float default_center[2];
} path_info_t;

static path_info_t g_path_info[PATH_TYPE_COUNT] = {
    [PATH_TYPE_TRIANGLE] = {
        .name = "triangle",
        .data = g_path_triangle,
        .size = sizeof(g_path_triangle),
        .min_x = 0.0f, .min_y = 0.0f, .max_x = 400.0f, .max_y = 400.0f,
        .default_center = {200.0f, 200.0f}
    },
    [PATH_TYPE_QUAD_BEZIER] = {
        .name = "quad_bezier",
        .data = g_path_quad_bezier,
        .size = sizeof(g_path_quad_bezier),
        .min_x = 0.0f, .min_y = 0.0f, .max_x = 400.0f, .max_y = 400.0f,
        .default_center = {200.0f, 200.0f}
    },
    [PATH_TYPE_CUBIC_BEZIER] = {
        .name = "cubic_bezier",
        .data = g_path_cubic_bezier,
        .size = sizeof(g_path_cubic_bezier),
        .min_x = 0.0f, .min_y = 0.0f, .max_x = 400.0f, .max_y = 400.0f,
        .default_center = {200.0f, 200.0f}
    },
    [PATH_TYPE_HEART] = {
        .name = "heart",
        .data = g_path_heart,
        .size = sizeof(g_path_heart),
        .min_x = 0.0f, .min_y = 0.0f, .max_x = 400.0f, .max_y = 380.0f,
        .default_center = {200.0f, 190.0f}
    },
    [PATH_TYPE_WAVE] = {
        .name = "wave",
        .data = g_path_wave,
        .size = sizeof(g_path_wave),
        .min_x = 0.0f, .min_y = 100.0f, .max_x = 400.0f, .max_y = 400.0f,
        .default_center = {200.0f, 250.0f}
    },
    [PATH_TYPE_FILL_RULE] = {
        .name = "fill_rule",
        .data = g_path_fill_rule,
        .size = sizeof(g_path_fill_rule),
        .min_x = 0.0f, .min_y = 0.0f, .max_x = 400.0f, .max_y = 400.0f,
        .default_center = {200.0f, 200.0f}
    },
    [PATH_TYPE_OFFSCREEN_TRIANGLE] = {
        .name = "offscreen_triangle",
        .data = g_path_offscreen_triangle,
        .size = sizeof(g_path_offscreen_triangle),
        .min_x = -100.0f, .min_y = -100.0f, .max_x = 300.0f, .max_y = 300.0f,
        .default_center = {100.0f, 100.0f}
    },
    [PATH_TYPE_OFFSCREEN_HEART] = {
        .name = "offscreen_heart",
        .data = g_path_offscreen_heart,
        .size = sizeof(g_path_offscreen_heart),
        .min_x = -100.0f, .min_y = -100.0f, .max_x = 300.0f, .max_y = 280.0f,
        .default_center = {100.0f, 90.0f}
    },
};

static const char *g_preset_color_names[PRESET_COLOR_COUNT] = {
    "red", "blue", "green", "yellow", "cyan", "magenta", "white", "black", "orange", "purple"
};

static uint32_t g_preset_colors[PRESET_COLOR_COUNT] = {
    COLOR_RED, COLOR_BLUE, COLOR_GREEN, COLOR_YELLOW, COLOR_CYAN,
    COLOR_MAGENTA, COLOR_WHITE, COLOR_BLACK, COLOR_ORANGE, COLOR_PURPLE
};

static const char *g_preset_gradient_names[PRESET_GRADIENT_COUNT] = {
    "red_blue", "blue_green", "rainbow", "fire", "ocean"
};

/* Path structure */
static artvg_path_t *g_path = NULL;

static void show_usage(const char *program_name);

static path_type_t parse_path_type(const char *name)
{
    for (int i = 0; i < PATH_TYPE_COUNT; i++) {
        if (strncmp(name, g_path_info[i].name, strlen(g_path_info[i].name) + 1) == 0) {
            return i;
        }
    }
    return PATH_TYPE_TRIANGLE;
}

static preset_color_t parse_preset_color(const char *name)
{
    for (int i = 0; i < PRESET_COLOR_COUNT; i++) {
        if (strncmp(name, g_preset_color_names[i], strlen(g_preset_color_names[i]) + 1) == 0) {
            return i;
        }
    }
    return PRESET_COLOR_RED;
}

static preset_gradient_t parse_preset_gradient(const char *name)
{
    for (int i = 0; i < PRESET_GRADIENT_COUNT; i++) {
        if (strncmp(name, g_preset_gradient_names[i], strlen(g_preset_gradient_names[i]) + 1) == 0) {
            return i;
        }
    }
    return PRESET_GRADIENT_RED_BLUE;
}

static void apply_preset_gradient(artvg_gradient_t *gradient, preset_gradient_t preset)
{
    switch (preset) {
    case PRESET_GRADIENT_RED_BLUE:
        gradient->colors[0] = COLOR_RED;
        gradient->colors[1] = COLOR_BLUE;
        gradient->count = 2;
        gradient->stops[0] = 0;
        gradient->stops[1] = 255;
        break;
    case PRESET_GRADIENT_BLUE_GREEN:
        gradient->colors[0] = COLOR_BLUE;
        gradient->colors[1] = COLOR_GREEN;
        gradient->count = 2;
        gradient->stops[0] = 0;
        gradient->stops[1] = 255;
        break;
    case PRESET_GRADIENT_RAINBOW:
        gradient->colors[0] = COLOR_RED;
        gradient->colors[1] = COLOR_YELLOW;
        gradient->colors[2] = COLOR_GREEN;
        gradient->colors[3] = COLOR_CYAN;
        gradient->colors[4] = COLOR_BLUE;
        gradient->colors[5] = COLOR_MAGENTA;
        gradient->count = 6;
        gradient->stops[0] = 0;
        gradient->stops[1] = 51;
        gradient->stops[2] = 102;
        gradient->stops[3] = 153;
        gradient->stops[4] = 204;
        gradient->stops[5] = 255;
        break;
    case PRESET_GRADIENT_FIRE:
        gradient->colors[0] = COLOR_RED;
        gradient->colors[1] = COLOR_ORANGE;
        gradient->colors[2] = COLOR_YELLOW;
        gradient->count = 3;
        gradient->stops[0] = 0;
        gradient->stops[1] = 128;
        gradient->stops[2] = 255;
        break;
    case PRESET_GRADIENT_OCEAN:
        gradient->colors[0] = COLOR_BLUE;
        gradient->colors[1] = COLOR_CYAN;
        gradient->colors[2] = 0xFF40E0D0; /* Turquoise */
        gradient->count = 3;
        gradient->stops[0] = 0;
        gradient->stops[1] = 128;
        gradient->stops[2] = 255;
        break;
    default:
        gradient->colors[0] = COLOR_RED;
        gradient->count = 1;
        gradient->stops[0] = 0;
        break;
    }
}

static artvg_path_t *parse_and_build_path(struct artvg *vg, const float *data, size_t size)
{
    artvg_error_t ret;
    int count = size / sizeof(float);
    int i = 0;

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
            ret = artvg_path_cubic_to(vg, path, data[i+1], data[i+2], data[i+3], data[i+4], data[i+5], data[i+6]);
            i += 7;
            break;
        default:
            i++;
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


/* Parse comma-separated float list */
static int parse_float_list(const char *str, float *values, int max_count)
{
    const char *token;
    char *str_copy = strdup(str);
    char *saveptr = NULL;
    int count = 0;

    if (!str_copy) return -1;

    token = strtok_r(str_copy, ",", &saveptr);
    while (token && count < max_count) {
        values[count++] = atof(token);
        token = strtok_r(NULL, ",", &saveptr);
    }

    free(str_copy);
    return count;
}

/* Create transformation matrix */
static void create_transform_matrix(artvg_matrix_t *matrix,
                                   const float *translate,
                                   const float *scale,
                                   float rotate_angle,
                                   const float *skew,
                                   const float *center,
                                   const char *order)
{
    float cx = center[0];
    float cy = center[1];

    artvg_matrix_identity(matrix);

    if (!order || strlen(order) == 0) {
        order = "trs";
    }

    for (int i = 0; order[i] != '\0'; i++) {
        switch (order[i]) {
        case 't':
            if (translate[0] != 0.0f || translate[1] != 0.0f) {
                artvg_matrix_translate(translate[0], translate[1], matrix);
            }
            break;

        case 's':
            if (scale[0] != 1.0f || scale[1] != 1.0f) {
                artvg_matrix_scale(scale[0], scale[1], matrix);
            }
            break;

        case 'r':
            if (rotate_angle != 0.0f) {
                if (cx != 0.0f || cy != 0.0f) {
                    artvg_point_t center_point = {cx, cy};
                    artvg_matrix_rotate_point(&center_point, rotate_angle, matrix);
                } else {
                    artvg_matrix_rotate(rotate_angle, matrix);
                }
            }
            break;

        case 'k':
            if (skew[0] != 0.0f || skew[1] != 0.0f) {
                artvg_matrix_skew(skew[0], skew[1], matrix);
            }
            break;

        default:
            printf("Warning: Unknown transformation order character '%c'\n", order[i]);
            continue;
        }
    }
}

/* Print matrix */
static void print_matrix(const artvg_matrix_t *matrix, const char *name)
{
    printf("%s matrix:\n", name);
    for (int i = 0; i < 3; i++) {
        printf("  [ ");
        for (int j = 0; j < 3; j++) {
            printf("%8.4f ", matrix->m[i][j]);
        }
        printf("]\n");
    }
}

/* Helper function to parse 2-float parameter */
static int parse_float2(const char *str, float *values, const char *param_name,
                        const char *program_name)
{
    if (parse_float_list(str, values, 2) != 2) {
        printf("Error: %s requires 2 comma-separated floats\n", param_name);
        show_usage(program_name);
        return -1;
    }
    return 0;
}

static void show_usage(const char *program_name)
{
    printf("Usage: %s [options]\n", program_name);
    printf("\nSimple Path Drawing Test - Draws paths with configurable transformations and fill modes\n");

    printf("\n=== Path Selection ===\n");
    printf("  --path-type <type>        Select preset path type (default: triangle)\n");
    printf("                            Available: triangle, quad_bezier, cubic_bezier, heart, wave, fill_rule,\n");
    printf("                                      offscreen_triangle, offscreen_heart\n");

    printf("\n=== Fill Mode ===\n");
    printf("  --fill-mode <mode>        Fill mode: solid, gradient (default: solid)\n");

    printf("\n=== Solid Fill Parameters ===\n");
    printf("  --solid-color <0xAARRGGBB>  Solid fill color (default: 0xFFFF0000 red)\n");
    printf("  --preset-color <name>       Preset color: red, blue, green, yellow, cyan, magenta, white, black, orange, purple\n");

    printf("\n=== Gradient Fill Parameters ===\n");
    printf("  --gradient                  Enable gradient fill (same as --fill-mode gradient)\n");
    printf("  --color1 <0xAARRGGBB>       Gradient start color (default: 0xFFFF0000)\n");
    printf("  --color2 <0xAARRGGBB>       Gradient end color (default: 0xFF0000FF)\n");
    printf("  --gradient-start <x,y>      Gradient start point (default: auto)\n");
    printf("  --gradient-end <x,y>        Gradient end point (default: auto)\n");
    printf("  --preset-gradient <name>    Preset gradient: red_blue, blue_green, rainbow, fire, ocean\n");

    printf("\n=== Transformation Parameters ===\n");
    printf("  --matrix-direct <m00,m01,m02,m10,m11,m12,m20,m21,m22>\n");
    printf("                        Direct 3x3 matrix specification\n");
    printf("  --translate <tx,ty>   Translation (default: 0,0)\n");
    printf("  --scale <sx,sy>       Scaling (default: 1.0,1.0)\n");
    printf("  --rotate <angle>      Rotation angle in degrees (default: 0.0)\n");
    printf("  --skew <kx,ky>        Skew/shear (default: 0.0,0.0)\n");
    printf("  --center <cx,cy>      Rotation center point (default: path center)\n");
    printf("  --transform-order <order> Transformation order (default: 'trs')\n");

    printf("\n=== Edge Fill Control Parameters ===\n");
    printf("  --fill-bg-en <0|1>    Enable background fill (default: 0)\n");
    printf("  --blend-edge-en <0|1> Enable edge blending (default: 0)\n");
    printf("  --edge-color <hex>    Edge fill color (default: 0xFF000000)\n");
    printf("  --edge-pad-mode <0-2> Edge fill mode (default: 0)\n");

    printf("\n=== Vector Control Parameters ===\n");
    printf("  --fill-rule <0|1>     Fill rule: 0=NON_ZERO (default), 1=EVEN_ODD\n");
    printf("  --curve-flat-limit <0-255> Curve flattening limit (default: 10)\n");

    printf("\n=== Blend Control Parameters ===\n");
    printf("  --blend-en <0|1>      Enable/disable alpha blending (default: 0)\n");
    printf("  --blend-mode <0-16>   Blend mode (default: 3=src_over):\n");
    printf("                        0=default, 1=clear, 2=src, 3=src_over, 4=dst_over, 5=src_in\n");
    printf("                        6=dst_in, 7=src_out, 8=dst_out, 9=src_atop, 10=dst_atop\n");
    printf("                        11=add, 12=xor, 13=dst, 14=arith_add, 15=arith_sub, 16=arith_mul\n");
    printf("  --src-alpha-mode <0-2>  Src alpha mode: 0=pixel (default), 1=global, 2=mixed\n");
    printf("  --src-global-alpha <0-255>  Src global alpha value (default: 255)\n");


    printf("\n=== Dst Crop Parameters ===\n");
    printf("  --dst-crop-en <0|1>   Enable/disable dst crop (default: 0)\n");
    printf("  --dst-crop <x,y,w,h>  Dst crop region (default: 0,0,0,0)\n");

    printf("\n=== Edge Buffer Pool Parameters ===\n");
    printf("  --edge-pool <0|1>        Enable edge buffer pool (default: 1)\n");
    printf("  --edge-pool-size <bytes> Edge buffer pool size (default: 524288)\n");

    printf("\n=== Clear Buffer Parameters ===\n");
    printf("  --clear-en <0|1>      Enable/disable clear buffer before draw (default: 1)\n");
    printf("  --clear-color <0xAARRGGBB>  Clear color (default: 0xFF000000)\n");

    printf("\n=== Help ===\n");
    printf("  --help                Show this help message\n");

    printf("\n=== Examples ===\n");
    printf("  1. Default triangle with solid red:\n");
    printf("     %s\n", program_name);
    printf("  2. Quadratic bezier with solid blue:\n");
    printf("     %s --path-type quad_bezier --preset-color blue\n", program_name);
    printf("  3. Cubic bezier with gradient:\n");
    printf("     %s --path-type cubic_bezier --fill-mode gradient\n", program_name);
    printf("  4. Heart shape with fire gradient:\n");
    printf("     %s --path-type heart --preset-gradient fire\n", program_name);
    printf("  5. Fill rule test (two intersecting triangles) with EVEN_ODD (center hollow):\n");
    printf("     %s --path-type fill_rule --fill-rule 1\n", program_name);
    printf("  6. Wave with custom gradient colors:\n");
    printf("     %s --path-type wave --gradient --color1 0xFFFF00FF --color2 0xFF00FFFF\n", program_name);
}

int main(int argc, char **argv)
{
    struct artvg *vg = NULL;
    struct mpp_buf buf = {0};
    drm_dev_t drm_dev = {0};
    artvg_matrix_t matrix;
    artvg_ctrl_t ctrl;
    artvg_vector_ctl_t vector_ctl;
    artvg_gradient_t gradient = {0};
    artvg_error_t ret;
    int result = 0;

    /* Path and fill configuration */
    path_type_t path_type = PATH_TYPE_TRIANGLE;
    fill_mode_t fill_mode = FILL_MODE_SOLID;
    uint32_t solid_color = COLOR_RED;

    /* Gradient configuration */
    int gradient_start_set = 0;
    int gradient_end_set = 0;
    float gradient_start[2] = {0.0f, 0.0f};
    float gradient_end[2] = {400.0f, 400.0f};
    preset_gradient_t preset_gradient = PRESET_GRADIENT_RED_BLUE;
    int use_preset_gradient = 0;

    /* Transformation parameters */
    float translate[2] = {0.0f, 0.0f};
    float scale[2] = {1.0f, 1.0f};
    float rotate_angle = 0.0f;
    float skew[2] = {0.0f, 0.0f};
    float center[2] = {200.0f, 200.0f};
    int center_set = 0;
    char transform_order[10] = "trs";
    float direct_matrix[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    int use_direct_matrix = 0;

    printf("=== ArtVG Simple Path Drawing Test ===\n\n");

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.alpha_en = 0;
    ctrl.alpha_rules = 0;

    memset(&vector_ctl, 0, sizeof(vector_ctl));
    vector_ctl.fill_rule = ARTVG_FILL_NON_ZERO;
    vector_ctl.curve_flat_limit = 10;

    /* Blend parameters */
    int blend_en = 0;
    int blend_mode = 3;
    int src_alpha_mode = 0;
    int src_global_alpha = 255;

    /* Dst crop parameters */
    int dst_crop_en = 0;
    int dst_crop_x = 0, dst_crop_y = 0, dst_crop_w = 0, dst_crop_h = 0;
    int edge_pool_enabled = 1;
    uint32_t edge_pool_size = 512 * 1024;
    int clear_en = 1;
    uint32_t clear_color = COLOR_BLACK;

    static struct option long_options[] = {
        /* Path selection */
        {"path-type", required_argument, 0, 0},

        /* Fill mode */
        {"fill-mode", required_argument, 0, 0},
        {"solid-color", required_argument, 0, 0},
        {"preset-color", required_argument, 0, 0},
        {"gradient", no_argument, 0, 0},
        {"color1", required_argument, 0, 0},
        {"color2", required_argument, 0, 0},
        {"gradient-start", required_argument, 0, 0},
        {"gradient-end", required_argument, 0, 0},
        {"preset-gradient", required_argument, 0, 0},

        /* Transformation */
        {"matrix-direct", required_argument, 0, 0},
        {"translate", required_argument, 0, 0},
        {"scale", required_argument, 0, 0},
        {"rotate", required_argument, 0, 0},
        {"skew", required_argument, 0, 0},
        {"center", required_argument, 0, 0},
        {"transform-order", required_argument, 0, 0},

        /* Edge fill control */
        {"fill-bg-en", required_argument, 0, 0},
        {"blend-edge-en", required_argument, 0, 0},
        {"edge-color", required_argument, 0, 0},
        {"edge-pad-mode", required_argument, 0, 0},

        /* Vector control */
        {"fill-rule", required_argument, 0, 0},
        {"curve-flat-limit", required_argument, 0, 0},

        /* Blend control */
        {"blend-en", required_argument, 0, 0},
        {"blend-mode", required_argument, 0, 0},
        {"src-alpha-mode", required_argument, 0, 0},
        {"src-global-alpha", required_argument, 0, 0},

        /* Dst crop */
        {"dst-crop-en", required_argument, 0, 0},
        {"dst-crop", required_argument, 0, 0},
        {"edge-pool", required_argument, 0, 0},
        {"edge-pool-size", required_argument, 0, 0},
        {"clear-en", required_argument, 0, 0},
        {"clear-color", required_argument, 0, 0},

        /* Help */
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while (1) {
        opt = getopt_long(argc, argv, "h", long_options, &option_index);
        if (opt == -1)
            break;

        switch (opt) {
        case 0: {
            const char *name = long_options[option_index].name;

            if (strncmp(name, "path-type", strlen("path-type")) == 0) {
                path_type = parse_path_type(optarg);
                if (path_type < 0 || path_type >= PATH_TYPE_COUNT) {
                    printf("Error: Unknown path type '%s'\n", optarg);
                    show_usage(argv[0]);
                    return -1;
                }
            }
            else if (strncmp(name, "fill-mode", strlen("fill-mode")) == 0) {
                if (strncmp(optarg, "solid", strlen("solid")) == 0) {
                    fill_mode = FILL_MODE_SOLID;
                } else if (strncmp(optarg, "gradient", strlen("gradient")) == 0) {
                    fill_mode = FILL_MODE_GRADIENT;
                } else {
                    printf("Error: Unknown fill mode '%s'\n", optarg);
                    show_usage(argv[0]);
                    return -1;
                }
            }
            else if (strncmp(name, "solid-color", strlen("solid-color")) == 0) {
                solid_color = strtoul(optarg, NULL, 0);
            }
            else if (strncmp(name, "preset-color", strlen("preset-color")) == 0) {
                solid_color = g_preset_colors[parse_preset_color(optarg)];
            }
            else if (strncmp(name, "gradient-start", strlen("gradient-start")) == 0) {
                if (parse_float2(optarg, gradient_start, "Gradient start", argv[0]) != 0)
                    return -1;
                gradient_start_set = 1;
            }
            else if (strncmp(name, "gradient-end", strlen("gradient-end")) == 0) {
                if (parse_float2(optarg, gradient_end, "Gradient end", argv[0]) != 0)
                    return -1;
                gradient_end_set = 1;
            }
            else if (strncmp(name, "gradient", strlen("gradient")) == 0) {
                fill_mode = FILL_MODE_GRADIENT;
            }
            else if (strncmp(name, "color1", strlen("color1")) == 0) {
                gradient.colors[0] = strtoul(optarg, NULL, 0);
            }
            else if (strncmp(name, "color2", strlen("color2")) == 0) {
                gradient.colors[1] = strtoul(optarg, NULL, 0);
            }
            else if (strncmp(name, "preset-gradient", strlen("preset-gradient")) == 0) {
                preset_gradient = parse_preset_gradient(optarg);
                use_preset_gradient = 1;
                fill_mode = FILL_MODE_GRADIENT;
            }
            else if (strncmp(name, "matrix-direct", strlen("matrix-direct")) == 0) {
                if (parse_float_list(optarg, direct_matrix, 9) != 9) {
                    printf("Error: Matrix requires 9 comma-separated floats\n");
                    show_usage(argv[0]);
                    return -1;
                }
                use_direct_matrix = 1;
            }
            else if (strncmp(name, "translate", strlen("translate")) == 0) {
                if (parse_float2(optarg, translate, "Translation", argv[0]) != 0)
                    return -1;
            }
            else if (strncmp(name, "scale", strlen("scale")) == 0) {
                if (parse_float2(optarg, scale, "Scaling", argv[0]) != 0)
                    return -1;
            }
            else if (strncmp(name, "rotate", strlen("rotate")) == 0) {
                rotate_angle = atof(optarg);
            }
            else if (strncmp(name, "skew", strlen("skew")) == 0) {
                if (parse_float2(optarg, skew, "Skew", argv[0]) != 0)
                    return -1;
            }
            else if (strncmp(name, "center", strlen("center")) == 0) {
                if (parse_float2(optarg, center, "Center point", argv[0]) != 0)
                    return -1;
                center_set = 1;
            }
            else if (strncmp(name, "transform-order", strlen("transform-order")) == 0) {
                strncpy(transform_order, optarg, sizeof(transform_order) - 1);
                transform_order[sizeof(transform_order) - 1] = '\0';
            }
            else if (strncmp(name, "fill-rule", strlen("fill-rule")) == 0) {
                vector_ctl.fill_rule = atoi(optarg);
            }
            else if (strncmp(name, "curve-flat-limit", strlen("curve-flat-limit")) == 0) {
                vector_ctl.curve_flat_limit = atoi(optarg);
            }
            else if (strncmp(name, "blend-en", strlen("blend-en")) == 0) {
                blend_en = atoi(optarg);
            }
            else if (strncmp(name, "blend-mode", strlen("blend-mode")) == 0) {
                blend_mode = atoi(optarg);
            }
            else if (strncmp(name, "src-alpha-mode", strlen("src-alpha-mode")) == 0) {
                src_alpha_mode = atoi(optarg);
            }
            else if (strncmp(name, "src-global-alpha", strlen("src-global-alpha")) == 0) {
                src_global_alpha = atoi(optarg);
            }
            else if (strncmp(name, "dst-crop-en", strlen("dst-crop-en")) == 0) {
                dst_crop_en = atoi(optarg);
            }
            else if (strncmp(name, "dst-crop", strlen("dst-crop")) == 0) {
                float crop_vals[4] = {0};
                if (parse_float_list(optarg, crop_vals, 4) != 4) {
                    printf("Error: Dst crop requires 4 comma-separated values (x,y,w,h)\n");
                    show_usage(argv[0]);
                    return -1;
                }
                dst_crop_x = (int)crop_vals[0];
                dst_crop_y = (int)crop_vals[1];
                dst_crop_w = (int)crop_vals[2];
                dst_crop_h = (int)crop_vals[3];
            }
            else if (strncmp(name, "edge-pool-size", strlen("edge-pool-size")) == 0) {
                edge_pool_size = (uint32_t)atoi(optarg);
            }
            else if (strncmp(name, "edge-pool", strlen("edge-pool")) == 0) {
                edge_pool_enabled = atoi(optarg);
            }
            else if (strncmp(name, "clear-en", strlen("clear-en")) == 0) {
                clear_en = atoi(optarg);
            }
            else if (strncmp(name, "clear-color", strlen("clear-color")) == 0) {
                clear_color = strtoul(optarg, NULL, 0);
            }
            break;
        }

        case 'h':
            show_usage(argv[0]);
            return 0;

        default:
            printf("Unknown option, use --help for usage\n");
            show_usage(argv[0]);
            return -1;
        }
    }


    /* Use path's default center if not set by user */
    if (!center_set) {
        center[0] = g_path_info[path_type].default_center[0];
        center[1] = g_path_info[path_type].default_center[1];
    }

    /* Configure gradient */
    if (fill_mode == FILL_MODE_GRADIENT) {
        if (use_preset_gradient) {
            apply_preset_gradient(&gradient, preset_gradient);
        } else {
            /* Use default 2-color gradient if not set */
            if (gradient.count == 0) {
                gradient.colors[0] = COLOR_RED;
                gradient.colors[1] = COLOR_BLUE;
                gradient.count = 2;
                gradient.stops[0] = 0;
                gradient.stops[1] = 255;
            }
        }

        /* Set gradient points */
        if (gradient_start_set) {
            gradient.start.x = (int32_t)gradient_start[0];
            gradient.start.y = (int32_t)gradient_start[1];
        } else {
            gradient.start.x = (int32_t)g_path_info[path_type].min_x;
            gradient.start.y = (int32_t)g_path_info[path_type].min_y;
        }

        if (gradient_end_set) {
            gradient.end.x = (int32_t)gradient_end[0];
            gradient.end.y = (int32_t)gradient_end[1];
        } else {
            gradient.end.x = (int32_t)g_path_info[path_type].max_x;
            gradient.end.y = (int32_t)g_path_info[path_type].max_y;
        }

        gradient.precision = ARTVG_GRADIENT_PRECISION_256;
        gradient.spread = ARTVG_GRADIENT_SPREAD_PAD;

        /* Transform gradient points to match path transformation */
        if (!use_direct_matrix) {
            artvg_transform_gradient_points(&gradient, &matrix);
        }
    } else {
        /* Solid fill: use single color with NONE precision */
        gradient.colors[0] = solid_color;
        gradient.count = 1;
        gradient.stops[0] = 0;
        gradient.precision = ARTVG_GRADIENT_PRECISION_NONE;
        gradient.spread = ARTVG_GRADIENT_SPREAD_NONE;
    }

    printf("Path type: %s\n", g_path_info[path_type].name);
    printf("Fill mode: %s\n", fill_mode == FILL_MODE_SOLID ? "solid" : "gradient");

    if (fill_mode == FILL_MODE_SOLID) {
        printf("Solid color: 0x%08X\n", solid_color);
    } else {
        printf("Gradient colors (%u):\n", gradient.count);
        for (uint32_t i = 0; i < gradient.count; i++) {
            printf("  Color[%u]: 0x%08X at %u (%.1f%%)\n", i, gradient.colors[i],
                   gradient.stops[i], gradient.stops[i] * 100.0f / 255.0f);
        }
        printf("Gradient start: (%d, %d)\n", gradient.start.x, gradient.start.y);
        printf("Gradient end: (%d, %d)\n", gradient.end.x, gradient.end.y);
    }

    printf("\nOpening ArtVG device...\n");
    vg = artvg_create();
    if (!vg) {
        printf("Failed to open artvg\n");
        return -1;
    }

    if (edge_pool_enabled) {
        printf("Enabling edge buffer pool (size: %u)...\n", edge_pool_size);
        ret = artvg_enable_edge_buffer_pool(vg, edge_pool_size);
        if (ret != ARTVG_SUCCESS) {
            printf("Warning: artvg_enable_edge_buffer_pool failed: %d\n", ret);
        }
    }

    printf("Opening DRM device (/dev/dri/card0)...\n");
    if (drm_device_open(&drm_dev, "/dev/dri/card0", -1) != 0) {
        printf("Failed to open DRM device\n");
        result = -1;
        goto cleanup;
    }

    printf("Display resolution: %ux%u\n", drm_dev.width, drm_dev.height);

    printf("Getting DRM buffer...\n");
    if (drm_buffer_to_mpp_buffer(&drm_dev, 0, &buf) != 0) {
        printf("Failed to convert DRM buffer to MPP buffer\n");
        result = -1;
        goto cleanup;
    }

    printf("Adding DMA FD...\n");
    if (artvg_add_dma_fd(vg, buf.fd[0]) != ARTVG_SUCCESS) {
        printf("Failed to add DMA FD\n");
        result = -1;
        goto cleanup;
    }

    printf("Creating %s path...\n", g_path_info[path_type].name);
    path_info_t *info = &g_path_info[path_type];
    g_path = parse_and_build_path(vg, info->data, info->size);
    if (!g_path) {
        printf("Failed to create path\n");
        result = -1;
        goto cleanup;
    }
    if (clear_en) {
        printf("Clearing buffer with color 0x%08X...\n", clear_color);
        ret = artvg_clear(vg, &buf, clear_color);
        if (ret != ARTVG_SUCCESS) {
            printf("Failed to clear buffer: %d\n", ret);
            result = -1;
            goto cleanup;
        }
    }

    /* Apply dst crop settings */
    buf.crop_en = dst_crop_en;
    if (dst_crop_en) {
        buf.crop.x = dst_crop_x;
        buf.crop.y = dst_crop_y;
        buf.crop.width = dst_crop_w;
        buf.crop.height = dst_crop_h;
    }
    /* Apply blend settings to ctrl */
    ctrl.alpha_en = blend_en;
    ctrl.alpha_rules = blend_mode;
    ctrl.src_alpha_mode = src_alpha_mode;
    ctrl.src_global_alpha = src_global_alpha;
    if (use_direct_matrix) {
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                matrix.m[i][j] = direct_matrix[i * 3 + j];
            }
        }
        printf("Using directly specified matrix\n");
    } else {
        create_transform_matrix(&matrix, translate, scale, rotate_angle,
                               skew, center, transform_order);
    }

    printf("\nTransformation parameters:\n");
    printf("  Translation: (%.2f, %.2f)\n", translate[0], translate[1]);
    printf("  Scaling: (%.2f, %.2f)\n", scale[0], scale[1]);
    printf("  Rotation: %.2f degrees\n", rotate_angle);
    printf("  Skew: (%.2f, %.2f)\n", skew[0], skew[1]);
    printf("  Center point: (%.2f, %.2f)\n", center[0], center[1]);
    printf("  Transformation order: %s\n", transform_order);

    print_matrix(&matrix, "Transform");

    printf("\nBlend parameters:\n");
    printf("  Alpha blending: %s\n", blend_en ? "enabled" : "disabled");
    printf("  Blend mode: %d\n", blend_mode);
    printf("  Src alpha mode: %d\n", src_alpha_mode);
    printf("  Src global alpha: %d\n", src_global_alpha);


    printf("\nDst crop parameters:\n");
    printf("  Crop enabled: %s\n", dst_crop_en ? "yes" : "no");
    if (dst_crop_en) {
        printf("  Crop region: (%d, %d, %d, %d)\n", dst_crop_x, dst_crop_y, dst_crop_w, dst_crop_h);
    }

    printf("\nClear buffer:\n");
    printf("  Enabled: %s\n", clear_en ? "yes" : "no");
    if (clear_en) {
        printf("  Color: 0x%08X\n", clear_color);
    }

    printf("\nDrawing control parameters:\n");
    printf("  Alpha blending: %s (rules: %d)\n",
           ctrl.alpha_en ? "enabled" : "disabled", ctrl.alpha_rules);


    printf("Vector control parameters:\n");
    printf("  Fill rule: %s\n", vector_ctl.fill_rule == ARTVG_FILL_EVEN_ODD ? "EVEN_ODD" : "NON_ZERO");
    printf("  Curve flat limit: %d\n", vector_ctl.curve_flat_limit);

    printf("\nDrawing path...\n");
    ret = artvg_draw_path(vg, &buf, g_path, &matrix, &ctrl,
                          &gradient, &vector_ctl);
    if (ret != ARTVG_SUCCESS) {
        printf("Failed to draw path: %d\n", ret);
        result = -1;
        goto cleanup;
    }

    artvg_dumping_cmd(vg);

    printf("Flushing operations...\n");
    ret = artvg_flush(vg);
    if (ret != ARTVG_SUCCESS) {
        printf("Failed to flush: %d\n", ret);
        result = -1;
        goto cleanup;
    }

    printf("Waiting for hardware completion...\n");
    ret = artvg_wait_finish(vg);
    if (ret != ARTVG_SUCCESS) {
        printf("Failed to wait finish: %d\n", ret);
        result = -1;
        goto cleanup;
    }

    printf("Flushing DRM buffer to display...\n");
    if (drm_buffer_flush(&drm_dev, 0) != 0) {
        printf("Failed to flush DRM buffer\n");
    }

    printf("Waiting for vsync...\n");
    if (drm_wait_vsync(&drm_dev) != 0) {
        printf("Failed to wait for vsync\n");
    }

    printf("\n=== Test Completed Successfully ===\n");
    printf("Press Enter to exit...\n");
    getchar();

cleanup:
    if (vg && g_path) {
        artvg_path_free(vg, g_path);
    }

    if (artvg_remove_dma_fd(vg, buf.fd[0]) != ARTVG_SUCCESS) {
        printf("Failed to remove DMA FD\n");
    }

    drm_device_close(&drm_dev);

    if (vg) {
        if (edge_pool_enabled)
            artvg_disable_edge_buffer_pool(vg);
        artvg_destroy(vg);
    }

    return result;
}
