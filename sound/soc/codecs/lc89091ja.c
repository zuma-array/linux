/*
 * ON Semiconductor LC89091JA ASoC codec driver
 *
 * Copyright (c) StreamUnlimited GmbH 2017
 * 	Martin Pietryka <martin.pietryka@streamunlimited.com>
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
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/soc.h>


#define LC89091JA_RATES		(SNDRV_PCM_RATE_32000 | \
				 SNDRV_PCM_RATE_44100 | \
				 SNDRV_PCM_RATE_48000 | \
				 SNDRV_PCM_RATE_64000 | \
				 SNDRV_PCM_RATE_88200 | \
				 SNDRV_PCM_RATE_96000 | \
				 SNDRV_PCM_RATE_176400 | \
				 SNDRV_PCM_RATE_192000)

#define LC89091JA_FORMATS	(SNDRV_PCM_FMTBIT_S24_LE | \
				 SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_codec_driver soc_codec_dev_lc89091ja = {

};

static struct snd_soc_dai_ops lc89091ja_dai_ops = {
};

static struct snd_soc_dai_driver lc89091ja_dai = {
	.name = "LC89091JA",
	.capture = {
		.stream_name	= "Capture",
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= LC89091JA_RATES,
		.formats	= LC89091JA_FORMATS,
	},
	.ops = &lc89091ja_dai_ops,
};

static int lc89091ja_i2c_probe(struct i2c_client *i2c_client,
				const struct i2c_device_id *id)
{
	return snd_soc_register_codec(&i2c_client->dev, &soc_codec_dev_lc89091ja, &lc89091ja_dai, 1);
}

static int lc89091ja_i2c_remove(struct i2c_client *i2c_client)
{
	return 0;
}

static const struct i2c_device_id lc89091ja_i2c_id[] = {
	{ "lc89091ja", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lc89091ja_i2c_id);

static const struct of_device_id lc89091ja_of_match[] = {
	{ .compatible = "onsemi,lc89091ja", },
	{ },
};
MODULE_DEVICE_TABLE(of, lc89091ja_of_match);


static struct i2c_driver lc89091ja_i2c_driver = {
	.driver = {
		.name = "lc89091a",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(lc89091ja_of_match),
	},
	.id_table = lc89091ja_i2c_id,
	.probe = lc89091ja_i2c_probe,
	.remove = lc89091ja_i2c_remove,
};
module_i2c_driver(lc89091ja_i2c_driver);


MODULE_AUTHOR("Martin Pietryka <martin.pietryka@streamunlimited.com>");
MODULE_DESCRIPTION("ASoC LC89091JA driver");
MODULE_LICENSE("GPL");
