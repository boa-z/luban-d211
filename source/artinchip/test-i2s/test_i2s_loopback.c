// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2026 ArtInChip Technology Co., Ltd.
 */
#include <alsa/asoundlib.h>
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_PB_DEV	   "hw:0,0"
#define DEFAULT_CAP_DEV	   "hw:0,0"
#define DEFAULT_RATE	   48000
#define DEFAULT_CHANNELS   2
#define TX_REAL_FRAMES	   512
#define TX_FLUSH_FRAMES	   256
#define TX_TOTAL_FRAMES	   (TX_REAL_FRAMES + TX_FLUSH_FRAMES)
#define PERIOD_SIZE	   128
#define PERIODS		   8
#define RX_BUF_SAMPLES	   4096
#define SYNC_WORD	   0x5AA55AA5U
#define RX_TIMEOUT_MS	   2000
#define MAX_MISMATCH_PRINT 8

struct loopback_ctx_t {
	snd_pcm_t *cap_pcm;
	uint32_t *rx_buf;
	uint32_t rx_capacity;
	uint32_t rx_total;
	unsigned int channels;
	pthread_t cap_thread;
	int cap_running;
	int thread_created;
	pthread_mutex_t lock;
	pthread_cond_t data_cond;
	int result;
};

static int s_verbose;

