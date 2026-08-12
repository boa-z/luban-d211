/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: zequan liang <zequan.liang@artinchip.com>
 *
 * artvg_vector_pool_verify.c -- Host-side verification of Vector Pool allocation
 *
 * This program simulates two vector section allocation methods and verifies
 * they produce identical command data and correct hardware-linked lists:
 *
 *   Mode A (ORIG):  Each section separately malloc'd (4096 bytes each)
 *   Mode B (POOL4K): Pool with block_size = 4096
 *   Mode C (POOL1K): Pool with block_size = 1024
 *
 * Build: gcc artvg_vector_pool_verify.c -o verify -lm -Wall
 * Run:   ./verify
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <math.h>
#include <pthread.h>
#include <assert.h>

/* ========== Simulated constants (matches artvg_context.h / artvg_reg.h) ========== */

#define VECTOR_NEXT_FIELD_SIZE      8
#define VECTOR_NEXT_SIZE_MASK       0xFFFFFF
#define VECTOR_NEXT_ADDR_SHIFT      24
#define VECTOR_MAX_DATA_SIZE        (VECTOR_NEXT_SIZE_MASK - VECTOR_NEXT_FIELD_SIZE)

#define ARTVG_VECTOR_SECTION_SIZE   4096
#define VECTOR_SECTION_DATA_SIZE    (ARTVG_VECTOR_SECTION_SIZE - VECTOR_NEXT_FIELD_SIZE)

#define ARTVG_VECTOR_POOL_MIN_BLOCK_SIZE 256

#define VECTOR_CMD_HW_SIZE_MOVE     8
#define VECTOR_CMD_HW_SIZE_LINE     8
#define VECTOR_CMD_HW_SIZE_QUAD     16
#define VECTOR_CMD_HW_SIZE_CUBIC    24

#define VECTOR_CMD_HW_MOVE_TO  0x00
#define VECTOR_CMD_HW_LINE_TO  0x01
#define VECTOR_CMD_HW_CONIC_TO 0x02
#define VECTOR_CMD_HW_CUBIC_TO 0x03

#define VECTOR_CMD_SIZE_SET(size) ((size) & 0xFFFFFF)

#define ARTVG_PATH_BUF_FLAG_VECTOR_POOL 0x00000002

#define ALIGN_64B(x) (((x) + (63)) & ~(63))

/* ========== Simulated linked list (simplified mpp_list) ========== */

struct mpp_list {
    struct mpp_list *next;
    struct mpp_list *prev;
};

static inline void mpp_list_init(struct mpp_list *list)
{
    list->next = list;
    list->prev = list;
}

static inline void mpp_list_add_tail(struct mpp_list *elem, struct mpp_list *head)
{
    elem->next = head;
    elem->prev = head->prev;
    head->prev->next = elem;
    head->prev = elem;
}

static inline void mpp_list_del(struct mpp_list *elem)
{
    elem->prev->next = elem->next;
    elem->next->prev = elem->prev;
    elem->next = NULL;
    elem->prev = NULL;
}

static inline int mpp_list_empty(struct mpp_list *head)
{
    return head->next == head;
}

#define mpp_offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#define mpp_list_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - mpp_offsetof(type, member)))

#define mpp_list_first_entry(ptr, type, member) \
    mpp_list_entry((ptr)->next, type, member)

#define mpp_list_for_each_entry_safe(pos, n, head, member) \
    for (pos = mpp_list_entry((head)->next, typeof(*pos), member), \
         n = mpp_list_entry(pos->member.next, typeof(*pos), member); \
         &pos->member != (head); \
         pos = n, n = mpp_list_entry(n->member.next, typeof(*n), member))

/* ========== Simulated DMA buffer info ========== */

struct dma_buf_info {
    int fd;
    uint32_t phy_addr;
};

/* ========== Path buffer section (matches artvg.h) ========== */

typedef struct artvg_path_buffer {
    struct artvg_path_buffer *prev;
    struct artvg_path_buffer *next;
    struct dma_buf_info buffer;
    void *addr;
    uint32_t size;
    uint32_t used;
    uint32_t flag;
} artvg_path_buffer_t;

/* ========== Simulated Vector Pool Node (matches artvg_context.h) ========== */

typedef struct _vector_pool_nodes {
    struct mpp_list list;
    uint32_t phy_addr;
    void *virt_addr;
} vector_pool_node_t;

/* ========== Simulated Context ========== */

typedef struct {
    /* Pool fields */
    bool use_vector_pool;
    uint32_t vector_pool_block_count;
    uint32_t vector_pool_block_size;
    vector_pool_node_t *vector_pool_nodes;
    struct mpp_list vector_pool_free_list;
    struct dma_buf_info vector_pool_dma;
    void *vector_pool_base_addr;

    /* For phy_addr simulation */
    uint32_t next_phy_addr;

    /* For debugging mode name */
    const char *mode_name;
    int section_size;

    /* Thread safety */
    pthread_mutex_t mutex;
} sim_context_t;

/* ========== Path (matches artvg.h) ========== */

typedef struct artvg_path {
    artvg_path_buffer_t vector_list;
    artvg_path_buffer_t *active_vector;
} artvg_path_t;

/* ========== Global phy address allocator ========== */

static uint32_t alloc_phy_addr(sim_context_t *sim)
{
    /* Simulate 32-bit MMIO space, each buffer at a unique address */
    uint32_t addr = sim->next_phy_addr;
    sim->next_phy_addr += sim->section_size;
    return addr;
}

/* ========== Core logic replication from artvg_path.c ========== */

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

/* ========== Pool management ========== */

