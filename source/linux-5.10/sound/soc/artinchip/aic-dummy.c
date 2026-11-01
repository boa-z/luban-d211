// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 Artinchip Inc.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "../codecs/dummy-codec.h"

#define AIC_DUMMY_MCLK_FREQ 12288000
#define AIC_DUMMY_MAX_LINKS 4

struct aic_dummy_link_data {
	unsigned int mclk_freq;
	int link_idx;
};

struct aic_dummy_card_data {
	struct aic_dummy_link_data link_data[AIC_DUMMY_MAX_LINKS];
	int num_links;
};

/* Set MCLK on CPU and codec DAI for the given link */
static int aic_dummy_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_card *card = rtd->card;
	struct aic_dummy_card_data *cdata = snd_soc_card_get_drvdata(card);
	int link_idx = rtd->num;
	struct aic_dummy_link_data *ldata;
	int ret;

	if (link_idx >= cdata->num_links)
		return -EINVAL;
	ldata = &cdata->link_data[link_idx];

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, ldata->mclk_freq,
				     SND_SOC_CLOCK_OUT);
	if (ret)
		return ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, ldata->mclk_freq,
				     SND_SOC_CLOCK_IN);
	if (ret)
		return ret;

	dev_info(card->dev,
		 "[link%d] hw_params: MCLK=%u rate=%u ch=%u fmt=%u\n", link_idx,
		 ldata->mclk_freq, params_rate(params), params_channels(params),
		 params_format(params));
	return 0;
}

static const struct snd_soc_ops aic_dummy_snd_ops = {
	.hw_params = aic_dummy_hw_params,
};

/* Build codec DAI name "dummy-codec-dai.N" matching the codec driver */
static int aic_dummy_build_dai_name(struct device *dev, int idx,
				    const char **out)
{
	char *name;

	name = devm_kasprintf(dev, GFP_KERNEL, "dummy-codec-dai.%d", idx);
	if (!name)
		return -ENOMEM;
	*out = name;
	return 0;
}

/* Build unique link and stream names for link N */
static int aic_dummy_build_link_name(struct device *dev, int idx,
				     const char **out_name,
				     const char **out_stream)
{
	*out_name = devm_kasprintf(dev, GFP_KERNEL, "aic-dummy-link%d", idx);
	*out_stream =
		devm_kasprintf(dev, GFP_KERNEL, "aic-dummy-link%d PCM", idx);
	if (!*out_name || !*out_stream)
		return -ENOMEM;
	return 0;
}

/* Release of_node references acquired by of_parse_phandle */
static void aic_dummy_release_of_nodes(struct snd_soc_dai_link *links,
				       int num_links)
{
	int i;

	for (i = 0; i < num_links; i++) {
		if (links[i].cpus && links[i].cpus->of_node)
			of_node_put(links[i].cpus->of_node);
		if (links[i].codecs && links[i].codecs->of_node)
			of_node_put(links[i].codecs->of_node);
		/* platforms->of_node shares cpus->of_node, no extra put */
	}
}

