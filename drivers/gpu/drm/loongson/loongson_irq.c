/*
 * Copyright (c) 2018 Loongson Technology Co., Ltd.
 * Authors:
 *	Chen Zhu <zhuchen@loongson.cn>
 *	Yaling Fang <fangyaling@loongson.cn>
 *	Dandan Zhang <zhangdandan@loongson.cn>
 *	Huacai Chen <chenhc@lemote.com>
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */


#include "loongson_drv.h"

static DEFINE_SPINLOCK(drmfb_lock);

/**
 * enable_vblank - enable vblank interrupt events
 * @dev: DRM device
 * @crtc: which irq to enable
 *
 * Enable vblank interrupts for @crtc.  If the device doesn't have
 * a hardware vblank counter, this routine should be a no-op, since
 * interrupts will have to stay on to keep the count accurate.
 *
 * RETURNS
 * Zero on success, appropriate errno if the given @crtc's vblank
 * interrupt cannot be enabled.
 */
int loongson_irq_enable_vblank(struct drm_device *dev,unsigned int crtc_id)
{
	return 0;
}

/**
 * disable_vblank - disable vblank interrupt events
 * @dev: DRM device
 * @crtc: which irq to enable
 *
 * Disable vblank interrupts for @crtc.  If the device doesn't have
 * a hardware vblank counter, this routine should be a no-op, since
 * interrupts will have to stay on to keep the count accurate.
 */
void loongson_irq_disable_vblank(struct drm_device *dev,unsigned int crtc_id)
{
}

static void crtc_irq(struct drm_crtc *crtc)
{
	struct drm_pending_vblank_event *event = crtc->state->event;

	drm_crtc_handle_vblank(crtc);

	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, event);
		drm_crtc_vblank_put(crtc);
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

irqreturn_t loongson_irq_handler(int irq, void *arg)
{
	unsigned int val, cfg;
	struct drm_device *dev = (struct drm_device *) arg;
	struct loongson_drm_device *ldev = dev->dev_private;
	void __iomem *base = ldev->rmmio;

	val = readl(base + LS_FB_INT_REG);
	spin_lock(&loongson_reglock);
	writel(0x0, base + LS_FB_INT_REG);
	spin_unlock(&loongson_reglock);

	if (val & BIT(LS_INT_DVO0_FB_END)){
		crtc_irq(&ldev->lcrtc[0].base);
	}

	if (val & BIT(LS_INT_DVO1_FB_END)){
		crtc_irq(&ldev->lcrtc[1].base);
	}

	spin_lock(&loongson_reglock);
	writel(ldev->int_reg, base + LS_FB_INT_REG);
	spin_unlock(&loongson_reglock);

	return IRQ_HANDLED;
}

void loongson_irq_preinstall(struct drm_device *dev)
{
	unsigned long flags;
	struct loongson_drm_device *ldev = dev->dev_private;
	volatile void *base = ldev->rmmio;

	/* disable interupt */
	spin_lock_irqsave(&loongson_reglock, flags);
	writel(0x0000 << 16, base + LS_FB_INT_REG);
	spin_unlock_irqrestore(&loongson_reglock, flags);
}

int loongson_irq_postinstall(struct drm_device *dev)
{
	return 0;
}

void loongson_irq_uninstall(struct drm_device *dev)
{
	unsigned long flags;
	struct loongson_drm_device *ldev = dev->dev_private;
	volatile void *base = ldev->rmmio;

	/* disable interupt */
	spin_lock_irqsave(&loongson_reglock, flags);
	writel(0x0000 << 16, base + LS_FB_INT_REG);
	spin_unlock_irqrestore(&loongson_reglock, flags);
}
