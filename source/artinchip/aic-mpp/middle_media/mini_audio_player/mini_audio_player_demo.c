/*
 * Copyright (C) 2020-2026 ArtInChip Technology Co. Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *  author: <jun.ma@artinchip.com>
 *  Desc: audio_player_demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <inttypes.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <getopt.h>

#include "mpp_log.h"
#include "mpp_mem.h"
#include "mini_audio_player.h"

#ifdef LPKG_USING_CPU_USAGE
#include "cpu_usage.h"
#endif

#define AUDIO_PLAYER_DEMO_FILE_MAX_NUM 128
#define AUDIO_PLAYER_DEMO_FILE_PATH_MAX_LEN 256
#define BUFFER_LEN 32

struct audio_file_list {
	char *file_path[AUDIO_PLAYER_DEMO_FILE_MAX_NUM];
	int file_num;
};

struct audio_player_ctx {
	struct mini_audio_player *player;
	struct audio_file_list  files;
	int volume;
	int loop_time;
	int trace_id;
	int pcm_dump;
	char pcm_out[256];
};

static void print_help(const char* prog)
{
	printf("name: %s\n", prog);
	printf("Compile time: %s\n", __TIME__);
	printf("Usage: player_demo [options]:\n"
		"  -i <file>    input stream file name\n"
		"  -t <dir>     directory of test files\n"
		"  -l <n>       loop time\n"
		"  -v <0-100>   volume\n"
		"  -d <0-7>     trace id\n"
		"  -p           enable PCM dump\n"
		"  -o <path>    PCM dump output path\n"
		"  -h           help\n\n"
		"---------------------------------------------------------------------------------------\n"
		"-------------------------------control key while playing-------------------------------\n"
		"---------------------------------------------------------------------------------------\n"
		"('d'): next \n"
		"('u'): previous \n"
		"('p'): pause \n"
		"('r'): resume \n"
		"('+'): volum+5 \n"
		"('-'): volum-5 \n"
		"('e'): eixt app \n");
}

static int read_dir(char* path, struct audio_file_list *files)
{
	char* ptr = NULL;
	int file_path_len = 0;
	struct dirent* dir_file;
	DIR* dir = opendir(path);
	if (dir == NULL) {
		printf("read dir failed");
		return -1;
	}

	while((dir_file = readdir(dir))) {
		if (strncmp(dir_file->d_name, ".", sizeof(".")) == 0 || strncmp(dir_file->d_name, "..", sizeof("..")) == 0)
			continue;

		ptr = strrchr(dir_file->d_name, '.');
		if (ptr == NULL)
			continue;

		if (strncmp(ptr, ".mp3", sizeof(".mp3")) && strncmp(ptr, ".wav", sizeof(".wav")) &&
		    strncmp(ptr, ".aac", sizeof(".aac")) && strncmp(ptr, ".ape", sizeof(".ape")) &&
		    strncmp(ptr, ".flac", sizeof(".flac")) && strncmp(ptr, ".ogg", sizeof(".ogg")) &&
		    strncmp(ptr, ".opus", sizeof(".opus")))
			continue;

		printf("name: %s\n", dir_file->d_name);

		file_path_len = 0;
		file_path_len += strlen(path);
		if (path[strlen(path) - 1] != '/') file_path_len += 1; // '/'
		file_path_len += strlen(dir_file->d_name);
		printf("file_path_len:%d\n",file_path_len);
		if (file_path_len > AUDIO_PLAYER_DEMO_FILE_PATH_MAX_LEN-1) {
			printf("%s too long \n",dir_file->d_name);
			continue;
		}
		files->file_path[files->file_num] = (char *)mpp_alloc(file_path_len+1);
		if (files->file_path[files->file_num] == NULL) {
			printf("mpp_alloc failed for file path\n");
			continue;
		}
		if (path[strlen(path) - 1] != '/')
			snprintf(files->file_path[files->file_num],
				 file_path_len + 1, "%s/%s", path, dir_file->d_name);
		else
			snprintf(files->file_path[files->file_num],
				 file_path_len + 1, "%s%s", path, dir_file->d_name);
		printf("i: %d, filename: %s\n", files->file_num, files->file_path[files->file_num]);
		files->file_num ++;
		if (files->file_num >= AUDIO_PLAYER_DEMO_FILE_MAX_NUM)
			break;
	}
	closedir(dir);
	return 0;
}

static int read_file(char* path, struct audio_file_list *files)
{
	int file_path_len;
	file_path_len = strlen(path);
	printf("file_path_len:%d\n",file_path_len);
	if (file_path_len > AUDIO_PLAYER_DEMO_FILE_PATH_MAX_LEN-1) {
		printf("file_path_len too long \n");
		return -1;
	}
	files->file_path[0] = (char *)mpp_alloc(file_path_len+1);
	files->file_path[0][file_path_len] = '\0';
	strncpy(files->file_path[0], path, file_path_len + 1);
	files->file_num = 1;
	printf("file path: %s\n", files->file_path[0]);
	return 0;
}

static int volume(int* vol,char ch)
{
	int volume = *vol;
	if (ch == '+') {
		volume += 5;
	} else {
		volume -= 5;
	}

	if (volume < 0) {
		volume = 0;
	} else if (volume > 100) {
		 volume = 100;
	}
	*vol = volume;
	printf("volum:%d\n",volume);
	return volume;
}

static void show_cpu_usage()
{
#ifdef LPKG_USING_CPU_USAGE
	static int index = 0;
	char data_str[64];
	float value = 0.0;

	if (index++ % 30 == 0) {
		value = cpu_load_average();
		#ifdef AIC_PRINT_FLOAT_CUSTOM
		int cpu_i;
		int cpu_frac;
		cpu_i = (int)value;
		cpu_frac = (value - cpu_i) * 100;
		snprintf(data_str, sizeof(data_str), "%d.%02d\n", cpu_i, cpu_frac);
		#else
		snprintf(data_str, sizeof(data_str), "%.2f\n", value);
		#endif
		printf("cpu_loading:%s\n",data_str);
	}
#endif
}

static int parse_options(struct audio_player_ctx *player_ctx,int cnt,char**options)
{
	int argc = cnt;
	char **argv = options;
	struct audio_player_ctx *ctx = player_ctx;
	int opt;

	if (!ctx || argc == 0 || !argv) {
		loge("para error !!!");
		return -1;
	}
	ctx->loop_time = 1;
	ctx->volume = 60;
	optind = 0;
	while (1) {
		opt = getopt(argc, argv, "i:t:l:v:d:o:ph");
		if (opt == -1) {
			break;
		}
		switch (opt) {
		case 'i':
			read_file(optarg,&ctx->files);
			break;
		case 't':
			read_dir(optarg, &ctx->files);
			break;
		case 'l':
			ctx->loop_time = atoi(optarg);
			break;
		case 'v':
			ctx->volume = atoi(optarg);
			if (ctx->volume < 0)
				ctx->volume = 0;
			else if (ctx->volume > 100)
				ctx->volume = 100;
			break;
		case 'd':
			ctx->trace_id = atoi(optarg);
			break;
		case 'p':
			ctx->pcm_dump = 1;
			break;
		case 'o':
			strncpy(ctx->pcm_out, optarg, sizeof(ctx->pcm_out) - 1);
			ctx->pcm_out[sizeof(ctx->pcm_out) - 1] = '\0';
			break;
		case 'h':
			print_help(argv[0]);
			return -1;
		default:
			break;
		}
	}
	if (ctx->files.file_num == 0) {
		print_help(argv[0]);
		printf("files.file_num ==0 !!!\n");
		return -1;
	}
	return 0;
}

static void setup_pcm_dump_for_file(struct audio_player_ctx *ctx, const char *file_path)
{
	char pcm_path[256];
	char path_buf[AUDIO_PLAYER_DEMO_FILE_PATH_MAX_LEN];
	int len = strlen(file_path);

	/* strip trailing '/' */
	while (len > 0 && file_path[len - 1] == '/')
		len--;
	if (len >= (int)sizeof(path_buf))
		len = sizeof(path_buf) - 1;
	memcpy(path_buf, file_path, len);
	path_buf[len] = '\0';

	const char *base = strrchr(path_buf, '/');
	base = base ? base + 1 : path_buf;
	const char *dot = strrchr(base, '.');
	int name_len = dot ? (int)(dot - base) : (int)strlen(base);
	if (name_len > 100)
		name_len = 100;
	const char *ext = dot ? dot + 1 : "";
	char ext_upper[8];
	int k;
	for (k = 0; ext[k] && k < 7; k++)
		ext_upper[k] = (ext[k] >= 'a' && ext[k] <= 'z') ?
			       ext[k] - 'a' + 'A' : ext[k];
	ext_upper[k] = '\0';

	if (ctx->pcm_out[0]) {
		int out_len = strlen(ctx->pcm_out);
		if (ctx->pcm_out[out_len - 1] == '/') {
			if (out_len > 128)
				out_len = 128;
			snprintf(pcm_path, sizeof(pcm_path), "%.*s%.*s_%s.pcm",
				 out_len, ctx->pcm_out, name_len, base, ext_upper);
		} else {
			snprintf(pcm_path, sizeof(pcm_path), "%s", ctx->pcm_out);
		}
	} else {
		snprintf(pcm_path, sizeof(pcm_path), "%.*s_%s.pcm",
			 name_len, base, ext_upper);
	}
	printf("pcm dump: %s\n", pcm_path);
	mini_audio_player_enable_pcm_dump(ctx->player, 1);
	mini_audio_player_set_pcm_dump_file(ctx->player, pcm_path);
}

