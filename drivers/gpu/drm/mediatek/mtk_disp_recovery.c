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

#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/sched/clock.h>
#include <uapi/linux/sched/types.h>
#include <drm/drmP.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#include "mtk_drm_drv.h"
#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_crtc.h"
#include "mtk_drm_helper.h"
#include "mtk_drm_assert.h"
#include "mtk_drm_mmp.h"

#define ESD_TRY_CNT 5
#define ESD_CHECK_PERIOD 2000 /* ms */

static inline int _can_switch_check_mode(struct drm_crtc *crtc,
					 struct mtk_panel_ext *panel_ext)
{
	struct mtk_drm_private *priv = crtc->dev->dev_private;
	int ret = 0;

	if (panel_ext->params->cust_esd_check == 0 &&
	    panel_ext->params->lcm_esd_check_table[0].cmd != 0 &&
	    mtk_drm_helper_get_opt(priv->helper_opt,
				   MTK_DRM_OPT_ESD_CHECK_SWITCH))
		ret = 1;

	return ret;
}

static inline int _lcm_need_esd_check(struct mtk_panel_ext *panel_ext)
{
	int ret = 0;

	if (panel_ext->params->esd_check_enable == 1) {
		/* TODO: check islcmconnected == 1 */
		ret = 1;
	}

	return ret;
}

static inline int need_wait_esd_eof(struct drm_crtc *crtc,
				    struct mtk_panel_ext *panel_ext)
{
	int ret = 1;

	/*
	 * 1.vdo mode
	 * 2.cmd mode te
	 */
	if (mtk_crtc_is_frame_trigger_mode(crtc))
		ret = 0;

	if (panel_ext->params->cust_esd_check == 0)
		ret = 0;

	return ret;
}

int _mtk_esd_check_read(struct drm_crtc *crtc)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_ddp_comp *output_comp;
	struct mtk_panel_ext *panel_ext;
	struct cmdq_pkt *cmdq_handle;
	int ret = 0;

	DDPINFO("[ESD]ESD read panel\n");

	/* TODO: ESD read panel have trouble so far, would skip process until
	 * solved
	 */
	return 0;

	mutex_lock(&mtk_crtc->lock);

	output_comp = mtk_ddp_comp_request_output(mtk_crtc);
	if (unlikely(!output_comp)) {
		DDPPR_ERR("%s:invalid output comp\n", __func__);
		return -EINVAL;
	}

	mtk_ddp_comp_io_cmd(output_comp, NULL, REQ_PANEL_EXT, &panel_ext);
	if (unlikely(!(panel_ext && panel_ext->params))) {
		DDPPR_ERR("%s:can't find panel_ext handle\n", __func__);
		return -EINVAL;
	}

	mtk_crtc_pkt_create(&cmdq_handle, crtc);
	cmdq_pkt_set_client(cmdq_handle, mtk_crtc->gce_obj.client[CLIENT_CFG]);
	if (mtk_crtc_with_sub_path(crtc, mtk_crtc->ddp_mode))
		mtk_crtc_wait_frame_done(mtk_crtc, cmdq_handle,
					 DDP_SECOND_PATH);
	else
		mtk_crtc_wait_frame_done(mtk_crtc, cmdq_handle, DDP_FIRST_PATH);

	if (mtk_dsi_is_cmd_mode(output_comp)) {
		cmdq_pkt_clear_event(cmdq_handle,
				     mtk_crtc->gce_obj.event[EVENT_ESD_EOF]);

		mtk_ddp_comp_io_cmd(output_comp, cmdq_handle, ESD_CHECK_READ,
				    (void *)mtk_crtc->gce_obj.buf.pa_base +
					    DISP_SLOT_ESD_READ_BASE);

		cmdq_pkt_set_event(cmdq_handle,
				   mtk_crtc->gce_obj.event[EVENT_ESD_EOF]);
	} else { /* VDO mode */
		mtk_ddp_comp_io_cmd(output_comp, cmdq_handle, DSI_STOP_VDO_MODE,
				    NULL);
		mtk_ddp_comp_io_cmd(output_comp, cmdq_handle, ESD_CHECK_READ,
				    (void *)mtk_crtc->gce_obj.buf.pa_base +
					    DISP_SLOT_ESD_READ_BASE);

		mtk_ddp_comp_io_cmd(output_comp, cmdq_handle,
				    DSI_START_VDO_MODE, NULL);

		mtk_disp_mutex_trigger(mtk_crtc->mutex[0], cmdq_handle);
		mtk_ddp_comp_io_cmd(output_comp, cmdq_handle, COMP_REG_START,
				    NULL);
	}

	mutex_unlock(&mtk_crtc->lock);
	ret = cmdq_pkt_flush(mtk_crtc->gce_obj.client[CLIENT_CFG], cmdq_handle);

	cmdq_pkt_destroy(cmdq_handle);

	if (ret) {
		if (need_wait_esd_eof(crtc, panel_ext)) {
			/* TODO: set ESD_EOF event through CPU is better */
			mtk_crtc_pkt_create(&cmdq_handle, crtc);
			cmdq_pkt_set_client(
				cmdq_handle,
				mtk_crtc->gce_obj.client[CLIENT_CFG]);

			cmdq_pkt_set_event(
				cmdq_handle,
				mtk_crtc->gce_obj.event[EVENT_ESD_EOF]);
			cmdq_pkt_flush(mtk_crtc->gce_obj.client[CLIENT_CFG],
				       cmdq_handle);
			cmdq_pkt_destroy(cmdq_handle);
		}
		goto done;
	}

	ret = mtk_ddp_comp_io_cmd(output_comp, NULL, ESD_CHECK_CMP,
				  (void *)mtk_crtc->gce_obj.buf.va_base +
					  DISP_SLOT_ESD_READ_BASE);