static int enable_vector_buffer_pool(sim_context_t *sim, uint32_t block_count, uint32_t block_size)
{
    uint32_t aligned_size = ALIGN_64B(block_size);
    uint32_t total_size = aligned_size * block_count;

    sim->vector_pool_nodes = (vector_pool_node_t *)calloc(block_count, sizeof(vector_pool_node_t));
    if (!sim->vector_pool_nodes)
        return -1;

    sim->vector_pool_base_addr = malloc(total_size);
    if (!sim->vector_pool_base_addr) {
        free(sim->vector_pool_nodes);
        sim->vector_pool_nodes = NULL;
        return -1;
    }

    mpp_list_init(&sim->vector_pool_free_list);

    /* Assign the pool's "physical" base address */
    sim->vector_pool_dma.phy_addr = alloc_phy_addr(sim);
    sim->vector_pool_dma.fd = -1;

    uint32_t base_phy = sim->vector_pool_dma.phy_addr;
    uint8_t *base_addr = (uint8_t *)sim->vector_pool_base_addr;

    for (uint32_t i = 0; i < block_count; i++) {
        sim->vector_pool_nodes[i].phy_addr = base_phy + i * aligned_size;
        sim->vector_pool_nodes[i].virt_addr = base_addr + i * aligned_size;
        memset(sim->vector_pool_nodes[i].virt_addr, 0, aligned_size);
        mpp_list_add_tail(&sim->vector_pool_nodes[i].list, &sim->vector_pool_free_list);
    }

    sim->vector_pool_block_count = block_count;
    sim->vector_pool_block_size = aligned_size;
    sim->use_vector_pool = true;

    return 0;
}

static void disable_vector_buffer_pool(sim_context_t *sim)
{
    if (!sim->use_vector_pool)
        return;

    /* Check all blocks returned */
    uint32_t free_count = 0;
    vector_pool_node_t *pos, *n;
    mpp_list_for_each_entry_safe(pos, n, &sim->vector_pool_free_list, list) {
        free_count++;
    }

    if (free_count < sim->vector_pool_block_count) {
        printf("  WARNING: Pool leak: %u/%u blocks still in use\n",
               sim->vector_pool_block_count - free_count, sim->vector_pool_block_count);
    }

    sim->use_vector_pool = false;
    free(sim->vector_pool_base_addr);
    sim->vector_pool_base_addr = NULL;
    free(sim->vector_pool_nodes);
    sim->vector_pool_nodes = NULL;
    sim->vector_pool_block_count = 0;
    sim->vector_pool_block_size = 0;
}

/* ========== Section allocation (replicates artvg_path.c logic) ========== */

static int allocate_vector_section(sim_context_t *sim, artvg_path_buffer_t *section)
{
    memset(section, 0, sizeof(artvg_path_buffer_t));
    section->prev = section->next = section;

    pthread_mutex_lock(&sim->mutex);
    if (sim->use_vector_pool && !mpp_list_empty(&sim->vector_pool_free_list)) {
        vector_pool_node_t *node = mpp_list_first_entry(&sim->vector_pool_free_list,
                                            vector_pool_node_t, list);
        mpp_list_del(&node->list);
        pthread_mutex_unlock(&sim->mutex);

        memcpy(&section->buffer, &sim->vector_pool_dma, sizeof(struct dma_buf_info));
        section->buffer.phy_addr = node->phy_addr;
        section->addr = node->virt_addr;
        section->size = sim->vector_pool_block_size;
        section->used = 0;
        section->flag = ARTVG_PATH_BUF_FLAG_VECTOR_POOL;
        return 0;
    }
    pthread_mutex_unlock(&sim->mutex);

    /* Non-pool fallback: allocate fresh buffer */
    void *addr = malloc(sim->section_size);
    if (!addr)
        return -1;
    memset(addr, 0, sim->section_size);

    section->buffer.fd = -1;
    section->buffer.phy_addr = alloc_phy_addr(sim);
    section->addr = addr;
    section->size = sim->section_size;
    section->used = 0;
    section->flag = 0;
    return 0;
}

/* ========== Write command functions ========== */

static void write_move_command(uint8_t *dst, float x, float y)
{
    uint32_t x_hw = convert_hw_coordinate_float(x);
    uint32_t y_hw = convert_hw_coordinate_float(y);
    uint32_t value = (VECTOR_CMD_HW_MOVE_TO << 30) | (x_hw & 0x000FFFFF);
    uint32_t y_value = y_hw & 0x000FFFFF;
    memcpy(dst, &value, 4);
    memcpy(dst + 4, &y_value, 4);
}

static void write_line_command(uint8_t *dst, float x, float y)
{
    uint32_t x_hw = convert_hw_coordinate_float(x);
    uint32_t y_hw = convert_hw_coordinate_float(y);
    uint32_t value = (VECTOR_CMD_HW_LINE_TO << 30) | (x_hw & 0x000FFFFF);
    uint32_t y_value = y_hw & 0x000FFFFF;
    memcpy(dst, &value, 4);
    memcpy(dst + 4, &y_value, 4);
}

static void write_quad_command(uint8_t *dst, float cx, float cy, float x, float y)
{
    uint32_t c0_x = convert_hw_coordinate_float(cx);
    uint32_t c0_y = convert_hw_coordinate_float(cy);
    uint32_t x_hw = convert_hw_coordinate_float(x);
    uint32_t y_hw = convert_hw_coordinate_float(y);
    uint32_t value = (VECTOR_CMD_HW_CONIC_TO << 30) | (c0_x & 0x000FFFFF);
    uint32_t c0_y_value = c0_y & 0x000FFFFF;
    uint32_t x_value = x_hw & 0x000FFFFF;
    uint32_t y_value = y_hw & 0x000FFFFF;
    memcpy(dst, &value, 4);
    memcpy(dst + 4, &c0_y_value, 4);
    memcpy(dst + 8, &x_value, 4);
    memcpy(dst + 12, &y_value, 4);
}

static void write_cubic_command(uint8_t *dst, float c0x, float c0y,
                                float c1x, float c1y, float x, float y)
{
    uint32_t c0_x = convert_hw_coordinate_float(c0x);
    uint32_t c0_y = convert_hw_coordinate_float(c0y);
    uint32_t c1_x = convert_hw_coordinate_float(c1x);
    uint32_t c1_y = convert_hw_coordinate_float(c1y);
    uint32_t x_hw = convert_hw_coordinate_float(x);
    uint32_t y_hw = convert_hw_coordinate_float(y);
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
}

/* ========== Section node allocation ========== */

static int allocate_vector_section_node(sim_context_t *sim, artvg_path_t *path)
{
    artvg_path_buffer_t *new_section = (artvg_path_buffer_t *)calloc(1, sizeof(artvg_path_buffer_t));
    if (!new_section)
        return -1;

    if (allocate_vector_section(sim, new_section) != 0) {
        free(new_section);
        return -1;
    }

    new_section->prev = path->vector_list.prev;
    new_section->next = &path->vector_list;
    path->vector_list.prev->next = new_section;
    path->vector_list.prev = new_section;
    path->active_vector = new_section;
    return 0;
}

