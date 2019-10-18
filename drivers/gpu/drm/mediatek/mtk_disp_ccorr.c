/*
 * Copyright (c) 2019 MediaTek Inc.
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
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#include "mtk_drm_crtc.h"
#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_drv.h"
#include "mtk_disp_ccorr.h"
#include "mtk_log.h"
#include "mtk_dump.h"

#define DISP_REG_CCORR_EN (0x000)
#define DISP_REG_CCORR_INTEN                     (0x008)
#define DISP_REG_CCORR_INTSTA                    (0x00C)
#define DISP_REG_CCORR_CFG (0x020)
#define DISP_REG_CCORR_SIZE (0x030)

#define CCORR_12BIT_MASK				0x0fff

#define CCORR_REG(idx) (idx * 4 + 0x80)
#define CCORR_CLIP(val, min, max) ((val >= max) ? \
	max : ((val <= min) ? min : val))

static unsigned int g_ccorr_relay_value[DISP_CCORR_TOTAL];
#define index_of_ccorr(module) ((module == DDP_COMPONENT_CCORR0) ? 0 : 1)

static atomic_t g_ccorr_is_clock_on[DISP_CCORR_TOTAL] = {
	ATOMIC_INIT(0), ATOMIC_INIT(0) };

static struct DISP_CCORR_COEF_T *g_disp_ccorr_coef[DISP_CCORR_TOTAL] = { NULL };
static int g_ccorr_color_matrix[3][3] = {
	{1024, 0, 0},
	{0, 1024, 0},
	{0, 0, 1024} };
static int g_ccorr_prev_matrix[3][3] = {
	{1024, 0, 0},
	{0, 1024, 0},
	{0, 0, 1024} };
static int g_rgb_matrix[3][3] = {
	{1024, 0, 0},
	{0, 1024, 0},
	{0, 0, 1024} };
static struct DISP_CCORR_COEF_T g_multiply_matrix_coef;
static int g_disp_ccorr_without_gamma;

static DECLARE_WAIT_QUEUE_HEAD(g_ccorr_get_irq_wq);
static DEFINE_SPINLOCK(g_ccorr_get_irq_lock);
static atomic_t g_ccorr_get_irq = ATOMIC_INIT(0);

/* FOR TRANSITION */
static DEFINE_SPINLOCK(g_pq_bl_change_lock);
static int g_pq_backlight;
static int g_pq_backlight_db;
static atomic_t g_ccorr_is_init_valid = ATOMIC_INIT(0);

static DEFINE_MUTEX(g_ccorr_global_lock);

/* TODO */
/* static ddp_module_notify g_ccorr_ddp_notify; */

// It's a work around for no comp assigned in functions.
struct mtk_ddp_comp *default_comp;

static int disp_ccorr_write_coef_reg(struct mtk_ddp_comp *comp,
	struct cmdq_pkt *handle, int lock);
/* static void ccorr_dump_reg(void); */

struct mtk_disp_ccorr {
	struct mtk_ddp_comp ddp_comp;
	struct drm_crtc *crtc;
};

