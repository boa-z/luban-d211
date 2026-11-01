/*
 * Copyright (c) 2024-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#ifndef _BT_YC8628_H_
#define _BT_YC8628_H_

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

// Maximum payload size for a frame
#define YC_FRAME_PAYLOAD_MAX_SIZE           32
// Minimum frame size (packet type + opcode + length)
#define YC_MIN_FRAME_SIZE                   3
// Maximum protocol frame size (payload + minimum frame size)
#define YC_PROTOCOL_FRAME_MAX_SIZE          (YC_FRAME_PAYLOAD_MAX_SIZE + YC_MIN_FRAME_SIZE)
// Timeout for protocol operations in milliseconds
#define YC_PROTOCOL_TIMEOUT                 200

#define YC_WIDTH    1920
#define YC_HEIGHT   1920

// Packet types used in communication
typedef enum
{
    PACKET_TYPE_CMD     = 0x01,   // Command packet type
    PACKET_TYPE_EVENT   = 0x02,   // Event packet type
} bt_yc_packet_type;

// Command opcodes for various operations
typedef enum
{
    OPCODE_CMD_SET_LE_ADDR_REQ  = 0x01,  // Set LE address request
    OPCODE_CMD_SET_VISIBILITY   = 0x02,  // Set visibility
    OPCODE_CMD_SET_LE_NAME      = 0x04,  // Set LE name
    OPCODE_CMD_READ_LE_NAME     = 0x05,  // Read LE name
    OPCODE_CMD_SET_UART_DATA    = 0x09,  // Set UART data
    OPCODE_CMD_VERSION_REQ      = 0x10,  // Firmware version request
    OPCODE_CMD_BLE_DISCONNECT   = 0x12,  // BLE disconnect command
    OPCODE_CMD_RESET_CHIP_REQ   = 0x51,  // Reset chip request
} bt_yc_opcode_cmd_e;

// Event opcodes for responses and notifications
typedef enum
{
    OPCODE_EVENT_CONNECTED      = 0x02,  // Device connected event
    OPCODE_EVENT_DISCONNECTED   = 0x05,  // Device disconnected event
    OPCODE_EVENT_RESPONSE       = 0x06,  // General response event
    OPCODE_EVENT_COMPLETE       = 0x09,  // Operation complete event
} bt_yc_opcode_event_e;

// Data-related opcodes
typedef enum
{
    OPCODE_DATA_TYPE      		= 0x17,  // Data type identifier
    OPCODE_DATA_REPORT_ID   	= 0x00,  // Data report ID
} bt_yc_opcode_data_e;

// Structure representing a protocol frame
typedef struct 
{
    uint8_t packet_type;              // Type of packet (command or event)
    uint8_t opcode;                   // Operation code
    uint8_t length;                   // Length of payload
    uint8_t payload[YC_FRAME_PAYLOAD_MAX_SIZE];  // Payload data
} bt_yc_frame_t;

// Callback function pointer for read/write operations
typedef void (*callback_t)(char *data, int size);

// Client structure containing callback functions
typedef struct
{
	callback_t cb_write;  // Callback for write operations
	callback_t cb_read;   // Callback for read operations
} client_t;

// Function declarations
client_t *bt_yc_hid_device_init(client_t *cli, callback_t cb_w, callback_t cb_r);  // Initialize HID device
int bt_yc_frame_parse(char *buf, int len);                                        // Parse incoming frame
int bt_yc_hid_msg_set(client_t *cli, char *hid_msg, int size);                    // Set HID message
int bt_yc_firmware_ver_get(client_t *cli, char *ver);                             // Get firmware version
int bt_yc_hid_rf_on(client_t *cli);                                               // Turn on RF for HID
int bt_yc_hid_rf_off(client_t *cli);                                              // Turn off RF for HID
int bt_yc_hid_name_set(client_t *cli, char *name_msg, int size);                  // Set HID device name
int bt_yc_hid_name_get(client_t *cli, char *name_msg, int size);                  // Get HID device name
int bt_yc_hid_device_disconnect(client_t *cli);                                   // Disconnect HID device
int bt_yc_hid_camera_set(client_t *cli);                                          // Take a picture

#endif