/* ========== Ensure space and write ========== */

static int ensure_space_and_write(sim_context_t *sim, artvg_path_t *path,
                                  uint32_t cmd_size,
                                  void (*write_cmd)(uint8_t *, void *), void *params)
{
    artvg_path_buffer_t *section = path->active_vector;
    uint32_t remaining = section->size - VECTOR_NEXT_FIELD_SIZE - section->used;

    if (cmd_size > remaining) {
        int ret = allocate_vector_section_node(sim, path);
        if (ret != 0)
            return ret;
        section = path->active_vector;
    }

    uint8_t *write_pos = (uint8_t *)section->addr + section->used;
    write_cmd(write_pos, params);
    section->used += cmd_size;
    return 0;
}

/* ========== Wrapper functions for path building (with cast helper) ========== */

typedef struct { float x, y; } move_params_t;
typedef struct { float cx, cy, x, y; } quad_params_t;
typedef struct { float c0x, c0y, c1x, c1y, x, y; } cubic_params_t;

static void write_move_wrapper(uint8_t *dst, void *p)
{
    move_params_t *m = (move_params_t *)p;
    write_move_command(dst, m->x, m->y);
}

static void write_line_wrapper(uint8_t *dst, void *p)
{
    move_params_t *m = (move_params_t *)p;
    write_line_command(dst, m->x, m->y);
}

static void write_quad_wrapper(uint8_t *dst, void *p)
{
    quad_params_t *q = (quad_params_t *)p;
    write_quad_command(dst, q->cx, q->cy, q->x, q->y);
}

static void write_cubic_wrapper(uint8_t *dst, void *p)
{
    cubic_params_t *c = (cubic_params_t *)p;
    write_cubic_command(dst, c->c0x, c->c0y, c->c1x, c->c1y, c->x, c->y);
}

static int path_move_to(sim_context_t *sim, artvg_path_t *path, float x, float y)
{
    move_params_t params = {x, y};
    return ensure_space_and_write(sim, path, VECTOR_CMD_HW_SIZE_MOVE,
                                  write_move_wrapper, &params);
}

static int path_line_to(sim_context_t *sim, artvg_path_t *path, float x, float y)
{
    move_params_t params = {x, y};
    return ensure_space_and_write(sim, path, VECTOR_CMD_HW_SIZE_LINE,
                                  write_line_wrapper, &params);
}

static int path_quad_to(sim_context_t *sim, artvg_path_t *path,
                        float cx, float cy, float x, float y)
{
    quad_params_t params = {cx, cy, x, y};
    return ensure_space_and_write(sim, path, VECTOR_CMD_HW_SIZE_QUAD,
                                  write_quad_wrapper, &params);
}

static int path_cubic_to(sim_context_t *sim, artvg_path_t *path,
                         float c0x, float c0y, float c1x, float c1y,
                         float x, float y)
{
    cubic_params_t params = {c0x, c0y, c1x, c1y, x, y};
    return ensure_space_and_write(sim, path, VECTOR_CMD_HW_SIZE_CUBIC,
                                  write_cubic_wrapper, &params);
}

/* ========== Path finish (replicates artvg_path_finish) ========== */

static int path_finish(sim_context_t *sim, artvg_path_t *path)
{
    (void)sim;
    if (path->vector_list.addr == NULL)
        return -1;
    if (path->active_vector == NULL)
        return 0;
    if (path->vector_list.used == 0)
        return -1;

    /* First pass: extra sections */
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
        section = next_section;
    }

    /* Second pass: head section */
    section = &path->vector_list;
    uint64_t next_val;
    if (section->next == section) {
        next_val = 0;
    } else {
        artvg_path_buffer_t *first_extra = section->next;
        next_val = pack_next_field(first_extra->buffer.phy_addr, section->used);
    }
    uint8_t *next_field_pos = (uint8_t *)section->addr + section->used;
    memcpy(next_field_pos, &next_val, sizeof(next_val));

    path->active_vector = NULL;
    return 0;
}

/* ========== Path free ==========
 * If sim is provided and section is from pool, returns block to pool free list.
 * If sim is NULL (cleanup after pool disabled), just frees non-pool memory.
 */

static void path_free(sim_context_t *sim, artvg_path_t *path)
{
    if (path->vector_list.addr == NULL) {
        memset(path, 0, sizeof(artvg_path_t));
        return;
    }

    artvg_path_buffer_t *section = path->vector_list.next;
    while (section != &path->vector_list) {
        artvg_path_buffer_t *next_section = section->next;
        section->prev->next = section->next;
        section->next->prev = section->prev;
        if (section->flag & ARTVG_PATH_BUF_FLAG_VECTOR_POOL) {
            if (sim) {
                uint32_t base_phy = sim->vector_pool_dma.phy_addr;
                uint32_t offset = section->buffer.phy_addr - base_phy;
                uint32_t index = offset / sim->vector_pool_block_size;
                pthread_mutex_lock(&sim->mutex);
                mpp_list_add_tail(&sim->vector_pool_nodes[index].list,
                                  &sim->vector_pool_free_list);
                pthread_mutex_unlock(&sim->mutex);
            }
        } else {
            free(section->addr);
        }
        free(section);
        section = next_section;
    }

    if (path->vector_list.flag & ARTVG_PATH_BUF_FLAG_VECTOR_POOL) {
        if (sim) {
            uint32_t base_phy = sim->vector_pool_dma.phy_addr;
            uint32_t offset = path->vector_list.buffer.phy_addr - base_phy;
            uint32_t index = offset / sim->vector_pool_block_size;
            pthread_mutex_lock(&sim->mutex);
            mpp_list_add_tail(&sim->vector_pool_nodes[index].list,
                              &sim->vector_pool_free_list);
            pthread_mutex_unlock(&sim->mutex);
        }
    } else {
        free(path->vector_list.addr);
    }

    memset(path, 0, sizeof(artvg_path_t));
}

/* ========== Path allocate ========== */

static int path_allocate(sim_context_t *sim, artvg_path_t *path)
{
    memset(path, 0, sizeof(artvg_path_t));
    path->vector_list.prev = path->vector_list.next = &path->vector_list;

    if (allocate_vector_section(sim, &path->vector_list) != 0)
        return -1;

    path->active_vector = &path->vector_list;
    return 0;
}