/* Parse DT aic,i2s-controllers / aic,codec-chips and build DAI links */
static int aic_dummy_init_dai_links(struct platform_device *pdev,
				    struct snd_soc_dai_link *links,
				    struct aic_dummy_card_data *cdata)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int i, num_i2s, num_codec, num_links;
	int ret;

	num_i2s = of_count_phandle_with_args(np, "aic,i2s-controllers", NULL);
	num_codec = of_count_phandle_with_args(np, "aic,codec-chips", NULL);

	if (num_i2s <= 0) {
		dev_err(dev, "aic,i2s-controllers not found or empty\n");
		return -EINVAL;
	}
	if (num_codec <= 0) {
		dev_err(dev, "aic,codec-chips not found or empty\n");
		return -EINVAL;
	}
	if (num_i2s != num_codec) {
		dev_err(dev,
			"aic,i2s-controllers (%d) and aic,codec-chips (%d) "
			"count mismatch\n",
			num_i2s, num_codec);
		return -EINVAL;
	}

	num_links = min(num_i2s, AIC_DUMMY_MAX_LINKS);
	if (num_i2s > AIC_DUMMY_MAX_LINKS)
		dev_warn(dev, "clamping to %d links (found %d)\n",
			 AIC_DUMMY_MAX_LINKS, num_i2s);

	for (i = 0; i < num_links; i++) {
		struct snd_soc_dai_link *link = &links[i];
		struct snd_soc_dai_link_component *cpus, *codecs, *platforms;
		const char *dai_name;

		/* CPU / Codec / Platform components (1 each) */
		cpus = devm_kzalloc(dev,
				    sizeof(struct snd_soc_dai_link_component),
				    GFP_KERNEL);
		codecs = devm_kzalloc(dev,
				      sizeof(struct snd_soc_dai_link_component),
				      GFP_KERNEL);
		platforms =
			devm_kzalloc(dev,
				     sizeof(struct snd_soc_dai_link_component),
				     GFP_KERNEL);
		if (!cpus || !codecs || !platforms) {
			aic_dummy_release_of_nodes(links, i);
			return -ENOMEM;
		}

		/* Assign early so release_of_nodes can clean up on error */
		link->cpus = cpus;
		link->num_cpus = 1;
		link->codecs = codecs;
		link->num_codecs = 1;
		link->platforms = platforms;
		link->num_platforms = 1;

		/* CPU: I2S controller */
		cpus->of_node = of_parse_phandle(np, "aic,i2s-controllers", i);
		if (!cpus->of_node) {
			dev_err(dev,
				"aic,i2s-controllers[%d]: invalid phandle\n",
				i);
			aic_dummy_release_of_nodes(links, i + 1);
			return -EINVAL;
		}

		/* Codec: dummy codec + per-instance DAI name */
		codecs->of_node = of_parse_phandle(np, "aic,codec-chips", i);
		if (!codecs->of_node) {
			dev_err(dev, "aic,codec-chips[%d]: invalid phandle\n",
				i);
			aic_dummy_release_of_nodes(links, i + 1);
			return -EINVAL;
		}

		ret = aic_dummy_build_dai_name(dev, i, &dai_name);
		if (ret) {
			aic_dummy_release_of_nodes(links, i + 1);
			return ret;
		}
		codecs->dai_name = dai_name;

		/* Platform: same as CPU (I2S provides DMA) */
		platforms->of_node = cpus->of_node;

		ret = aic_dummy_build_link_name(dev, i, &link->name,
						&link->stream_name);
		if (ret) {
			aic_dummy_release_of_nodes(links, i + 1);
			return ret;
		}

		/* I2S master, codec slave */
		link->dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS |
				SND_SOC_DAIFMT_NB_NF;
		link->ops = &aic_dummy_snd_ops;

		cdata->link_data[i].mclk_freq = AIC_DUMMY_MCLK_FREQ;
		cdata->link_data[i].link_idx = i;

		dev_info(dev, "[link%d] cpu=%s codec_dai=%s MCLK=%u\n", i,
			 of_node_full_name(cpus->of_node), dai_name,
			 AIC_DUMMY_MCLK_FREQ);
	}

	cdata->num_links = num_links;
	return num_links;
}

static int aic_dummy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct aic_dummy_card_data *cdata;
	struct snd_soc_dai_link *links;
	struct snd_soc_card *card;
	int num_links, ret;

	cdata = devm_kzalloc(dev, sizeof(*cdata), GFP_KERNEL);
	if (!cdata)
		return -ENOMEM;

	links = devm_kcalloc(dev, AIC_DUMMY_MAX_LINKS,
			     sizeof(struct snd_soc_dai_link), GFP_KERNEL);
	if (!links)
		return -ENOMEM;

	num_links = aic_dummy_init_dai_links(pdev, links, cdata);
	if (num_links < 0)
		return num_links;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card) {
		aic_dummy_release_of_nodes(links, num_links);
		return -ENOMEM;
	}

	card->name = "aic-dummy";
	card->owner = THIS_MODULE;
	card->dev = dev;
	card->dai_link = links;
	card->num_links = num_links;

	snd_soc_card_set_drvdata(card, cdata);

	dev_info(dev, "aic-dummy: registering card with %d link(s)\n",
		 num_links);

	ret = devm_snd_soc_register_card(dev, card);
	if (ret) {
		dev_err(dev, "Failed to register sound card: %d\n", ret);
		aic_dummy_release_of_nodes(links, num_links);
		return ret;
	}

	dev_info(dev, "aic-dummy sound card registered with %d link(s)\n",
		 num_links);
	return 0;
}

static int aic_dummy_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	if (card)
		aic_dummy_release_of_nodes(card->dai_link, card->num_links);

	dev_info(&pdev->dev, "aic-dummy sound card removed\n");
	return 0;
}

static const struct of_device_id aic_dummy_dt_ids[] = {
	{ .compatible = "artinchip,aic-dummy-audio" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, aic_dummy_dt_ids);

static struct platform_driver aic_dummy_driver = {
	.driver = {
		.name           = "aic-dummy-audio",
		.of_match_table = aic_dummy_dt_ids,
	},
	.probe  = aic_dummy_probe,
	.remove = aic_dummy_remove,
};

module_platform_driver(aic_dummy_driver);

MODULE_DESCRIPTION("AIC Dummy Codec Machine Driver");
MODULE_AUTHOR("xyg <yiguan.ding@artinchip.com>");
MODULE_LICENSE("GPL");
