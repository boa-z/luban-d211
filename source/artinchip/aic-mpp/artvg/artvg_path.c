/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  zequan liang <zequan.liang@artinchip.com>
 */

#include <stdbool.h>
#include "artvg_context.h"

/* Parameter structures for command writing */
typedef struct {
    float x;
    float y;
} move_params_t;

typedef struct {
    float cx;
    float cy;
    float x;
    float y;
} quad_params_t;

typedef struct {
    float c0x;
    float c0y;
    float c1x;
    float c1y;
    float x;
    float y;
} cubic_params_t;

/* Static function declarations */
static int matrix_is_identity(artvg_matrix_t *matrix);
static uint32_t convert_hw_coordinate_float(float user_coord);
static artvg_error_t allocate_vector_section(artvg_t *vg, artvg_path_buffer_t *section);
static void free_vector_section(artvg_t *vg, artvg_path_buffer_t *section);
static artvg_error_t allocate_vector_section_node(artvg_t *vg, artvg_path_t *path);
static artvg_error_t ensure_space_and_write(artvg_t *vg, artvg_path_t *path, uint32_t cmd_size,
                                            void (*write_cmd)(uint8_t *, void *), void *params);
static void write_move_command(uint8_t *dst, void *params);
static void write_line_command(uint8_t *dst, void *params);
static void write_quad_command(uint8_t *dst, void *params);
static void write_cubic_command(uint8_t *dst, void *params);

static uint32_t estimate_edge_buffer_size(uint32_t height)
{
    uint64_t estimated = (uint64_t)ARTVG_EDGE_BUFFER_PER_LINE * height;
    if (estimated < DMA_BUF_MIN_SIZE)
        estimated = DMA_BUF_MIN_SIZE;
    return estimated;
}

static uint64_t pack_next_field(uint64_t next_phys, uint32_t next_data_size);
static artvg_error_t config_vector(struct cmd_slot *slot, artvg_path_t *path, artvg_vector_ctl_t *vector);

static inline bool path_is_finished(const artvg_path_t *path)
{
    return path->active_vector == NULL;
}

artvg_error_t artvg_draw_path(artvg_t *vg,
                         struct mpp_buf *dst,
                         artvg_path_t *path,
                         artvg_matrix_t *matrix,
                         artvg_ctrl_t *ctrl,
                         artvg_gradient_t *gradient,
                         artvg_vector_ctl_t *vector)
{
    return artvg_draw_path2(vg, dst, dst, path, matrix, ctrl, gradient, vector);
}