/* ========== Collect section info for analysis ========== */

typedef struct {
    artvg_path_buffer_t *section;
    int index;           /* 0 = head, 1+ = extra */
    uint8_t *cmd_data;   /* Pointer to command data (addr[0..used-1]) */
    uint8_t *cmd_copy;   /* Allocated copy for cross-mode comparison */
    uint32_t used;
    uint32_t size;       /* section total size */
    uint32_t phy_addr;
    uint64_t next_field; /* The 8-byte next field at addr[used] */
    int is_pool;
} section_info_t;

#define MAX_SECTIONS 32

typedef struct {
    section_info_t sections[MAX_SECTIONS];
    int count;
    uint32_t total_used;     /* Sum of all sections' used */
    uint32_t head_section_total; /* head->used + VECTOR_NEXT_FIELD_SIZE (config_vector would use) */
} path_analysis_t;

static void collect_path_info(artvg_path_t *path, path_analysis_t *info)
{
    memset(info, 0, sizeof(*info));

    artvg_path_buffer_t *section = &path->vector_list;
    int idx = 0;

    /* Head section first */
    info->sections[idx].section = section;
    info->sections[idx].index = idx;
    info->sections[idx].used = section->used;
    info->sections[idx].cmd_data = (uint8_t *)section->addr;
    if (section->used > 0) {
        info->sections[idx].cmd_copy = (uint8_t *)malloc(section->used);
        memcpy(info->sections[idx].cmd_copy, section->addr, section->used);
    }
    info->sections[idx].size = section->size;
    info->sections[idx].phy_addr = section->buffer.phy_addr;
    info->sections[idx].is_pool = !!(section->flag & ARTVG_PATH_BUF_FLAG_VECTOR_POOL);
    info->total_used += section->used;
    idx++;

    /* Extra sections */
    section = path->vector_list.next;
    while (section != &path->vector_list && idx < MAX_SECTIONS) {
        info->sections[idx].section = section;
        info->sections[idx].index = idx;
        info->sections[idx].used = section->used;
        info->sections[idx].cmd_data = (uint8_t *)section->addr;
        if (section->used > 0) {
            info->sections[idx].cmd_copy = (uint8_t *)malloc(section->used);
            memcpy(info->sections[idx].cmd_copy, section->addr, section->used);
        }
        info->sections[idx].size = section->size;
        info->sections[idx].phy_addr = section->buffer.phy_addr;
        info->sections[idx].is_pool = !!(section->flag & ARTVG_PATH_BUF_FLAG_VECTOR_POOL);
        info->total_used += section->used;
        idx++;
        section = section->next;
    }
    info->count = idx;
    info->head_section_total = path->vector_list.used + VECTOR_NEXT_FIELD_SIZE;
}

static void read_next_fields(artvg_path_t *path, path_analysis_t *info)
{
    (void)path;
    int i;
    for (i = 0; i < info->count; i++) {
        artvg_path_buffer_t *s = info->sections[i].section;
        uint8_t *next_field_pos = (uint8_t *)s->addr + s->used;
        uint64_t nf;
        memcpy(&nf, next_field_pos, sizeof(nf));
        info->sections[i].next_field = nf;
    }
}

/* ========== Print helpers ========== */

static void print_section_header(const char *label)
{
    printf("\n  %s\n", label);
    printf("  %-6s %-6s %8s %8s %-5s  %-18s  %s\n",
           "Idx", "Used", "Size", "PhyAddr", "Pool", "NextField(phys<<24|size)", "PhyChk");
    printf("  %-6s %-6s %8s %8s %-5s  %-18s  %s\n",
           "-----", "-----", "------", "------", "----", "-------------------", "------");
}

static void print_section_info(const path_analysis_t *info, int i)
{
    const section_info_t *si = &info->sections[i];
    uint64_t nf = si->next_field;
    uint64_t nf_phys = nf >> VECTOR_NEXT_ADDR_SHIFT;
    uint32_t nf_size = nf & VECTOR_NEXT_SIZE_MASK;

    char nf_str[64];
    if (nf == 0) {
        snprintf(nf_str, sizeof(nf_str), "0 (terminator)");
    } else {
        snprintf(nf_str, sizeof(nf_str), "phys=0x%" PRIx64 " size=%" PRIu32,
                 nf_phys, nf_size);
    }

    /* Verify the size field:
     * Head (i==0) encodes its own used+8; extra sections encode next section's used+8 */
    char phy_chk[32] = "";
    if (nf == 0) {
        snprintf(phy_chk, sizeof(phy_chk), "END");
    } else if (i == 0) {
        /* Head: next field size = head->used + 8 */
        uint32_t exp = info->sections[0].used + VECTOR_NEXT_FIELD_SIZE;
        snprintf(phy_chk, sizeof(phy_chk), nf_size == exp ? "sizeOK" : "SZ_MIS_H");
    } else if (i < info->count - 1) {
        /* Extra: next field size = next_section->used + 8 */
        uint32_t exp = info->sections[i + 1].used + VECTOR_NEXT_FIELD_SIZE;
        snprintf(phy_chk, sizeof(phy_chk), nf_size == exp ? "sizeOK" : "SZ_MIS_E");
    } else {
        snprintf(phy_chk, sizeof(phy_chk), "TAIL_NF_NZ");
    }

    printf("  %-6d %-6u %8u 0x%06x %-5s  %-18s  %s\n",
           si->index, si->used, si->size, si->phy_addr,
           si->is_pool ? "pool" : "heap",
           nf_str, phy_chk);
}

/* ========== Verification: command data comparison ========== */

static int verify_cmd_data_match(const path_analysis_t *ref, const path_analysis_t *test,
                                 const char *test_name)
{
    int errors = 0;
    int min_count = ref->count < test->count ? ref->count : test->count;
    int i;

    for (i = 0; i < min_count; i++) {
        uint32_t min_used = ref->sections[i].used < test->sections[i].used
                            ? ref->sections[i].used : test->sections[i].used;

        if (memcmp(ref->sections[i].cmd_copy, test->sections[i].cmd_copy, min_used) != 0) {
            printf("    FAIL: Section %d cmd data differs (first %u bytes)\n", i, min_used);
            errors++;
        }
    }

    if (ref->count != test->count) {
        printf("    FAIL: Section count differs: ref=%d, %s=%d\n",
               ref->count, test_name, test->count);
        errors++;
    }

    return errors;
}

