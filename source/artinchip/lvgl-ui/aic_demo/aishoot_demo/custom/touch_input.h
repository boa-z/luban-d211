/*
 * Copyright (c) 2024-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#ifndef TOUCH_INPUT_H
#define TOUCH_INPUT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define MAX_TOUCH_POINTS 2  // Maximum number of touch points supported

// Structure to store touch information
typedef struct {
    int x[MAX_TOUCH_POINTS];     // Array of X coordinates (raw values)
    int y[MAX_TOUCH_POINTS];     // Array of Y coordinates (raw values)
    int id[MAX_TOUCH_POINTS];    // Array of touch IDs (for multi-touch support)
    int pressed[MAX_TOUCH_POINTS]; // Array of press states (1 = pressed, 0 = released)
    int point_count;             // Number of active touch points
} TouchPoint;

// Node structure for dynamic FIFO linked list
typedef struct Node {
    TouchPoint data;      // Data stored in the node
    struct Node* next;    // Pointer to the next node
} Node;

// FIFO queue management structure
typedef struct {
    Node* head;              // Pointer to the front of the queue
    Node* tail;              // Pointer to the rear of the queue
    int count;               // Current number of elements in the queue
    pthread_mutex_t mutex;   // Mutex for thread synchronization
} FIFOLinkedList;

// Structure for resolution mapping parameters
typedef struct {
    int src_width;   // Original screen width
    int src_height;  // Original screen height
    int dst_width;   // Target resolution width
    int dst_height;  // Target resolution height
    int img_width;   // Centered image width
    int img_height;  // Centered image height
    int offset_x;    // Horizontal offset of centered image
    int offset_y;    // Vertical offset of centered image
} ResolutionMap;

// Function declarations
void fifo_init(FIFOLinkedList* list);  // Initialize the FIFO linked list
int fifo_push(FIFOLinkedList* list, const TouchPoint* tp);  // Push a touch point into the FIFO
int fifo_pop(FIFOLinkedList* list, TouchPoint* tp);         // Pop a touch point from the FIFO
void fifo_destroy(FIFOLinkedList* list);                   // Destroy the FIFO linked list
void init_resolution_map(ResolutionMap* map, int src_w, int src_h, int dst_w, int dst_h, int pic_w, int pic_h);  // Initialize resolution mapping
void map_coordinates(const ResolutionMap* map, int* x, int* y);  // Map coordinates based on resolution mapping

#endif // TOUCH_INPUT_H
