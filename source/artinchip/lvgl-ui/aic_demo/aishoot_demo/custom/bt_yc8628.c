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

#include "bt_yc8628.h"

/**
 * @brief Validates the protocol frame header.
 * 
 * Checks if the buffer contains a valid protocol frame header based on the minimum frame size
 * and packet type. Returns specific error codes for invalid frames.
 * 
 * @param buf Pointer to the buffer containing the frame data.
 * @param len Length of the buffer.
 * @return 0 if the frame is valid, -1 for incomplete frames, 2 for invalid packet types.
 */
static int bt_yc_proto_framecheck(char *buf, int len)
{
    if (len < YC_MIN_FRAME_SIZE) {
        return (*buf == PACKET_TYPE_EVENT) ? -1 : 2;
    }

    const unsigned char *offset = (const unsigned char *)buf;
    bt_yc_frame_t frame = {0};

    frame.packet_type = *offset++;
    if (frame.packet_type != PACKET_TYPE_EVENT) {
        return 2;
    }

    frame.opcode = *offset++;
    frame.length = *offset++;

    if (len != YC_MIN_FRAME_SIZE + frame.length) {
        return -1;
    }

    return 0;
}

/**
 * @brief Parses and formats a protocol frame.
 * 
 * Extracts the packet type, opcode, length, and payload from the buffer into the provided frame structure.
 * Validates the packet type and ensures the payload does not exceed the maximum allowed size.
 * 
 * @param buf Pointer to the buffer containing the frame data.
 * @param len Length of the buffer.
 * @param frame Pointer to the bt_yc_frame_t structure to populate.
 * @return 0 on success, -1 if the frame is invalid or parameters are incorrect.
 */
static int bt_yc_frame_fmt_package(char *buf, int len, bt_yc_frame_t *frame)
{
    if (!frame || !buf || len <= 0) {
        return -1;
    }

    const unsigned char *offset = (const unsigned char *)buf;
    frame->packet_type = *offset++;
    frame->opcode = *offset++;

    if (frame->packet_type != PACKET_TYPE_EVENT) {
        return -1;
    }

    frame->length = *offset++;
    frame->length = (frame->length > YC_FRAME_PAYLOAD_MAX_SIZE)
                        ? YC_FRAME_PAYLOAD_MAX_SIZE
                        : frame->length;
    memcpy(frame->payload, offset, frame->length);

    return 0;
}

/**
 * @brief Parses a complete protocol frame and handles events.
 * 
 * Validates the frame using bt_yc_proto_framecheck and formats it using bt_yc_frame_fmt_package.
 * Handles different opcodes by switching on the frame's opcode field.
 * 
 * @param buf Pointer to the buffer containing the frame data.
 * @param len Length of the buffer.
 * @return The opcode of the parsed frame, or an error code if parsing fails.
 */
int bt_yc_frame_parse(char *buf, int len)
{
    bt_yc_frame_t frame = {0};
    int ret = bt_yc_proto_framecheck(buf, len);
    if (ret != 0) {
        perror("Protocol frame check error\n");
        return ret;
    }

    ret = bt_yc_frame_fmt_package(buf, len, &frame);
    if (ret != 0) {
        perror("Frame format package error\n");
        return ret;
    }

    switch (frame.opcode) {
        case OPCODE_EVENT_CONNECTED:
            // Handle connected event
            break;
        case OPCODE_EVENT_DISCONNECTED:
            // Handle disconnected event
            break;
        case OPCODE_EVENT_RESPONSE:
            // Handle response event
            break;
        case OPCODE_EVENT_COMPLETE:
            // Handle complete event
            break;
        default:
            // Handle unknown opcode
            break;
    }

    return frame.opcode;
}

/**
 * @brief Formats a command frame for transmission.
 * 
 * Constructs a command frame with the specified opcode and optional payload.
 * Copies the payload into the buffer if provided.
 * 
 * @param opcode The command opcode to include in the frame.
 * @param buf Pointer to the buffer where the formatted frame will be stored.
 * @param payload Pointer to the payload data (optional).
 * @param payload_len Length of the payload data.
 * @return The total length of the formatted frame, or -1 if the buffer is invalid.
 */
