#ifndef _HWCOUNTER_MX7_H_
#define _HWCOUNTER_MX7_H_

#include <linux/uio_driver.h>

struct hwcounter_data {
	void __iomem *timer_base;
	struct clk *clk_per;

	u32 per_div;
	u32 prescaler;
	bool use_per_clk;
	struct uio_info *uio;
};

u32 hwcounter_get_value(struct hwcounter_data *hwcounter);
s32 hwcounter_get_per_rate(struct hwcounter_data *hwcounter);

#endif
