// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2022-2026 ArtInChip Technology Co., Ltd.
 * Author: senye.liang <senye.liang@artinchip.com>
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdbool.h>

#define DIR_CLK "/sys/kernel/debug/clk"
#define GET_PARENT	0
#define GET_ALL_INFO 	1
#define GET_NAME	2
#define GET_MAJOR_INFO  3
#define PATH_MAX_LEN    256


char clk_info_type_aic[3][20] = {
	"clk_rate",
	"clk_enable_count",
	"clk_parent"
};

static void usage(char *program)
{
	printf("\n");
	printf("Usage: %s [-a [name...]] [-p name...] [-f name...] [-v] [-h]\n", program);
	printf("\n");
	printf("Options:\n");
	printf("  -a [name...]  Get all info for clocks (default: show all clocks if no name)\n");
	printf("  -f name       Get major info (rate, enable count, parent) for clock\n");
	printf("  -p name       Get parent clock chain for clock\n");
	printf("  -v            Display version\n");
	printf("  -h            Display this help screen\n");
	printf("\n");
	printf("Examples:\n");
	printf("  %s -a                    Show all clocks summary\n", program);
	printf("  %s -a uart1 i2c0         Show detailed info for uart1 and i2c0\n", program);
	printf("  %s -f uart1              Show major info for uart1\n", program);
	printf("  %s -p i2c0               Show parent chain: osc24m -> apb1 -> i2c0\n", program);
	printf("  %s -v                    Show buil info\n", program);
	printf("\n");
}


static char *clk_info_get(char *path, char *filename)
{
	FILE *file;
	char *buffer = NULL;
	char temp[PATH_MAX_LEN];

	snprintf(temp, sizeof(temp), "%s/%s", path, filename);

	file = fopen(temp, "r");

	if (file == NULL)
		return NULL;

	buffer = (char *)malloc(32 * sizeof(char));
	if (buffer == NULL) {
		fclose(file);
		return NULL;
	}

	if (fscanf(file, "%31s", buffer) != 1) {
		free(buffer);
		buffer = NULL;
	}
	fclose(file);

	return buffer;
}

char *clk_get_parent_rate(char *clk_name)
{
	char path[PATH_MAX_LEN];
	char *parent_name = NULL;
	char *parent_rate = NULL;
	snprintf(path, sizeof(path), "%s/%s", DIR_CLK, clk_name);

	/*get parent clock*/
	parent_name = clk_info_get(path, "clk_parent");
	if (parent_name == NULL)
		return NULL;

	/*get parent rate*/
	snprintf(path, sizeof(path), "%s/%s", DIR_CLK, parent_name);
	parent_rate = clk_info_get(path, "clk_rate");

	free(parent_name);

	return parent_rate;
}


static int clk_get_parent(char *clk_name)
{
	char path[PATH_MAX_LEN];
	char *parent_name = NULL;

	snprintf(path, sizeof(path), "%s/%s", DIR_CLK, clk_name);

	parent_name = clk_info_get(path, "clk_parent");
	if (parent_name == NULL) {
		printf("%s", clk_name);
		return 0;
	}

	clk_get_parent(parent_name);
	printf(" -> %s", clk_name);

	free(parent_name);

	return 0;
}


int clk_each_file(char *clk_name)
{
	char dirname[PATH_MAX_LEN];
	DIR *dir;
	struct dirent *entry;
	char *buffer = NULL;

	printf("\n-----\t%s\t-----\n", clk_name);
	snprintf(dirname, sizeof(dirname), "%s/%s", DIR_CLK, clk_name);

	dir = opendir(dirname);
	printf("dirname:%s\n", dirname);
	if (dir == NULL) {
		perror("Unable to open directory");
		return -1;
	}

	/*each all files in this clock dir*/
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type != DT_REG)
			continue;

		buffer = clk_info_get(dirname, entry->d_name);
		if (buffer) {
			printf("%-20s%s", entry->d_name, buffer);
			printf("\n");
			free(buffer);
			buffer = NULL;
		} else {
			printf("%-20s<read error>\n", entry->d_name);
		}
	};

	closedir(dir);
	return 0;
}

