/*
 * Copyright (c) 2024-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#ifdef ENABLE_BT_YC8628

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdlib.h>
#include <limits.h>

#include "touch_input.h"

/**
 * @brief Initializes a FIFO linked list.
 * 
 * This function sets up the initial state of the FIFO linked list by initializing
 * the head and tail pointers to NULL, setting the element count to zero, and
 * initializing the mutex for thread safety.
 * 
 * @param list Pointer to the FIFOLinkedList structure to initialize.
 */
void fifo_init(FIFOLinkedList* list)
{
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
    pthread_mutex_init(&list->mutex, NULL);
}

/**
 * @brief Adds a touch point to the end of the FIFO linked list.
 * 
 * This function allocates memory for a new node, copies the provided touch point
 * data into the node, and appends it to the end of the FIFO queue. Thread-safe
 * operations are ensured using a mutex lock.
 * 
 * @param list Pointer to the FIFOLinkedList structure.
 * @param tp Pointer to the TouchPoint structure containing the data to add.
 * @return 0 on success, -1 if memory allocation fails.
 */
int fifo_push(FIFOLinkedList* list, const TouchPoint* tp)
{
    Node* new_node = (Node*)malloc(sizeof(Node));
    if (!new_node) {
        return -1; // Memory allocation failed
    }

    new_node->data = *tp;
    new_node->next = NULL;

    pthread_mutex_lock(&list->mutex);
    if (list->tail) {
        list->tail->next = new_node;
    } else {
        list->head = new_node;
    }
    list->tail = new_node;
    list->count++;
    pthread_mutex_unlock(&list->mutex);

    return 0; // Success
}

/**
 * @brief Removes and retrieves the first touch point from the FIFO linked list.
 * 
 * This function removes the head node from the FIFO queue, copies its data to
 * the provided TouchPoint structure, and frees the node's memory. Thread-safe
 * operations are ensured using a mutex lock.
 * 
 * @param list Pointer to the FIFOLinkedList structure.
 * @param tp Pointer to the TouchPoint structure where the retrieved data will be stored.
 * @return 0 on success, -1 if the queue is empty.
 */
int fifo_pop(FIFOLinkedList* list, TouchPoint* tp)
{
    pthread_mutex_lock(&list->mutex);
    if (!list->head) {
        pthread_mutex_unlock(&list->mutex);
        return -1; // Queue is empty
    }

    Node* temp = list->head;
    *tp = temp->data;
    list->head = list->head->next;
    if (!list->head) {
        list->tail = NULL;
    }
    list->count--;
    free(temp);

    pthread_mutex_unlock(&list->mutex);
    return 0; // Success
}

/**
 * @brief Destroys the FIFO linked list and frees all associated resources.
 * 
 * This function iteratively removes all nodes from the FIFO queue, freeing their
 * memory, and destroys the mutex used for thread synchronization.
 * 
 * @param list Pointer to the FIFOLinkedList structure to destroy.
 */
void fifo_destroy(FIFOLinkedList* list)
{
    pthread_mutex_lock(&list->mutex);
    while (list->head) {
        Node* temp = list->head;
        list->head = list->head->next;
        free(temp);
    }
    list->tail = NULL;
    list->count = 0;
    pthread_mutex_unlock(&list->mutex);
    pthread_mutex_destroy(&list->mutex);
}

/**
 * @brief Initializes a resolution mapping structure with source and target dimensions.
 * 
 * This function sets the source and target screen dimensions in the provided
 * ResolutionMap structure, which is used for coordinate transformation.
 * 
 * @param map Pointer to the ResolutionMap structure to initialize.
 * @param src_w Source screen width.
 * @param src_h Source screen height.
 * @param dst_w Target screen width.
 * @param dst_h Target screen height.
 * @param pic_w Source image width.
 * @param pic_h Source image height.
 */
void init_resolution_map(ResolutionMap* map, int src_w, int src_h, int dst_w, int dst_h, int pic_w, int pic_h)
{
    map->src_width = src_w;
    map->src_height = src_h;
    map->dst_width = dst_w;
    map->dst_height = dst_h;
    map->img_width = pic_w;
    map->img_height = pic_h;
    map->offset_x = (map->src_width - map->img_width) / 2;
    map->offset_y = (map->src_height - map->img_height) / 2;
}

/**
 * @brief Maps coordinates from source resolution to target resolution.
 * 
 * This function transforms the provided coordinates in two stages:
 * 1. First, maps coordinates from source resolution to an intermediate image resolution with offset
 * 2. Then, scales the coordinates from the intermediate resolution to the final target resolution
 * 
 * The transformation includes:
 * - Input coordinate validation and constraining to source resolution boundaries
 * - Proportional mapping using floating-point ratios
 * - Addition of offset values to position coordinates correctly
 * - Final scaling to destination resolution with overflow protection
 * - Boundary checks to ensure final coordinates are within target resolution
 *
 * @param map Pointer to the ResolutionMap structure containing source, intermediate, and target dimensions.
 * @param x Pointer to the X-coordinate to be transformed (modified in-place).
 * @param y Pointer to the Y-coordinate to be transformed (modified in-place).
 */
void map_coordinates(const ResolutionMap* map, int* x, int* y)
{
    // Check for null pointers and invalid dimensions
    if (!map || !x || !y || map->img_width <= 0 || map->img_height <= 0) {
        return;
    }

    // Constrain input coordinates to valid screen range
    int constrained_x = (*x < 0) ? 0 : ((*x > map->src_width) ? map->src_width : *x);
    int constrained_y = (*y < 0) ? 0 : ((*y > map->src_height) ? map->src_height : *y);

    // Map the entire screen coordinates to the centered image coordinates
    // Left half of screen maps to left half of image, right half to right half
    float x_ratio = (float)constrained_x / map->src_width;
    float y_ratio = (float)constrained_y / map->src_height;

    // Apply ratios to image dimensions
    int temp_x = (int)(x_ratio * map->img_width);
    int temp_y = (int)(y_ratio * map->img_height);

    // Apply offset
    temp_x = temp_x + map->offset_x;
    temp_y = temp_y + map->offset_y;

    // Apply final linear mapping to destination resolution
    // Prevent integer overflow by checking values before multiplication
    if (temp_x > 0 && map->dst_width > 0 && temp_x > INT_MAX / map->dst_width) {
        temp_x = map->dst_width; // Set to max to prevent overflow
    } else {
        temp_x = temp_x * map->dst_width / map->src_width;
    }

    if (temp_y > 0 && map->dst_height > 0 && temp_y > INT_MAX / map->dst_height) {
        temp_y = map->dst_height; // Set to max to prevent overflow
    } else {
        temp_y = temp_y * map->dst_height / map->src_height;
    }

    // Ensure final coordinates are within bounds
    temp_x = (temp_x < 0) ? 0 : (temp_x > map->dst_width ? map->dst_width : temp_x);
    temp_y = (temp_y < 0) ? 0 : (temp_y > map->dst_height ? map->dst_height : temp_y);

    *x = temp_x;
    *y = temp_y;
}

#endif