artvg_error_t artvg_draw_path2(artvg_t *vg,
                          struct mpp_buf *dst,
                          struct mpp_buf *output,
                          artvg_path_t *path,
                          artvg_matrix_t *matrix,
                          artvg_ctrl_t *ctrl,
                          artvg_gradient_t *gradient,
                          artvg_vector_ctl_t *vector)
{
    if (!vg || !output || !dst || !path || !gradient) {
        return ARTVG_INVALID_PARAM;
    }

    artvg_vector_ctl_t vector_ctl_default = {0};
    artvg_matrix_t affine_matrix = {0};
    artvg_ctrl_t ctl_default = {0};
    artvg_error_t error = ARTVG_SUCCESS;

    artvg_matrix_identity(&affine_matrix);
    matrix = matrix == NULL ? &affine_matrix : matrix;
    ctrl = ctrl == NULL ? &ctl_default : ctrl;
    vector = vector == NULL ? &vector_ctl_default : vector;

    if (artvg_resolution_detect(vg, output->size.width, output->size.height, 0) != ARTVG_SUCCESS)
        return ARTVG_NOT_SUPPORTED;

    ARTVG_RETURN_ERROR(artvg_validate_dither_scan_order(ctrl));

    if (path->vector_list.addr == NULL) {
        artvg_log_err("path not created\n");
        return ARTVG_INVALID_PARAM;
    }

    if (path->active_vector != NULL) {
        artvg_log_err("path not finished\n");
        return ARTVG_INVALID_PARAM;
    }

    struct cmd_slot *slot = acquire_slot_for_thread(vg);
    if (!slot) {
        return ARTVG_OUT_OF_RESOURCES;
    }

    if (reserve_command_buffer_space(vg, slot) != ARTVG_SUCCESS) {
        return ARTVG_OUT_OF_RESOURCES;
    }

    uint32_t cmd_count_saved = slot->cmd_count;
    bool is_identity_matrix = matrix_is_identity(matrix);
    bool use_affine = !is_identity_matrix;
    struct mpp_rect dst_rect = {0};
    struct mpp_rect output_rect = {0};

    get_crop_rect(dst, &dst_rect);
    get_crop_rect(output, &output_rect);

    slot->edge_buffer_size = estimate_edge_buffer_size(dst_rect.height);

    if (use_affine) {
        ARTVG_ERR_ROOLBACK(configure_affine_transform(slot, matrix, ARTVG_AFFINE_TRANS_VECTOR), slot, cmd_count_saved);
    }

    ARTVG_ERR_ROOLBACK(config_vector(slot, path, vector), slot, cmd_count_saved);
    ARTVG_ERR_ROOLBACK(configure_fill_color(slot, gradient), slot, cmd_count_saved);

    struct mpp_buf intern_src = {0};
    struct mpp_rect intern_src_rect = {0};
    intern_src.buf_type = MPP_PHY_ADDR;
    intern_src.format = MPP_FMT_ARGB_8888;

    uint32_t src_func_sel = 0;
    src_func_sel |= use_affine ? FUNC_SELECT(4) : FUNC_SELECT(3);
    src_func_sel |= COLOR_FILL_EN;
    src_func_sel |= use_affine ? TRANS_PAD_MODE_EN(ARTVG_PAD_MODE_FIXED_COLOR) : 0;
    src_func_sel |= ctrl->src_recolor_en ? RECOLOR_EN : 0;
    src_func_sel |= FILL_SPREAD(gradient->spread);
    src_func_sel |= COLOR_FILL_MODE(gradient->precision);

    ARTVG_ERR_ROOLBACK(configure_src_surface(vg, slot, &intern_src, NULL, ctrl, src_func_sel, true, &intern_src_rect, output->format), slot, cmd_count_saved);

    ARTVG_ERR_ROOLBACK(configure_dst_surface(vg, slot, dst, ctrl, &dst_rect, output->format), slot, cmd_count_saved);

    ARTVG_ERR_ROOLBACK(configure_output(vg, slot, output, ctrl->output_dither, &output_rect, 0), slot, cmd_count_saved);

    ARTVG_ERR_ROOLBACK(configure_blend_mode_finish(slot, ctrl), slot, cmd_count_saved);

    return ARTVG_SUCCESS;
}

artvg_error_t artvg_enable_edge_buffer_pool(artvg_t *vg, uint32_t size)
{
    if (vg == NULL || size == 0) {
        return ARTVG_INVALID_PARAM;
    }

    uint32_t alloc_size = size;
    int ret = artvg_drv_command(vg->drv, ARTVG_COMMAND_ALLOC_EDGE_BUF, &alloc_size);
    if (ret != 0) {
        artvg_log_err("Failed to allocate kernel edge buf, size=%u\n", alloc_size);
        return ARTVG_OUT_OF_MEMORY;
    }

    artvg_log_info("Enabled edge buffer pool: size=%u\n", alloc_size);
    return ARTVG_SUCCESS;
}

artvg_error_t artvg_disable_edge_buffer_pool(artvg_t *vg)
{
    if (vg == NULL) {
        return ARTVG_INVALID_PARAM;
    }

    int ret = artvg_drv_command(vg->drv, ARTVG_COMMAND_FREE_EDGE_BUF, NULL);

    artvg_log_info("Released edge buffer\n");
    return (ret == 0) ? ARTVG_SUCCESS : ARTVG_GENERIC_IO;
}

