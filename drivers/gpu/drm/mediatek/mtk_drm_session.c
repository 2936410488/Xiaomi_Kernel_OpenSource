/*
 * Copyright (c) 2015 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_gem.h>
#include <linux/dma-buf.h>
#include <drm/mediatek_drm.h>

#include "mtk_drm_drv.h"
#include "mtk_drm_session.h"

static DEFINE_MUTEX(disp_session_lock);

int mtk_drm_session_create(struct drm_device *dev,
			   struct drm_mtk_session *config)
{
	int ret = 0;
	int is_session_inited = 0;
	struct mtk_drm_private *private = dev->dev_private;
	unsigned int session =
		MAKE_MTK_SESSION(config->type, config->device_id);
	int i, idx = -1;

	/* 1.To check if this session exists already */
	mutex_lock(&disp_session_lock);
	for (i = 0; i < MAX_SESSION_COUNT; i++) {
		if (private->session_id[i] == session) {
			is_session_inited = 1;
			idx = i;
			DDPPR_ERR("[DRM] create session is exited:0x%x\n",
				  session);
			break;
		}
	}

	if (is_session_inited == 1) {
		config->session_id = session;
		goto done;
	}

	for (i = 0; i < MAX_SESSION_COUNT; i++) {
		if (private->session_id[i] == 0 && idx == -1) {
			idx = i;
			break;
		}
	}
	/* 1.To check if support this session (mode,type,dev) */
	/* 2. Create this session */
	if (idx != -1) {
		config->session_id = session;
	      private
		->session_id[idx] = session;
	      private
		->num_sessions = idx + 1;
		DDPINFO("[DRM] New session:0x%x, idx:%d\n", session, idx);
	} else {
		DDPPR_ERR("[DRM] Invalid session creation request\n");
		ret = -1;
	}
done:
	mutex_unlock(&disp_session_lock);

	DDPINFO("[DRM] new session done\n");
	return ret;
}

int mtk_session_get_mode(struct drm_device *dev, struct drm_crtc *crtc)
{
	int crtc_idx = drm_crtc_index(crtc);
	struct mtk_drm_private *private = dev->dev_private;
	int session_mode = private->session_mode;
	const struct mtk_session_mode_tb *mode_tb = private->data->mode_tb;

	if (!mode_tb[session_mode].en)
		return -EINVAL;
	return mode_tb[session_mode].ddp_mode[crtc_idx];
}

int mtk_session_set_mode(struct drm_device *dev, unsigned int session_mode)
{
	int i;
	struct mtk_drm_private *private = dev->dev_private;
	const struct mtk_session_mode_tb *mode_tb = private->data->mode_tb;

	if (session_mode >= MTK_DRM_SESSION_NUM || !mode_tb[session_mode].en) {
		DDPPR_ERR("%s Invalid session mode:%d en:%d\n", __func__,
			  session_mode, mode_tb[session_mode].en);
		return -EINVAL;
	}

	if (session_mode == private->session_mode)
		return 0;
	/* For releasing HW resource purpose, the ddp mode should
	 * switching reversely in some situation.
	 * CRTC2 -> CRTC1 ->CRTC0
	 */
	if (session_mode == MTK_DRM_SESSION_DC_MIRROR ||
	    private->session_mode == MTK_DRM_SESSION_TRIPLE_DL) {
		for (i = MAX_CRTC - 1; i >= 0; i--) {
			if (private->crtc[i])
				mtk_crtc_path_switch(
					private->crtc[i],
					mode_tb[session_mode].ddp_mode[i], 1);
		}
	} else {
		for (i = 0; i < MAX_CRTC; i++) {
			if (private->crtc[i])
				mtk_crtc_path_switch(
					private->crtc[i],
					mode_tb[session_mode].ddp_mode[i], 1);
		}
	}
	private->session_mode = session_mode;
	return 0;
}

int mtk_drm_session_destroy(struct drm_device *dev,
			    struct drm_mtk_session *config)
{
	int ret = -1;
	unsigned int session = config->session_id;
	struct mtk_drm_private *private = dev->dev_private;
	int i;

	DDPINFO("disp_destroy_session, 0x%x", config->session_id);

	/* 1.To check if this session exists already, and remove it */
	mutex_lock(&disp_session_lock);
	for (i = 0; i < MAX_SESSION_COUNT; i++) {
		if (private->session_id[i] == session) {
			private->session_id[i] = 0;
			ret = 0;
			break;
		}
	}

	mutex_unlock(&disp_session_lock);

	/* 2. Destroy this session */
	if (ret == 0)
		DDPINFO("Destroy session(0x%x)\n", session);
	else
		DDPPR_ERR("session(0x%x) does not exists\n", session);

	return ret;
}

int mtk_drm_session_create_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	int ret = 0;
	struct drm_mtk_session *config = data;

	if (mtk_drm_session_create(dev, config) != 0)
		ret = -EFAULT;

	return ret;
}

int mtk_drm_session_destroy_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv)
{
	int ret = 0;
	struct drm_mtk_session *config = data;

	if (mtk_drm_session_destroy(dev, config) != 0)
		ret = -EFAULT;

	return ret;
}
