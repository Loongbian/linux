// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2020, Jiaxun Yang <jiaxun.yang@flygoat.com>
 *  Loongson PCH PIC support
 */

#define pr_fmt(fmt) "pch-pic: " fmt

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

/* Registers */
#define PCH_PIC_MASK		0x20
#define PCH_PIC_HTMSI_EN	0x40
#define PCH_PIC_EDGE		0x60
#define PCH_PIC_CLR			0x80
#define PCH_PIC_AUTO0		0xc0
#define PCH_PIC_AUTO1		0xe0
#define PCH_INT_ROUTE(irq)	(0x100 + irq)
#define PCH_INT_HTVEC(irq)	(0x200 + irq)
#define PCH_PIC_POL			0x3e0

#define PIC_COUNT_PER_REG	32
#define PIC_REG_COUNT		2
#define PIC_COUNT			(PIC_COUNT_PER_REG * PIC_REG_COUNT)
#define PIC_REG_IDX(irq_id)	((irq_id) / PIC_COUNT_PER_REG)
#define PIC_REG_BIT(irq_id)	((irq_id) % PIC_COUNT_PER_REG)

struct pch_pic {
	void __iomem		*base;
	struct irq_domain	*pic_domain;
	u32			ht_vec_base;
	raw_spinlock_t		pic_lock;
};

static void pch_pic_bitset(struct pch_pic *priv, int offset, int bit)
{
	void __iomem *addr = priv->base + offset + PIC_REG_IDX(bit) * 4;
	unsigned long flags;
	u32 reg;

	raw_spin_lock_irqsave(&priv->pic_lock, flags);
	reg = readl(addr);
	reg |= BIT(PIC_REG_BIT(bit));
	writel(reg, addr);
	raw_spin_unlock_irqrestore(&priv->pic_lock, flags);
}

static void pch_pic_bitclr(struct pch_pic *priv, int offset, int bit)
{
	void __iomem *addr = priv->base + offset + PIC_REG_IDX(bit) * 4;
	unsigned long flags;
	u32 reg;

	raw_spin_lock_irqsave(&priv->pic_lock, flags);
	reg = readl(addr);
	reg &= ~BIT(PIC_REG_BIT(bit));
	writel(reg, addr);
	raw_spin_unlock_irqrestore(&priv->pic_lock, flags);
}

static void pch_pic_eoi_irq(struct irq_data *d)
{
	struct pch_pic *priv = irq_data_get_irq_chip_data(d);
	u32 idx = PIC_REG_IDX(d->hwirq);

	writel(BIT(PIC_REG_BIT(d->hwirq)),
			priv->base + PCH_PIC_CLR + idx * 4);
}

static void pch_pic_mask_irq(struct irq_data *d)
{
	struct pch_pic *priv = irq_data_get_irq_chip_data(d);

	pch_pic_bitset(priv, PCH_PIC_MASK, d->hwirq);
	irq_chip_mask_parent(d);
}

static void pch_pic_unmask_irq(struct irq_data *d)
{
	struct pch_pic *priv = irq_data_get_irq_chip_data(d);

	pch_pic_bitclr(priv, PCH_PIC_MASK, d->hwirq);
	irq_chip_unmask_parent(d);
}