int clk_major_info(char *clk_name)
{
	int num = sizeof(clk_info_type_aic)/sizeof(clk_info_type_aic[0]);
	char path[PATH_MAX_LEN];
	char *parent_rate = NULL;

	printf("%8s  ", clk_name);

	snprintf(path, sizeof(path), "%s/%s", DIR_CLK, clk_name);
	for (int i= 0; i < num; i++) {
		char *buffer = clk_info_get(path, clk_info_type_aic[i]);
		if (buffer) {
			printf("%14s",buffer);
			free(buffer);
		}
	}
	parent_rate = clk_get_parent_rate(clk_name);
	if (parent_rate) {
		printf("%16s", parent_rate);
		free(parent_rate);
	} else {
		printf("%16s", "N/A");
	}

	printf("\n");
	return 0;
}

int get_byname(char *name, int ch)
{
	DIR *dir = opendir(DIR_CLK);
	struct dirent *entry;
	int ret = 0;

	if (dir == NULL) {
		printf("Unable to open directory");
		return -1;
	}

	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type != DT_DIR || strcmp(entry->d_name,".") == 0\
			|| strcmp(entry->d_name, "..") == 0) {
			continue;
		}

		ret = strncmp(entry->d_name, name, strlen(name));
		if (ret == 0) {
			switch (ch) {
			case GET_PARENT:
				ret = clk_get_parent(entry->d_name);
				printf("\n");
				if (ret < 0)
					goto _err;
				break;
			case GET_ALL_INFO:
				ret = clk_each_file(entry->d_name);
				printf("\n");
				if (ret < 0)
					goto _err;
				break;
			case GET_MAJOR_INFO:
				ret = clk_major_info(entry->d_name);
				printf("\n");
				if (ret < 0)
					goto _err;
				break;
			default:
				break;
			}
		}
	}
	closedir(dir);
	return 0;
_err:
	closedir(dir);
	return -1;
}


int get_all_info_task(char *clk[], int start_idx)
{
	int i = start_idx;
	int ret = 0;
	if (clk[i]) {
		while (clk[i]) {
			ret = get_byname(clk[i], GET_ALL_INFO);
			if (ret < 0)
				goto _err;
			i++;
		}
		printf("\n");
	} else {
		system("cat /sys/kernel/debug/clk/clk_summary");
	}

	return 0;
_err:
	return -1;
}

int get_parent_task(char *clk[], int start_idx)
{
	int i = start_idx;
	int ret = 0;
	printf("------------ get parent --------------\n");
	while (clk[i]) {
		ret = get_byname(clk[i], GET_PARENT);
		if (ret < 0)
			goto _err;
		i++;
	}
	printf("\n");

	return 0;
_err:
	return -1;
}

int get_major_info_task(char *clk[], int start_idx)
{
	int i = start_idx;
	int ret = 0;
	printf("---------------------------------------------------------------------\n");
	printf("   clock         rate        enable count  parent parent      rate   \n");
	printf("------------  -----------  --------------  -------------  -----------\n");

	while (clk[i]) {
		ret = get_byname(clk[i], GET_MAJOR_INFO);
		if (ret < 0)
			goto _err;
		i++;
	}
	printf("\n");

	return 0;
_err:
	return -1;
}

int main(int argc, char *argv[])
{
	int opt;
	int ret = 0;
	extern int optind, opterr, optopt;
	extern char *optarg;

	if (access("/sys/kernel/debug/clk/", F_OK) != 0)
		system("mount -t debugfs none /sys/kernel/debug/");

	while ((opt = getopt(argc, argv, "a::p:f:vh")) != -1) {
		switch (opt) {
		case 'a':
			ret = get_all_info_task(argv, optind);
			if (ret <0)
				goto _err;
			exit(0);
			break;
		case 'p':
			ret = get_parent_task(argv, optind - 1);
			if (ret <0)
				goto _err;
			exit(0);
			break;
		case 'f':
			ret = get_major_info_task(argv, optind - 1);
			if (ret <0)
				goto _err;
			exit(0);
			break;
		case 'v':
			printf("%s build info:%s\n", argv[0], __DATE__ " " __TIME__);
			exit(0);
			break;
		case 'h':
			usage(argv[0]);
			exit(0);
		default:
			goto _err;
		}
	}

	usage(argv[0]);
	return 0;
_err:
	return -1;
}
