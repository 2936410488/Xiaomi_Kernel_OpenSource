/*
 * Copyright (C) 2019 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "mtk_layering_rule.h"
#include "mtk_drm_crtc.h"
#include "mtk_disp_pmqos.h"
#include "mtk_drm_mmp.h"
#include "mtk_drm_drv.h"

static struct drm_crtc *dev_crtc;

int __mtk_disp_pmqos_slot_look_up(int comp_id, int mode)
{
	switch (comp_id) {
	case DDP_COMPONENT_OVL0:
		if (mode == DISP_BW_FBDC_MODE)
			return DISP_PMQOS_OVL0_FBDC_BW;
		else
			return DISP_PMQOS_OVL0_BW;
	case DDP_COMPONENT_OVL0_2L:
		if (mode == DISP_BW_FBDC_MODE)
			return DISP_PMQOS_OVL0_2L_FBDC_BW;
		else
			return DISP_PMQOS_OVL0_2L_BW;
	case DDP_COMPONENT_OVL1_2L:
		if (mode == DISP_BW_FBDC_MODE)
			return DISP_PMQOS_OVL1_2L_FBDC_BW;
		else
			return DISP_PMQOS_OVL1_2L_BW;
	case DDP_COMPONENT_RDMA0:
		return DISP_PMQOS_RDMA0_BW;
	case DDP_COMPONENT_RDMA1:
		return DISP_PMQOS_RDMA1_BW;
	case DDP_COMPONENT_RDMA2:
		return DISP_PMQOS_RDMA2_BW;
	case DDP_COMPONENT_WDMA0:
		return DISP_PMQOS_WDMA0_BW;
	case DDP_COMPONENT_WDMA1:
		return DISP_PMQOS_WDMA1_BW;
	default:
		DDPPR_ERR("%s, unknown comp %d\n", __func__, comp_id);
		break;
	}

	return -EINVAL;
}

int __mtk_disp_pmqos_port_look_up(int comp_id)
{
	switch (comp_id) {
	case DDP_COMPONENT_OVL0:
		return SMI_PORT_DISP_OVL0;
	case DDP_COMPONENT_OVL0_2L:
		return SMI_PORT_DISP_OVL0_2L;
	case DDP_COMPONENT_OVL1_2L:
		return SMI_PORT_DISP_OVL1_2L;
	case DDP_COMPONENT_RDMA0:
		return SMI_PORT_DISP_RDMA0;
	case DDP_COMPONENT_RDMA1:
		return SMI_PORT_DISP_RDMA1;
	case DDP_COMPONENT_RDMA2:
		/* 6779 does not exist RDMA2 */
		return SMI_PORT_DISP_RDMA1;
	case DDP_COMPONENT_WDMA0:
		return SMI_PORT_DISP_WDMA0;
	case DDP_COMPONENT_WDMA1:
		/* 6779 does not exist WDMA1 */
		return SMI_PORT_DISP_WDMA0;
	default:
		DDPPR_ERR("%s, unknown comp %d\n", __func__, comp_id);
		break;
	}

	return -EINVAL;
}

int __mtk_disp_set_module_bw(struct mm_qos_request *request, int comp_id,
			     unsigned int bandwidth, int bw_mode)
{
	DDPINFO("set module %d, bw %u\n", comp_id, bandwidth);
	mm_qos_set_bw_request(request, bandwidth, BW_COMP_NONE);

	DRM_MMP_MARK(pmqos, comp_id, bandwidth);

	return 0;
}

void __mtk_disp_set_module_hrt(struct mm_qos_request *request,
			       unsigned int bandwidth)
{
	mm_qos_set_hrt_request(request, bandwidth);
}

int mtk_disp_set_hrt_bw(struct mtk_drm_crtc *mtk_crtc, unsigned int overlap_num)
{
	struct drm_crtc *crtc = &mtk_crtc->base;
	struct mtk_drm_private *priv = crtc->dev->dev_private;
	struct mtk_ddp_comp *comp;
	unsigned long long bw_base;
	unsigned int tmp;
	int i, j, ret = 0;

	DRM_MMP_MARK(hrt_bw, 0, overlap_num);

	bw_base = _layering_get_frame_bw(crtc->mode.hdisplay,
					 crtc->mode.vdisplay);
	bw_base /= 2;

	tmp = bw_base * overlap_num;

	for (i = 0; i < DDP_PATH_NR; i++) {
		if (!(mtk_crtc->ddp_ctx[mtk_crtc->ddp_mode].req_hrt[i]))
			continue;
		for_each_comp_in_crtc_target_path(comp, mtk_crtc, j, i) {
			ret |= mtk_ddp_comp_io_cmd(comp, NULL, PMQOS_SET_HRT_BW,
						   &tmp);
		}
	}

	if (ret == RDMA_REQ_HRT)
		tmp = bw_base * 2;

	mm_qos_set_hrt_request(&priv->hrt_bw_request, tmp);
	DDPINFO("set HRT bw %u\n", tmp);
	mm_qos_update_all_request(&priv->hrt_request_list);

	return ret;
}

int mtk_disp_hrt_cond_change_cb(struct notifier_block *nb, unsigned long value,
				void *v)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(dev_crtc);
	int i, ret;
	unsigned int hrt_idx;

	mutex_lock(&mtk_crtc->lock);

	switch (value) {
	case BW_THROTTLE_START: /* CAM on */
		DDPMSG("DISP BW Throttle start\n");
		/* TODO: concider memory session */
		DDPINFO("CAM trigger repaint\n");
		hrt_idx = _layering_rule_get_hrt_idx();
		hrt_idx++;
		mutex_unlock(&mtk_crtc->lock);
		drm_trigger_repaint(DRM_REPAINT_FOR_IDLE, dev_crtc->dev);
		for (i = 0; i < 5; ++i) {
			ret = wait_event_timeout(
				mtk_crtc->qos_ctx->hrt_cond_wq,
				atomic_read(&mtk_crtc->qos_ctx->hrt_cond_sig),
				HZ / 5);
			if (ret == 0)
				DDPINFO("wait repaint timeout %d\n", i);
			atomic_set(&mtk_crtc->qos_ctx->hrt_cond_sig, 0);
			if (atomic_read(&mtk_crtc->qos_ctx->last_hrt_idx) >=
			    hrt_idx)
				break;
		}
		mutex_lock(&mtk_crtc->lock);
		break;
	case BW_THROTTLE_END: /* CAM off */
		DDPMSG("DISP BW Throttle end\n");
		/* TODO: switch DC */
		break;
	default:
		break;
	}

	mutex_unlock(&mtk_crtc->lock);

	return 0;
}

struct notifier_block pmqos_hrt_notifier = {
	.notifier_call = mtk_disp_hrt_cond_change_cb,
};

int mtk_disp_hrt_bw_dbg(void)
{
	mtk_disp_hrt_cond_change_cb(NULL, BW_THROTTLE_START, NULL);

	return 0;
}

int mtk_disp_hrt_cond_init(struct drm_crtc *crtc)
{
	struct mtk_drm_crtc *mtk_crtc;

	dev_crtc = crtc;
	mtk_crtc = to_mtk_crtc(dev_crtc);

	mtk_crtc->qos_ctx = vmalloc(sizeof(struct mtk_drm_qos_ctx));
	if (mtk_crtc->qos_ctx == NULL) {
		DDPPR_ERR("%s:allocate qos_ctx failed\n", __func__);
		return -ENOMEM;
	}

	return 0;
}
