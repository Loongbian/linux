/*
 * Copyright (C) 2017 Loongson Technology Corporation Limited
 *
 * Author: Juxin Gao <gaojuxin@loongson.cn>
 * License terms: GNU General Public License (GPL)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/of_device.h>
#include <linux/slab.h>

/* counter offest */
#define LOW_BUFFER  0x004
#define FULL_BUFFER 0x008
#define CTRL		0x00c

/* CTRL counter each bit */
#define CTRL_EN		BIT(0)
#define CTRL_OE		BIT(3)
#define CTRL_SINGLE	BIT(4)
#define CTRL_INTE	BIT(5)
#define CTRL_INT	BIT(6)
#define CTRL_RST	BIT(7)
#define CTRL_CAPTE	BIT(8)
#define CTRL_INVERT	BIT(9)
#define CTRL_DZONE	BIT(10)

#define to_ls2k_pwm_chip(_chip)		container_of(_chip, struct ls2k_pwm_chip, chip)
#define NS_IN_HZ (1000000000UL)
#define CPU_FRQ_PWM (125000000UL)

struct ls2k_pwm_chip{
	struct pwm_chip chip;
	void __iomem		*mmio_base;
};

static int ls2k_pwm_set_polarity(struct pwm_chip *chip,
				      struct pwm_device *pwm,
				      enum pwm_polarity polarity)
{
	struct ls2k_pwm_chip *ls2k_pwm = to_ls2k_pwm_chip(chip);
	u16 val;

	val = readl(ls2k_pwm->mmio_base + CTRL);
	val |= CTRL_INVERT;
	writel(val, ls2k_pwm->mmio_base);
	return 0;
}

static void ls2k_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ls2k_pwm_chip *ls2k_pwm = to_ls2k_pwm_chip(chip);
	u32 ret;

	ret = readl(ls2k_pwm->mmio_base + CTRL);
	ret &= ~CTRL_EN;
	writel(ret, ls2k_pwm->mmio_base + CTRL);
}

static int ls2k_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ls2k_pwm_chip *ls2k_pwm = to_ls2k_pwm_chip(chip);
	int ret;

	ret = readl(ls2k_pwm->mmio_base + CTRL);
	ret |= CTRL_EN;
	writel(ret, ls2k_pwm->mmio_base + CTRL);
	return 0;
}

static int ls2k_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
				int duty_ns, int period_ns)
{
	struct ls2k_pwm_chip *ls2k_pwm = to_ls2k_pwm_chip(chip);
	unsigned int period, duty;
	unsigned long long val0,val1;

	if (period_ns > NS_IN_HZ || duty_ns > NS_IN_HZ)
		return -ERANGE;

	val0 = CPU_FRQ_PWM * period_ns;
	do_div(val0, NSEC_PER_SEC);
	if (val0 < 1)
		val0 = 1;
	period = val0;

	val1 = CPU_FRQ_PWM * duty_ns;
	do_div(val1, NSEC_PER_SEC);
	if (val1 < 1)
		val1 = 1;
	duty = val1;

	writel(duty,ls2k_pwm->mmio_base + LOW_BUFFER);
	writel(period,ls2k_pwm->mmio_base + FULL_BUFFER);

	return 0;
}

static const struct pwm_ops ls2k_pwm_ops = {
	.config = ls2k_pwm_config,
	.set_polarity = ls2k_pwm_set_polarity,
	.enable = ls2k_pwm_enable,
	.disable = ls2k_pwm_disable,
	.owner = THIS_MODULE,
};

static int ls2k_pwm_probe(struct platform_device *pdev)
{
	struct ls2k_pwm_chip *pwm;
	struct resource *mem;
	int err;
	if (pdev->id > 3)
		dev_err(&pdev->dev, "Currently only four PWM controller is implemented,namely, 0,1,2,3.\n");
	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if(!pwm){
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	pwm->chip.dev = &pdev->dev;
	pwm->chip.ops = &ls2k_pwm_ops;
	pwm->chip.base = -1;
	pwm->chip.npwm = 1;

	mem = platform_get_resource(pdev,IORESOURCE_MEM, 0);
	if(!mem){
		dev_err(&pdev->dev, "no mem resource?\n");
		return -ENODEV;
	}
	pwm->mmio_base = devm_ioremap_resource(&pdev->dev, mem);
	if(!pwm->mmio_base){
		dev_err(&pdev->dev, "mmio_base is null\n");
		return -ENOMEM;
	}
	err = pwmchip_add(&pwm->chip);
	if(err < 0){
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n",err);
		return err;
	}

	platform_set_drvdata(pdev, pwm);

	dev_dbg(&pdev->dev, "pwm probe successful\n");
	return 0;
}

static int ls2k_pwm_remove(struct platform_device *pdev)
{
	struct ls2k_pwm_chip *pwm = platform_get_drvdata(pdev);
	int err;
	if (!pwm)
		return -ENODEV;
	err = pwmchip_remove(&pwm->chip);
	if(err < 0)
		return err;

	return 0;
}
#ifdef CONFIG_OF
static struct of_device_id ls2k_pwm_id_table[] = {
	{.compatible = "loongson,ls2k-pwm"},
	{},
};
#endif
static struct platform_driver ls2k_pwm_driver = {
	.driver = {
		.name = "ls2k-pwm",
		.owner = THIS_MODULE,
		.bus = &platform_bus_type,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(ls2k_pwm_id_table),
#endif
	},
	.probe = ls2k_pwm_probe,
	.remove = ls2k_pwm_remove,
};
module_platform_driver(ls2k_pwm_driver);

MODULE_AUTHOR("Juxin Gao <gaojuxin@loongson.com>");
MODULE_DESCRIPTION("Loongson 2k1000 Pwm Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ls2k-pwm");