done:
	return ret;
}

static irqreturn_t _esd_check_ext_te_irq_handler(int irq, void *data)
{
	struct mtk_drm_esd_ctx *esd_ctx = (struct mtk_drm_esd_ctx *)data;

	atomic_set(&esd_ctx->ext_te_event, 1);
	wake_up_interruptible(&esd_ctx->ext_te_wq);

	return IRQ_HANDLED;
}

static int _mtk_esd_check_eint(struct drm_crtc *crtc)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_drm_esd_ctx *esd_ctx = mtk_crtc->esd_ctx;
	int ret = 1;

	DDPINFO("[ESD]ESD check eint\n");

	if (unlikely(!esd_ctx)) {
		DDPPR_ERR("%s:invalid ESD context\n", __func__);
		return -EINVAL;
	}

	enable_irq(esd_ctx->eint_irq);
	if (wait_event_interruptible_timeout(
		    esd_ctx->ext_te_wq, atomic_read(&esd_ctx->ext_te_event),
		    HZ / 2) > 0)
		ret = 0;

	disable_irq(esd_ctx->eint_irq);
	atomic_set(&esd_ctx->ext_te_event, 0);

	return ret;
}

static int mtk_drm_request_eint(struct drm_crtc *crtc)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_drm_esd_ctx *esd_ctx = mtk_crtc->esd_ctx;
	struct mtk_ddp_comp *output_comp;
	struct device_node *node;
	u32 ints[2] = {0, 0};
	char *compat_str;
	int ret = 0;

	if (unlikely(!esd_ctx)) {
		DDPPR_ERR("%s:invalid ESD context\n", __func__);
		return -EINVAL;
	}

	output_comp = mtk_ddp_comp_request_output(mtk_crtc);

	if (unlikely(!output_comp)) {
		DDPPR_ERR("%s:invalid output comp\n", __func__);
		return -EINVAL;
	}

	mtk_ddp_comp_io_cmd(output_comp, NULL, REQ_ESD_EINT_COMPAT,
			    &compat_str);
	if (unlikely(!compat_str)) {
		DDPPR_ERR("%s: invalid compat string\n", __func__);
		return -EINVAL;
	}
	node = of_find_compatible_node(NULL, NULL, compat_str);
	if (unlikely(!node)) {
		DDPPR_ERR("can't find ESD TE eint compatible node\n");
		return -EINVAL;
	}

	of_property_read_u32_array(node, "debounce", ints, ARRAY_SIZE(ints));
	esd_ctx->eint_irq = irq_of_parse_and_map(node, 0);

	ret = request_irq(esd_ctx->eint_irq, _esd_check_ext_te_irq_handler,
			  IRQF_TRIGGER_RISING, "ESD_TE-eint", esd_ctx);
	if (ret) {
		DDPPR_ERR("eint irq line not available!\n");
		return ret;
	}

	disable_irq(esd_ctx->eint_irq);

	/* TODO: change DSI_TE GPIO to TE MODE */

	return ret;
}

