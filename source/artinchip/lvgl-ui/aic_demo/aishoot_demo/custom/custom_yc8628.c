/*
 * Copyright (c) 2024-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */
#ifdef ENABLE_BT_YC8628

#include "custom.h"
#include "battery.h"
#include <pthread.h>
#include "time.h"
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <termios.h>
#include "key_event.h"
#include "tffcastserver.h"
#include "touch_input.h"
#include "bt_yc8628.h"
#include "mpp_dec.h"

#define MSG_LEN 16
//PE.19 power ctrl
#define POWER_CTRL 79
//PF.0 charge detect
#define CHARGE_DET 80
#define UART_DEVICE "/dev/ttyS4"
#define UART_BAUNDRATE B1500000
#define INPUT_DEVICE "/dev/input/event0"
#define KEY_DEVICE "/dev/input/event1"
#define LCD_WIDTH 640
#define LCD_HEIGHT 1136

static FIFOLinkedList touch_fifo = {0};
static ResolutionMap res_map = {0};
static client_t cli = {0};

static int uart_fd = -1;
static screen_t *scr = NULL;
static int last_level = -1;
static int last_charge = -1;
static lv_timer_t *bat_timer = NULL;
static bool ui_show = true;
static int res_init = 0;
static lv_timer_t *des_timer = NULL;
static TFFCastInitPara para = {0};
static struct crop_info info = {0};

void tff_cast_start()
{
	printf("ui hide \n");
	ui_show = false;
}

void tff_cast_stop()
{
	printf("ui show \n");
	ui_show = true;
    res_init = 0;
}

void tff_cast_volume()
{
	printf("volume \n");
}

static void descrip_battery_callback(lv_timer_t *tmr)
{
	if (ui_show && !scr->image_descrip) {
		scr->image_descrip = lv_img_create(scr->obj);
		lv_img_set_src(scr->image_descrip, LVGL_IMAGE_PATH(description.png));
		lv_img_set_pivot(scr->image_descrip, 50, 50);
		lv_img_set_angle(scr->image_descrip, 0);
		lv_obj_set_style_img_opa(scr->image_descrip, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_pos(scr->image_descrip, 0, 0);
		lv_obj_move_background(scr->image_descrip);
	} else if (!ui_show && scr->image_descrip) {
		lv_obj_del(scr->image_descrip);
		scr->image_descrip = NULL;
	}
}

static void timer_battery_callback(lv_timer_t *tmr)
{
    int value = -1;
    int level = check_battery_level();

    value = gpio_det_get();

    if (last_level == level && last_charge == value)
        return;

    lv_obj_add_flag(scr->image_battery_low, LV_OBJ_FLAG_HIDDEN);

    switch (level) {
        case 0:
            gpio_set_value(POWER_CTRL, 0);
            break;
        case 1:
            lv_img_set_src(scr->image_battery, LVGL_IMAGE_PATH(battery_0.png));
            lv_obj_clear_flag(scr->image_battery_low, LV_OBJ_FLAG_HIDDEN);
            break;
        case 25:
            lv_img_set_src(scr->image_battery, LVGL_IMAGE_PATH(battery_1.png));
            break;
        case 50:
            lv_img_set_src(scr->image_battery, LVGL_IMAGE_PATH(battery_2.png));
            break;
        case 75:
            lv_img_set_src(scr->image_battery, LVGL_IMAGE_PATH(battery_3.png));
            break;
        case 100:
            lv_img_set_src(scr->image_battery, LVGL_IMAGE_PATH(battery_4.png));
            break;
        default:
            break;
    }

    if (value == 0) {
        lv_img_set_src(scr->image_battery, LVGL_IMAGE_PATH(charge.png));
    }

    last_level = level;
    last_charge = value;

    return;
}

int open_uart(char *dev_name)
{
    struct termios options = {0};
    int ret = 0;

    int fd = open(dev_name, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        perror("open uart device fail \n");
    }

    ret = fcntl(fd, F_SETFL, 0);
    if (ret < 0)
        printf("fcntl for failed!\n");

    tcgetattr(fd, &options);

    bzero(&options, sizeof(options));

    options.c_cflag |= CLOCAL | CREAD;
    options.c_cflag &= ~CSIZE;

    cfsetispeed(&options, UART_BAUNDRATE);
    cfsetospeed(&options, UART_BAUNDRATE);

    options.c_cflag |= CS8;
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cc[VTIME] = 1;
    options.c_cc[VMIN] = 1;

    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        perror("tcsetattr faild \n");
		close(fd);
        return -1;
    }

    return fd;
}

