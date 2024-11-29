/*
 * Copyright (C) 2023-2024 ArtInChip Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors:  dwj <weijie.ding@artinchip.com>
 */

#include <sys/time.h>
#include <pthread.h>
#include <linux/rpmsg_aic.h>
#include <artinchip/sample_base.h>

struct mbox_info {
        int fd;
        char *name;
};

#define MAX_REMOTE_NUMBER               3

/* Open a device file to be needed. */
static int device_open(char *_fname, int _flag)
{
        s32 fd = -1;

        fd = open(_fname, _flag);
        if (fd < 0) {
                ERR("Failed to open %s errno: %d[%s]\n",
                    _fname, errno, strerror(errno));
                exit(0);
        }
        return fd;
}

int main(int argc, char **argv)
{
        int ret = 0, i;
        struct aic_rpmsg *msg;
        struct mbox_info mbox_dev[MAX_REMOTE_NUMBER];

        memset(mbox_dev, 0, sizeof(mbox_dev));
        mbox_dev[0].name = "/dev/rpmsg0";
        mbox_dev[1].name = "/dev/rpmsg1";
        mbox_dev[2].name = "/dev/mbox0";

        for (i = 0; i < MAX_REMOTE_NUMBER; i++)
                mbox_dev[i].fd = device_open(mbox_dev[i].name, O_RDWR);

        msg = malloc(sizeof(struct aic_rpmsg) + 128);
        msg->cmd = RPMSG_CMD_IS_IDLE;
        msg->seq = 0;
        msg->len = 0;

        // check SPSS/SCSS/SESS is idle or not
        for (i = 0; i < MAX_REMOTE_NUMBER; i++) {
                ret = write(mbox_dev[i].fd, msg, 32);
                if (ret < 0) {
                        ERR("Failed to send msg IS_IDLE to %s\n",
                            mbox_dev[i].name);
                        goto __exit;
                }
        }

        for (i = 0; i < MAX_REMOTE_NUMBER; i++) {
                ret = read(mbox_dev[i].fd, msg, 32);
                if (ret < 0) {
                        ERR("Failed to read msg from %s!\n", mbox_dev[i].name);
                        goto __exit;
                }
        }

        /*********Notify SPSS/SCSS/SESS to standby********/
        msg->cmd = RPMSG_CMD_PRE_STANDBY;
        for (i = 0; i < MAX_REMOTE_NUMBER; i++) {
                ret = write(mbox_dev[i].fd, msg, 32);
                if (ret < 0) {
                        ERR("Failed to send msg PRE_STANDBY to %s\n",
                            mbox_dev[i].name);
                        goto __exit;
                }
        }

        for (i = 0; i < MAX_REMOTE_NUMBER; i++)
                close(mbox_dev[i].fd);

        // CSYS enter to standby
        system("echo mem > /sys/power/state");

__exit:
        printf("CSYS resumed\n");
        free(msg);
        return ret;
}