static int bt_yc_protocol_format(bt_yc_opcode_cmd_e opcode, char *buf, 
                                uint8_t *payload, uint8_t payload_len)
{
    if (!buf) {
        return -1;
    }

    buf[0] = PACKET_TYPE_CMD;
    buf[1] = opcode;
    buf[2] = payload_len;

    int frame_len = YC_MIN_FRAME_SIZE + payload_len;
    if (payload) {
        memcpy(buf + YC_MIN_FRAME_SIZE, payload, payload_len);
    }

    return frame_len;
}

/**
 * @brief Sends an HID message via the Bluetooth interface.
 * 
 * Constructs an HID message frame and sends it using the client's write callback.
 * 
 * @param cli Pointer to the client structure.
 * @param hid_msg Pointer to the HID message data.
 * @param size Size of the HID message data.
 * @return 0 on success, -1 if the client or callback is uninitialized.
 */
int bt_yc_hid_msg_set(client_t *cli, char *hid_msg, int size)
{
    if (!cli || !cli->cb_write) {
        perror("Client or callback not initialized\n");
        return -1;
    }

    uint8_t msg[YC_FRAME_PAYLOAD_MAX_SIZE] = {0};
    msg[0] = OPCODE_DATA_TYPE;
    msg[1] = OPCODE_DATA_REPORT_ID;
    memcpy(&msg[2], hid_msg, size);

    int len = bt_yc_protocol_format(OPCODE_CMD_SET_UART_DATA, hid_msg, msg, size + 2);
    if (len < 0) {
        perror("Message protocol write error\n");
        return len;
    }

    cli->cb_write(hid_msg, len);
    return 0;
}

/**
 * @brief Retrieves the firmware version of the Bluetooth device.
 * 
 * Placeholder function for retrieving the firmware version. Currently unimplemented.
 * 
 * @param cli Pointer to the client structure.
 * @param ver Pointer to the buffer where the version string will be stored.
 * @return 0 on success, -1 if the client or callback is uninitialized.
 */
int bt_yc_firmware_ver_get(client_t *cli, char *ver)
{
    if (!cli->cb_read) {
        perror("Client is not initialized\n");
        return -1;
    }

    return 0;
}

/**
 * @brief Turns on the Bluetooth RF module.
 * 
 * Sends a command to enable the Bluetooth RF module.
 * 
 * @param cli Pointer to the client structure.
 * @return 0 on success, -1 if the client or callback is uninitialized.
 */
int bt_yc_hid_rf_on(client_t *cli)
{
    if (!cli || !cli->cb_write) {
        perror("Client or callback not initialized\n");
        return -1;
    }

    uint8_t data[] = {0x04};
    char msg[YC_FRAME_PAYLOAD_MAX_SIZE] = {0};

    int len = bt_yc_protocol_format(OPCODE_CMD_SET_VISIBILITY, msg, data, sizeof(data));
    if (len < 0) {
        perror("RF on protocol write error\n");
        return len;
    }

    cli->cb_write(msg, len);
    return 0;
}

/**
 * @brief Turns off the Bluetooth RF module.
 * 
 * Sends a command to disable the Bluetooth RF module.
 * 
 * @param cli Pointer to the client structure.
 * @return 0 on success, -1 if the client or callback is uninitialized.
 */
int bt_yc_hid_rf_off(client_t *cli)
{
    if (!cli || !cli->cb_write) {
        perror("Client or callback not initialized\n");
        return -1;
    }

    uint8_t data[] = {0x00};
    char msg[YC_FRAME_PAYLOAD_MAX_SIZE] = {0};

    int len = bt_yc_protocol_format(OPCODE_CMD_SET_VISIBILITY, msg, data, sizeof(data));
    if (len < 0) {
        perror("RF off protocol write error\n");
        return len;
    }

    cli->cb_write(msg, len);
    return 0;
}