static int mtk_drm_esd_check(struct drm_crtc *crtc)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_panel_ext *panel_ext;
	struct mtk_drm_esd_ctx *esd_ctx = mtk_crtc->esd_ctx;
	int ret = 0;

	CRTC_MMP_EVENT_START(drm_crtc_index(crtc), esd_check, 0, 0);
	mutex_lock(&mtk_crtc->lock);

	if (mtk_crtc->enabled == 0) {
		CRTC_MMP_MARK(drm_crtc_index(crtc), esd_check, 0, 99);
		DDPINFO("[ESD] CRTC %d disable. skip esd check\n",
			drm_crtc_index(crtc));
		mutex_unlock(&mtk_crtc->lock);
		goto done;
	}
	mutex_unlock(&mtk_crtc->lock);

	panel_ext = mtk_crtc->panel_ext;
	if (unlikely(!(panel_ext && panel_ext->params))) {
		DDPPR_ERR("can't find panel_ext handle\n");
		ret = -EINVAL;
		goto done;
	}

	/* Check panel EINT */
	if (panel_ext->params->cust_esd_check == 0 &&
	    esd_ctx->chk_mode == READ_EINT) {
		CRTC_MMP_MARK(drm_crtc_index(crtc), esd_check, 1, 0);
		ret = _mtk_esd_check_eint(crtc);
	} else { /* READ LCM CMD  */
		CRTC_MMP_MARK(drm_crtc_index(crtc), esd_check, 2, 0);
		ret = _mtk_esd_check_read(crtc);
	}

	/* switch ESD check mode */
	if (_can_switch_check_mode(crtc, panel_ext) &&
	    !mtk_crtc_is_frame_trigger_mode(crtc))
		esd_ctx->chk_mode =
			(esd_ctx->chk_mode == READ_EINT) ? READ_LCM : READ_EINT;

done:
	CRTC_MMP_EVENT_END(drm_crtc_index(crtc), esd_check, 0, ret);
	return ret;
}

static int mtk_drm_esd_recover(struct drm_crtc *crtc)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_ddp_comp *output_comp;
	int ret = 0;

	CRTC_MMP_EVENT_START(drm_crtc_index(crtc), esd_recovery, 0, 0);
	mutex_lock(&mtk_crtc->lock);
	CRTC_MMP_MARK(drm_crtc_index(crtc), esd_recovery, 0, 1);
	output_comp = mtk_ddp_comp_request_output(mtk_crtc);

	if (unlikely(!output_comp)) {
		DDPPR_ERR("%s: invalid output comp\n", __func__);
		mutex_unlock(&mtk_crtc->lock);
		ret = -EINVAL;
		goto done;
	}

	mtk_drm_idlemgr_kick(__func__, crtc, 0);

	mtk_ddp_comp_io_cmd(output_comp, NULL, CONNECTOR_PANEL_DISABLE, NULL);

	mtk_drm_crtc_disable(crtc);
	CRTC_MMP_MARK(drm_crtc_index(crtc), esd_recovery, 0, 2);

	mtk_drm_crtc_enable(crtc);
	CRTC_MMP_MARK(drm_crtc_index(crtc), esd_recovery, 0, 3);

	mtk_ddp_comp_io_cmd(output_comp, NULL, CONNECTOR_PANEL_ENABLE, NULL);

	CRTC_MMP_MARK(drm_crtc_index(crtc), esd_recovery, 0, 4);

	mutex_unlock(&mtk_crtc->lock);
done:
	CRTC_MMP_EVENT_END(drm_crtc_index(crtc), esd_recovery, 0, ret);

	return 0;
}

