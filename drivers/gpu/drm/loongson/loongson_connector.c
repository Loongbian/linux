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

#include <linux/export.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/pm_runtime.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
//#include <drm/drm_probe_helper.h>
#include <drm/drm_edid.h>

#include "loongson_drv.h"

#define DVO_I2C_NAME "loongson_dvo_i2c"
#define adapter_to_i2c_client(d) container_of(d, struct i2c_client, adapter)

static struct eep_info{
	struct i2c_adapter *adapter;
	unsigned short addr;
}eeprom_info[2];

static DEFINE_MUTEX(edid_lock);
struct i2c_client *loongson_drm_i2c_client[2];

/**
 * loongson_connector_best_encoder
 *
 * @connector: point to the drm_connector structure
 *
 * Select the best encoder for the given connector. Used by both the helpers in
 * drm_atomic_helper_check_modeset() and legacy CRTC helpers
 */
static struct drm_encoder *loongson_connector_best_encoder(struct drm_connector
						  *connector)
{
	int enc_id = drm_connector_index(connector);
	/* pick the encoder ids */
	if (enc_id)
		return drm_encoder_find(connector->dev, NULL, enc_id);
	return NULL;
}


/**
 * loongson_vga_get_modes
 *
 * @connetcor: central DRM connector control structure
 *
 * Fill in all modes currently valid for the sink into the connector->probed_modes list.
 * It should also update the EDID property by calling drm_connector_update_edid_property().
 */
static int loongson_vga_get_modes(struct drm_connector *connector)
{
	int ret;
	if(drm_connector_index(connector) == 0){
		ret = drm_add_modes_noedid(connector, 1024, 768);
		drm_set_preferred_mode(connector, 1024, 768);
	} else {
		ret = drm_add_modes_noedid(connector, 1024, 768);
		drm_set_preferred_mode(connector, 1024, 768);
	}
	return ret;
}

/**
 * loongson_i2c_create
 *
 * @dev: point to drm_device structure
 *
 * Create i2c adapter
 */
struct loongson_i2c_chan *loongson_i2c_create(struct drm_device *dev)
{
	int ret, data, clock;
	struct loongson_i2c_chan *i2c;
	struct loongson_drm_device *ldev = dev->dev_private;

	return i2c;
}

/**
 * loongson_i2c_destroy
 *
 * @i2c: point to loongson_i2c_chan
 *
 * Destroy i2c adapter
 */
void loongson_i2c_destroy(struct loongson_i2c_chan *i2c)
{
	if (!i2c)
		return;
	i2c_del_adapter(&i2c->adapter);
	kfree(i2c);
}

/**
 * loongson_vga_detect
 *
 * @connector: point to drm_connector
 * @force: bool
 *
 * Check to see if anything is attached to the connector.
 * The parameter force is set to false whilst polling,
 * true when checking the connector due to a user request
 */
static enum drm_connector_status loongson_vga_detect(struct drm_connector
						   *connector, bool force)
{
	if (drm_connector_index(connector) == 0)
		return connector_status_connected;
	return connector_status_disconnected;
}


/**
 * loongson_connector_destroy
 *
 * @connector: point to the drm_connector structure
 *
 * Clean up connector resources
 */
static void loongson_connector_destroy(struct drm_connector *connector)
{
	struct loongson_connector *loongson_connector = to_loongson_connector(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}


/**
 * These provide the minimum set of functions required to handle a connector
 *
 * Helper operations for connectors.These functions are used
 * by the atomic and legacy modeset helpers and by the probe helpers.
 */
static const struct drm_connector_helper_funcs loongson_vga_connector_helper_funcs = {
        .get_modes = loongson_vga_get_modes,
};

/**
 * These provide the minimum set of functions required to handle a connector
 *
 * Control connectors on a given device.
 * The functions below allow the core DRM code to control connectors,
 * enumerate available modes and so on.
 */
static const struct drm_connector_funcs loongson_vga_connector_funcs = {
//       .dpms = drm_helper_connector_dpms,
        .detect = loongson_vga_detect,
        .fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

/**
 * loongson_vga_init
 *
 * @dev: drm device
 * @connector_id:
 *
 * Vga is the interface between host and monitor
 * This function is to init vga
 */
struct drm_connector *loongson_vga_init(struct drm_device *dev, unsigned int connector_id)
{
	struct i2c_adapter *i2c_adap;
	struct i2c_board_info i2c_info;
	struct drm_connector *connector;
	struct loongson_connector *loongson_connector;
	struct loongson_drm_device *ldev = (struct loongson_drm_device*)dev->dev_private;

	loongson_connector = kzalloc(sizeof(struct loongson_connector), GFP_KERNEL);
	if (!loongson_connector)
		return NULL;

	pr_info("Connector INIT ID: %d\n", connector_id);

	connector = &loongson_connector->base;

	drm_connector_helper_add(connector, &loongson_vga_connector_helper_funcs);

	drm_connector_init(dev, connector,
			   &loongson_vga_connector_funcs, DRM_MODE_CONNECTOR_VGA);

	drm_connector_register(connector);

	return connector;
}
