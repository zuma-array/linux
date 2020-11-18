#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <sound/simple_card.h>

#include "stream195x.h"

#include "../fsl/fsl_sai.h"

#define MCLK_RATE_48k	(24576000UL)
#define MCLK_RATE_44k1	(22579200UL)

#define PLL_NOMINAL_RATE_48k	(786432000UL)
#define PLL_NOMINAL_RATE_44k1	(722534400UL)

static int snd_soc_stream195x_ppm_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->value.integer.min = -500;
	uinfo->value.integer.max = 500;
	uinfo->count = 1;

	return 0;
}

static int snd_soc_stream195x_ppm_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct snd_soc_stream195x_data *priv = snd_soc_card_get_drvdata(card);

	ucontrol->value.integer.value[0] = priv->cur_ppm;

	return 0;
}

static int snd_soc_stream195x_ppm_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct snd_soc_stream195x_data *priv = snd_soc_card_get_drvdata(card);

	int ret;
	unsigned int comp;
	unsigned int clk_rate;
	int ppm = ucontrol->value.integer.value[0];

	comp = DIV_ROUND_CLOSEST_ULL((u64)PLL_NOMINAL_RATE_48k * abs(ppm), 1000000UL);
	clk_rate = PLL_NOMINAL_RATE_48k;
	if (ppm > 0)
		clk_rate += comp;
	else
		clk_rate -= comp;

	ret = clk_set_rate(priv->pll8k_clk, clk_rate);
	if (ret)
		dev_warn(card->dev, "failed to set pll8k rate %d\n", ret);


	comp = DIV_ROUND_CLOSEST_ULL((u64)PLL_NOMINAL_RATE_44k1 * abs(ppm), 1000000UL);
	clk_rate = PLL_NOMINAL_RATE_44k1;
	if (ppm > 0)
		clk_rate += comp;
	else
		clk_rate -= comp;

	ret = clk_set_rate(priv->pll11k_clk, clk_rate);
	if (ret)
		dev_warn(card->dev, "failed to set pll11k rate %d\n", ret);

	priv->cur_ppm = ppm;

	return 1;
}

static const struct snd_kcontrol_new snd_soc_stream195x_controls[] = {
	{
		.name = "Drift compensator",
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.info = snd_soc_stream195x_ppm_info,
		.get = snd_soc_stream195x_ppm_get,
		.put = snd_soc_stream195x_ppm_put,
	},
};

static int snd_soc_stream195x_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
	int ret;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_stream195x_data *priv = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_stream195x_dai_link_data *dai_link_data = priv->dai_link_data + rtd->num;

	unsigned int rate = params_rate(params);
	unsigned int mclk_rate, pll_rate;
	struct clk *pll;

	if ((rate % 8000) == 0) {
		pll_rate = PLL_NOMINAL_RATE_48k;
		pll = priv->pll8k_clk;
		mclk_rate = MCLK_RATE_48k;
	} else {
		pll_rate = PLL_NOMINAL_RATE_44k1;
		pll = priv->pll11k_clk;
		mclk_rate = MCLK_RATE_44k1;
	}

	/*
	 * We could have done the tdm slot setup only once, and not in every hw_params()
	 * call, however sooner or later we will support DSD where the tdm slot setup
	 * will change at runtime, so let's just add it here.
	 */
	ret = snd_soc_dai_set_tdm_slot(codec_dai, BIT(params_channels(params)) - 1, BIT(params_channels(params)) - 1, dai_link_data->slots, dai_link_data->slot_width);
	if (ret && ret != -ENOTSUPP)
		return ret;

	ret = snd_soc_dai_set_tdm_slot(cpu_dai, 0, 0, dai_link_data->slots, dai_link_data->slot_width);
	if (ret && ret != -ENOTSUPP)
		return ret;


	/*
	 * First we reset the PLL rate to the nominal value, otherwise, when
	 * the PLL is in a state where the frequency is skewed, the
	 * calculations inside snd_soc_dai_set_sysclk() might fail, because
	 * the skewed PLL value cannot be cleanly divided down anymore.
	 */
	priv->cur_ppm = 0;
	ret = clk_set_rate(pll, pll_rate);
	if (ret)
		return ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, mclk_rate, SND_SOC_CLOCK_IN);
	if (ret && ret != -ENOTSUPP)
		return ret;

	/* TODO: make the id parameter configurable via the device-tree */
	ret = snd_soc_dai_set_sysclk(cpu_dai, FSL_SAI_CLK_MAST1, mclk_rate, SND_SOC_CLOCK_OUT);
	if (ret && ret != -ENOTSUPP)
		return ret;


	return 0;
}