artvg_error_t artvg_enable_vector_buffer_pool(artvg_t *vg, uint32_t block_count, uint32_t block_size)
{
    if (vg == NULL || block_count == 0 || block_size < ARTVG_VECTOR_POOL_MIN_BLOCK_SIZE)
        return ARTVG_INVALID_PARAM;

    if (vg->use_vector_pool)
        return ARTVG_SUCCESS;

    uint32_t aligned_size = ALIGN_64B(block_size);
    uint32_t total_size = aligned_size * block_count;

    struct dma_buf_info dma_buf;
    void *base_addr = NULL;
    if (artvg_alloc_buffer_and_map(vg, total_size, &dma_buf, &base_addr) != ARTVG_SUCCESS) {
        artvg_log_err("Failed to allocate vector pool buffer: %u bytes\n", total_size);
        return ARTVG_OUT_OF_MEMORY;
    }

    void *nodes = artvg_os_malloc(block_count * sizeof(vg->vector_pool_nodes[0]));
    if (!nodes) {
        artvg_free_buffer_and_unmap(vg, &dma_buf, total_size, &base_addr);
        return ARTVG_OUT_OF_MEMORY;
    }

    artvg_os_mutex_lock(vg->os);
    if (vg->use_vector_pool) {
        artvg_os_mutex_unlock(vg->os);
        artvg_free_buffer_and_unmap(vg, &dma_buf, total_size, &base_addr);
        artvg_os_free(nodes);
        return ARTVG_SUCCESS;
    }

    vg->vector_pool_dma = dma_buf;
    vg->vector_pool_base_addr = base_addr;
    vg->vector_pool_nodes = nodes;
    mpp_list_init(&vg->vector_pool_free_list);

    uint32_t base_phy = vg->vector_pool_dma.phy_addr;
    uint8_t *base_addr_u8 = (uint8_t *)vg->vector_pool_base_addr;
    for (uint32_t i = 0; i < block_count; i++) {
        vg->vector_pool_nodes[i].phy_addr = base_phy + i * aligned_size;
        vg->vector_pool_nodes[i].virt_addr = base_addr_u8 + i * aligned_size;
        mpp_list_add_tail(&vg->vector_pool_nodes[i].list, &vg->vector_pool_free_list);
    }

    vg->vector_pool_block_count = block_count;
    vg->vector_pool_block_size = aligned_size;
    vg->use_vector_pool = true;

    artvg_os_mutex_unlock(vg->os);
    artvg_log_info("Enabled vector pool: %u blocks x %u bytes, total=%u\n",
                   block_count, aligned_size, total_size);
    return ARTVG_SUCCESS;
}

artvg_error_t artvg_disable_vector_buffer_pool(artvg_t *vg)
{
    if (vg == NULL) {
        return ARTVG_INVALID_PARAM;
    }

    artvg_os_mutex_lock(vg->os);
    if (!vg->use_vector_pool) {
        artvg_os_mutex_unlock(vg->os);
        return ARTVG_SUCCESS;
    }

    uint32_t total = vg->vector_pool_block_count;
    uint32_t free_count = 0;

    vector_pool_node_t *node, *tmp;
    mpp_list_for_each_entry_safe(node, tmp, &vg->vector_pool_free_list, list) {
        free_count++;
    }

    if (free_count < total) {
        artvg_log_err("Cannot release pool: %u/%u blocks in use\n",
                      total - free_count, total);
        artvg_os_mutex_unlock(vg->os);
        return ARTVG_GENERIC_IO;
    }

    vg->use_vector_pool = false;

    uint32_t block_size = vg->vector_pool_block_size;
    struct dma_buf_info dma = vg->vector_pool_dma;
    void *base_addr = vg->vector_pool_base_addr;
    void *nodes = vg->vector_pool_nodes;
    vg->vector_pool_block_count = 0;
    vg->vector_pool_block_size = 0;
    vg->vector_pool_nodes = NULL;
    vg->vector_pool_base_addr = NULL;
    artvg_os_mutex_unlock(vg->os);

    artvg_free_buffer_and_unmap(vg, &dma, total * block_size, &base_addr);
    artvg_os_free(nodes);

    artvg_log_info("Released vector pool\n");
    return ARTVG_SUCCESS;
}

