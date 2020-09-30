// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Lemote Inc.
 * Authors:
 *	Jiaxun Yang <jiaxun.yang@flygoat.com>
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_fourcc.h>

#include "loongson_drv.h"

static const uint32_t loongson_primary_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
};

static const uint64_t loongson_format_modifiers[] = {
    DRM_FORMAT_MOD_LINEAR,
    DRM_FORMAT_MOD_INVALID
};

static int loongson_crtc_enable_vblank(struct drm_crtc *crtc) {
	struct loongson_crtc *lcrtc = to_loongson_crtc(crtc);
	struct loongson_drm_device *ldev = lcrtc->ldev;

	if(lcrtc->crtc_id == 0) {
		ldev->int_reg |= (BIT(LS_INT_DVO0_FB_END) << 16);
	} else {
		ldev->int_reg |= (BIT(LS_INT_DVO1_FB_END) << 16);
	}

	writel(ldev->int_reg, ldev->rmmio  + LS_FB_INT_REG);

	return 0;
}

static void loongson_crtc_disable_vblank(struct drm_crtc *crtc) {
	struct loongson_crtc *lcrtc = to_loongson_crtc(crtc);
	struct loongson_drm_device *ldev = lcrtc->ldev;

	if(lcrtc->crtc_id == 0) {
		ldev->int_reg &= (~BIT(LS_INT_DVO0_VSYNC) << 16);
	} else {
		ldev->int_reg &= (~BIT(LS_INT_DVO1_VSYNC) << 16);
	}

	writel(ldev->int_reg, ldev->rmmio  + LS_FB_INT_REG);

}

static const struct drm_crtc_funcs loongson_crtc_funcs = {
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = loongson_crtc_enable_vblank,
	.disable_vblank = loongson_crtc_disable_vblank,
};

static enum drm_mode_status loongson_crtc_mode_valid(struct drm_crtc *crtc,
						    const struct drm_display_mode *mode)
{
	if(mode->hdisplay % 16)
		return MODE_BAD;

	return MODE_OK;
}

u32 ls_crtc_read(struct loongson_crtc *lcrtc, u32 offset)
{
    struct loongson_drm_device *ldev = lcrtc->ldev;
//	pr_err("ls_read: %lx", ldev->rmmio + offset + (lcrtc->crtc_id * REG_OFF));
    return readl(ldev->rmmio + offset + (lcrtc->crtc_id * REG_OFF));
	return 0;
}

void ls_crtc_write(struct loongson_crtc *lcrtc, u32 offset, u32 val)
{
    struct loongson_drm_device *ldev = lcrtc->ldev;
//	pr_err("ls_write: %lx, %lx", ldev->rmmio + offset + (lcrtc->crtc_id * REG_OFF), val);
    writel(val, ldev->rmmio + offset + (lcrtc->crtc_id * REG_OFF));
}

static void loongson_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
    struct loongson_crtc *lcrtc = to_loongson_crtc(crtc);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;


    unsigned int hr, hss, hse, hfl;
	unsigned int vr, vss, vse, vfl;
    unsigned int width, height;
	unsigned int crtc_id, pix_freq;

    hr	= mode->hdisplay;
	hss	= mode->hsync_start;
	hse	= mode->hsync_end;
	hfl	= mode->htotal;

	vr	= mode->vdisplay;
	vss	= mode->vsync_start;
	vse	= mode->vsync_end;
	vfl	= mode->vtotal;

	pix_freq = mode->clock;


//    pr_info("fb width = %d, height = %d\n", width, height);
	pr_info("crtc_id = %d, hr = %d, hss = %d, hse = %d, hfl = %d, vr = %d, vss = %d, vse = %d, vfl = %d, pix_freq = %d,\n",
			lcrtc->crtc_id, hr, hss, hse, hfl, vr, vss, vse, vfl, pix_freq);

	spin_lock_irq(&loongson_reglock);
    ls_crtc_write(lcrtc, LS_FB_DITCFG_DVO0_REG, 0);
    ls_crtc_write(lcrtc, LS_FB_DITTAB_LO_DVO0_REG, 0);
    ls_crtc_write(lcrtc, LS_FB_DITTAB_HI_DVO0_REG, 0);
    ls_crtc_write(lcrtc, LS_FB_PANCFG_DVO0_REG, 0x80001311);
    ls_crtc_write(lcrtc, LS_FB_PANTIM_DVO0_REG, 0);

    ls_crtc_write(lcrtc, LS_FB_HDISPLAY_DVO0_REG, (mode->crtc_htotal << 16) | mode->crtc_hdisplay);
    ls_crtc_write(lcrtc, LS_FB_HSYNC_DVO0_REG, 0x40000000 | (mode->crtc_hsync_end << 16) | mode->crtc_hsync_start);

    ls_crtc_write(lcrtc, LS_FB_VDISPLAY_DVO0_REG, (mode->crtc_vtotal << 16) | mode->crtc_vdisplay);
    ls_crtc_write(lcrtc, LS_FB_VSYNC_DVO0_REG, 0x40000000 | (mode->crtc_vsync_end << 16) | mode->crtc_vsync_start);

	ls_crtc_write(lcrtc, LS_FB_STRI_DVO0_REG, (crtc->primary->state->fb->pitches[0] + 255) & ~255);

	pr_info("stride: %x",(crtc->primary->state->fb->pitches[0] + 255) & ~255);


    switch (crtc->primary->state->fb->format->format) {
		case DRM_FORMAT_RGB565:
			pr_info("16bit");
			lcrtc->cfg_reg |= 0x3;
			break;
		case DRM_FORMAT_RGB888:
		default:
			pr_info("24/32bit");
			lcrtc->cfg_reg |= 0x4;
			break;
    }
	spin_unlock_irq(&loongson_reglock);

	ls_config_pll(lcrtc->crtc_id, mode->clock);
}

