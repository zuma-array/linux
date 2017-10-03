/*
 * ES9018K2M ASoC codec driver
 *
 * Copyright (c) StreamUnlimited GmbH 2015-2016
 *     Fionn Cleary <fionn.cleary@streamunlimited.com>
 *
 * Based on TAS5086 code from Daniel Mack <zonque@gmail.com> with additions
 * from Marek Belisko <marek.belisko@streamunlimited.com>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>

#define ES9018_PCM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |	\
			    SNDRV_PCM_FMTBIT_S24_LE |	\
			    SNDRV_PCM_FMTBIT_S24_3LE |	\
			    SNDRV_PCM_FMTBIT_S32_LE |	\
			    SNDRV_PCM_FMTBIT_DSD_U8)

#define ES9018_PCM_RATES   (SNDRV_PCM_RATE_5512  | SNDRV_PCM_RATE_8000 | \
			    SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_16000 | \
			    SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 | \
			    SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | \
			    SNDRV_PCM_RATE_64000 | SNDRV_PCM_RATE_88200 | \
			    SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 | \
			    SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_352800 | \
			    SNDRV_PCM_RATE_384000 | SNDRV_PCM_RATE_705600 | \
			    SNDRV_PCM_RATE_768000 | SNDRV_PCM_RATE_1411200 | \
			    SNDRV_PCM_RATE_1536000 | SNDRV_PCM_RATE_2822400 | \
			    SNDRV_PCM_RATE_3072000)

/* ES9018 registers */
#define ES9018_INPUT_CONF	1
#define ES9018_SOFT_VOL3	6
#define ES9018_GENERAL		7
#define ES9018_GPIO		8
#define ES9018_CHANNELMAP	11
#define ES9018_DPLL_BW		12
#define ES9018_VOL1_LEFT 	15
#define ES9018_VOL2_RIGHT	16
#define ES9018_GPIO_INPUT_SEL	21
#define ES9018_CHIP_STATUS	64
/* ES9018_SOFT_VOL3 Masks */
#define ES9018_SOFT_VOL3_VOL_RATE 7
/* ES9018_GENERAL Masks */
#define ES9018_SOFT_MUTE_MASK	3
/* ES9018_CHIP_STATUS Masks */
#define ES9018_CHIP_ID_MASK	0x3C

#define ES9018_CHIP_ID		0x30 /* Includes a left shift for
				      * position in register [4:2]. */

#define ES9018_GPIO1_MASK	0x0F
#define ES9018_GPIO1_INPUT	8
#define ES9018_GPIO1_INPUT_SEL	9

#define ES9018_GPIO2_MASK	(ES9018_GPIO1_MASK << 4)
#define ES9018_GPIO2_INPUT	(ES9018_GPIO1_INPUT << 4)
#define ES9018_GPIO2_INPUT_SEL	(ES9018_GPIO1_INPUT_SEL << 4)

#define ES9018_GPIO_INPUT_SEL_HIGH_MASK (3 << 6)
#define ES9018_GPIO_INPUT_SEL_HIGH_I2S	(0 << 6)
#define ES9018_GPIO_INPUT_SEL_HIGH_DSD	(3 << 6)

#define ES9018_GPIO_INPUT_SEL_LOW_MASK	(3 << 4)
#define ES9018_GPIO_INPUT_SEL_LOW_I2S	(0 << 4)
#define ES9018_GPIO_INPUT_SEL_LOW_DSD	(3 << 4)

#define ES9018_INPUT_CONF_AUTO_SEL_MASK (3 << 2)
#define ES9018_INPUT_CONF_AUTO_SEL_NONE (0 << 2)

#define ES9018_DPLL_BW_DSD_MASK (0x0F)
#define ES9018_DPLL_BW_I2S_MASK (0xF0)

struct es9018_private {
	struct regmap	*regmap;
	unsigned int	format;
	/* GPIO driving Reset pin, if any */
	int		gpio_reset;
};

static int es9018_digital_mute(struct snd_soc_dai *dai, int mute)
{
	return snd_soc_update_bits(dai->codec, ES9018_GENERAL,
				   ES9018_SOFT_MUTE_MASK,
				   mute ? ES9018_SOFT_MUTE_MASK : 0);
}

#define HBW_BCLK_RATE	(2822400UL)

static bool is_dsd(snd_pcm_format_t fmt)
{
	switch (fmt) {
		case SNDRV_PCM_FORMAT_DSD_U8:
		case SNDRV_PCM_FORMAT_DSD_U16_LE:
		case SNDRV_PCM_FORMAT_DSD_U16_BE:
		case SNDRV_PCM_FORMAT_DSD_U32_LE:
		case SNDRV_PCM_FORMAT_DSD_U32_BE:
			return true;

		default:
			return false;
	}
}

