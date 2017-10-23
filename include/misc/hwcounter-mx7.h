#ifndef _HWCOUNTER_MX7_H_
#define _HWCOUNTER_MX7_H_

struct hwcounter_data {
	void __iomem *timer_base;
	struct clk *clk_per;
};

u32 hwcounter_get_value(struct hwcounter_data *hwcounter);

#endif
