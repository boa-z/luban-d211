/*
 * Copyright (c) 2024-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  haidong.pan <haidong.pan@artinchip.com>
 */

#ifdef ENABLE_BT_AP2029

#include "custom.h"
#include "battery.h"
#include <pthread.h>
#include "time.h"
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include "bt_uart.h"
#include "key_event.h"
#include "tffcastserver.h"

#define MSG_LEN 16

//PE.19 power ctrl
#define POWER_CTRL 79

//PF.0 charge detect
#define CHARGE_DET 80

#define UART_DEVICE "/dev/ttyS4"
#define KEY_DEVICE  "/dev/input/event0"

static screen_t *scr = NULL;
static int last_level = -1;
static int last_charge = -1;
static lv_timer_t *bat_timer = NULL;
static bool ui_show = true;
static lv_timer_t *des_timer = NULL;
static TFFCastInitPara para = {0};

void tff_cast_start()
{
    printf("ui hide \n");
    ui_show = false;
}

void tff_cast_stop()
{
    printf("ui show \n");
    ui_show = true;
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

void* uart_thread(void *p)
{
    unsigned char msg_buf[MSG_LEN] = {0};
    int len = -1, ret = -1;

    ret = config_uart(UART_DEVICE);
    if (ret < 0) {
        return NULL;
    }
    printf("devicename : %s\n", para.deviceName);
    ret = rename_bt_uart(para.deviceName);
    if (ret <= 0) {
        return NULL;
    }

    while (1) {
        memset(msg_buf, 0, sizeof(msg_buf));
        len = recv_uart((char *)msg_buf, MSG_LEN);
        if (len > 0) {
            msg_buf[len] = '\0';
            for (int i = 0; i< len; i++) {
                printf("0x%02x\n", msg_buf[i]);
            }
        } else {
            continue;
        }
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
            handle_key_event(&ev, &current_key, NULL);
        }
        usleep(10000);
    }
    close(fd);

    return NULL;
}

void custom_init(ui_manager_t *ui)
{
    int ret = -1;
    pthread_t key_thread_id, uart_thread_id;

    /* Add your codes here */
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

    /*battery icon show*/
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

    ret = pthread_create(&key_thread_id, NULL, key_thread, NULL);
    if (ret != 0) {
        perror("Failed to create key thread");
    }
    
    ret = pthread_create(&uart_thread_id, NULL, uart_thread, NULL);
    if (ret != 0) {
        perror("Failed to create uart thread");
    }
}

#endif