/* ========== Verification: used values ========== */

static int verify_used_match(const path_analysis_t *ref, const path_analysis_t *test,
                             const char *test_name)
{
    int errors = 0;
    int min_count = ref->count < test->count ? ref->count : test->count;
    int i;

    for (i = 0; i < min_count; i++) {
        if (ref->sections[i].used != test->sections[i].used) {
            printf("    FAIL: Section %d used differs: ref=%u, %s=%u\n",
                   i, ref->sections[i].used, test_name, test->sections[i].used);
            errors++;
        }
    }

    return errors;
}

/* ========== Cleanup analysis copies ========== */

static void analysis_cleanup(path_analysis_t *info)
{
    int i;
    for (i = 0; i < info->count; i++) {
        free(info->sections[i].cmd_copy);
        info->sections[i].cmd_copy = NULL;
    }
}

/* ========== Verification: next field size component ========== */

static int verify_next_field_size(const path_analysis_t *ref, const path_analysis_t *test,
                                  const char *test_name)
{
    int errors = 0;
    int min_count = ref->count < test->count ? ref->count : test->count;
    int i;

    for (i = 0; i < min_count; i++) {
        uint32_t ref_size = ref->sections[i].next_field & VECTOR_NEXT_SIZE_MASK;
        uint32_t test_size = test->sections[i].next_field & VECTOR_NEXT_SIZE_MASK;
        if (ref_size != test_size) {
            printf("    FAIL: Section %d next size field differs: ref=0x%x, %s=0x%x\n",
                   i, ref_size, test_name, test_size);
            errors++;
        }
    }

    return errors;
}

/* ========== Verification: head section total (config_vector) ========== */

static int verify_head_total(const path_analysis_t *ref, const path_analysis_t *test,
                             const char *test_name)
{
    int errors = 0;
    if (ref->head_section_total != test->head_section_total) {
        printf("    FAIL: head_section_total differs: ref=%u, %s=%u\n",
               ref->head_section_total, test_name, test->head_section_total);
        errors++;
    }
    return errors;
}

/* ========== Verification: total command data across all sections ========== */

static int verify_total_data(const path_analysis_t *ref, const path_analysis_t *test,
                             const char *test_name)
{
    /* Concatenate all sections' command data and compare */
    uint8_t *ref_total = (uint8_t *)malloc(ref->total_used);
    uint8_t *test_total = (uint8_t *)malloc(test->total_used);
    int errors = 0;
    int i, offset = 0;

    for (i = 0; i < ref->count; i++) {
        if (ref->sections[i].cmd_copy && ref->sections[i].used > 0) {
            memcpy(ref_total + offset, ref->sections[i].cmd_copy, ref->sections[i].used);
            offset += ref->sections[i].used;
        }
    }

    offset = 0;
    for (i = 0; i < test->count; i++) {
        if (test->sections[i].cmd_copy && test->sections[i].used > 0) {
            memcpy(test_total + offset, test->sections[i].cmd_copy, test->sections[i].used);
            offset += test->sections[i].used;
        }
    }

    if (ref->total_used != test->total_used) {
        printf("    FAIL: Total data size differs: ref=%u, %s=%u\n",
               ref->total_used, test_name, test->total_used);
        errors++;
    } else if (memcmp(ref_total, test_total, ref->total_used) != 0) {
        printf("    FAIL: Total concatenated command data differs\n");
        errors++;
    } else {
        printf("    Total concatenated data: PASS (%u bytes identical)\n", ref->total_used);
    }

    free(ref_total);
    free(test_total);
    return errors;
}

/* ========== Verification: next field chain validity ========== */

static int verify_next_field_chain(const path_analysis_t *info, const char *mode)
{
    int errors = 0;
    int i;

    for (i = 0; i < info->count; i++) {
        uint64_t nf = info->sections[i].next_field;
        uint64_t nf_phys = nf >> VECTOR_NEXT_ADDR_SHIFT;
        uint32_t nf_size = nf & VECTOR_NEXT_SIZE_MASK;

        if (i < info->count - 1) {
            /* Has a next section: verify size field */
            uint32_t exp_size;
            if (i == 0) {
                /* Head: next field encodes head's own used + 8 */
                exp_size = info->sections[0].used + VECTOR_NEXT_FIELD_SIZE;
            } else {
                /* Extra: next field encodes next section's used + 8 */
                exp_size = info->sections[i + 1].used + VECTOR_NEXT_FIELD_SIZE;
            }
            if (nf_size != exp_size) {
                printf("    FAIL [%s]: Sec %d next size=0x%x, expected 0x%x\n",
                       mode, i, nf_size, exp_size);
                errors++;
            }

            /* Verify the next phys points to the correct section */
            uint32_t expected_phys = info->sections[i + 1].phy_addr;
            if (nf_phys != expected_phys) {
                printf("    FAIL [%s]: Sec %d next_phys=0x%" PRIx64 ", expected 0x%x\n",
                       mode, i, nf_phys, expected_phys);
                errors++;
            }
        } else {
            /* Last section should have terminator */
            if (nf != 0) {
                printf("    FAIL [%s]: Last section (idx %d) should have terminator, but next=0x%" PRIx64 "\n",
                       mode, i, nf);
                errors++;
            }
        }
    }

    return errors;
}

/* ========== Run a complete path test for one mode ========== */

typedef struct {
    const char *name;
    int section_size;       /* sim->section_size for non-pool fallback */
    bool use_pool;
    int pool_block_count;
    int pool_block_size;    /* 0 if no pool */
} mode_config_t;

