// SPDX-License-Identifier: Apache-2.0
/*
 * Key Suspend Test
 *
 * This daemon monitors the GPIO key and triggers system suspend
 * when the key is released.
 *
 * Copyright (C) 2022-2026 ArtInChip Technology Co., Ltd.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/select.h>
#include <getopt.h>

#define DEFAULT_DEVICE_PATH	"/dev/input/event1"
#define DEBOUNCE_INTERVAL_US	500000  /* 500ms debounce after resume */

static const char sopts[] = "dvh";
static const struct option lopts[] = {
	{"device",	required_argument, NULL, 'd'},
	{"verbose",	no_argument,       NULL, 'v'},
	{"help",	no_argument,       NULL, 'h'},
	{0, 0, 0, 0}
};

static volatile int running = 1;
static int verbose_mode = 0;
static struct timespec last_suspend_ts = {0, 0};

static void usage(char *program)
{
	printf("\nUsage: %s [options]\n", program);
	printf("\t-d, --device\t\tThe input event device (default: /dev/input/event1)\n");
	printf("\t-v, --verbose\t\tEnable verbose debug output\n");
	printf("\t-h, --help\t\tShow this help message\n");
	printf("\nExample:\n");
	printf("\t%s -d /dev/input/event1 -v\n", program);
	printf("\t%s\n", program);
}

static void signal_handler(int sig)
{
	if (verbose_mode)
		printf("[DEBUG] Signal %d received\n", sig);
	running = 0;
}

static int setup_signal_handlers(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	if (sigaction(SIGTERM, &sa, NULL) < 0) {
		perror("sigaction SIGTERM");
		return -1;
	}

	if (sigaction(SIGINT, &sa, NULL) < 0) {
		perror("sigaction SIGINT");
		return -1;
	}

	return 0;
}

static int trigger_suspend(void)
{
	int fd;
	ssize_t ret;

	if (verbose_mode)
		printf("[DEBUG] Triggering suspend...\n");

	fd = open("/sys/power/state", O_WRONLY);
	if (fd < 0) {
		printf("[ERROR] Failed to open /sys/power/state: %s\n", strerror(errno));
		return -1;
	}

	ret = write(fd, "mem", 3);
	if (ret < 0) {
		printf("[ERROR] Failed to write to /sys/power/state: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);

	/* Record the time when suspend was triggered */
	clock_gettime(CLOCK_MONOTONIC, &last_suspend_ts);

	if (verbose_mode)
		printf("[DEBUG] Suspend triggered successfully\n");
	return 0;
}

static int is_debounce_period(void)
{
	struct timespec now;
	long long elapsed_us;

	if (last_suspend_ts.tv_sec == 0 && last_suspend_ts.tv_nsec == 0)
		return 0;

	clock_gettime(CLOCK_MONOTONIC, &now);

	/* Calculate elapsed time in microseconds */
	elapsed_us = ((long long)(now.tv_sec - last_suspend_ts.tv_sec)) * 1000000 +
             ((long long)(now.tv_nsec - last_suspend_ts.tv_nsec)) / 1000;

	if (verbose_mode)
		printf("[DEBUG] Elapsed since last suspend: %lld us (threshold: %d us)\n",
		       elapsed_us, DEBOUNCE_INTERVAL_US);

	/* Check if we're still in debounce period (500ms after resume) */
	if (elapsed_us >= 0 && elapsed_us < DEBOUNCE_INTERVAL_US) {
		if (verbose_mode)
			printf("[DEBUG] In debounce period, ignoring event\n");
		return 1;
	}

	return 0;
}

static void flush_input_events(int fd)
{
	struct input_event ev;
	int count = 0;
	int flags;

	/* Save current flags and set non-blocking mode */
	flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	/* Flush any pending events in the buffer */
	while (read(fd, &ev, sizeof(ev)) > 0) {
		count++;
		if (verbose_mode)
			printf("[DEBUG] Flushed event: type=%d, code=%d, value=%d\n",
			       ev.type, ev.code, ev.value);
	}

	/* Restore original flags */
	fcntl(fd, F_SETFL, flags);

	if (count > 0 && verbose_mode)
		printf("[DEBUG] Flushed %d events\n", count);
}

static int handle_key_event(int fd, struct input_event *ev)
{
	/* Process only KEY_POWER release events */
	if (ev->type == EV_KEY && ev->code == KEY_POWER && ev->value == 0) {
		if (verbose_mode)
			printf("[DEBUG] KEY_POWER release detected\n");

		/* Skip events during debounce period after resume */
		if (is_debounce_period()) {
			if (verbose_mode)
				printf("[DEBUG] Skipping due to debounce\n");
			return 0;
		}

		printf("Key released, triggering suspend...\n");

		/* Small delay to avoid bounce */
		usleep(100000);

		if (trigger_suspend() < 0) {
			printf("[ERROR] Failed to trigger suspend\n");
		} else {
			printf("System resumed from suspend\n");
		}

		/* After suspend returns (resume), flush events */
		if (verbose_mode)
			printf("[DEBUG] System resumed, flushing events...\n");
		flush_input_events(fd);

		/* re-check running status and continue monitoring */
		return 1;
	}

	return 0;
}

static int process_key_events(int fd)
{
	struct input_event ev;
	ssize_t n;

	/* Read and process all available events */
	while (running) {
		n = read(fd, &ev, sizeof(ev));

		if (n < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;  /* No more events */

			/* Also handle EBADF error to prevent infinite loops */
			if (errno == EBADF) {
				printf("[ERROR] Bad file descriptor, exiting loop\n");
				return -1;
			}

			printf("[ERROR] read() failed: %s\n", strerror(errno));
			return -1;
		}

		if (n != sizeof(ev)) {
			printf("[ERROR] Incomplete read: got %zd bytes, expected %zu\n",
			       n, sizeof(ev));
			return -1;
		}

		if (handle_key_event(fd, &ev))
			break;
	}

	return 0;
}

static int monitor_key(const char *device_path)
{
	int fd;
	int ret;
	fd_set readfds;
	struct timeval timeout;

	fd = open(device_path, O_RDONLY);
	if (fd < 0) {
		printf("[ERROR] Failed to open %s: %s\n", device_path, strerror(errno));
		return -1;
	}

	printf("Monitoring key on %s (Press Ctrl+C to exit)\n", device_path);

	while (running) {
		FD_ZERO(&readfds);
		FD_SET(fd, &readfds);

		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		ret = select(fd + 1, &readfds, NULL, NULL, &timeout);

		if (ret < 0) {
			if (errno == EINTR)
				continue;

			printf("[ERROR] select() failed: %s\n", strerror(errno));
			break;
		} else if (ret == 0) {
			/* Timeout, continue loop */
			continue;
		}

		if (process_key_events(fd) < 0) {
			printf("[ERROR] Failed to process key events\n");
			break;
		}
	}

	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	const char *device_path = DEFAULT_DEVICE_PATH;
	int c;

	while ((c = getopt_long(argc, argv, sopts, lopts, NULL)) != -1) {
		switch (c) {
		case 'd':
			device_path = optarg;
			break;
		case 'v':
			verbose_mode = 1;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return -1;
		}
	}

	if (setup_signal_handlers() < 0) {
		printf("[ERROR] Failed to setup signal handlers\n");
		return EXIT_FAILURE;
	}

	printf("Using device: %s\n", device_path);

	/* Monitor key */
	if (monitor_key(device_path) < 0) {
		printf("[ERROR] Failed to monitor key\n");
		return EXIT_FAILURE;
	}

	printf("\nKey suspend test exiting\n");

	return EXIT_SUCCESS;
}