static int process_command(struct audio_player_ctx *player_ctx,char cmd,void *para)
{
	struct audio_player_ctx *ctx = player_ctx;
	int ret = 0;
	if (cmd == 'p') {// pause
		ret = mini_audio_player_pause(ctx->player);
		printf("pause cmd!!!\n");
	} else if (cmd == 'r') {
		ret = mini_audio_player_resume(ctx->player);
		printf("resume cmd!!!\n");
	} else if (cmd == '-' || cmd == '+') {
		ret = mini_audio_player_set_volume(ctx->player,volume(&ctx->volume,cmd));
		printf("volume %c  cmd !!!",cmd);
	} else if (cmd == 'd') {
		ret = mini_audio_player_stop(ctx->player);
		printf("next cmd!!!\n");
	} else if (cmd == 'u') {
		int* p = (int*)para;
		int j = *p;
		j -= 2;
		j = (j < -1)?(-1):(j);
		*p = j;
		ret = mini_audio_player_stop(ctx->player);
		printf("previous cmd!!!\n");
	} else {
		printf("unsupport cmd!!!\n");
	}
	return ret;
}

int main(int argc, char **argv)
{
	int i,j;
	char ch;
	int flag;
	char buffer[BUFFER_LEN];
	struct audio_player_ctx *ctx = NULL;

	ctx = mpp_alloc(sizeof(struct audio_player_ctx));
	if (!ctx) {
		loge("mpp_alloc fail!!!");
		return -1;
	}
	memset(ctx,0x00,sizeof(struct audio_player_ctx));

	if (parse_options(ctx,argc,argv)) {
		loge("parse_options fail!!!\n");
		goto _exit;
	}

	ctx->player = mini_audio_player_create_ext(ctx->trace_id);
	if (ctx->player == NULL) {
		printf("mini_audio_player_create fail!!!\n");
		goto _exit;
	}

	mini_audio_player_set_volume(ctx->player,ctx->volume);

	flag = fcntl(STDIN_FILENO,F_GETFL);
	flag |= O_NONBLOCK;
	fcntl(STDIN_FILENO,F_SETFL,flag);

	for(i = 0;i < ctx->loop_time; i++) {
		for(j = 0; j < ctx->files.file_num; j++) {
			if (ctx->pcm_dump)
				setup_pcm_dump_for_file(ctx, ctx->files.file_path[j]);
			else
				mini_audio_player_enable_pcm_dump(ctx->player, 0);
			mini_audio_player_play(ctx->player,ctx->files.file_path[j]);
			printf("loop:%d,index:%d,path:%s\n",i,j,ctx->files.file_path[j]);
			while(1) {
				if (read(STDIN_FILENO, buffer, BUFFER_LEN) > 0) {
					ch = buffer[0];
					if (ch == 'e') {// exit app
						printf("exit app \n");
						goto _exit;
					}
					process_command(ctx,ch,&j);
				}
				if (mini_audio_player_get_state(ctx->player) == MINI_AUDIO_PLAYER_STATE_STOPED) {
					printf("[%s:%d]\n",__FUNCTION__,__LINE__);
					break;
				}
				usleep(100*1000);
				show_cpu_usage();
			}
		}
	}
_exit:
	if (ctx) {
		if (ctx->player) {
			mini_audio_player_destroy(ctx->player);
			ctx->player = NULL;
		}
		for(i = 0; i <ctx->files.file_num ;i++) {
			if (ctx->files.file_path[i]) {
				mpp_free(ctx->files.file_path[i]);
				ctx->files.file_path[i] = NULL;
			}
		}
		mpp_free(ctx);
		ctx = NULL;
	}
}