static int run_path_test(const mode_config_t *cfg, artvg_path_t *path_out,
                         path_analysis_t *analysis,
                         void (*build_func)(sim_context_t *, artvg_path_t *))
{
    sim_context_t sim;
    memset(&sim, 0, sizeof(sim));
    sim.mode_name = cfg->name;
    sim.section_size = cfg->section_size;
    sim.next_phy_addr = 0x10000000; /* Start of simulated MMIO space */
    pthread_mutex_init(&sim.mutex, NULL);

    if (cfg->use_pool) {
        if (enable_vector_buffer_pool(&sim, cfg->pool_block_count, cfg->pool_block_size) != 0) {
            printf("  FAIL: Could not enable pool for %s\n", cfg->name);
            pthread_mutex_destroy(&sim.mutex);
            return -1;
        }
    }

    if (path_allocate(&sim, path_out) != 0) {
        printf("  FAIL: Could not allocate path for %s\n", cfg->name);
        if (cfg->use_pool)
            disable_vector_buffer_pool(&sim);
        pthread_mutex_destroy(&sim.mutex);
        return -1;
    }

    build_func(&sim, path_out);

    path_finish(&sim, path_out);

    collect_path_info(path_out, analysis);
    read_next_fields(path_out, analysis);

    /* Free path (returns pool blocks if applicable), then disable pool */
    path_free(&sim, path_out);
    if (cfg->use_pool)
        disable_vector_buffer_pool(&sim);

    pthread_mutex_destroy(&sim.mutex);
    return 0;
}

/* ========== Test Case Builders ========== */

/* ===== Test 1: Standard multi-section path (~6800 bytes) ===== */

static void build_test1_path(sim_context_t *sim, artvg_path_t *path)
{
    path_move_to(sim, path, 10.5f, 20.3f);
    for (int i = 0; i < 200; i++)
        path_line_to(sim, path, i * 1.0f, i * 2.0f);
    for (int i = 0; i < 100; i++)
        path_quad_to(sim, path, (float)i, (float)i, (float)(i + 1), (float)(i + 1));
    for (int i = 0; i < 150; i++)
        path_cubic_to(sim, path, (float)i, (float)i,
                      (float)(i + 1), (float)(i + 1),
                      (float)(i + 2), (float)(i + 2));
}

/* ===== Test 2: Exact fill section ===== */
/* Fill section data area exactly + one extra command */

static void build_test2_path(sim_context_t *sim, artvg_path_t *path)
{
    /* Each MOVE/LINE = 8 bytes. section data = sim->section_size - 8 */
    int section_data = sim->section_size - VECTOR_NEXT_FIELD_SIZE;
    int count = section_data / VECTOR_CMD_HW_SIZE_LINE; /* 4088/8 = 511 (for 4096) or 1016/8=127 (for 1024) */

    path_move_to(sim, path, 1.0f, 2.0f);
    for (int i = 0; i < count - 1; i++)
        path_line_to(sim, path, (float)(i + 1), (float)(i + 2));
    /* One more to trigger section switch */
    path_line_to(sim, path, 999.0f, 888.0f);
}

/* ===== Test 3: Cross-section single command ===== */
/* Position write so a quad (16 bytes) straddles section boundary */

static void build_test3_path(sim_context_t *sim, artvg_path_t *path)
{
    int section_data = sim->section_size - VECTOR_NEXT_FIELD_SIZE;
    int line_count = (section_data - VECTOR_CMD_HW_SIZE_MOVE) / VECTOR_CMD_HW_SIZE_LINE;

    path_move_to(sim, path, 1.0f, 2.0f);
    /* Fill right up to (section_data - 15) so a 16-byte quad will barely not fit */
    int bytes_used = VECTOR_CMD_HW_SIZE_MOVE + line_count * VECTOR_CMD_HW_SIZE_LINE;
    while (bytes_used + VECTOR_CMD_HW_SIZE_QUAD > section_data) {
        line_count--;
        bytes_used = VECTOR_CMD_HW_SIZE_MOVE + line_count * VECTOR_CMD_HW_SIZE_LINE;
    }
    /* Add a few more bytes to make remaining < 16 */
    int extra = section_data - bytes_used;
    if (extra >= VECTOR_CMD_HW_SIZE_LINE) {
        path_line_to(sim, path, 100.0f, 200.0f);
        bytes_used += VECTOR_CMD_HW_SIZE_LINE;
    }

    /* Now remaining should be < 16, so quad triggers section switch */
    path_quad_to(sim, path, 10.0f, 20.0f, 30.0f, 40.0f);
    path_cubic_to(sim, path, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f);
}

/* ===== Test 4: Empty path (move only) ===== */

static void build_test4_path(sim_context_t *sim, artvg_path_t *path)
{
    path_move_to(sim, path, 100.0f, 200.0f);
}

/* ===== Test 5: Large path (5+ sections) ===== */

static void build_test5_path(sim_context_t *sim, artvg_path_t *path)
{
    path_move_to(sim, path, 0.0f, 0.0f);
    /* Many small commands to spread over many sections */
    for (int i = 0; i < 2000; i++)
        path_line_to(sim, path, (float)i, (float)(i * 2));
}

/* ========== Print section details ========== */

static void print_path_details(const path_analysis_t *info, const char *label)
{
    printf("    %s: %d sections, total_used=%u bytes, head_total=%u\n",
           label, info->count, info->total_used, info->head_section_total);

    print_section_header("Section details:");
    int i;
    for (i = 0; i < info->count; i++)
        print_section_info(info, i);
}

/* ========== Comprehensive verification between two modes ========== */

static int verify_modes_match(const path_analysis_t *ref, const path_analysis_t *test,
                              const char *test_name)
{
    int total_errors = 0;
    int e;

    printf("\n    --- Verifying %s vs Reference ---\n", test_name);

    e = verify_cmd_data_match(ref, test, test_name);
    printf("    Command data match: %s\n", e ? "FAIL" : "PASS");
    total_errors += e;

    e = verify_used_match(ref, test, test_name);
    printf("    Section used match: %s\n", e ? "FAIL" : "PASS");
    total_errors += e;

    e = verify_next_field_size(ref, test, test_name);
    printf("    Next field size  : %s\n", e ? "FAIL" : "PASS");
    total_errors += e;

    e = verify_head_total(ref, test, test_name);
    printf("    Head total match: %s\n", e ? "FAIL" : "PASS");
    total_errors += e;

    return total_errors;
}

/* ========== Run one test case across all 3 modes ========== */

typedef struct {
    const char *test_name;
    void (*build_func)(sim_context_t *, artvg_path_t *);
} test_case_t;