#define VLOG(fmt, ...)                                                         \
	do {                                                                   \
		if (s_verbose)                                                 \
			printf("    " fmt "\n", ##__VA_ARGS__);                \
	} while (0)

/* Find sync word in RX buffer and compare against TX pattern. */
static int verify_loopback(const uint32_t *rx_buf, uint32_t rx_samples,
			   const uint32_t *tx_buf, uint32_t tx_count)
{
	uint32_t sync_idx = (uint32_t)-1;
	uint32_t rx_avail = 0, cmp = 0, mismatch = 0;
	uint32_t i = 0;
	int sync_count = 0;

	/* Locate sync word in captured data */
	for (i = 0; i < rx_samples; i++) {
		if (rx_buf[i] == SYNC_WORD) {
			if (sync_idx == (uint32_t)-1)
				sync_idx = i;
			sync_count++;
		}
	}

	printf("  Sync word 0x%08X found %d time(s)", SYNC_WORD, sync_count);
	if (sync_idx != (uint32_t)-1)
		printf(", first at index %u\n", sync_idx);
	else
		printf("\n");

	if (sync_idx == (uint32_t)-1) {
		printf("  [FAIL] Sync word not found in RX buffer\n");
		return -1;
	}

	rx_avail = rx_samples - sync_idx;
	cmp = (rx_avail < tx_count) ? rx_avail : tx_count;

	printf("  TX samples: %u | RX samples from sync: %u\n", tx_count,
	       rx_avail);

	if (rx_avail < tx_count)
		printf("  [WARN] Size mismatch: expected %u, got %u\n",
		       tx_count, rx_avail);

	for (i = 0; i < cmp; i++) {
		if (rx_buf[sync_idx + i] != tx_buf[i]) {
			if (mismatch < MAX_MISMATCH_PRINT)
				printf("    [MISMATCH] [%u] tx=0x%08X rx=0x%08X\n",
				       i, tx_buf[i], rx_buf[sync_idx + i]);
			mismatch++;
		}
	}

	if (mismatch > MAX_MISMATCH_PRINT)
		printf("    ... %u more mismatch(es) omitted\n",
		       mismatch - MAX_MISMATCH_PRINT);

	if (mismatch == 0 && rx_avail >= tx_count) {
		printf("  [PASS] All %u samples matched\n", cmp);
		return 0;
	}

	printf("  [FAIL] %u / %u sample(s) mismatched\n", mismatch, cmp);
	return -1;
}

/* Capture thread: read PCM data in period-sized chunks. */
static void *capture_thread(void *arg)
{
	struct loopback_ctx_t *ctx = arg;
	snd_pcm_sframes_t frames = 0;
	uint32_t remaining_frames = 0;
	uint32_t to_read = 0;

	VLOG("capture thread started, waiting for data...");

	while (ctx->cap_running && ctx->rx_total < ctx->rx_capacity) {
		remaining_frames = ctx->rx_capacity - ctx->rx_total;
		if (remaining_frames == 0)
			break;

		/* Limit each read to PERIOD_SIZE to keep ring buffer drained */
		to_read = remaining_frames;
		if (to_read > PERIOD_SIZE)
			to_read = PERIOD_SIZE;

		frames = snd_pcm_readi(
			ctx->cap_pcm,
			ctx->rx_buf + ctx->rx_total * ctx->channels, to_read);
		if (frames < 0) {
			/* Recover from xrun */
			frames = snd_pcm_recover(ctx->cap_pcm, frames, 1);
			if (frames < 0) {
				fprintf(stderr, "  capture unrecoverable: %s\n",
					snd_strerror(frames));
				break;
			}
		}
		if (frames > 0) {
			ctx->rx_total += frames;
			VLOG("captured %ld frames, total %u frames", frames,
			     ctx->rx_total);
		}
	}

	VLOG("capture thread exiting, total %u frames", ctx->rx_total);
	return NULL;
}

/* Configure PCM hardware parameters. */
static int pcm_configure(snd_pcm_t *pcm, snd_pcm_format_t fmt,
			 unsigned int rate, unsigned int ch)
{
	snd_pcm_hw_params_t *hw = NULL;
	int ret = 0;

	snd_pcm_hw_params_alloca(&hw);
	snd_pcm_hw_params_any(pcm, hw);

	ret = snd_pcm_hw_params_set_access(pcm, hw,
					   SND_PCM_ACCESS_RW_INTERLEAVED);
	if (ret < 0) {
		fprintf(stderr, "  set_access: %s\n", snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_hw_params_set_format(pcm, hw, fmt);
	if (ret < 0) {
		fprintf(stderr, "  set_format: %s\n", snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_hw_params_set_rate(pcm, hw, rate, 0);
	if (ret < 0) {
		fprintf(stderr, "  set_rate: %s\n", snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_hw_params_set_channels(pcm, hw, ch);
	if (ret < 0) {
		fprintf(stderr, "  set_channels: %s\n", snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_hw_params_set_period_size(pcm, hw, PERIOD_SIZE, 0);
	if (ret < 0) {
		fprintf(stderr, "  set_period_size: %s\n", snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_hw_params_set_periods(pcm, hw, PERIODS, 0);
	if (ret < 0) {
		fprintf(stderr, "  set_periods: %s\n", snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_hw_params(pcm, hw);
	if (ret < 0) {
		fprintf(stderr, "  hw_params: %s\n", snd_strerror(ret));
		return ret;
	}

	return 0;
}

/* Enable/disable hardware loopback via ALSA kcontrol. */
static int i2s_loopback_set(const char *ctl_dev, int enable)
{
	snd_ctl_t *ctl = NULL;
	snd_ctl_elem_list_t *list = NULL;
	snd_ctl_elem_id_t *id = NULL;
	snd_ctl_elem_value_t *val = NULL;
	unsigned int count = 0;
	unsigned int i = 0;
	int ret = -1;

	if (snd_ctl_open(&ctl, ctl_dev, 0) < 0) {
		fprintf(stderr, "  Cannot open control %s\n", ctl_dev);
		return -1;
	}

	if (snd_ctl_elem_list_malloc(&list) < 0)
		goto out;

	/* First call to get actual element count */
	snd_ctl_elem_list(ctl, list);
	count = snd_ctl_elem_list_get_used(list);

	/* Allocate exact space needed, then re-fetch full list */
	if (count > 0) {
		snd_ctl_elem_list_alloc_space(list, count);
		snd_ctl_elem_list(ctl, list);
		count = snd_ctl_elem_list_get_used(list);
	}

	/* Search for Loopback Switch control */
	for (i = 0; i < count; i++) {
		snd_ctl_elem_list_set_offset(list, i);
		snd_ctl_elem_list(ctl, list);
		if (!strncmp(snd_ctl_elem_list_get_name(list, 0),
			     "Loopback Switch", sizeof("Loopback Switch"))) {
			if (snd_ctl_elem_id_malloc(&id) < 0)
				goto out;
			snd_ctl_elem_list_get_id(list, 0, id);
			break;
		}
	}

	if (i == count) {
		fprintf(stderr,
			"  [WARN] 'Loopback Switch' control not found.\n");
		fprintf(stderr,
			"         Enable via devmem or update driver.\n");
		goto out;
	}

	snd_ctl_elem_value_alloca(&val);
	snd_ctl_elem_value_set_id(val, id);
	snd_ctl_elem_value_set_integer(val, 0, enable ? 1 : 0);
	if (snd_ctl_elem_write(ctl, val) < 0) {
		fprintf(stderr, "  Failed to set Loopback Switch\n");
		goto out;
	}

	printf("  Loopback Switch %s via ALSA control\n",
	       enable ? "enabled" : "disabled");
	ret = 0;

out:
	if (id)
		snd_ctl_elem_id_free(id);
	if (list)
		snd_ctl_elem_list_free(list);
	if (ctl)
		snd_ctl_close(ctl);
	return ret;
}

/* Build TX pattern: ramp data + flush padding. */
static void build_tx_buffer_stereo(uint32_t *buf, uint32_t real_frames,
				   uint32_t flush_frames, unsigned int channels)
{
	uint32_t real_samples = real_frames * channels;
	uint32_t flush_samples = flush_frames * channels;
	uint32_t i = 0;

	buf[0] = SYNC_WORD;
	for (i = 1; i < real_samples; i++)
		buf[i] = (uint32_t)(i * 5);

	/* Fill flush region with distinct constant for RX dump identification
	 */
	for (i = 0; i < flush_samples; i++)
		buf[real_samples + i] = 0xF1051234U;
}

/* Run the complete loopback test sequence. */
static int run_loopback_test(const char *pb_dev, const char *cap_dev,
			     unsigned int rate, unsigned int channels)
{
	struct loopback_ctx_t ctx;
	snd_pcm_t *pb_pcm = NULL, *cap_pcm = NULL;
	snd_pcm_sframes_t frames = 0;
	uint32_t tx_buf[TX_TOTAL_FRAMES * 2];
	uint32_t written = 0, chunk;
	const char *comma;
	char ctl_dev[32] = { 0 };
	int len;
	int ret = 0;

	/* Validate channels to prevent tx_buf overflow (allocated for max 2 ch)
	 */
	if (channels < 1 || channels > 2) {
		fprintf(stderr,
			"[FAIL] Invalid channels: %u (must be 1 or 2)\n",
			channels);
		return -1;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.result = -1;

	/* Init synchronisation primitives early so cleanup paths are safe */
	pthread_mutex_init(&ctx.lock, NULL);
	pthread_cond_init(&ctx.data_cond, NULL);

	/* Derive control device from pb_dev (e.g. "hw:0,0" -> "hw:0") */
	comma = strchr(pb_dev, ',');
	len = comma ? (int)(comma - pb_dev) : (int)strlen(pb_dev);
	if (len >= (int)sizeof(ctl_dev))
		len = (int)sizeof(ctl_dev) - 1;
	memcpy(ctl_dev, pb_dev, len);
	ctl_dev[len] = '\0';

	printf("\n========== I2S Loopback Test ==========\n");
	printf("PB device: %s  CAP device: %s\n", pb_dev, cap_dev);
	printf("Format: S32_LE  Rate: %u  Channels: %u\n", rate, channels);

	/* Open PCM devices */
	ret = snd_pcm_open(&pb_pcm, pb_dev, SND_PCM_STREAM_PLAYBACK, 0);
	if (ret < 0) {
		fprintf(stderr, "[FAIL] Cannot open PB device %s: %s\n", pb_dev,
			snd_strerror(ret));
		goto cleanup;
	}

	ret = snd_pcm_open(&cap_pcm, cap_dev, SND_PCM_STREAM_CAPTURE, 0);
	if (ret < 0) {
		fprintf(stderr, "[FAIL] Cannot open CAP device %s: %s\n",
			cap_dev, snd_strerror(ret));
		goto cleanup;
	}

	/* Configure PB and CAP with identical parameters */
	if (pcm_configure(pb_pcm, SND_PCM_FORMAT_S32_LE, rate, channels) < 0) {
		fprintf(stderr, "[FAIL] PB configure failed\n");
		goto cleanup;
	}
	if (pcm_configure(cap_pcm, SND_PCM_FORMAT_S32_LE, rate, channels) < 0) {
		fprintf(stderr, "[FAIL] CAP configure failed\n");
		goto cleanup;
	}

	/* Enable loopback */
	if (i2s_loopback_set(ctl_dev, 1) < 0) {
		printf("[WARN] Could not enable loopback via ALSA control.\n");
		printf("       Enable manually: devmem <I2S_BASE> 32 $(($(devmem "
		       "<I2S_BASE>) | 0x8))\n");
	}

	/* Build TX pattern with flush padding */
	build_tx_buffer_stereo(tx_buf, TX_REAL_FRAMES, TX_FLUSH_FRAMES,
			       channels);

	/* Initialize RX context (units in frames) */
	ctx.cap_pcm = cap_pcm;
	ctx.rx_buf = calloc(RX_BUF_SAMPLES, sizeof(uint32_t));
	ctx.rx_capacity = RX_BUF_SAMPLES / channels;
	ctx.rx_total = 0;
	ctx.channels = channels;
	ctx.cap_running = 1;

	if (!ctx.rx_buf) {
		fprintf(stderr, "[FAIL] RX buffer alloc failed\n");
		goto cleanup;
	}

	/* Start capture thread before TX */
	ret = pthread_create(&ctx.cap_thread, NULL, capture_thread, &ctx);
	if (ret) {
		fprintf(stderr, "[FAIL] Cannot create capture thread: %s\n",
			strerror(ret));
		goto cleanup_buf;
	}
	ctx.thread_created = 1;

	/* Let capture thread arm DMA (~1 period) */
	usleep(10000);

	/* Write TX data period-by-period, then drain to flush FIFO */
	while (written < TX_TOTAL_FRAMES) {
		chunk = TX_TOTAL_FRAMES - written;
		if (chunk > PERIOD_SIZE)
			chunk = PERIOD_SIZE;

		frames = snd_pcm_writei(pb_pcm, tx_buf + written * channels,
					chunk);
		if (frames < 0) {
			frames = snd_pcm_recover(pb_pcm, frames, 1);
			if (frames < 0) {
				fprintf(stderr, "[FAIL] PB write failed: %s\n",
					snd_strerror(frames));
				ctx.cap_running = 0;
				if (ctx.thread_created)
					pthread_join(ctx.cap_thread, NULL);
				goto cleanup_buf;
			}
			continue;
		}
		written += frames;
		VLOG("wrote %ld frames, total %u/%u frames", frames, written,
		     TX_TOTAL_FRAMES);
	}

	/* Drain: block until all frames are clocked out */
	ret = snd_pcm_drain(pb_pcm);
	if (ret < 0)
		fprintf(stderr, "[WARN] drain failed: %s\n", snd_strerror(ret));
	VLOG("drain done, all %u TX frames flushed", TX_TOTAL_FRAMES);

	/* Wait for capture completion */
	usleep(RX_TIMEOUT_MS * 1000);
	ctx.cap_running = 0;

	/* Stop capture and join thread */
	snd_pcm_drop(cap_pcm);
	if (ctx.thread_created)
		pthread_join(ctx.cap_thread, NULL);

	printf("  RX captured: %u frames (%u samples, %lu bytes)\n",
	       ctx.rx_total, ctx.rx_total * channels,
	       (unsigned long)(ctx.rx_total * channels * sizeof(uint32_t)));

	/* Verify captured data against TX pattern */
	if (ctx.rx_total == 0) {
		printf("  [FAIL] No data captured\n");
		ctx.result = -1;
	} else {
		ctx.result =
			verify_loopback(ctx.rx_buf, ctx.rx_total * channels,
					tx_buf, TX_REAL_FRAMES * channels);
	}

cleanup_buf:
	free(ctx.rx_buf);
	ctx.rx_buf = NULL;

cleanup:
	pthread_mutex_destroy(&ctx.lock);
	pthread_cond_destroy(&ctx.data_cond);
	VLOG("cleanup: closing PB");
	fflush(stdout);
	if (pb_pcm) {
		snd_pcm_drop(pb_pcm);
		snd_pcm_close(pb_pcm);
		pb_pcm = NULL;
	}
	VLOG("cleanup: closing CAP");
	fflush(stdout);
	if (cap_pcm) {
		snd_pcm_drop(cap_pcm);
		snd_pcm_close(cap_pcm);
		cap_pcm = NULL;
	}

	/* Disable loopback */
	VLOG("cleanup: disabling loopback");
	fflush(stdout);
	i2s_loopback_set(ctl_dev, 0);

	printf("========== Test %s ==========\n\n",
	       ctx.result == 0 ? "PASSED" : "FAILED");
	fflush(stdout);
	return ctx.result;
}

static void usage(const char *prog)
{
	printf("Usage: %s [options]\n", prog);
	printf("Options:\n");
	printf("  -p <dev>   Playback PCM device  (default: %s)\n",
	       DEFAULT_PB_DEV);
	printf("  -c <dev>   Capture PCM device   (default: %s)\n",
	       DEFAULT_CAP_DEV);
	printf("  -r <rate>  Sample rate          (default: %u)\n",
	       DEFAULT_RATE);
	printf("  -n <ch>    Channels             (default: %u)\n",
	       DEFAULT_CHANNELS);
	printf("  -v         Verbose output\n");
	printf("  -h         Help\n");
}

int main(int argc, char *argv[])
{
	const char *pb_dev = DEFAULT_PB_DEV;
	const char *cap_dev = DEFAULT_CAP_DEV;
	unsigned int rate = DEFAULT_RATE;
	unsigned int channels = DEFAULT_CHANNELS;
	int opt = 0;

	while ((opt = getopt(argc, argv, "p:c:r:n:vh")) != -1) {
		switch (opt) {
		case 'p':
			pb_dev = optarg;
			break;
		case 'c':
			cap_dev = optarg;
			break;
		case 'r':
			rate = atoi(optarg);
			break;
		case 'n':
			channels = atoi(optarg);
			break;
		case 'v':
			s_verbose = 1;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	return run_loopback_test(pb_dev, cap_dev, rate, channels);
}