static int es9018_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	snd_pcm_format_t fmt = params_format(params);
	u32 rate = params_rate(params);
	u32 bclk_rate = rate * snd_pcm_format_physical_width(fmt);

	if (is_dsd(fmt)) {
		/*
		 * For DSD64 keep the DSD DPLL bandwidth at the default value of 0x0A,
		 * for higher speeds set the bandwidth to a value of 0x0C.
		 */
		u32 bw_val = (bclk_rate <= HBW_BCLK_RATE) ? 0x0A : 0x0C;
		snd_soc_update_bits(codec, ES9018_DPLL_BW, ES9018_DPLL_BW_DSD_MASK, bw_val);
	} else {
		/*
		 * For BCLK below ~2.8 MHz we keep the DPLL bandwidth at the default value of 0x50,
		 * for higher speeds set the bandwidth to a value of 0xF0.
		 */
		u32 bw_val = (bclk_rate <= HBW_BCLK_RATE) ? 0x50 : 0xF0;
		snd_soc_update_bits(codec, ES9018_DPLL_BW, ES9018_DPLL_BW_I2S_MASK, bw_val);
	}

	return 0;
}

static const char * const es9018_pcm_rolloff_filter_txt[] = {
	"fast", "slow", "minimum phase"
};
static SOC_ENUM_SINGLE_DECL(es9018_pcm_rolloff_filter,
			    ES9018_GENERAL, 5,
			    es9018_pcm_rolloff_filter_txt);

static const char * const es9018_dsd_rolloff_filter_txt[] = {
	"47k", "50k", "60k", "70k"
};
static SOC_ENUM_SINGLE_DECL(es9018_dsd_rolloff_filter,
			    ES9018_GENERAL, 2,
			    es9018_dsd_rolloff_filter_txt);

static const char * const es9018_analog_polarity_txt[] = {
	"normal", "inverted"
};
static SOC_ENUM_DOUBLE_DECL(es9018_analog_polarity,
			    ES9018_CHANNELMAP, 2, 3,
			    es9018_analog_polarity_txt);

static const struct snd_kcontrol_new es9018_controls[] = {
	SOC_DOUBLE_R("Master Playback Volume", ES9018_VOL1_LEFT,
		     ES9018_VOL2_RIGHT, 0, 0xff, 1),
	SOC_DOUBLE("Master Playback Switch", ES9018_GENERAL, 0, 1, 1, 1),
	SOC_ENUM("PCM Rolloff filter", es9018_pcm_rolloff_filter),
	SOC_ENUM("DSD Rolloff filter", es9018_dsd_rolloff_filter),
	SOC_ENUM("Audio Polarity", es9018_analog_polarity),
};

static const struct snd_soc_dai_ops es9018_dai_ops = {
	.digital_mute	= es9018_digital_mute,
	.hw_params	= es9018_hw_params,
};

static struct snd_soc_dai_driver es9018_dai = {
	.name = "ESS ES9018K2M Sabre",
	.playback = {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= ES9018_PCM_RATES,
		.formats	= ES9018_PCM_FORMATS,
	},
	.ops = &es9018_dai_ops,
};

static int es9018_probe(struct snd_soc_codec *codec)
{
	/* According to rev 1 of the data sheet, vol_rate must set to
	 * 3'd7 for lowest THD.  This is the maximum rate, the default
	 * is 2. */
	snd_soc_update_bits(codec, ES9018_SOFT_VOL3,
			    ES9018_SOFT_VOL3_VOL_RATE, 7);

	/*
	 * Set GPIO2 to be a GPIO input and GPIO1 to be an input select.
	 */
	snd_soc_write(codec, ES9018_GPIO, ES9018_GPIO2_INPUT | ES9018_GPIO1_INPUT_SEL);

	/*
	 * Configure input select to be DSD when low and PCM when high.
	 */
	snd_soc_write(codec, ES9018_GPIO_INPUT_SEL, ES9018_GPIO_INPUT_SEL_HIGH_I2S | ES9018_GPIO_INPUT_SEL_LOW_DSD);

	/*
	 * Disable automatic input detection and use input select, which is the GPIO
	 */
	snd_soc_update_bits(codec, ES9018_INPUT_CONF, ES9018_INPUT_CONF_AUTO_SEL_MASK, ES9018_INPUT_CONF_AUTO_SEL_NONE);


	return 0;
}

static int es9018_remove(struct snd_soc_codec *codec)
{
	struct es9018_private *priv = snd_soc_codec_get_drvdata(codec);

	if (gpio_is_valid(priv->gpio_reset))
		/* Set codec to the reset state */
		gpio_set_value(priv->gpio_reset, 0);

	return 0;
};

static struct snd_soc_codec_driver soc_codec_dev_es9018 = {
	.probe			= es9018_probe,
	.remove			= es9018_remove,
	.controls		= es9018_controls,
	.num_controls		= ARRAY_SIZE(es9018_controls),
};