int bt_yc_hid_camera_set(client_t *cli)
{
    int len = 0;

    if (!cli || !cli->cb_write) {
        perror("Client or callback not initialized\n");
        return -1;
    }

    char msg[YC_FRAME_PAYLOAD_MAX_SIZE] = {0};

    uint8_t data_press[] = {0x13, 0x00, 0x02, 0x00};
    len = bt_yc_protocol_format(OPCODE_CMD_SET_UART_DATA, msg, data_press, sizeof(data_press));
    if (len < 0) {
        perror("RF off protocol write error\n");
        return len;
    }
    cli->cb_write(msg, len);

    usleep(500 * 1000);

    uint8_t data_release[] = {0x13, 0x00, 0x00, 0x00};
    len = bt_yc_protocol_format(OPCODE_CMD_SET_UART_DATA, msg, data_release, sizeof(data_release));
    if (len < 0) {
        perror("RF off protocol write error\n");
        return len;
    }
    cli->cb_write(msg, len);

    return 0;
}

/**
 * @brief Sets the Bluetooth device name.
 * 
 * Sends a command to set the Bluetooth device name.
 * 
 * @param cli Pointer to the client structure.
 * @param name_msg Pointer to the name string.
 * @param size Size of the name string.
 * @return 0 on success, -1 if the client or callback is uninitialized.
 */
int bt_yc_hid_name_set(client_t *cli, char *name_msg, int size)
{
    if (!cli || !cli->cb_write) {
        perror("Client or callback not initialized\n");
        return -1;
    }

    char msg[YC_FRAME_PAYLOAD_MAX_SIZE] = {0};
    int len = bt_yc_protocol_format(OPCODE_CMD_SET_LE_NAME, msg, (uint8_t *)name_msg, size);
    if (len < 0) {
        perror("Name set protocol write error\n");
        return len;
    }

    cli->cb_write(msg, len);
    return 0;
}

/**
 * @brief Retrieves the Bluetooth device name.
 * 
 * Placeholder function for retrieving the Bluetooth device name. Currently unimplemented.
 * 
 * @param cli Pointer to the client structure.
 * @param name_msg Pointer to the buffer where the name string will be stored.
 * @param size Size of the name string buffer.
 * @return 0 on success, -1 if the client or callback is uninitialized.
 */
int bt_yc_hid_name_get(client_t *cli, char *name_msg, int size)
{
    if (!cli->cb_read) {
        perror("Client is not initialized\n");
        return -1;
    }

    return 0;
}

/**
 * @brief Disconnects the Bluetooth device.
 * 
 * Sends a command to disconnect the Bluetooth device.
 * 
 * @param cli Pointer to the client structure.
 * @return 0 on success, -1 if the client or callback is uninitialized.
 */
int bt_yc_hid_device_disconnect(client_t *cli)
{
    if (!cli || !cli->cb_write) {
        perror("Client or callback not initialized\n");
        return -1;
    }

    uint8_t data[] = {0x01, 0x12, 0x00};
    char msg[YC_FRAME_PAYLOAD_MAX_SIZE] = {0};

    int len = bt_yc_protocol_format(OPCODE_CMD_BLE_DISCONNECT, msg, data, sizeof(data));
    if (len < 0) {
        perror("Disconnect protocol write error\n");
        return len;
    }

    cli->cb_write(msg, len);
    return 0;
}

/**
 * @brief Initializes the Bluetooth HID device client.
 * 
 * Assigns the provided write and read callbacks to the client structure.
 * 
 * @param cli Pointer to the client structure.
 * @param cb_w Write callback function.
 * @param cb_r Read callback function.
 * @return Pointer to the initialized client structure, or NULL if initialization fails.
 */
client_t *bt_yc_hid_device_init(client_t *cli, callback_t cb_w, callback_t cb_r)
{
    if (!cli) {
        perror("Client cannot be NULL\n");
        return NULL;
    }

    cli->cb_write = cb_w;
    cli->cb_read = cb_r;

    return cli;
}

#endif