static int run_test_case(const test_case_t *tc)
{
    printf("\n============================================================\n");
    printf("Test: %s\n", tc->test_name);
    printf("============================================================\n");

    mode_config_t modes[] = {
        {"ORIG4K", 4096, false, 0, 0},
        {"POOL4K", 4096, true,  32, 4096},
        {"POOL1K", 4096, true,  64, 1024},
    };
    int nmodes = sizeof(modes) / sizeof(modes[0]);

    artvg_path_t paths[3];
    path_analysis_t analyses[3];
    int i;

    for (i = 0; i < nmodes; i++) {
        printf("\n  --- Mode %s ---\n", modes[i].name);
        if (run_path_test(&modes[i], &paths[i], &analyses[i], tc->build_func) != 0) {
            printf("  FAILED to build path in mode %s\n", modes[i].name);
            return 1;
        }
        print_path_details(&analyses[i], modes[i].name);
    }

    /* Verify next field chain for each mode independently */
    printf("\n  --- Next Field Chain Verification ---\n");
    int chain_ok = 0;
    for (i = 0; i < nmodes; i++) {
        int e = verify_next_field_chain(&analyses[i], modes[i].name);
        printf("    %s chain: %s\n", modes[i].name, e ? "FAIL" : "PASS");
        chain_ok += e;
    }

    /* Cross-compare: ORIG4K vs POOL4K (same section size, section-by-section) */
    printf("\n  --- Cross-Mode Comparison ---\n");
    int cross_errors = 0;
    cross_errors += verify_modes_match(&analyses[0], &analyses[1], "POOL4K");

    /* ORIG4K vs POOL1K (different section sizes: compare total concatenated data) */
    printf("\n    --- Verifying POOL1K total data vs Reference ---\n");
    int e = verify_total_data(&analyses[0], &analyses[2], "POOL1K");
    if (e) cross_errors += e;
    e = verify_head_total(&analyses[0], &analyses[2], "POOL1K");
    printf("    Head total match: %s\n", e ? "FAIL (expected for different block sizes)" : "PASS (same block size)");
    /* head_total differs when block sizes differ - that's expected */
    if (analyses[0].total_used != analyses[2].total_used) {
        printf("    WARN: total_used differs between modes\n");
    } else {
        printf("    Total used match: PASS (%u bytes)\n", analyses[0].total_used);
    }

    /* Cleanup analysis copies (paths already freed inside run_path_test) */
    for (i = 0; i < nmodes; i++)
        analysis_cleanup(&analyses[i]);

    printf("\n  --- Result: %s ---\n",
           (chain_ok == 0 && cross_errors == 0) ? "ALL PASS" : "SOME FAILED");

    return (chain_ok == 0 && cross_errors == 0) ? 0 : 1;
}

/* ========== Multi-threaded pool stress test ========== */

typedef struct {
    sim_context_t *sim;
    int thread_id;
    int ops_per_thread; /* number of alloc/free cycles */
} thread_arg_t;

static void *thread_worker(void *arg)
{
    thread_arg_t *ta = (thread_arg_t *)arg;
    sim_context_t *sim = ta->sim;
    int alloc_count = 0;

    for (int i = 0; i < ta->ops_per_thread; i++) {
        artvg_path_t path;
        if (path_allocate(sim, &path) != 0) {
            printf("    [Thread %d] FAIL to allocate at iter %d\n", ta->thread_id, i);
            return (void *)(intptr_t)-1;
        }

        /* Write some path commands */
        path_move_to(sim, &path, 1.0f, 2.0f);
        for (int j = 0; j < 10; j++)
            path_line_to(sim, &path, (float)j, (float)(j * 2));

        path_finish(sim, &path);

        /* Count sections */
        int sec_count = 0;
        artvg_path_buffer_t *s = path.vector_list.next;
        while (s != &path.vector_list) { sec_count++; s = s->next; }
        alloc_count += sec_count + 1; /* +1 for head */

        path_free(sim, &path);
    }

    return (void *)(intptr_t)alloc_count;
}

static int run_multithread_test(void)
{
    int nthreads = 4;
    int ops_per_thread = 100;
    int pool_blocks = 64;
    int block_size = 1024;

    printf("\n============================================================\n");
    printf("Multi-threaded Pool Stress Test\n");
    printf("  Threads: %d, Ops/thread: %d, Pool blocks: %d, Block size: %d\n",
           nthreads, ops_per_thread, pool_blocks, block_size);
    printf("============================================================\n");

    sim_context_t sim;
    memset(&sim, 0, sizeof(sim));
    sim.mode_name = "MT-POOL";
    sim.section_size = block_size;
    sim.next_phy_addr = 0x20000000;
    pthread_mutex_init(&sim.mutex, NULL);

    if (enable_vector_buffer_pool(&sim, pool_blocks, block_size) != 0) {
        printf("  FAIL: Could not enable pool\n");
        pthread_mutex_destroy(&sim.mutex);
        return 1;
    }

    pthread_t threads[8];
    thread_arg_t args[8];
    int total_alloc = 0;
    int i;

    for (i = 0; i < nthreads; i++) {
        args[i].sim = &sim;
        args[i].thread_id = i;
        args[i].ops_per_thread = ops_per_thread;
    }

    for (i = 0; i < nthreads; i++) {
        pthread_create(&threads[i], NULL, thread_worker, &args[i]);
    }

    for (i = 0; i < nthreads; i++) {
        void *ret;
        pthread_join(threads[i], &ret);
        intptr_t allocs = (intptr_t)ret;
        if (allocs < 0) {
            printf("  Thread %d FAILED\n", i);
            total_alloc = -1;
        } else {
            printf("  Thread %d completed, allocated %d sections\n", i, (int)allocs);
            total_alloc += (int)allocs;
        }
    }

    /* Verify free list has all blocks */
    uint32_t free_count = 0;
    vector_pool_node_t *pos, *n;
    mpp_list_for_each_entry_safe(pos, n, &sim.vector_pool_free_list, list) {
        free_count++;
    }

    printf("\n  Pool free blocks: %u/%u\n", free_count, sim.vector_pool_block_count);

    int result = 0;
    if (total_alloc < 0) {
        printf("  Multi-thread test: FAIL (thread error)\n");
        result = 1;
    } else if (free_count == sim.vector_pool_block_count) {
        printf("  Multi-thread test: PASS (all blocks returned, %d total allocs)\n", total_alloc);
    } else {
        printf("  Multi-thread test: FAIL (pool leak: %u blocks missing)\n",
               sim.vector_pool_block_count - free_count);
        result = 1;
    }

    disable_vector_buffer_pool(&sim);
    pthread_mutex_destroy(&sim.mutex);
    return result;
}

/* ========== Hybrid test: pool sections + heap sections in same path ========== */