static void disp_ccorr_multiply_3x3(unsigned int ccorrCoef[3][3],
	int color_matrix[3][3], unsigned int resultCoef[3][3])
{
	int temp_Result;
	int signedCcorrCoef[3][3];
	int i, j;

	/* convert unsigned 12 bit ccorr coefficient to signed 12 bit format */
	for (i = 0; i < 3; i += 1) {
		for (j = 0; j < 3; j += 1) {
			if (ccorrCoef[i][j] > 2047) {
				signedCcorrCoef[i][j] =
					(int)ccorrCoef[i][j] - 4096;
			} else {
				signedCcorrCoef[i][j] =
					(int)ccorrCoef[i][j];
			}
		}
	}

	for (i = 0; i < 3; i += 1) {
		DDPINFO("signedCcorrCoef[%d][0-2] = {%d, %d, %d}\n", i,
			signedCcorrCoef[i][0],
			signedCcorrCoef[i][1],
			signedCcorrCoef[i][2]);
	}

	temp_Result = (int)((signedCcorrCoef[0][0]*color_matrix[0][0] +
		signedCcorrCoef[0][1]*color_matrix[1][0] +
		signedCcorrCoef[0][2]*color_matrix[2][0]) / 1024);
	resultCoef[0][0] = CCORR_CLIP(temp_Result, -2048, 2047) & 0xFFF;

	temp_Result = (int)((signedCcorrCoef[0][0]*color_matrix[0][1] +
		signedCcorrCoef[0][1]*color_matrix[1][1] +
		signedCcorrCoef[0][2]*color_matrix[2][1]) / 1024);
	resultCoef[0][1] = CCORR_CLIP(temp_Result, -2048, 2047) & 0xFFF;

	temp_Result = (int)((signedCcorrCoef[0][0]*color_matrix[0][2] +
		signedCcorrCoef[0][1]*color_matrix[1][2] +
		signedCcorrCoef[0][2]*color_matrix[2][2]) / 1024);
	resultCoef[0][2] = CCORR_CLIP(temp_Result, -2048, 2047) & 0xFFF;

	temp_Result = (int)((signedCcorrCoef[1][0]*color_matrix[0][0] +
		signedCcorrCoef[1][1]*color_matrix[1][0] +
		signedCcorrCoef[1][2]*color_matrix[2][0]) / 1024);
	resultCoef[1][0] = CCORR_CLIP(temp_Result, -2048, 2047) & 0xFFF;

	temp_Result = (int)((signedCcorrCoef[1][0]*color_matrix[0][1] +
		signedCcorrCoef[1][1]*color_matrix[1][1] +
		signedCcorrCoef[1][2]*color_matrix[2][1]) / 1024);
	resultCoef[1][1] = CCORR_CLIP(temp_Result, -2048, 2047) & 0xFFF;

	temp_Result = (int)((signedCcorrCoef[1][0]*color_matrix[0][2] +
		signedCcorrCoef[1][1]*color_matrix[1][2] +
		signedCcorrCoef[1][2]*color_matrix[2][2]) / 1024);
	resultCoef[1][2] = CCORR_CLIP(temp_Result, -2048, 2047) & 0xFFF;

	temp_Result = (int)((signedCcorrCoef[2][0]*color_matrix[0][0] +
		signedCcorrCoef[2][1]*color_matrix[1][0] +
		signedCcorrCoef[2][2]*color_matrix[2][0]) / 1024);
	resultCoef[2][0] = CCORR_CLIP(temp_Result, -2048, 2047) & 0xFFF;

	temp_Result = (int)((signedCcorrCoef[2][0]*color_matrix[0][1] +
		signedCcorrCoef[2][1]*color_matrix[1][1] +
		signedCcorrCoef[2][2]*color_matrix[2][1]) / 1024);
	resultCoef[2][1] = CCORR_CLIP(temp_Result, -2048, 2047) & 0xFFF;

	temp_Result = (int)((signedCcorrCoef[2][0]*color_matrix[0][2] +
		signedCcorrCoef[2][1]*color_matrix[1][2] +
		signedCcorrCoef[2][2]*color_matrix[2][2]) / 1024);
	resultCoef[2][2] = CCORR_CLIP(temp_Result, -2048, 2047) & 0xFFF;

	for (i = 0; i < 3; i += 1) {
		DDPINFO("resultCoef[%d][0-2] = {0x%x, 0x%x, 0x%x}\n", i,
			resultCoef[i][0],
			resultCoef[i][1],
			resultCoef[i][2]);
	}
}

static int disp_ccorr_write_coef_reg(struct mtk_ddp_comp *comp,
	struct cmdq_pkt *handle, int lock)
{
	struct DISP_CCORR_COEF_T *ccorr, *multiply_matrix;
	int ret = 0;
	int id = index_of_ccorr(comp->id);
	unsigned int temp_matrix[3][3];

	if (lock)
		mutex_lock(&g_ccorr_global_lock);

