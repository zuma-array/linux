#ifndef __STREAM195x_H__
#define __STREAM195x_H__

struct snd_soc_stream195x_dai_link_data {
	struct gpio_desc *mute_gpio;
	unsigned int slots;
	unsigned int slot_width;
};

struct snd_soc_stream195x_data {
	struct device *dev;

	struct snd_soc_card card;

	struct snd_soc_dai_link *dai_links;
	struct snd_soc_stream195x_dai_link_data *dai_link_data;

	struct clk *pll8k_clk;
	struct clk *pll11k_clk;
	int cur_ppm;

	struct gpio_desc *powerdown_gpio;
};

int snd_soc_stream195x_parse_dai_link_of(struct snd_soc_stream195x_data *priv, int index,
						struct device_node *np, const struct snd_soc_ops *link_ops);
int snd_soc_stream195x_parse_of(struct snd_soc_stream195x_data *priv, const struct snd_soc_ops *link_ops);

#endif /* __STREAM195x_H__ */