artvg_path_t *artvg_path_allocate(artvg_t *vg)
{
    if (vg == NULL) {
        artvg_log_err("Invalid parameters: vg=%p\n", vg);
        return NULL;
    }

    artvg_path_t *path = (artvg_path_t *)artvg_os_malloc(sizeof(artvg_path_t));
    if (!path) {
        artvg_log_err("Failed to allocate artvg_path_t structure\n");
        return NULL;
    }

    memset(path, 0, sizeof(artvg_path_t));
    path->vector_list.prev = path->vector_list.next = &path->vector_list;

    if (allocate_vector_section(vg, &path->vector_list) != ARTVG_SUCCESS) {
        artvg_os_free(path);
        return NULL;
    }

    path->active_vector = &path->vector_list;

    artvg_log_debug("Allocated path: %p, section_size=%d\n", path, ARTVG_VECTOR_SECTION_SIZE);
    return path;
}

artvg_error_t artvg_path_move_to(artvg_t *vg, artvg_path_t *path, float x, float y)
{
    if (vg == NULL || path == NULL) {
        artvg_log_err("Invalid parameters for move_to: vg=%p, path=%p\n", vg, path);
        return ARTVG_INVALID_PARAM;
    }

    if (path_is_finished(path)) {
        artvg_log_err("Cannot write to a finished path\n");
        return ARTVG_INVALID_PARAM;
    }

    move_params_t params = {x, y};
    return ensure_space_and_write(vg, path, VECTOR_CMD_HW_SIZE_MOVE, write_move_command, &params);
}

artvg_error_t artvg_path_line_to(artvg_t *vg, artvg_path_t *path, float x, float y)
{
    if (vg == NULL || path == NULL) {
        artvg_log_err("Invalid parameters for line_to: vg=%p, path=%p\n", vg, path);
        return ARTVG_INVALID_PARAM;
    }

    if (path_is_finished(path)) {
        artvg_log_err("Cannot write to a finished path\n");
        return ARTVG_INVALID_PARAM;
    }

    move_params_t params = {x, y};
    return ensure_space_and_write(vg, path, VECTOR_CMD_HW_SIZE_LINE, write_line_command, &params);
}

artvg_error_t artvg_path_quad_to(artvg_t *vg, artvg_path_t *path, float cx, float cy, float x, float y)
{
    if (vg == NULL || path == NULL) {
        artvg_log_err("Invalid parameters for quad_to: vg=%p, path=%p\n", vg, path);
        return ARTVG_INVALID_PARAM;
    }

    if (path_is_finished(path)) {
        artvg_log_err("Cannot write to a finished path\n");
        return ARTVG_INVALID_PARAM;
    }

    quad_params_t params = {cx, cy, x, y};
    return ensure_space_and_write(vg, path, VECTOR_CMD_HW_SIZE_QUAD, write_quad_command, &params);
}

artvg_error_t artvg_path_cubic_to(artvg_t *vg, artvg_path_t *path,
                                  float c0x, float c0y,
                                  float c1x, float c1y,
                                  float x, float y)
{
    if (vg == NULL || path == NULL) {
        artvg_log_err("Invalid parameters for cubic_to: vg=%p, path=%p\n", vg, path);
        return ARTVG_INVALID_PARAM;
    }

    if (path_is_finished(path)) {
        artvg_log_err("Cannot write to a finished path\n");
        return ARTVG_INVALID_PARAM;
    }

    cubic_params_t params = {c0x, c0y, c1x, c1y, x, y};
    return ensure_space_and_write(vg, path, VECTOR_CMD_HW_SIZE_CUBIC, write_cubic_command, &params);
}