static int run_hybrid_pool_heap_test(void)
{
    printf("\n============================================================\n");
    printf("Hybrid Pool+Heap Test\n");
    printf("  Small pool (2 blocks x 512 bytes), path needs ~4 sections\n");
    printf("  Sections 0-1 from pool, sections 2+ from heap fallback\n");
    printf("============================================================\n");

    /* Config: small pool, large path to force overflow */
    uint32_t pool_blocks = 2;
    uint32_t block_size = 512;

    sim_context_t sim;
    memset(&sim, 0, sizeof(sim));
    sim.mode_name = "HYBRID";
    sim.section_size = block_size;
    sim.next_phy_addr = 0x30000000;
    pthread_mutex_init(&sim.mutex, NULL);

    if (enable_vector_buffer_pool(&sim, pool_blocks, block_size) != 0) {
        printf("  FAIL: Could not enable pool\n");
        pthread_mutex_destroy(&sim.mutex);
        return 1;
    }

    artvg_path_t path;
    path_analysis_t analysis;
    memset(&analysis, 0, sizeof(analysis));

    if (path_allocate(&sim, &path) != 0) {
        printf("  FAIL: Could not allocate path\n");
        disable_vector_buffer_pool(&sim);
        pthread_mutex_destroy(&sim.mutex);
        return 1;
    }

    /* Build path: fill ~4+ sections.
     * 512-byte blocks, 504 bytes data per section (512-8).
     * move=8, each line=8 => 63 lines per section.
     * 4*63 + 1 move = 253 lines + 1 move ~= 2032 bytes => ~4 sections.
     */
    path_move_to(&sim, &path, 1.0f, 2.0f);
    for (int i = 0; i < 300; i++)
        path_line_to(&sim, &path, (float)i, (float)(i * 2));

    path_finish(&sim, &path);

    collect_path_info(&path, &analysis);
    read_next_fields(&path, &analysis);

    printf("\n  Path sections: %d, total_used=%u bytes\n",
           analysis.count, analysis.total_used);

    print_section_header("Section details:");
    int i;
    for (i = 0; i < analysis.count; i++)
        print_section_info(&analysis, i);

    /* Verify chain linking */
    printf("\n  --- Chain Verification ---\n");
    int chain_err = verify_next_field_chain(&analysis, "HYBRID");
    printf("    Chain: %s\n", chain_err ? "FAIL" : "PASS");

    /* Verify pool vs heap allocation pattern */
    printf("\n  --- Allocation Source Verification ---\n");
    int source_err = 0;
    for (i = 0; i < analysis.count; i++) {
        int expected_pool = (i < (int)pool_blocks);
        int actual_pool = analysis.sections[i].is_pool;
        if (expected_pool != actual_pool) {
            printf("    FAIL: Section %d expected %s, got %s\n",
                   i,
                   expected_pool ? "pool" : "heap",
                   actual_pool ? "pool" : "heap");
            source_err++;
        } else {
            printf("    Section %d: %s (expected) -> %s (actual)  %s\n",
                   i,
                   expected_pool ? "pool" : "heap",
                   actual_pool ? "pool" : "heap",
                   "OK");
        }
    }

    /* Verify pool blocks correctly freed, heap blocks don't interfere */
    path_free(&sim, &path);

    uint32_t free_count = 0;
    vector_pool_node_t *pos, *n;
    mpp_list_for_each_entry_safe(pos, n, &sim.vector_pool_free_list, list) {
        free_count++;
    }

    printf("\n  Pool blocks after free: %u/%u returned\n",
           free_count, pool_blocks);

    int free_err = 0;
    if (free_count != pool_blocks) {
        printf("    FAIL: %u blocks leaked\n", pool_blocks - free_count);
        free_err = 1;
    } else {
        printf("    Pool free: PASS\n");
    }

    /* Cleanup */
    analysis_cleanup(&analysis);
    disable_vector_buffer_pool(&sim);
    pthread_mutex_destroy(&sim.mutex);

    printf("\n  --- Result: %s ---\n",
           (chain_err == 0 && source_err == 0 && free_err == 0) ? "PASS" : "FAIL");

    return (chain_err == 0 && source_err == 0 && free_err == 0) ? 0 : 1;
}

/* ========== Main ========== */

int main(void)
{
    printf("============================================================\n");
    printf("ArtVG Vector Pool Verification Program\n");
    printf("Simulating two allocation methods:\n");
    printf("  ORIG4K: heap-allocated sections (4096 bytes each)\n");
    printf("  POOL4K: pool-allocated blocks (4096 bytes each)\n");
    printf("  POOL1K: pool-allocated blocks (1024 bytes each)\n");
    printf("============================================================\n");

    test_case_t tests[] = {
        {"Test 1: Multi-section path (move+200line+100quad+150cubic ~6800 bytes)",
         build_test1_path},
        {"Test 2: Exact fill section boundary",
         build_test2_path},
        {"Test 3: Cross-section single command (quad straddles boundary)",
         build_test3_path},
        {"Test 4: Empty path (move only)",
         build_test4_path},
        {"Test 5: Large path (2000 line commands, 5+ sections)",
         build_test5_path},
    };
    int ntests = sizeof(tests) / sizeof(tests[0]);

    int pass_count = 0;
    int fail_count = 0;
    int i;

    for (i = 0; i < ntests; i++) {
        if (run_test_case(&tests[i]) == 0) {
            pass_count++;
        } else {
            fail_count++;
        }
    }

    int mt_result = run_multithread_test();
    int hybrid_result = run_hybrid_pool_heap_test();

    printf("\n\n");
    printf("============================================================\n");
    printf("SUMMARY\n");
    printf("============================================================\n");
    printf("  Path tests passed    : %d/%d\n", pass_count, ntests);
    printf("  Path tests failed    : %d/%d\n", fail_count, ntests);
    printf("  Multi-thread test    : %s\n", mt_result == 0 ? "PASS" : "FAIL");
    printf("  Hybrid pool+heap test: %s\n", hybrid_result == 0 ? "PASS" : "FAIL");
    printf("\n  Overall          : %s\n",
           (fail_count == 0 && mt_result == 0 && hybrid_result == 0)
               ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    printf("============================================================\n");

    return (fail_count > 0 || mt_result != 0 || hybrid_result != 0) ? 1 : 0;
}