static void loongson_crtc_atomic_enable(struct drm_crtc *crtc,
				       struct drm_crtc_state *old_state)
{
	struct loongson_crtc *lcrtc = to_loongson_crtc(crtc);

	lcrtc->cfg_reg |= CFG_ENABLE;

	spin_lock_irq(&loongson_reglock);
    ls_crtc_write(lcrtc, LS_FB_CFG_DVO0_REG, 0x0);
	mdelay(100);
    ls_crtc_write(lcrtc, LS_FB_CFG_DVO0_REG, lcrtc->cfg_reg);
	mdelay(10);
	spin_unlock_irq(&loongson_reglock);

	drm_crtc_vblank_on(crtc);
}

static void loongson_crtc_atomic_disable(struct drm_crtc *crtc,
					struct drm_crtc_state *old_state)
{
	struct loongson_crtc *lcrtc = to_loongson_crtc(crtc);

	lcrtc->cfg_reg &= ~CFG_ENABLE;
	spin_lock_irq(&loongson_reglock);
//    ls_crtc_write(lcrtc, LS_FB_CFG_DVO0_REG, lcrtc->cfg_reg);
    ls_crtc_write(lcrtc, LS_FB_CFG_DVO0_REG, 0x0);
	spin_unlock_irq(&loongson_reglock);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	drm_crtc_vblank_off(crtc);
}

static void loongson_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_crtc_state *old_crtc_state)
{
	struct loongson_crtc *lcrtc = to_loongson_crtc(crtc);
	struct drm_pending_vblank_event *event = crtc->state->event;

	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		if (drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, event);
		else
			drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}


static const struct drm_crtc_helper_funcs loongson_crtc_helper_funcs = {
	.mode_valid = loongson_crtc_mode_valid,
	.mode_set_nofb	= loongson_crtc_mode_set_nofb,
	.atomic_enable	= loongson_crtc_atomic_enable,
	.atomic_disable	= loongson_crtc_atomic_disable,
	.atomic_flush	= loongson_crtc_atomic_flush,
};

static void loongson_plane_atomic_update(struct drm_plane *plane,
					struct drm_plane_state *old_state)
{
	struct loongson_crtc *lcrtc;

	if (!plane->state->crtc || !plane->state->fb)
		return;

	lcrtc = to_loongson_crtc(plane->state->crtc);
	spin_lock_irq(&loongson_reglock);
	if(old_state->fb){
		if(old_state->fb->pitches[0] != plane->state->fb->pitches[0])
    		ls_crtc_write(lcrtc, LS_FB_STRI_DVO0_REG, plane->state->fb->pitches[0]);
	} else {
//		ls_crtc_write(lcrtc, LS_FB_STRI_DVO0_REG, plane->state->fb->pitches[0]);
	}
	if(((ls_crtc_read(lcrtc, LS_FB_CFG_DVO0_REG) & CFG_FBNUM)  >> CFG_FBNUM_BIT) == 1) {
    	ls_crtc_write(lcrtc, LS_FB_ADDR0_DVO0_REG, drm_fb_cma_get_gem_addr(plane->state->fb, plane->state, 0));
	} else {
    	ls_crtc_write(lcrtc, LS_FB_ADDR1_DVO0_REG, drm_fb_cma_get_gem_addr(plane->state->fb, plane->state, 0));
	}
	ls_crtc_write(lcrtc, LS_FB_CFG_DVO0_REG, lcrtc->cfg_reg | CFG_FBSWITCH);
	spin_unlock_irq(&loongson_reglock);
}

static const struct drm_plane_helper_funcs loongson_plane_helper_funcs = {
	.atomic_update = loongson_plane_atomic_update,
};

static void loongson_plane_destroy(struct drm_plane *plane)
{
	drm_plane_cleanup(plane);
}


static const struct drm_plane_funcs loongson_plane_funcs = {
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
	.destroy = loongson_plane_destroy,
	.disable_plane = drm_atomic_helper_disable_plane,
	.reset = drm_atomic_helper_plane_reset,
	.update_plane = drm_atomic_helper_update_plane,
};

void loongson_crtc_init(struct loongson_drm_device *ldev)
{
	struct loongson_crtc *ls_crtc;
	int i, ret;

	for(i=0;i<ldev->vbios->crtc_num;i++){
		ldev->lcrtc[i].ldev = ldev;
        ldev->lcrtc[i].crtc_id = i;
		ldev->lcrtc[i].cfg_reg = CFG_RESET;

		ls_crtc_write(&ldev->lcrtc[i], LS_FB_CFG_DVO0_REG, CFG_RESET);

		pr_info("init crtc");

        ldev->lcrtc[i].primary = devm_kzalloc(ldev->dev->dev, sizeof(*ldev->lcrtc[i].primary), GFP_KERNEL);
	    if (!ldev->lcrtc[i].primary)
		    return;

	    ret = drm_universal_plane_init(ldev->dev, ldev->lcrtc[i].primary, BIT(i), &loongson_plane_funcs,
				       loongson_primary_formats,
				       ARRAY_SIZE(loongson_primary_formats),
				       loongson_format_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
        if(ret)
            return;
        drm_plane_helper_add(ldev->lcrtc[i].primary, &loongson_plane_helper_funcs);

        ret = drm_crtc_init_with_planes(ldev->dev, &ldev->lcrtc[i].base,ldev->lcrtc[i].primary, NULL,
				&loongson_crtc_funcs, NULL);
        if (ret) {
		    loongson_plane_destroy(ldev->lcrtc[i].primary);
		return;
	    }
        drm_crtc_helper_add(&ldev->lcrtc[i].base, &loongson_crtc_helper_funcs);
    }
}