artvg_error_t artvg_path_finish(artvg_t *vg, artvg_path_t *path)
{
    if (vg == NULL || path == NULL) {
        artvg_log_err("Invalid parameters for path_finish\n");
        return ARTVG_INVALID_PARAM;
    }

    if (path->vector_list.addr == NULL) {
        artvg_log_err("Path not created\n");
        return ARTVG_INVALID_PARAM;
    }

    if (path->active_vector == NULL) {
        artvg_log_warn("Path already finished\n");
        return ARTVG_SUCCESS;
    }

    if (path->vector_list.used == 0) {
        artvg_log_err("Cannot finish empty path\n");
        return ARTVG_INVALID_PARAM;
    }

    artvg_path_buffer_t *section = path->vector_list.next;
    while (section != &path->vector_list) {
        artvg_path_buffer_t *next_section = section->next;
        uint64_t next_val;
        if (next_section != &path->vector_list) {
            uint64_t next_phys = next_section->buffer.phy_addr;
            next_val = pack_next_field(next_phys, next_section->used);
        } else {
            next_val = 0;
        }

        uint8_t *next_field_pos = (uint8_t *)section->addr + section->used;
        memcpy(next_field_pos, &next_val, sizeof(next_val));

        artvg_os_dma_buf_sync_range(vg->os, &section->buffer, section->addr,
                                    ALIGN_CACHE_LINE(section->used + sizeof(uint64_t)));
        section = next_section;
    }

    section = &path->vector_list;
    uint64_t next_val;
    if (section->next == section) {
        next_val = 0;
    } else {
        artvg_path_buffer_t *first_extra = section->next;
        next_val = pack_next_field(first_extra->buffer.phy_addr,
                                   first_extra->used);
    }
    uint8_t *next_field_pos = (uint8_t *)section->addr + section->used;
    memcpy(next_field_pos, &next_val, sizeof(next_val));
    artvg_os_dma_buf_sync_range(vg->os, &section->buffer, section->addr,
                                ALIGN_CACHE_LINE(section->used + sizeof(uint64_t)));

    path->active_vector = NULL;

    artvg_log_debug("Finished path\n");
    return ARTVG_SUCCESS;
}

artvg_error_t artvg_path_free(artvg_t *vg, artvg_path_t *path)
{
    if (vg == NULL || path == NULL) {
        return ARTVG_INVALID_PARAM;
    }

    if (path->vector_list.addr == NULL) {
        artvg_os_free(path);
        return ARTVG_SUCCESS;
    }

    artvg_path_buffer_t *section = path->vector_list.next;
    while (section != &path->vector_list) {
        artvg_path_buffer_t *next_section = section->next;
        section->prev->next = section->next;
        section->next->prev = section->prev;
        free_vector_section(vg, section);
        artvg_os_free(section);
        section = next_section;
    }

    free_vector_section(vg, &path->vector_list);

    artvg_os_free(path);

    return ARTVG_SUCCESS;
}

static artvg_error_t config_vector(struct cmd_slot *slot, artvg_path_t *path, artvg_vector_ctl_t *vector)
{
    uint32_t vector_draw_ctl = 0;
    uint32_t curve_flat_limit = vector->curve_flat_limit;

    if (curve_flat_limit == 0) {
        artvg_log_warn("curve_flat_limit cannot be 0, using default %d\n",
                       ARTVG_CURVE_FLAT_LIMIT_DEFAULT);
        curve_flat_limit = ARTVG_CURVE_FLAT_LIMIT_DEFAULT;
    } else if (curve_flat_limit > ARTVG_CURVE_FLAT_LIMIT_MAX) {
        artvg_log_warn("curve_flat_limit %u exceeds maximum %u, clamping\n",
                       curve_flat_limit, ARTVG_CURVE_FLAT_LIMIT_MAX);
        curve_flat_limit = ARTVG_CURVE_FLAT_LIMIT_MAX;
    }

    vector_draw_ctl |= FILL_RULE_SET(vector->fill_rule);
    vector_draw_ctl |= curve_flat_limit & 0xff;
    vector_draw_ctl |= EDGE_LIMIT_SIZE(0x7);

    artvg_path_buffer_t *vector_draw_cmd = &path->vector_list;
    uint32_t vector_draw_cmd_size = vector_draw_cmd->used + VECTOR_NEXT_FIELD_SIZE;

    uint32_t vector_addr[] = {
        vector_draw_cmd->buffer.phy_addr,
        VECTOR_CMD_SIZE_SET(vector_draw_cmd_size),
    };
    command_queue_add_command(slot, VECTOR_CMD_LOW_ADDR, ARRAY_SIZE(vector_addr), vector_addr, false);
    command_queue_add_command(slot, VECTOR_DRAW_CTRL, 1, &vector_draw_ctl, false);

    /* Edge regs (0x098/0x09C) intentionally NOT in cmd stream;
     * kernel programs them in artvg_hw_start. */

    artvg_log_debug("Vector CMD: phy=0x%llx, first_sec=%u\n",
                    (unsigned long long)vector_draw_cmd->buffer.phy_addr,
                    vector_draw_cmd_size);

    return ARTVG_SUCCESS;
}

