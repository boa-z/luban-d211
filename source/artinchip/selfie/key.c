#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "gatt.h"

#define LONG_PRESS_TIME 1000 // Long press to determine the time(ms)

struct key_context {
	struct task_struct *thread;
	struct input_dev *input;
};

struct key_context ctx;

void *key_detect_thread(void *data) {
	struct input_event ev;
	struct timespec current_time;
	struct timespec press_start;
	long duration = 0; 
	int fd = -1;
	char *dev_path = "/dev/input/event0";

	fd = open(dev_path, O_RDONLY);
	if (fd == -1) {
		printf("Failed to open input device");
		return NULL;
	}

	while (1) {
		if (read(fd, &ev, sizeof(ev)) != sizeof(ev)) {
			continue;
		}

		clock_gettime(CLOCK_MONOTONIC, &current_time);
		if (ev.type == EV_KEY) {
			if (ev.value == 1) {
				clock_gettime(CLOCK_MONOTONIC, &press_start);
				printf("pressed\n");
				send_shutter();
			} else if (ev.value == 0) {
				duration = (current_time.tv_sec -
					    press_start.tv_sec) *
						   1000 +
					   (current_time.tv_nsec -
					    press_start.tv_nsec) /
						   1000000;
				if (duration >= LONG_PRESS_TIME) {
					printf("long pressed\n");
				} else {
					printf("short pressed\n");
				}
			}
		}
	}

	close(fd);
	return NULL;
}

int key_init(void)
{
	pthread_t tid = 0;

	printf("Listening for key events\n");
	pthread_create(&tid, NULL, key_detect_thread, NULL);

	return 0;
}