int close_uart(int fd)
{
    if (fd != -1) {
        close(fd);
        fd = -1;
    }
    return 0;
}

void call_read(char *data, int size)
{
    printf("read data from uart device\n");
    return;
}

void call_write(char *data, int size)
{
    int len =  write(uart_fd, data, size);
    if (len != size) {
        printf("write data to uart device failed\n");
    }
}

unsigned int generate_seed_from_mac()
{
    struct ifreq ifr = {0};

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    unsigned char mac[6] = {0};

    if (sock >= 0) {
        strncpy(ifr.ifr_name, "wlan1", IFNAMSIZ);
        if (ioctl(sock, SIOCGIFHWADDR, &ifr) >= 0) {
            memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
        }
        close(sock);
    }

    return (mac[4] << 8) | mac[5];
}

void* touch_reader_thread(void* arg) {
    fd_set readfds = {0};
    struct timeval timeout = {0};
    struct input_event ev = {0};
    TouchPoint tp = {{0}, {0}, {0}, {0}, 0};  // Initialize with default values for all arrays
    int current_slot = 0;

    int fd = open(INPUT_DEVICE, O_RDONLY);
    if (fd == -1) {
        perror("Failed to open /dev/input/event0");
        return NULL;
    }

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ret = select(fd + 1, &readfds, NULL, NULL, &timeout);
        if (ret == -1) {
            perror("select failed");
            break;
        } else if (ret == 0) {
            continue;  // Timeout, check again
        }

        if (FD_ISSET(fd, &readfds)) {
            if (read(fd, &ev, sizeof(struct input_event)) == sizeof(struct input_event)) {
                if (ev.type == EV_ABS) {
                    switch (ev.code) {
                        case ABS_MT_SLOT:
                            current_slot = ev.value;
                            break;
                        case ABS_MT_POSITION_X:
                            if (current_slot < MAX_TOUCH_POINTS) {
                                tp.x[current_slot] = ev.value;
                            }
                            break;
                        case ABS_MT_POSITION_Y:
                            if (current_slot < MAX_TOUCH_POINTS) {
                                tp.y[current_slot] = ev.value;
                            }
                            break;
                        case ABS_MT_TRACKING_ID:
                            if (current_slot < MAX_TOUCH_POINTS) {
                                tp.id[current_slot] = current_slot;
                                tp.pressed[current_slot] = (ev.value >= 0) ? 1 : 0;
                                if (ev.value >= 0) {
                                    tp.point_count = (current_slot + 1 > tp.point_count) ? (current_slot + 1) : tp.point_count;
                                }
                            }
                            break;
                    }
                } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                    fifo_push(&touch_fifo, &tp);
                }
            }
        }
    }
    close(fd);
    return NULL;
}

