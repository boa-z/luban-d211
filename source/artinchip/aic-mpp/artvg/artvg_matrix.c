/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "artvg_context.h"
#include "artvg.h"

#ifndef M_PI
#define M_PI 3.1415926f
#endif
#define MATRIX_EPS 2.2204460492503131e-14
#define ABS(x) (((x) < 0) ? -(x) : (x))
static artvg_error_t swap(float *a, float *b);

void artvg_matrix_identity(artvg_matrix_t *matrix)
{
    matrix->m[0][0] = 1.0f;
    matrix->m[0][1] = 0.0f;
    matrix->m[0][2] = 0.0f;
    matrix->m[1][0] = 0.0f;
    matrix->m[1][1] = 1.0f;
    matrix->m[1][2] = 0.0f;
    matrix->m[2][0] = 0.0f;
    matrix->m[2][1] = 0.0f;
    matrix->m[2][2] = 1.0f;
}

void artvg_matrix_multiply(artvg_matrix_t *result, const artvg_matrix_t *a, const artvg_matrix_t *b)
{
    artvg_matrix_t temp;
    int row, col;

    for (row = 0; row < 3; row++) {
        for (col = 0; col < 3; col++) {
            temp.m[row][col] = a->m[row][0] * b->m[0][col] +
                               a->m[row][1] * b->m[1][col] +
                               a->m[row][2] * b->m[2][col];
        }
    }

    memcpy(result, &temp, sizeof(temp));
}

void artvg_matrix_translate(float x, float y, artvg_matrix_t *matrix)
{
    artvg_matrix_t t = {{{1.0f, 0.0f, x},
                         {0.0f, 1.0f, y},
                         {0.0f, 0.0f, 1.0f}}};

    artvg_matrix_multiply(matrix, matrix, &t);
}

void artvg_matrix_scale(float scale_x, float scale_y, artvg_matrix_t *matrix)
{
    artvg_matrix_t s = {{{scale_x, 0.0f, 0.0f},
                         {0.0f, scale_y, 0.0f},
                         {0.0f, 0.0f, 1.0f}}};

    artvg_matrix_multiply(matrix, matrix, &s);
}

void artvg_matrix_rotate(float degrees, artvg_matrix_t *matrix)
{
    float angle = degrees / 180.0f * M_PI;
    float cos_angle = cosf(angle);
    float sin_angle = sinf(angle);

    artvg_matrix_t r = {{{cos_angle, -sin_angle, 0.0f},
                         {sin_angle, cos_angle, 0.0f},
                         {0.0f, 0.0f, 1.0f}}};

    artvg_matrix_multiply(matrix, matrix, &r);
}

void artvg_matrix_rotate_point(artvg_point_t *point, float angle, artvg_matrix_t *matrix)
{
    if (!matrix)
        return;

    artvg_matrix_translate(-point->x, -point->y, matrix);
    artvg_matrix_rotate(angle, matrix);
    artvg_matrix_translate(point->x, point->y, matrix);
}

void artvg_matrix_skew(float skew_x, float skew_y, artvg_matrix_t *matrix)
{
    float tan_x = tanf(skew_x * M_PI / 180.0f);
    float tan_y = tanf(skew_y * M_PI / 180.0f);

    artvg_matrix_t s = {{{1.0f, tan_x, 0.0f},
                         {tan_y, 1.0f, 0.0f},
                         {0.0f, 0.0f, 1.0f}}};

    artvg_matrix_multiply(matrix, matrix, &s);
}

void artvg_matrix_perspective(float px, float py, artvg_matrix_t *matrix)
{
    artvg_matrix_t p = {{{1.0f, 0.0f, 0.0f},
                         {0.0f, 1.0f, 0.0f},
                         {px, py, 1.0f}}};

    artvg_matrix_multiply(matrix, matrix, &p);
}

bool artvg_matrix_is_identity(const artvg_matrix_t *matrix)
{
    return (matrix->m[0][0] == 1.0f && matrix->m[0][1] == 0.0f && matrix->m[0][2] == 0.0f &&
            matrix->m[1][0] == 0.0f && matrix->m[1][1] == 1.0f && matrix->m[1][2] == 0.0f &&
            matrix->m[2][0] == 0.0f && matrix->m[2][1] == 0.0f && matrix->m[2][2] == 1.0f);
}