	ccorr = g_disp_ccorr_coef[id];
	if (ccorr == NULL) {
		DDPINFO("%s: [%d] is not initialized\n", __func__, id);
		ret = -EFAULT;
		goto ccorr_write_coef_unlock;
	}

	if (id == 0) {
		multiply_matrix = &g_multiply_matrix_coef;
		disp_ccorr_multiply_3x3(ccorr->coef, g_ccorr_color_matrix,
			temp_matrix);
		disp_ccorr_multiply_3x3(temp_matrix, g_rgb_matrix,
			multiply_matrix->coef);
		ccorr = multiply_matrix;
	}

	if (handle == NULL) {
		/* use CPU to write */
		writel(((ccorr->coef[0][0] & CCORR_12BIT_MASK) << 16) |
			(ccorr->coef[0][1] & CCORR_12BIT_MASK),
			comp->regs + CCORR_REG(0));
		writel(((ccorr->coef[0][2] & CCORR_12BIT_MASK) << 16) |
			(ccorr->coef[1][0] & CCORR_12BIT_MASK),
			comp->regs + CCORR_REG(1));
		writel(((ccorr->coef[1][1] & CCORR_12BIT_MASK) << 16) |
			(ccorr->coef[1][2] & CCORR_12BIT_MASK),
			comp->regs + CCORR_REG(2));
		writel(((ccorr->coef[2][0] & CCORR_12BIT_MASK) << 16) |
			(ccorr->coef[2][1] & CCORR_12BIT_MASK),
			comp->regs + CCORR_REG(3));
		writel(((ccorr->coef[2][2] & CCORR_12BIT_MASK) << 16),
			comp->regs + CCORR_REG(4));
	} else {
		/* use CMDQ to write */
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + CCORR_REG(0),
			(ccorr->coef[0][0] & CCORR_12BIT_MASK << 16) |
			(ccorr->coef[0][1] & CCORR_12BIT_MASK), ~0);
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + CCORR_REG(1),
			(ccorr->coef[0][2] & CCORR_12BIT_MASK << 16) |
			(ccorr->coef[1][0] & CCORR_12BIT_MASK), ~0);
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + CCORR_REG(2),
			(ccorr->coef[1][1] & CCORR_12BIT_MASK << 16) |
			(ccorr->coef[1][2] & CCORR_12BIT_MASK), ~0);
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + CCORR_REG(3),
			(ccorr->coef[2][0] & CCORR_12BIT_MASK << 16) |
			(ccorr->coef[2][1] & CCORR_12BIT_MASK), ~0);
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + CCORR_REG(4),
			(ccorr->coef[2][2] & CCORR_12BIT_MASK << 16), ~0);
	}

	DDPINFO("%s: finish\n", __func__);
ccorr_write_coef_unlock:
	if (lock)
		mutex_unlock(&g_ccorr_global_lock);

	return ret;
}

void disp_ccorr_on_end_of_frame(struct mtk_ddp_comp *comp)
{
	unsigned int intsta;
	unsigned long flags;

	intsta = readl(comp->regs + DISP_REG_CCORR_INTSTA);

	DDPINFO("%s: intsta: 0x%x", __func__, intsta);

	if (intsta & 0x2) {	/* End of frame */
		if (spin_trylock_irqsave(&g_ccorr_get_irq_lock, flags)) {
			writel(intsta & ~0x3, comp->regs
				+ DISP_REG_CCORR_INTSTA);

			atomic_set(&g_ccorr_get_irq, 1);

			spin_unlock_irqrestore(&g_ccorr_get_irq_lock, flags);

			wake_up_interruptible(&g_ccorr_get_irq_wq);
		}
	}
}

