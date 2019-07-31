#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/gpio/machine.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

#include <sound/simple_card.h>

#include "stream195x.h"

#define PREFIX	"sue-card,"

#define DAI	"sound-dai"
#define CELL	"#sound-dai-cells"

/*
 * This is a wrapper around devm_gpiod_get_from_of_node() to assemble
 * a nice gpio label based on dai_link->codec_dai_name.
 */
static struct gpio_desc *devm_dailink_gpiod_get_from_of_node(struct device *dev, struct snd_soc_dai_link *dai_link,
								struct device_node *np, const char *name, enum gpiod_flags flags)
{
	char buf[32], *gpio_label;

	snprintf(buf, sizeof(buf), "%s-%s", dai_link->codec_dai_name, name);
	gpio_label = devm_kstrdup(dev, buf, GFP_KERNEL);

	return devm_gpiod_get_from_of_node(dev, np, name, 0, flags, gpio_label);
}

int snd_soc_stream195x_parse_dai_link_of(struct snd_soc_stream195x_data *priv, int index,
						struct device_node *np, const struct snd_soc_ops *link_ops)
{
	int ret;
	struct device *dev = priv->dev;
	struct device_node *cpu = NULL;
	struct device_node *codec = NULL;

	struct snd_soc_dai_link *dai_link = priv->dai_links + index;
	struct snd_soc_stream195x_dai_link_data *dai_link_data = priv->dai_link_data + index;
	int single_cpu;

	cpu = of_get_child_by_name(np, "cpu");
	if (!cpu) {
		dev_err(dev, "%s: Can't find cpu DT node\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	codec = of_get_child_by_name(np, "codec");
	if (!codec) {
		dev_err(dev, "%s: Can't find codec DT node\n", __func__);
		ret = -EINVAL;
		goto exit;
	}

	ret = asoc_simple_card_parse_daifmt(dev, np, codec, "", &dai_link->dai_fmt);
	if (ret < 0)
		goto exit;

	ret = snd_soc_of_parse_tdm_slot(np, NULL, NULL, &dai_link_data->slots, &dai_link_data->slot_width);
	if (ret < 0)
		goto exit;

	ret = asoc_simple_card_parse_cpu(cpu, dai_link, DAI, CELL, &single_cpu);
	if (ret < 0)
		goto exit;

	if (of_property_read_bool(codec, "use-dummy-codec")) {
		dai_link->codec_dai_name = "snd-soc-dummy-dai";
		dai_link->codec_name = "snd-soc-dummy";
	} else if (asoc_simple_card_parse_codec(codec, dai_link, DAI, CELL) < 0)
		goto exit;

	ret = asoc_simple_card_canonicalize_dailink(dai_link);
	if (ret < 0)
		goto exit;

	ret = asoc_simple_card_set_dailink_name(dev, dai_link,
						"%s-%s",
						dai_link->cpu_dai_name,
						dai_link->codec_dai_name);
	if (ret < 0)
		goto exit;


	dai_link_data->mute_gpio = devm_dailink_gpiod_get_from_of_node(dev, dai_link, np, "mute-gpios", GPIOD_OUT_HIGH);
	if (IS_ERR(dai_link_data->mute_gpio))
		dai_link_data->mute_gpio = NULL;

	dai_link->ops = link_ops;
	/* dai_link->init = asoc_simple_card_dai_init; */

	asoc_simple_card_canonicalize_cpu(dai_link, single_cpu);
exit:
	of_node_put(cpu);
	of_node_put(codec);

	return ret;
}

int snd_soc_stream195x_parse_of(struct snd_soc_stream195x_data *priv, const struct snd_soc_ops *link_ops)
{
	int ret = 0;
	int i;
	struct device *dev = priv->dev;
	struct device_node *of = dev->of_node;
	struct device_node *dai_link, *np;

	struct snd_soc_card *card;
	int num_links;
	struct snd_soc_dai_link *dai_links;
	struct snd_soc_stream195x_dai_link_data *dai_link_data;

	/* TODO: should we return an error if no clock is specified? */
	priv->pll8k_clk = devm_clk_get(dev, "pll8k");
	if (IS_ERR(priv->pll8k_clk))
		priv->pll8k_clk = NULL;

	priv->pll11k_clk = devm_clk_get(dev, "pll11k");
	if (IS_ERR(priv->pll11k_clk))
		priv->pll11k_clk = NULL;

	dai_link = of_get_child_by_name(of, PREFIX "dai-link");
	if (!dai_link) {
		dev_err(dev, "could not find dai-link nodes\n");
		return -EINVAL;
	}

	card = &priv->card;
	num_links = of_get_child_count(of);

	dai_link_data = devm_kzalloc(dev, sizeof(*dai_link_data) * num_links, GFP_KERNEL);
	dai_links  = devm_kzalloc(dev, sizeof(*dai_links) * num_links, GFP_KERNEL);
	if (!dai_link_data || !dai_links) {
		ret = -ENOMEM;
		goto exit_put_dai_link;
	}

	priv->dai_link_data = dai_link_data;
	priv->dai_links = dai_links;

	card->owner = THIS_MODULE;
	card->dev = dev;
	card->dai_link = dai_links;
	card->num_links = num_links;

	i = 0;
	for_each_child_of_node(of, np) {
		ret = snd_soc_stream195x_parse_dai_link_of(priv, i, np, link_ops);
		i++;

		if (ret < 0) {
			dev_err(dev, "failed to parse dai-link\n");
			of_node_put(np);
			goto exit_put_dai_link;
		}
	}

	ret = asoc_simple_card_parse_card_name(card, PREFIX);
	if (ret < 0)
		goto exit_put_dai_link;

	priv->powerdown_gpio = devm_gpiod_get_optional(dev, "powerdown", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->powerdown_gpio))
		priv->powerdown_gpio = NULL;

exit_put_dai_link:
	of_node_put(dai_link);
	return ret;
}