static const struct i2c_device_id es9018_i2c_id[] = {
	{ "es9018", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, es9018_i2c_id);

static const struct regmap_range es9018_read_registers_range[] = {
	regmap_reg_range(ES9018_INPUT_CONF, ES9018_INPUT_CONF),
	regmap_reg_range(ES9018_SOFT_VOL3, ES9018_GPIO),
	regmap_reg_range(ES9018_VOL1_LEFT, ES9018_VOL2_RIGHT),
	regmap_reg_range(ES9018_CHANNELMAP, ES9018_DPLL_BW),
	regmap_reg_range(ES9018_GPIO_INPUT_SEL, ES9018_GPIO_INPUT_SEL),
	regmap_reg_range(ES9018_CHIP_STATUS, ES9018_CHIP_STATUS),
};

static const struct regmap_range es9018_write_registers_range[] = {
	regmap_reg_range(ES9018_INPUT_CONF, ES9018_INPUT_CONF),
	regmap_reg_range(ES9018_SOFT_VOL3, ES9018_GPIO),
	regmap_reg_range(ES9018_VOL1_LEFT, ES9018_VOL2_RIGHT),
	regmap_reg_range(ES9018_CHANNELMAP, ES9018_DPLL_BW),
	regmap_reg_range(ES9018_GPIO_INPUT_SEL, ES9018_GPIO_INPUT_SEL),
};

static const struct reg_default es9018_reg_defaults[] = {
	{ ES9018_INPUT_CONF	, 0x8C },
	{ ES9018_SOFT_VOL3	, 0x4A },
	{ ES9018_GENERAL	, 0x80 },
	{ ES9018_GPIO		, 0x10 },
	{ ES9018_CHANNELMAP	, 0x02 },
	{ ES9018_GPIO_INPUT_SEL	, 0x00 },
	{ ES9018_VOL1_LEFT	, 0x00 },
	{ ES9018_VOL2_RIGHT	, 0x00 },
	{ ES9018_DPLL_BW	, 0x5A },
};

static const struct regmap_access_table es9018_read_registers = {
	.yes_ranges = es9018_read_registers_range,
	.n_yes_ranges = ARRAY_SIZE(es9018_read_registers_range),
	.no_ranges = NULL,
	.n_no_ranges = 0,
};

static const struct regmap_access_table es9018_write_registers = {
	.yes_ranges = es9018_write_registers_range,
	.n_yes_ranges = ARRAY_SIZE(es9018_write_registers_range),
	.no_ranges = NULL,
	.n_no_ranges = 0,
};

static const struct regmap_config es9018_regmap = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.wr_table		= &es9018_write_registers,
	.rd_table		= &es9018_read_registers,
	.reg_defaults		= es9018_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(es9018_reg_defaults),
};

#ifdef CONFIG_OF
static const struct of_device_id es9018_dt_ids[] = {
	{ .compatible = "ess,es9018k2m", },
	{ }
};
MODULE_DEVICE_TABLE(of, es9018_dt_ids);
#endif

static int es9018_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct es9018_private *priv;
	struct device *dev = &i2c->dev;
	int ret;
	unsigned int val;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->regmap = devm_regmap_init_i2c(i2c, &es9018_regmap);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err(dev, "Failed to create regmap: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(i2c, priv);

	if (regmap_read(priv->regmap, ES9018_CHIP_STATUS, &val) < 0) {
		dev_err(dev, "Failed to read chip status!\n");
		return -EINVAL;
	}
	if ((val & ES9018_CHIP_ID_MASK) != ES9018_CHIP_ID) {
		dev_err(dev, "Failed to read chip id, read: 0x%X!\n", val);
		return -EINVAL;
	}

	if (of_match_device(of_match_ptr(es9018_dt_ids), dev)) {
		struct device_node *of_node = dev->of_node;
		priv->gpio_reset = of_get_named_gpio(of_node, "reset-gpio", 0);
	}

	if (gpio_is_valid(priv->gpio_reset))
		if (devm_gpio_request_one(dev, priv->gpio_reset, GPIOF_OUT_INIT_HIGH, "ES9018 Reset"))
			priv->gpio_reset = -EINVAL;

	return snd_soc_register_codec(&i2c->dev, &soc_codec_dev_es9018, &es9018_dai, 1);
}

static int es9018_i2c_remove(struct i2c_client *i2c)
{
	snd_soc_unregister_codec(&i2c->dev);
	return 0;
}

static struct i2c_driver es9018_i2c_driver = {
	.driver = {
		.name	= "ESS ES9018K2M Sabre",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(es9018_dt_ids),
	},
	.id_table	= es9018_i2c_id,
	.probe		= es9018_i2c_probe,
	.remove		= es9018_i2c_remove,
};

module_i2c_driver(es9018_i2c_driver);

MODULE_AUTHOR("Fionn Cleary <fionn.cleary@streamunlimited.com>");
MODULE_DESCRIPTION("ESS ES9018K2M Sabre ALSA SoC Codec Driver");
MODULE_LICENSE("GPL");