static void disp_ccorr_set_interrupt(struct mtk_ddp_comp *comp,
					int enabled)
{
	int index = index_of_ccorr(comp->id);

	if (default_comp == NULL)
		default_comp = comp;

	if (atomic_read(&g_ccorr_is_clock_on[index]) != 1) {
		DDPINFO("%s: clock is off\n", __func__);
		return;
	}

	if (enabled) {
		if (readl(comp->regs + DISP_REG_CCORR_EN) == 0) {
			/* Print error message */
			DDPINFO("[WARNING] DISP_REG_CCORR_EN not enabled!\n");
		}
		/* Enable output frame end interrupt */
		writel(0x2, comp->regs + DISP_REG_CCORR_INTEN);
		DDPINFO("%s: Interrupt enabled\n", __func__);
	} else {
		/* Disable output frame end interrupt */
		writel(0x0, comp->regs + DISP_REG_CCORR_INTEN);
		DDPINFO("%s: Interrupt disabled\n", __func__);
	}
}

static void disp_ccorr_clear_irq_only(struct mtk_ddp_comp *comp)
{
	unsigned int intsta;
	unsigned long flags;

	intsta = readl(comp->regs + DISP_REG_CCORR_INTSTA);

	DDPINFO("%s: intsta: 0x%x\n", __func__, intsta);

	if (intsta & 0x2) { /* End of frame */
		if (spin_trylock_irqsave(&g_ccorr_get_irq_lock, flags)) {

			writel(intsta & ~0x3, comp->regs
				+ DISP_REG_CCORR_INTSTA);

			spin_unlock_irqrestore(&g_ccorr_get_irq_lock, flags);
		}
	}

	/* disable interrupt */
	disp_ccorr_set_interrupt(comp, 0);
}

static irqreturn_t mtk_disp_ccorr_irq_handler(int irq, void *dev_id)
{
	struct mtk_disp_ccorr *priv = dev_id;
	struct mtk_ddp_comp *ccorr = &priv->ddp_comp;
	unsigned long flags;
	u32 status;

	status = readl(ccorr->regs + DISP_REG_CCORR_INTSTA);
	if (status & 0x2) {	/* End of frame */
		if (spin_trylock_irqsave(&g_ccorr_get_irq_lock, flags)) {
			writel((status & ~0x3), ccorr->regs
				+ DISP_REG_CCORR_INTSTA);
			atomic_set(&g_ccorr_get_irq, 1);

			spin_unlock_irqrestore(&g_ccorr_get_irq_lock, flags);

			wake_up_interruptible(&g_ccorr_get_irq_wq);
		}
	}

	return IRQ_HANDLED;
}

static int disp_ccorr_wait_irq(unsigned long timeout)
{
	unsigned long flags;
	int ret = 0;

	if (atomic_read(&g_ccorr_get_irq) == 0) {
		ret = wait_event_interruptible(g_ccorr_get_irq_wq,
			atomic_read(&g_ccorr_get_irq) == 1);
		DDPINFO("%s: get_irq = 1, waken up", __func__);
		DDPINFO("%s: get_irq = 1, ret = %d", __func__, ret);
	} else {
		/* If g_ccorr_get_irq is already set, */
		/* means PQService was delayed */
		DDPINFO("%s: get_irq = 0", __func__);
	}

	spin_lock_irqsave(&g_ccorr_get_irq_lock, flags);
	atomic_set(&g_ccorr_get_irq, 0);
	spin_unlock_irqrestore(&g_ccorr_get_irq_lock, flags);

	return ret;
}

static int disp_pq_copy_backlight_to_user(int *backlight)
{
	unsigned long flags;
	int ret = 0;

	/* We assume only one thread will call this function */
	spin_lock_irqsave(&g_pq_bl_change_lock, flags);
	g_pq_backlight_db = g_pq_backlight;
	spin_unlock_irqrestore(&g_pq_bl_change_lock, flags);

	memcpy(backlight, &g_pq_backlight_db, sizeof(int));

	DDPINFO("%s: %d\n", __func__, ret);

	return ret;
}

void disp_pq_notify_backlight_changed(int bl_1024)
{
	unsigned long flags;
	int old_bl;

	spin_lock_irqsave(&g_pq_bl_change_lock, flags);
	old_bl = g_pq_backlight;
	g_pq_backlight = bl_1024;
	spin_unlock_irqrestore(&g_pq_bl_change_lock, flags);

	if (atomic_read(&g_ccorr_is_init_valid) != 1)
		return;

	DDPINFO("%s: %d\n", __func__, bl_1024);

	if (default_comp != NULL && (old_bl == 0 || bl_1024 == 0)) {
		disp_ccorr_set_interrupt(default_comp, 1);

		if (default_comp != NULL &&
				default_comp->mtk_crtc != NULL)
			mtk_crtc_check_trigger(default_comp->mtk_crtc, false);

		DDPINFO("%s: trigger refresh when backlight ON/Off", __func__);
	}
}

