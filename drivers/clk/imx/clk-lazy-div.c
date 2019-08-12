#include <linux/clk-provider.h>
#include <linux/slab.h>

#define div_mask(width)	((1 << (width)) - 1)

/*
 * The dividers on the imx7 and imx8 will create a glitch everytime a value is
 * written to the configuration register, even when the value is the same. One way
 * to work around this issue is to only write the new value to the register if
 * it actually has changed. Previously we introduced a new divider flag to handle
 * this, however because the flag is only 8 bits all the flags are already in use
 * in the current kernel version. Instead of changing the size of the divider flags
 * field we introduce a new variant of the divider driver. The code in
 * `imx_clk_register_lazy_divider()` and `clk_lazy_divider_set_rate()` is mostly
 * based on the current code from `drivers/clk/clk-divider.c`, the only real
 * difference is the behaviour inside `clk_lazy_divider_set_rate()`, where the
 * register will only be written if the value really has changed.
 */

static unsigned long clk_lazy_divider_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return clk_divider_ops.recalc_rate(hw, parent_rate);
}

static long clk_lazy_divider_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	return clk_divider_ops.round_rate(hw, rate, prate);
}

static int clk_lazy_divider_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_divider *divider = to_clk_divider(hw);
	int value;
	unsigned long flags = 0;
	u32 val;

	value = divider_get_val(rate, parent_rate, divider->table,
				divider->width, divider->flags);
	if (value < 0)
		return value;

	if ((divider->flags & CLK_DIVIDER_ZERO_GATE) &&
	    !clk_hw_is_enabled(hw)) {
		divider->cached_val = value;
		return 0;
	}

	if (divider->lock)
		spin_lock_irqsave(divider->lock, flags);
	else
		__acquire(divider->lock);

	if (divider->flags & CLK_DIVIDER_HIWORD_MASK) {
		val = div_mask(divider->width) << (divider->shift + 16);
	} else {
		val = clk_readl(divider->reg);
		val &= ~(div_mask(divider->width) << divider->shift);
	}
	val |= (u32)value << divider->shift;

	/*
	 * All this boilerplate code for this single if, we only write the new register
	 * value if it actually is different.
	 */
	if (val != clk_readl(divider->reg))
		clk_writel(val, divider->reg);

	if (divider->lock)
		spin_unlock_irqrestore(divider->lock, flags);
	else
		__release(divider->lock);

	return 0;
}

static int clk_lazy_divider_enable(struct clk_hw *hw)
{
	return clk_divider_ops.enable(hw);
}

static void clk_lazy_divider_disable(struct clk_hw *hw)
{
	return clk_divider_ops.disable(hw);
}

static int clk_lazy_divider_is_enabled(struct clk_hw *hw)
{
	return clk_divider_ops.is_enabled(hw);
}


static const struct clk_ops clk_lazy_divider_ops = {
	.recalc_rate = clk_lazy_divider_recalc_rate,
	.round_rate = clk_lazy_divider_round_rate,
	.set_rate = clk_lazy_divider_set_rate,
	.enable = clk_lazy_divider_enable,
	.disable = clk_lazy_divider_disable,
	.is_enabled = clk_lazy_divider_is_enabled,
};

struct clk *imx_clk_register_lazy_divider(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 shift, u8 width,
		u16 clk_divider_flags, spinlock_t *lock)
{
	struct clk_divider *div;
	struct clk_hw *hw;
	struct clk_init_data init;
	u32 val;
	int ret;

	if (clk_divider_flags & CLK_DIVIDER_HIWORD_MASK) {
		if (width + shift > 16) {
			pr_warn("divider value exceeds LOWORD field\n");
			return ERR_PTR(-EINVAL);
		}
	}

	/* allocate the divider */
	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_lazy_divider_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name: NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_divider assignments */
	div->reg = reg;
	div->shift = shift;
	div->width = width;
	div->flags = clk_divider_flags;
	div->lock = lock;
	div->hw.init = &init;

	if (div->flags & CLK_DIVIDER_ZERO_GATE) {
		val = clk_readl(reg) >> shift;
		val &= div_mask(width);
		div->cached_val = val;
	}

	/* register the clock */
	hw = &div->hw;
	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(div);
		return ERR_PTR(ret);
	}

	return hw->clk;
}