artvg_error_t artvg_transform_gradient_points(artvg_gradient_t *gradient, artvg_matrix_t *matrix)
{
    artvg_point_t temp;

    if (!gradient || !matrix) {
        return ARTVG_INVALID_PARAM;
    }

    if (!transform(&temp, gradient->start.x, gradient->start.y, matrix)) {
        return ARTVG_INVALID_PARAM;
    }
    gradient->start.x = temp.x;
    gradient->start.y = temp.y;

    if (!transform(&temp, gradient->end.x, gradient->end.y, matrix)) {
        return ARTVG_INVALID_PARAM;
    }
    gradient->end.x = temp.x;
    gradient->end.y = temp.y;

    return ARTVG_SUCCESS;
}

static int matrix_is_identity(artvg_matrix_t *matrix)
{
    return fabsf(matrix->m[0][0] - 1.0f) < MATRIX_EPSILON &&
           fabsf(matrix->m[0][1]) < MATRIX_EPSILON &&
           fabsf(matrix->m[0][2]) < MATRIX_EPSILON &&
           fabsf(matrix->m[1][0]) < MATRIX_EPSILON &&
           fabsf(matrix->m[1][1] - 1.0f) < MATRIX_EPSILON &&
           fabsf(matrix->m[1][2]) < MATRIX_EPSILON &&
           fabsf(matrix->m[2][0]) < MATRIX_EPSILON &&
           fabsf(matrix->m[2][1]) < MATRIX_EPSILON &&
           fabsf(matrix->m[2][2] - 1.0f) < MATRIX_EPSILON;
}

static uint32_t convert_hw_coordinate_float(float user_coord)
{
    int32_t fixed_value = (int32_t)(user_coord * 64.0f);

    if (fixed_value > 524287)
        fixed_value = 524287;
    if (fixed_value < -524287)
        fixed_value = -524287;

    return (uint32_t)(fixed_value & 0xFFFFF);
}


static uint64_t pack_next_field(uint64_t next_phys, uint32_t next_data_size)
{
    uint32_t next_section_size = next_data_size + VECTOR_NEXT_FIELD_SIZE;
    return (next_phys << VECTOR_NEXT_ADDR_SHIFT) | (next_section_size & VECTOR_NEXT_SIZE_MASK);
}