static int disp_ccorr_set_coef(
	const struct DISP_CCORR_COEF_T *user_color_corr,
	struct mtk_ddp_comp *comp,
	struct cmdq_pkt *handle)
{
	int ret = 0;
	struct DISP_CCORR_COEF_T *ccorr, *old_ccorr;
	int id = index_of_ccorr(comp->id);

	ccorr = kmalloc(sizeof(struct DISP_CCORR_COEF_T), GFP_KERNEL);
	if (ccorr == NULL) {
		DDPPR_ERR("%s: no memory\n", __func__);
		return -EFAULT;
	}

	if (user_color_corr == NULL) {
		ret = -EFAULT;
		kfree(ccorr);
	} else {
		memcpy(ccorr, user_color_corr,
			sizeof(struct DISP_CCORR_COEF_T));

		if (id >= 0 && id < DISP_CCORR_TOTAL) {
			mutex_lock(&g_ccorr_global_lock);

			old_ccorr = g_disp_ccorr_coef[id];
			g_disp_ccorr_coef[id] = ccorr;

			DDPINFO("%s: Set module(%d) coef", __func__, id);
			ret = disp_ccorr_write_coef_reg(comp, handle, 0);

			mutex_unlock(&g_ccorr_global_lock);

			if (old_ccorr != NULL)
				kfree(old_ccorr);
			/* TODO: NEED TO BE FIXED */
			/* disp_ccorr_trigger_refresh(comp); */
		} else {
			DDPPR_ERR("%s: invalid ID = %d\n", __func__, id);
			ret = -EFAULT;
			kfree(ccorr);
		}
	}

	return ret;
}

int disp_ccorr_set_color_matrix(struct mtk_ddp_comp *comp,
	struct cmdq_pkt *handle, int32_t matrix[16], int32_t hint)
{
	int ret = 0;
	int i, j;
	int ccorr_without_gamma = 0;
	bool need_refresh = false;

	if (handle == NULL) {
		DDPPR_ERR("%s: cmdq can not be NULL\n", __func__);
		return -EFAULT;
	}

	mutex_lock(&g_ccorr_global_lock);

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			/* Copy Color Matrix */
			g_ccorr_color_matrix[i][j] = matrix[j*4 + i];

			/* early jump out */
			if (ccorr_without_gamma == 1)
				continue;

			if (i == j && g_ccorr_color_matrix[i][j] != 1024)
				ccorr_without_gamma = 1;
			else if (i != j && g_ccorr_color_matrix[i][j] != 0)
				ccorr_without_gamma = 1;
		}
	}

	g_disp_ccorr_without_gamma = ccorr_without_gamma;

	disp_ccorr_write_coef_reg(comp, handle, 0);

	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			if (g_ccorr_prev_matrix[i][j]
				!= g_ccorr_color_matrix[i][j]) {
				/* refresh when matrix changed */
				need_refresh = true;
			}
			/* Copy Color Matrix */
			g_ccorr_prev_matrix[i][j] = g_ccorr_color_matrix[i][j];
		}
	}

	for (i = 0; i < 3; i += 1) {
		DDPINFO("g_ccorr_color_matrix[%d][0-2] = {%d, %d, %d}\n",
				i,
				g_ccorr_color_matrix[i][0],
				g_ccorr_color_matrix[i][1],
				g_ccorr_color_matrix[i][2]);
	}

	DDPINFO("g_disp_ccorr_without_gamma: [%d], need_refresh: [%d]\n",
		g_disp_ccorr_without_gamma, need_refresh);

	mutex_unlock(&g_ccorr_global_lock);

	if (need_refresh == true && comp->mtk_crtc != NULL)
		mtk_crtc_check_trigger(comp->mtk_crtc, false);

	return ret;
}

