#include <asm/io.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/uio_driver.h>

#include <misc/hwcounter-mx7.h>

#define MXC_GPT_REG_CR	0x00
#define MXC_GPT_REG_PR	0x04
#define MXC_GPT_REG_IR	0x0C
#define MXC_GPT_REG_CNT	0x24

#define MXC_GPT_CR_EN		(1 << 0) /* Enable the counter */
#define MXC_GPT_CR_ENMOD	(1 << 1) /* Clear counter on disable */
#define MXC_GPT_CR_DBGEN	(1 << 2) /* Keep counter enabled in debug mode */
#define MXC_GPT_CR_WAITEN	(1 << 3) /* Keep counter enabled in wait mode */
#define MXC_GPT_CR_DOZEEN	(1 << 4) /* Keep counter enabled in doze mode */
#define MXC_GPT_CR_STOPEN	(1 << 5) /* Keep counter enabled in stop mode */
#define MXC_GPT_CR_FRR		(1 << 9) /* Free running mode */

#define MXC_GPT_CR_CLKSRC_PER	(1 << 6)
#define MXC_GPT_CR_CLKSRC_CLKIN	(3 << 6)

#define DRIVER_NAME "sue_hwcounter"


inline u32 hwcounter_get_value(struct hwcounter_data *hwcounter)
{
	return ioread32(hwcounter->timer_base + MXC_GPT_REG_CNT);
}
EXPORT_SYMBOL_GPL(hwcounter_get_value);

s32 hwcounter_get_per_rate(struct hwcounter_data *hwcounter)
{
	if (!hwcounter->use_per_clk)
		return -EINVAL;

	/*
	 * If we count the peripheral clock we need to divide it by the prescaler which we
	 * set to get the real counting value.
	 *
	 * If the peripheral clock had some divider in between then clk_get_rate() will already
	 * return the proper value.
	 */
	return clk_get_rate(hwcounter->clk_per) / (hwcounter->prescaler + 1);
}
EXPORT_SYMBOL_GPL(hwcounter_get_per_rate);

static ssize_t value_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int written;
	struct hwcounter_data *pdata = (struct hwcounter_data *)dev_get_drvdata(dev);

	u32 gpio_counter = hwcounter_get_value(pdata);

	written = scnprintf(buf, PAGE_SIZE, "%u\n", gpio_counter);

	return written;
}
DEVICE_ATTR(value, 0400, value_show, NULL);

static int hwcounter_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hwcounter_data *pdata;
	int ret;
	u32 reg;
	struct resource res;
	struct uio_info *uio;

	pdata = devm_kzalloc(dev, sizeof(struct hwcounter_data), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	uio = devm_kzalloc(dev, sizeof(struct uio_info), GFP_KERNEL);
	if (!uio)
		return -ENOMEM;

	/*
	 * This is essentially what of_iomap() does but we also want to extract the
	 * resource info as well since we will need the physical address for uio.
	 */
	ret = of_address_to_resource(dev->of_node, 0, &res);
	if (ret)
		return ret;

	pdata->timer_base = ioremap(res.start, resource_size(&res));
	if (!pdata->timer_base) {
		dev_err(dev, "failed ioremap() call\n");
		return -ENXIO;
	}
	dev_info(dev, "timer_base is %p\n", pdata->timer_base);


	/*
	 * Register the UIO device early since the uio_register_device()
	 * call might return EPROBE_DEFER.
	 */
	pdata->uio = uio;
	uio->name = DRIVER_NAME;
	uio->version = "0.1";

	uio->mem[0].name = "timer_regs";
	uio->mem[0].addr = res.start;
	uio->mem[0].size = 4096;
	uio->mem[0].memtype = UIO_MEM_PHYS;

	ret = uio_register_device(dev, uio);
	if (ret)
		return ret;

	pdata->clk_per = of_clk_get_by_name(dev->of_node, "per");
	if (IS_ERR(pdata->clk_per)) {
		iounmap(pdata->timer_base);
		dev_err(dev, "failed to get per clk\n");
		return -ENXIO;
	}

	/* read out and apply peripheral clock divider */
	ret = of_property_read_u32(dev->of_node, "sue,per-div", &pdata->per_div);
	if (ret < 0)
		pdata->per_div = 1;

	ret = clk_set_rate(pdata->clk_per, clk_get_rate(clk_get_parent(pdata->clk_per)) / pdata->per_div);
	if (ret < 0)
		dev_err(dev, "failed to set peripheral clock rate %d\n", ret);

	clk_prepare_enable(pdata->clk_per);


	if (of_get_property(dev->of_node, "sue,use-per-clk", NULL))
		pdata->use_per_clk = true;
	else
		pdata->use_per_clk = false;


	ret = of_property_read_u32(dev->of_node, "sue,prescaler", &pdata->prescaler);
	if (ret == 0) {
		/* Set prescaler according to value from device tree */
		dev_info(dev, "setting prescaler to %u\n", pdata->prescaler);
	} else {
		dev_info(dev, "prescaler will be disabled\n");
		pdata->prescaler = 0;
	}

	/* Set configuration to 0 */
	iowrite32(0, pdata->timer_base + MXC_GPT_REG_CR);
	/* Set prescaler */
	iowrite32(pdata->prescaler, pdata->timer_base + MXC_GPT_REG_PR);
	/* Disable all interrupts */
	iowrite32(0, pdata->timer_base + MXC_GPT_REG_IR);

	reg = 0;
	if (pdata->use_per_clk)
		reg |= MXC_GPT_CR_CLKSRC_PER;
	else
		reg |= MXC_GPT_CR_CLKSRC_CLKIN;

	reg |= MXC_GPT_CR_FRR | MXC_GPT_CR_ENMOD;
	reg |= MXC_GPT_CR_STOPEN | MXC_GPT_CR_DOZEEN | MXC_GPT_CR_WAITEN | MXC_GPT_CR_DBGEN;

	iowrite32(reg, pdata->timer_base + MXC_GPT_REG_CR);

	msleep(5);

	reg |= MXC_GPT_CR_EN;
	iowrite32(reg, pdata->timer_base + MXC_GPT_REG_CR);

	dev_info(dev, "timer configured\n");

	device_create_file(dev, &dev_attr_value);

	dev_set_drvdata(dev, pdata);

	return 0;
}

static int hwcounter_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hwcounter_data *pdata = (struct hwcounter_data *)dev_get_drvdata(dev);

	uio_unregister_device(pdata->uio);

	device_remove_file(dev, &dev_attr_value);

	/* Set configuration to 0, thus disabling the counter */
	iowrite32(0, pdata->timer_base + MXC_GPT_REG_CR);

	clk_disable_unprepare(pdata->clk_per);

	iounmap(pdata->timer_base);

	return 0;
}

static const struct of_device_id hwcounter_dt_ids[] = {
	{ .compatible = "sue,hwcounter-mx7" },
	{}
};

MODULE_DEVICE_TABLE(of, hwcounter_dt_ids);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Martin Pietryka <martin.pietryka@streamunlimited.com>");

static struct platform_driver hwcounter_driver = {
	.probe		= hwcounter_probe,
	.remove		= hwcounter_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table	= of_match_ptr(hwcounter_dt_ids),
	},
};

module_platform_driver(hwcounter_driver);