static artvg_error_t allocate_vector_section(artvg_t *vg, artvg_path_buffer_t *section)
{
    if (vg == NULL || section == NULL) {
        return ARTVG_INVALID_PARAM;
    }

    memset(section, 0, sizeof(artvg_path_buffer_t));
    section->prev = section->next = section;

    artvg_os_mutex_lock(vg->os);
    if (vg->use_vector_pool && !mpp_list_empty(&vg->vector_pool_free_list)) {
        vector_pool_node_t *node = mpp_list_first_entry(&vg->vector_pool_free_list,
                                            vector_pool_node_t, list);
        mpp_list_del(&node->list);
        artvg_os_mutex_unlock(vg->os);

        memcpy(&section->buffer, &vg->vector_pool_dma, sizeof(struct dma_buf_info));
        section->buffer.phy_addr = node->phy_addr;
        section->addr = node->virt_addr;
        section->size = vg->vector_pool_block_size;
        section->used = 0;
        section->flag = ARTVG_PATH_BUF_FLAG_VECTOR_POOL;

        artvg_log_debug("Vector section from pool, phy=0x%x\n", node->phy_addr);
        return ARTVG_SUCCESS;
    }
    artvg_os_mutex_unlock(vg->os);

    if (artvg_alloc_buffer_and_map(vg, ARTVG_VECTOR_SECTION_SIZE,
                                   &section->buffer, &section->addr) != ARTVG_SUCCESS) {
        artvg_log_err("Failed to allocate vector section\n");
        return ARTVG_OUT_OF_MEMORY;
    }

    section->size = ARTVG_VECTOR_SECTION_SIZE;
    section->used = 0;
    section->flag = 0;

    return ARTVG_SUCCESS;
}

static void free_vector_section(artvg_t *vg, artvg_path_buffer_t *section)
{
    if (vg == NULL || section == NULL || section->addr == NULL) {
        return;
    }

    if (section->flag & ARTVG_PATH_BUF_FLAG_VECTOR_POOL) {
        artvg_os_mutex_lock(vg->os);
        if (vg->use_vector_pool) {
            uint32_t base_phy = vg->vector_pool_dma.phy_addr;
            uint32_t offset = section->buffer.phy_addr - base_phy;
            uint32_t index = offset / vg->vector_pool_block_size;
            mpp_list_add_tail(&vg->vector_pool_nodes[index].list,
                              &vg->vector_pool_free_list);
        }
        artvg_os_mutex_unlock(vg->os);

        artvg_log_debug("Vector section returned to pool, index=%u\n", index);
    } else if (!(section->flag & ARTVG_PATH_BUF_FLAG_PREALLOC)) {
        artvg_free_buffer_and_unmap(vg, &section->buffer, section->size, &section->addr);
    }

    memset(section, 0, sizeof(artvg_path_buffer_t));
}

static artvg_error_t allocate_vector_section_node(artvg_t *vg, artvg_path_t *path)
{
    artvg_path_buffer_t *new_section = (artvg_path_buffer_t *)artvg_os_malloc(sizeof(artvg_path_buffer_t));
    if (!new_section) {
        artvg_log_err("Failed to allocate section node\n");
        return ARTVG_OUT_OF_MEMORY;
    }

    if (allocate_vector_section(vg, new_section) != ARTVG_SUCCESS) {
        artvg_os_free(new_section);
        return ARTVG_OUT_OF_MEMORY;
    }

    new_section->prev = path->vector_list.prev;
    new_section->next = &path->vector_list;
    path->vector_list.prev->next = new_section;
    path->vector_list.prev = new_section;
    path->active_vector = new_section;

    artvg_log_debug("Added new vector section\n");
    return ARTVG_SUCCESS;
}

static void write_move_command(uint8_t *dst, void *params)
{
    move_params_t *p = (move_params_t *)params;
    uint32_t x_hw = convert_hw_coordinate_float(p->x);
    uint32_t y_hw = convert_hw_coordinate_float(p->y);

    uint32_t value = (VECTOR_CMD_HW_MOVE_TO << 30) | (x_hw & 0x000FFFFF);
    uint32_t y_value = y_hw & 0x000FFFFF;

    memcpy(dst, &value, 4);
    memcpy(dst + 4, &y_value, 4);

    artvg_log_debug("MOVE: (%.2f,%.2f) -> 0x%08x 0x%08x\n", p->x, p->y, value, y_value);
}