static const struct snd_soc_ops snd_soc_stream195x_ops = {
	.hw_params = snd_soc_stream195x_hw_params,
};

static void snd_soc_stream195x_set_powerdown(struct snd_soc_stream195x_data *priv, int value)
{
	if (priv->powerdown_gpio)
		gpiod_set_value(priv->powerdown_gpio, value);
}

static void snd_soc_stream195x_set_all_links_mute(struct snd_soc_stream195x_data *priv, int value)
{
	int i;
	struct snd_soc_card *card = &priv->card;

	for (i = 0; i < card->num_links; i++) {
		struct snd_soc_stream195x_dai_link_data *dai_link_data = priv->dai_link_data + i;
		if (dai_link_data->mute_gpio)
			gpiod_set_value_cansleep(dai_link_data->mute_gpio, value);
	}
}

static int snd_soc_stream195x_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct snd_soc_stream195x_data *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	ret = snd_soc_stream195x_parse_of(priv, &snd_soc_stream195x_ops);
	if (ret < 0) {
		dev_err(dev, "failed to parse device-tree\n");
		goto exit;
	}

	snd_soc_card_set_drvdata(&priv->card, priv);

	priv->card.controls = snd_soc_stream195x_controls;
	priv->card.num_controls = ARRAY_SIZE(snd_soc_stream195x_controls);

	/*
	 * Power up the card before calling register. There might be cases
	 * when the registing and probing of the components will fail if the
	 * powerdown is enabled
	 */
	snd_soc_stream195x_set_powerdown(priv, 0);

	ret = devm_snd_soc_register_card(dev, &priv->card);
	if (ret < 0) {
		dev_err(dev, "failed to register card");
		goto exit;
	}

	/* Unmute all links */
	snd_soc_stream195x_set_all_links_mute(priv, 0);

exit:
	/* In the error case put all cpu and codec of nodes */
	if (ret != 0) {
		snd_soc_stream195x_set_all_links_mute(priv, 1);
		snd_soc_stream195x_set_powerdown(priv, 1);
		asoc_simple_card_clean_reference(&priv->card);
	}

	return ret;
}

static int snd_soc_stream195x_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct snd_soc_stream195x_data *priv = snd_soc_card_get_drvdata(card);

	snd_soc_stream195x_set_all_links_mute(priv, 1);
	snd_soc_stream195x_set_powerdown(priv, 1);

	return asoc_simple_card_clean_reference(card);
}

static const struct of_device_id snd_soc_stream195x_match[] = {
	{ .compatible	= "sue,stream195x-audio" },
	{ }
};

static struct platform_driver snd_soc_stream195x_driver = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "snd-soc-stream195x",
		.of_match_table	= snd_soc_stream195x_match,
	},
	.probe		= snd_soc_stream195x_probe,
	.remove		= snd_soc_stream195x_remove,
};
module_platform_driver(snd_soc_stream195x_driver);

MODULE_AUTHOR("Martin Pietryka <martin.pietryka@streamunlimited.com>");
MODULE_DESCRIPTION("Stream Unlimited Stream195x ASoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:snd-soc-stream195x");