int disp_ccorr_set_RGB_Gain(struct mtk_ddp_comp *comp,
	struct cmdq_pkt *handle,
	int r, int g, int b)
{
	int ret;

	mutex_lock(&g_ccorr_global_lock);
	g_rgb_matrix[0][0] = r;
	g_rgb_matrix[1][1] = g;
	g_rgb_matrix[2][2] = b;

	DDPINFO("%s: r[%d], g[%d], b[%d]", __func__, r, g, b);
	ret = disp_ccorr_write_coef_reg(comp, NULL, 0);
	mutex_unlock(&g_ccorr_global_lock);

	return ret;
}

int mtk_drm_ioctl_set_ccorr(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	int ret = 0;
	struct DISP_CCORR_COEF_T *config = data;
	/* TODO: dual pipe */
	struct mtk_drm_private *private = dev->dev_private;
	struct mtk_ddp_comp *comp = private->ddp_comp[DDP_COMPONENT_CCORR0];
	/* primary display */
	struct drm_crtc *crtc = private->crtc[0];
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct cmdq_pkt *handle;
	struct cmdq_client *client = mtk_crtc->gce_obj.client[CLIENT_CFG];

	mtk_crtc_pkt_create(&handle, crtc, client);

	if (disp_ccorr_set_coef(config,
		comp, handle) < 0) {
		DDPPR_ERR("DISP_IOCTL_SET_CCORR: failed\n");
		ret = -EFAULT;
	}

	cmdq_pkt_flush(handle);
	cmdq_pkt_destroy(handle);

	return ret;
}

int mtk_drm_ioctl_ccorr_eventctl(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	int ret = 0;
	/* TODO: dual pipe */
	int *enabled = data;
	struct mtk_drm_private *private = dev->dev_private;
	struct mtk_ddp_comp *comp = private->ddp_comp[DDP_COMPONENT_CCORR0];

	disp_ccorr_set_interrupt(comp, *enabled);

	/* TODO */
	/*
	 * if (enabled)
	 *	disp_ccorr_trigger_refresh(comp);
	 */

	return ret;
}

int mtk_drm_ioctl_ccorr_get_irq(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	int ret = 0;

	atomic_set(&g_ccorr_is_init_valid, 1);

	disp_ccorr_wait_irq(60);

	if (disp_pq_copy_backlight_to_user((int *) data) < 0) {
		DDPPR_ERR("%s: failed", __func__);
		ret = -EFAULT;
	}

	return ret;
}

static void mtk_ccorr_config(struct mtk_ddp_comp *comp,
			     struct mtk_ddp_config *cfg,
			     struct cmdq_pkt *handle)
{
	DDPINFO("%s\n", __func__);

	cmdq_pkt_write(handle, comp->cmdq_base,
		       comp->regs_pa + DISP_REG_CCORR_SIZE,
		       (cfg->w << 16) | cfg->h, ~0);
}

static void mtk_ccorr_start(struct mtk_ddp_comp *comp, struct cmdq_pkt *handle)
{
	DDPINFO("%s\n", __func__);
	disp_ccorr_write_coef_reg(comp, handle, 1);

	cmdq_pkt_write(handle, comp->cmdq_base,
		       comp->regs_pa + DISP_REG_CCORR_EN, 0x1, 0x1);
}

static void mtk_ccorr_bypass(struct mtk_ddp_comp *comp, struct cmdq_pkt *handle)
{
	DDPINFO("%s\n", __func__);

	cmdq_pkt_write(handle, comp->cmdq_base,
		       comp->regs_pa + DISP_REG_CCORR_CFG, 0x1, 0x1);
	g_ccorr_relay_value[index_of_ccorr(comp->id)] = 0x1;
}

static void mtk_ccorr_prepare(struct mtk_ddp_comp *comp)
{
	mtk_ddp_comp_clk_prepare(comp);
	atomic_set(&g_ccorr_is_clock_on[index_of_ccorr(comp->id)], 1);
}

