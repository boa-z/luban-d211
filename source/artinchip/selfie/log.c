#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <stdint.h>

#define MAX_BUF_SIZE 1024

void u_tm_log(char *arg, ...)
{
	char buf[MAX_BUF_SIZE];
	uint32_t len, n;
	struct timespec tm;
	va_list vl;

	clock_gettime(CLOCK_MONOTONIC, &tm);

	n = sprintf(buf, "[%ld.%06ld] ", tm.tv_sec, tm.tv_nsec / 1000);

	va_start(vl, arg);
	len = vsprintf(buf + n, arg, vl);
	va_end(vl);
	len = len + n;
	buf[len] = 0;
	fprintf(stderr, "%s", buf);
}

void u_tm_log_hex(char *info, unsigned char *buf, int count)
{
	int i, n, index;
	char str[MAX_BUF_SIZE];

	index = sprintf(str, "%s", info);
	for (i = 0; i < count; i++) {
		n = sprintf(str + index, "%02X ", buf[i]);
		index += n;
		if (index >= (MAX_BUF_SIZE - n - 5)) {
			str[index] = '.';
			str[index + 1] = '.';
			str[index + 2] = '.';
			str[index + 3] = 0;
			break;
		}
	}

	u_tm_log("%s\n", str);
}