bool artvg_matrix_inverse(artvg_matrix_t *result, const artvg_matrix_t *m)
{
    float det = m->m[0][0] * (m->m[1][1] * m->m[2][2] - m->m[1][2] * m->m[2][1]) -
                m->m[0][1] * (m->m[1][0] * m->m[2][2] - m->m[1][2] * m->m[2][0]) +
                m->m[0][2] * (m->m[1][0] * m->m[2][1] - m->m[1][1] * m->m[2][0]);

    if (det == 0.0f)
        return false;

    float inv_det = 1.0f / det;
    result->m[0][0] = (m->m[1][1] * m->m[2][2] - m->m[1][2] * m->m[2][1]) * inv_det;
    result->m[0][1] = (m->m[0][2] * m->m[2][1] - m->m[0][1] * m->m[2][2]) * inv_det;
    result->m[0][2] = (m->m[0][1] * m->m[1][2] - m->m[0][2] * m->m[1][1]) * inv_det;
    result->m[1][0] = (m->m[1][2] * m->m[2][0] - m->m[1][0] * m->m[2][2]) * inv_det;
    result->m[1][1] = (m->m[0][0] * m->m[2][2] - m->m[0][2] * m->m[2][0]) * inv_det;
    result->m[1][2] = (m->m[0][2] * m->m[1][0] - m->m[0][0] * m->m[1][2]) * inv_det;
    result->m[2][0] = (m->m[1][0] * m->m[2][1] - m->m[1][1] * m->m[2][0]) * inv_det;
    result->m[2][1] = (m->m[0][1] * m->m[2][0] - m->m[0][0] * m->m[2][1]) * inv_det;
    result->m[2][2] = (m->m[0][0] * m->m[1][1] - m->m[0][1] * m->m[1][0]) * inv_det;

    return true;
}

artvg_error_t artvg_get_transform_matrix(artvg_point4_t src, artvg_point4_t dst, artvg_matrix_t *mat)
{
    float a[8][8], b[9], A[64];
    int i, j, k, m = 8, n = 1;
    int astep = 8, bstep = 1;
    float d;

    if (src == NULL || dst == NULL || mat == NULL)
        return ARTVG_INVALID_PARAM;

    for (i = 0; i < 4; ++i) {
        a[i][0] = a[i + 4][3] = src[i].x;
        a[i][1] = a[i + 4][4] = src[i].y;
        a[i][2] = a[i + 4][5] = 1;
        a[i][3] = a[i][4] = a[i][5] =
            a[i + 4][0] = a[i + 4][1] = a[i + 4][2] = 0;
        a[i][6] = -src[i].x * dst[i].x;
        a[i][7] = -src[i].y * dst[i].x;
        a[i + 4][6] = -src[i].x * dst[i].y;
        a[i + 4][7] = -src[i].y * dst[i].y;
        b[i] = dst[i].x;
        b[i + 4] = dst[i].y;
    }

    for (i = 0; i < 8; ++i) {
        for (j = 0; j < 8; ++j) {
            A[8 * i + j] = a[i][j];
        }
    }

    for (i = 0; i < m; i++) {
        k = i;
        for (j = i + 1; j < m; j++) {
            if (ABS(A[j * astep + i]) > ABS(A[k * astep + i]))
                k = j;
        }

        if (ABS(A[k * astep + i]) < MATRIX_EPS)
            return ARTVG_INVALID_PARAM;

        if (k != i) {
            for (j = i; j < m; j++)
                swap(&A[i * astep + j], &A[k * astep + j]);
            for (j = 0; j < n; j++)
                swap(&b[i * bstep + j], &b[k * bstep + j]);
        }

        d = -1 / A[i * astep + i];
        for (j = i + 1; j < m; j++) {
            float alpha = A[j * astep + i] * d;
            for (k = i + 1; k < m; k++)
                A[j * astep + k] += alpha * A[i * astep + k];
            for (k = 0; k < n; k++)
                b[j * bstep + k] += alpha * b[i * bstep + k];
        }
    }

    for (i = m - 1; i >= 0; i--) {
        for (j = 0; j < n; j++) {
            float s = b[i * bstep + j];
            for (k = i + 1; k < m; k++)
                s -= A[i * astep + k] * b[k * bstep + j];
            b[i * bstep + j] = s / A[i * astep + i];
        }
    }

    b[8] = 1;

    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 3; ++j) {
            mat->m[i][j] = b[i * 3 + j];
        }
    }

    return ARTVG_SUCCESS;
}

static artvg_error_t swap(float *a, float *b)
{
    float temp;
    if (a == NULL || b == NULL)
        return ARTVG_INVALID_PARAM;
    temp = *a;
    *a = *b;
    *b = temp;
    return ARTVG_SUCCESS;
}