static void mtk_ccorr_unprepare(struct mtk_ddp_comp *comp)
{
	disp_ccorr_clear_irq_only(comp);
	atomic_set(&g_ccorr_is_clock_on[index_of_ccorr(comp->id)], 0);
	mtk_ddp_comp_clk_unprepare(comp);
}

static const struct mtk_ddp_comp_funcs mtk_disp_ccorr_funcs = {
	.config = mtk_ccorr_config,
	.start = mtk_ccorr_start,
	.bypass = mtk_ccorr_bypass,
	.prepare = mtk_ccorr_prepare,
	.unprepare = mtk_ccorr_unprepare,
};

static int mtk_disp_ccorr_bind(struct device *dev, struct device *master,
			       void *data)
{
	struct mtk_disp_ccorr *priv = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	int ret;

	DDPINFO("%s\n", __func__);

	ret = mtk_ddp_comp_register(drm_dev, &priv->ddp_comp);
	if (ret < 0) {
		dev_err(dev, "Failed to register component %s: %d\n",
			dev->of_node->full_name, ret);
		return ret;
	}

	return 0;
}

static void mtk_disp_ccorr_unbind(struct device *dev, struct device *master,
				  void *data)
{
	struct mtk_disp_ccorr *priv = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;

	mtk_ddp_comp_unregister(drm_dev, &priv->ddp_comp);
}

static const struct component_ops mtk_disp_ccorr_component_ops = {
	.bind	= mtk_disp_ccorr_bind,
	.unbind = mtk_disp_ccorr_unbind,
};

void mtk_ccorr_dump(struct mtk_ddp_comp *comp)
{
	void __iomem *baddr = comp->regs;

	DDPDUMP("== %s REGS ==\n", mtk_dump_comp_str(comp));
	mtk_cust_dump_reg(baddr, 0x0, 0x20, 0x30, -1);
	mtk_cust_dump_reg(baddr, 0x24, 0x28, -1, -1);
}

static int mtk_disp_ccorr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_disp_ccorr *priv;
	enum mtk_ddp_comp_id comp_id;
	int irq;
	int ret;

	DDPPR_ERR("%s\n", __func__);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	comp_id = mtk_ddp_comp_get_id(dev->of_node, MTK_DISP_CCORR);
	if ((int)comp_id < 0) {
		DDPPR_ERR("Failed to identify by alias: %d\n", comp_id);
		return comp_id;
	}

	ret = mtk_ddp_comp_init(dev, dev->of_node, &priv->ddp_comp, comp_id,
				&mtk_disp_ccorr_funcs);
	if (ret != 0) {
		DDPPR_ERR("Failed to initialize component: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, priv);

	ret = devm_request_irq(dev, irq, mtk_disp_ccorr_irq_handler,
			       IRQF_TRIGGER_NONE | IRQF_SHARED,
			       dev_name(dev), priv);

	pm_runtime_enable(dev);

	ret = component_add(dev, &mtk_disp_ccorr_component_ops);
	if (ret != 0) {
		dev_err(dev, "Failed to add component: %d\n", ret);
		pm_runtime_disable(dev);
	}

	default_comp = NULL;

	return ret;
}

static int mtk_disp_ccorr_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_ccorr_component_ops);

	pm_runtime_disable(&pdev->dev);
	return 0;
}

static const struct of_device_id mtk_disp_ccorr_driver_dt_match[] = {
	{.compatible = "mediatek,mt6779-disp-ccorr",},
	{.compatible = "mediatek,mt6885-disp-ccorr",},
	{},
};

MODULE_DEVICE_TABLE(of, mtk_disp_ccorr_driver_dt_match);

struct platform_driver mtk_disp_ccorr_driver = {
	.probe = mtk_disp_ccorr_probe,
	.remove = mtk_disp_ccorr_remove,
	.driver = {

			.name = "mediatek-disp-ccorr",
			.owner = THIS_MODULE,
			.of_match_table = mtk_disp_ccorr_driver_dt_match,
		},
};
