/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 Lemote Inc.
 * Author : Jiaxun Yang <jisxun.yang@flygoat.com>
 */

#include <drm/drm_crtc_helper.h>
#include "loongson_drv.h"



static void loongson_encoder_destroy(struct drm_encoder *encoder)
{
	struct loongson_encoder *loongson_encoder = to_loongson_encoder(encoder);
	drm_encoder_cleanup(encoder);
	kfree(loongson_encoder);
}

static int loongson_encoder_atomic_check(struct drm_encoder *encoder,
				    struct drm_crtc_state *crtc_state,
				    struct drm_connector_state *conn_state)
{
	/* do nothing */
	return 0;
}

static void loongson_encoder_atomic_mode_set(struct drm_encoder *encoder,
				struct drm_crtc_state *crtc_state,
				struct drm_connector_state *conn_state)

{
	struct loongson_encoder *lenc = to_loongson_encoder(encoder);
	struct loongson_crtc *lcrtc_new = to_loongson_crtc(crtc_state->crtc);
	struct loongson_crtc *lcrtc_orig = lenc->lcrtc;

	if(lcrtc_orig->crtc_id == lcrtc_new->crtc_id) {
		pr_info("encoder: crtc %x, direct pipe\n", lcrtc_orig->crtc_id);
		/* Display from this CRTC */
		lcrtc_orig->cfg_reg &= ~CFG_PANELSWITCH;
		lcrtc_orig->cfg_reg |= CFG_RESET;
	} else {
		pr_info("encoder: crtc %x, clone mode\n", lcrtc_orig->crtc_id);
		/* Set up clone mode */
		lcrtc_orig->cfg_reg |= CFG_PANELSWITCH;
		lcrtc_orig->cfg_reg |= CFG_RESET;
	}
	ls_crtc_write(lcrtc_orig, LS_FB_CFG_DVO0_REG, lcrtc_orig->cfg_reg);
}


/**
 * These provide the minimum set of functions required to handle a encoder
 *
 * Helper operations for encoders
 */
static const struct drm_encoder_helper_funcs loongson_encoder_helper_funcs = {
	.atomic_check	= loongson_encoder_atomic_check,
	.atomic_mode_set = loongson_encoder_atomic_mode_set,
};

/**
 * These provide the minimum set of functions required to handle a encoder
 *
 * Encoder controls,encoder sit between CRTCs and connectors
 */
static const struct drm_encoder_funcs loongson_encoder_encoder_funcs = {
	.destroy = loongson_encoder_destroy,
};


/**
 * loongson_encoder_init
 *
 * @dev: point to the drm_device structure
 *
 * Init encoder
 */
struct drm_encoder *loongson_encoder_init(struct loongson_drm_device *ldev, struct drm_device *dev,unsigned int encoder_id)
{
	struct drm_encoder *encoder;
	struct loongson_encoder *loongson_encoder;

	loongson_encoder = kzalloc(sizeof(struct loongson_encoder), GFP_KERNEL);
	if (!loongson_encoder)
		return NULL;

	loongson_encoder->encoder_id = encoder_id;
	loongson_encoder->lcrtc = &ldev->lcrtc[encoder_id];
	encoder = &loongson_encoder->base;
	pr_info("init encoder %d",encoder_id);

	encoder->possible_crtcs = BIT(0) | BIT(1);
	encoder->possible_clones = BIT(0) | BIT(1);

	drm_encoder_helper_add(encoder, &loongson_encoder_helper_funcs);
	drm_encoder_init(dev, encoder, &loongson_encoder_encoder_funcs,
			 DRM_MODE_ENCODER_DAC, NULL);

	return encoder;
}