void* touch_processor_thread(void* arg) {
    TouchPoint tp = {0};
    int ret = -1, msg_index = -1;
    uint8_t msg[YC_FRAME_PAYLOAD_MAX_SIZE] = {0};

    while (1) {
        if (res_init == 0) {
            sleep(1);
            mpp_get_crop_info(NULL, &info);
            printf("info width: %d, height: %d, x: %d, y: %d, img_width: %d, img_height: %d\n",
                info.width, info.height, info.x, info.y, info.img_width, info.img_height);
            if(info.width == 0) {
                continue;
            } else {
                int img_width = LCD_WIDTH * info.img_width / info.width;
                int img_height = LCD_HEIGHT * info.img_height / info.height;
                init_resolution_map(&res_map, LCD_WIDTH, LCD_HEIGHT, YC_WIDTH, YC_HEIGHT, img_width, img_height);
                res_init = 1;
            }
        }

        if (fifo_pop(&touch_fifo, &tp) == 0) {
            msg_index = 0;
            for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
                map_coordinates(&res_map, &tp.x[i], &tp.y[i]);

                if (i == 0)
                    msg[msg_index++] = (tp.pressed[i] == 1) ? 0x83 : 0x82;
                else
                    msg[msg_index++] = (tp.pressed[i] == 1) ? 0x87 : 0x86;

                msg[msg_index++] = tp.x[i] & 0xFF;
                msg[msg_index++] = (tp.x[i] >> 8) & 0xFF;

                msg[msg_index++] = tp.y[i] & 0xFF;
                msg[msg_index++] = (tp.y[i] >> 8) & 0xFF;
            }

            ret = bt_yc_hid_msg_set(&cli, (char*)msg, msg_index);
            if (ret < 0) {
                printf("Error sending touch data to Bluetooth YC8628\n");
            }
        }

        usleep(10000);
    }
    return NULL;
}

void* key_thread(void *p)
{
    int fd = open(KEY_DEVICE, O_RDONLY);
    if (fd == -1) {
        perror("Failed to open input device");
        return NULL;
    }

    struct input_event ev;
    struct key_state current_key = {0};

    while (1) {
        if (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
            handle_key_event(&ev, &current_key, (void *)&cli);
        }
        usleep(10000);
    }
    close(fd);

    return NULL;
}

void custom_init(ui_manager_t *ui)
{
    int ret = -1;
    pthread_t key_thread_id, reader_thread_id, processor_thread_id;

    scr = screen_get(ui);
    if (!scr)
        return;

    ret = gpio_export(POWER_CTRL);
    if (ret < 0)
        perror("power ctrl gpio export failed\n");

    ret = gpio_set_dir(POWER_CTRL, "out");
    if (ret < 0)
        perror("power ctrl gpio set dir failed\n");

    gpio_set_value(POWER_CTRL, 1);

    bat_timer = lv_timer_create(timer_battery_callback, 1000, 0);
    des_timer = lv_timer_create(descrip_battery_callback, 200, 0);

    unsigned int seed = generate_seed_from_mac() ^ (unsigned int)time(NULL);

    para.resolution = 0;
    para.OnSetVolume = tff_cast_volume;
    para.OnStart = tff_cast_start;
    para.OnStop = tff_cast_stop;

    srand(seed);
    int random_num = rand()%9000 + 1000;
    snprintf(para.deviceName, sizeof(para.deviceName), "Aishoot-%d", random_num);

    TFFCast_startService(&para);

    fifo_init(&touch_fifo);

    uart_fd = open_uart(UART_DEVICE);
    if (uart_fd == -1) {
        perror("Failed to open UART device");
        return;
    }

    if (bt_yc_hid_device_init(&cli, call_write, call_read) == NULL) {
        perror("Failed to initialize Bluetooth HID device");
        close_uart(uart_fd);
        return;
    }

    ret = bt_yc_hid_name_set(&cli, para.deviceName, strlen(para.deviceName));
    if (ret < 0) {
        perror("Error setting Bluetooth HID device name");
    }

    ret = pthread_create(&key_thread_id, NULL, key_thread, NULL);
    if (ret != 0) {
        perror("Failed to create key thread");
    }
    
    ret = pthread_create(&reader_thread_id, NULL, touch_reader_thread, NULL);
    if (ret != 0) {
        perror("Failed to create touch reader thread");
    }
    
    ret = pthread_create(&processor_thread_id, NULL, touch_processor_thread, NULL);
    if (ret != 0) {
        perror("Failed to create touch processor thread");
    }
}

#endif