static int mtk_drm_esd_check_worker_kthread(void *data)
{
	struct sched_param param = {.sched_priority = 87};
	struct drm_crtc *crtc = (struct drm_crtc *)data;
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_drm_esd_ctx *esd_ctx = mtk_crtc->esd_ctx;
	int ret = 0;
	int i = 0;
	int recovery_flg = 0;

	sched_setscheduler(current, SCHED_RR, &param);

	if (!crtc) {
		DDPPR_ERR("%s invalid CRTC context, stop thread\n", __func__);

		return -EINVAL;
	}

	while (1) {
		msleep(ESD_CHECK_PERIOD);
		ret = wait_event_interruptible(
			esd_ctx->check_task_wq,
			atomic_read(&esd_ctx->check_wakeup));
		if (ret < 0) {
			DDPINFO("[ESD]check thread waked up accidently\n");
			continue;
		}

		/* 1. esd check & recovery */
		if (!esd_ctx->chk_active)
			continue;

		i = 0; /* repeat */
		do {
			ret = mtk_drm_esd_check(crtc);

			if (!ret) /* success */
				break;

			DDPPR_ERR(
				"[ESD]esd check fail, will do esd recovery. try=%d\n",
				i);
			mtk_drm_esd_recover(crtc);
			recovery_flg = 1;
		} while (++i < ESD_TRY_CNT);

		if (ret != 0) {
			DDPPR_ERR(
				"[ESD]after esd recovery %d times, still fail, disable esd check\n",
				ESD_TRY_CNT);
			mtk_disp_esd_check_switch(crtc, false);
			break;
		} else if (recovery_flg) {
			DDPINFO("[ESD] esd recovery success\n");
			recovery_flg = 0;
		}

		/* 2. other check & recovery */
		if (kthread_should_stop())
			break;
	}
	return 0;
}

void mtk_disp_esd_check_switch(struct drm_crtc *crtc, bool enable)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_drm_esd_ctx *esd_ctx = mtk_crtc->esd_ctx;

	if (unlikely(!esd_ctx)) {
		DDPINFO("%s:invalid ESD context, crtc id:%d\n",
			__func__, drm_crtc_index(crtc));
		return;
	}
	esd_ctx->chk_active = enable;
	atomic_set(&esd_ctx->check_wakeup, enable);
	if (enable)
		wake_up_interruptible(&esd_ctx->check_task_wq);
}

static void mtk_disp_esd_chk_deinit(struct drm_crtc *crtc)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_drm_esd_ctx *esd_ctx = mtk_crtc->esd_ctx;

	if (unlikely(!esd_ctx)) {
		DDPPR_ERR("%s:invalid ESD context\n", __func__);
		return;
	}

	/* Stop ESD task */
	mtk_disp_esd_check_switch(crtc, false);

	/* Stop ESD kthread */
	kthread_stop(esd_ctx->disp_esd_chk_task);

	kfree(esd_ctx);
}

static void mtk_disp_esd_chk_init(struct drm_crtc *crtc)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_panel_ext *panel_ext;
	struct mtk_drm_esd_ctx *esd_ctx;

	panel_ext = mtk_crtc->panel_ext;
	if (!(panel_ext && panel_ext->params)) {
		DDPMSG("can't find panel_ext handle\n");
		return;
	}

	if (_lcm_need_esd_check(panel_ext) == 0)
		return;

	DDPINFO("create ESD thread\n");
	/* primary display check thread init */
	esd_ctx = kzalloc(sizeof(*esd_ctx), GFP_KERNEL);
	if (!esd_ctx) {
		DDPPR_ERR("allocate ESD context failed!\n");
		return;
	}
	mtk_crtc->esd_ctx = esd_ctx;

	esd_ctx->disp_esd_chk_task = kthread_create(
		mtk_drm_esd_check_worker_kthread, crtc, "disp_echk");

	init_waitqueue_head(&esd_ctx->check_task_wq);
	init_waitqueue_head(&esd_ctx->ext_te_wq);
	atomic_set(&esd_ctx->check_wakeup, 0);
	atomic_set(&esd_ctx->ext_te_event, 0);
	esd_ctx->chk_mode = READ_EINT;
	mtk_drm_request_eint(crtc);

	wake_up_process(esd_ctx->disp_esd_chk_task);
}

void mtk_disp_chk_recover_deinit(struct drm_crtc *crtc)
{
	struct mtk_drm_private *priv = crtc->dev->dev_private;
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);

	/* TODO : check function work in other CRTC & other connector */
	if (mtk_drm_helper_get_opt(priv->helper_opt,
				   MTK_DRM_OPT_ESD_CHECK_RECOVERY) &&
	    drm_crtc_index(&mtk_crtc->base) == 0)
		mtk_disp_esd_chk_deinit(crtc);
}

void mtk_disp_chk_recover_init(struct drm_crtc *crtc)
{
	struct mtk_drm_private *priv = crtc->dev->dev_private;
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);

	/* TODO : check function work in other CRTC & other connector */
	if (mtk_drm_helper_get_opt(priv->helper_opt,
				   MTK_DRM_OPT_ESD_CHECK_RECOVERY) &&
	    drm_crtc_index(&mtk_crtc->base) == 0)
		mtk_disp_esd_chk_init(crtc);
}