static int pch_pic_set_type(struct irq_data *d, unsigned int type)
{
	struct pch_pic *priv = irq_data_get_irq_chip_data(d);
	int ret = 0;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		pch_pic_bitset(priv, PCH_PIC_EDGE, d->hwirq);
		pch_pic_bitclr(priv, PCH_PIC_POL, d->hwirq);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		pch_pic_bitset(priv, PCH_PIC_EDGE, d->hwirq);
		pch_pic_bitset(priv, PCH_PIC_POL, d->hwirq);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		pch_pic_bitclr(priv, PCH_PIC_EDGE, d->hwirq);
		pch_pic_bitclr(priv, PCH_PIC_POL, d->hwirq);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		pch_pic_bitclr(priv, PCH_PIC_EDGE, d->hwirq);
		pch_pic_bitset(priv, PCH_PIC_POL, d->hwirq);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static struct irq_chip pch_pic_irq_chip = {
	.name			= "PCH PIC",
	.irq_mask		= pch_pic_mask_irq,
	.irq_unmask		= pch_pic_unmask_irq,
	.irq_ack		= irq_chip_ack_parent,
	.irq_eoi		= pch_pic_eoi_irq,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.irq_set_type		= pch_pic_set_type,
};

static int pch_pic_alloc(struct irq_domain *domain, unsigned int virq,
			      unsigned int nr_irqs, void *arg)
{
	struct pch_pic *priv = domain->host_data;
	struct irq_fwspec fwspec;
	unsigned long hwirq;
	unsigned int type;
	int err;

	irq_domain_translate_twocell(domain, arg, &hwirq, &type);

	fwspec.fwnode = domain->parent->fwnode;
	fwspec.param_count = 1;
	fwspec.param[0] = hwirq + priv->ht_vec_base;

	err = irq_domain_alloc_irqs_parent(domain, virq, 1, &fwspec);
	if (err)
		return err;

	irq_domain_set_info(domain, virq, hwirq,
			    &pch_pic_irq_chip, priv,
			    handle_fasteoi_ack_irq, NULL, NULL);
	irq_set_probe(virq);

	return 0;
}

static const struct irq_domain_ops pch_pic_domain_ops = {
	.translate	= irq_domain_translate_twocell,
	.alloc		= pch_pic_alloc,
	.free		= irq_domain_free_irqs_parent,
};

static void pch_pic_reset(struct pch_pic *priv)
{
	int i;

	for (i = 0; i < PIC_COUNT; i++) {
		/* Write vectore ID */
		writeb(priv->ht_vec_base + i, priv->base + PCH_INT_HTVEC(i));
		/* Hardcode to HT0 Lo */
		writeb(1, priv->base + PCH_INT_ROUTE(i));
	}

	for (i = 0; i < PIC_REG_COUNT; i++) {
		/* Clear IRQ cause registers, mask all interrupts */
		writel_relaxed(0xFFFFFFFF, priv->base + PCH_PIC_MASK + 4 * i);
		writel_relaxed(0xFFFFFFFF, priv->base + PCH_PIC_CLR + 4 * i);
		/* Clear auto bounce, we don't need that */
		writel_relaxed(0, priv->base + PCH_PIC_AUTO0 + 4 * i);
		writel_relaxed(0, priv->base + PCH_PIC_AUTO1 + 4 * i);
		/* Enable HTMSI transformer */
		writel_relaxed(0xFFFFFFFF, priv->base + PCH_PIC_HTMSI_EN + 4 * i);
	}
}

static int pch_pic_of_init(struct device_node *node,
				struct device_node *parent)
{
	struct pch_pic *priv;
	struct irq_domain *parent_domain;
	int err;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	raw_spin_lock_init(&priv->pic_lock);
	priv->base = of_iomap(node, 0);
	if (!priv->base) {
		err = -ENOMEM;
		goto free_priv;
	}

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		pr_err("Failed to find the parent domain\n");
		err = -ENXIO;
		goto iounmap_base;
	}

	if (of_property_read_u32(node, "loongson,pic-base-vec",
				&priv->ht_vec_base)) {
		pr_err("Failed to determine pic-base-vec\n");
		err = -EINVAL;
		goto iounmap_base;
	}
	

	priv->pic_domain = irq_domain_create_hierarchy(parent_domain, 0,
						     PIC_COUNT,
						     of_node_to_fwnode(node),
						     &pch_pic_domain_ops,
						     priv);
	if (!priv->pic_domain) {
		pr_err("Failed to create IRQ domain\n");
		err = -ENOMEM;
		goto iounmap_base;
	}

	pch_pic_reset(priv);

	return 0;

iounmap_base:
	iounmap(priv->base);
free_priv:
	kfree(priv);

	return err;
}

IRQCHIP_DECLARE(pch_pic, "loongson,pch-pic-1.0", pch_pic_of_init);