static void write_line_command(uint8_t *dst, void *params)
{
    move_params_t *p = (move_params_t *)params;
    uint32_t x_hw = convert_hw_coordinate_float(p->x);
    uint32_t y_hw = convert_hw_coordinate_float(p->y);

    uint32_t value = (VECTOR_CMD_HW_LINE_TO << 30) | (x_hw & 0x000FFFFF);
    uint32_t y_value = y_hw & 0x000FFFFF;

    memcpy(dst, &value, 4);
    memcpy(dst + 4, &y_value, 4);

    artvg_log_debug("LINE: (%.2f,%.2f) -> 0x%08x 0x%08x\n", p->x, p->y, value, y_value);
}

static void write_quad_command(uint8_t *dst, void *params)
{
    quad_params_t *p = (quad_params_t *)params;
    uint32_t c0_x = convert_hw_coordinate_float(p->cx);
    uint32_t c0_y = convert_hw_coordinate_float(p->cy);
    uint32_t x_hw = convert_hw_coordinate_float(p->x);
    uint32_t y_hw = convert_hw_coordinate_float(p->y);

    uint32_t value = (VECTOR_CMD_HW_CONIC_TO << 30) | (c0_x & 0x000FFFFF);
    uint32_t c0_y_value = c0_y & 0x000FFFFF;
    uint32_t x_value = x_hw & 0x000FFFFF;
    uint32_t y_value = y_hw & 0x000FFFFF;

    memcpy(dst, &value, 4);
    memcpy(dst + 4, &c0_y_value, 4);
    memcpy(dst + 8, &x_value, 4);
    memcpy(dst + 12, &y_value, 4);

    artvg_log_debug("QUAD: (%.2f,%.2f)->(%.2f,%.2f) -> 0x%08x 0x%08x 0x%08x 0x%08x\n",
                    p->cx, p->cy, p->x, p->y, value, c0_y_value, x_value, y_value);
}

static void write_cubic_command(uint8_t *dst, void *params)
{
    cubic_params_t *p = (cubic_params_t *)params;
    uint32_t c0_x = convert_hw_coordinate_float(p->c0x);
    uint32_t c0_y = convert_hw_coordinate_float(p->c0y);
    uint32_t c1_x = convert_hw_coordinate_float(p->c1x);
    uint32_t c1_y = convert_hw_coordinate_float(p->c1y);
    uint32_t x_hw = convert_hw_coordinate_float(p->x);
    uint32_t y_hw = convert_hw_coordinate_float(p->y);

    uint32_t value = (VECTOR_CMD_HW_CUBIC_TO << 30) | (c0_x & 0x000FFFFF);
    uint32_t c0_y_value = c0_y & 0x000FFFFF;
    uint32_t c1_x_value = c1_x & 0x000FFFFF;
    uint32_t c1_y_value = c1_y & 0x000FFFFF;
    uint32_t x_value = x_hw & 0x000FFFFF;
    uint32_t y_value = y_hw & 0x000FFFFF;

    memcpy(dst, &value, 4);
    memcpy(dst + 4, &c0_y_value, 4);
    memcpy(dst + 8, &c1_x_value, 4);
    memcpy(dst + 12, &c1_y_value, 4);
    memcpy(dst + 16, &x_value, 4);
    memcpy(dst + 20, &y_value, 4);

    artvg_log_debug("CUBIC: (%.2f,%.2f)->(%.2f,%.2f)->(%.2f,%.2f)\n",
                    p->c0x, p->c0y, p->c1x, p->c1y, p->x, p->y);
}

static artvg_error_t ensure_space_and_write(artvg_t *vg, artvg_path_t *path, uint32_t cmd_size,
                                            void (*write_cmd)(uint8_t *, void *), void *params)
{
    artvg_path_buffer_t *section = path->active_vector;
    uint32_t remaining = section->size - VECTOR_NEXT_FIELD_SIZE - section->used;

    if (cmd_size > remaining) {
        artvg_error_t ret = allocate_vector_section_node(vg, path);
        if (ret != ARTVG_SUCCESS) {
            return ret;
        }
        section = path->active_vector;
    }

    uint8_t *write_pos = (uint8_t *)section->addr + section->used;
    write_cmd(write_pos, params);

    section->used += cmd_size;

    return ARTVG_SUCCESS;
}