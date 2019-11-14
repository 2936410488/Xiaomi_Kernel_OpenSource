/*
 * Copyright (c) 2017 MediaTek Inc.
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
#include "mtk_disp_color.h"
#include "mtk_dump.h"

#define UNUSED(expr) (void)(expr)
#define index_of_color(module) ((module == DDP_COMPONENT_COLOR0) ? 0 : 1)
#define PQ_MODULE_NUM 9
static struct DISP_PQ_PARAM g_Color_Param[DISP_COLOR_TOTAL];

int ncs_tuning_mode;

static unsigned int g_split_en;
static unsigned int g_split_window_x_start;
static unsigned int g_split_window_y_start;
static unsigned int g_split_window_x_end = 0xFFFF;
static unsigned int g_split_window_y_end = 0xFFFF;

static unsigned long g_color_dst_w[DISP_COLOR_TOTAL];
static unsigned long g_color_dst_h[DISP_COLOR_TOTAL];

static atomic_t g_color_is_clock_on[DISP_COLOR_TOTAL] = { ATOMIC_INIT(0),
	ATOMIC_INIT(0)};

static DEFINE_SPINLOCK(g_color_clock_lock);

static int g_color_bypass[DISP_COLOR_TOTAL];

static DEFINE_MUTEX(g_color_reg_lock);
static struct DISPLAY_COLOR_REG g_color_reg;
static int g_color_reg_valid;

enum COLOR_IOCTL_CMD {
	SET_PQPARAM = 0,
	SET_COLOR_REG,
	WRITE_REG,
	BYPASS_COLOR,
	PQ_SET_WINDOW
};

struct mtk_disp_color_data {
	unsigned int color_offset;
	bool support_color21;
	bool support_color30;
	struct mtk_pq_reg_table reg_table[PQ_MODULE_NUM];
	unsigned int reg_num;
	unsigned int color_window;
};

/**
 * struct mtk_disp_color - DISP_COLOR driver structure
 * @ddp_comp - structure containing type enum and hardware resources
 * @crtc - associated crtc to report irq events to
 */
struct mtk_disp_color {
	struct mtk_ddp_comp ddp_comp;
	struct drm_crtc *crtc;
	const struct mtk_disp_color_data *data;
};

/* global PQ param for kernel space */
static struct DISP_PQ_PARAM g_Color_Param[2] = {
	{
u4SHPGain:2,
u4SatGain:4,
u4PartialY:0,
u4HueAdj:{9, 9, 9, 9},
u4SatAdj:{0, 0, 0, 0},
u4Contrast:4,
u4Brightness:4,
u4Ccorr:0,
u4ColorLUT:0
	 },
	{
u4SHPGain:2,
u4SatGain:4,
u4PartialY:0,
u4HueAdj:{9, 9, 9, 9},
u4SatAdj:{0, 0, 0, 0},
u4Contrast:4,
u4Brightness:4,
u4Ccorr:1,
u4ColorLUT:0
	}
};

/* initialize index */
/* (because system default is 0, need fill with 0x80) */

static struct DISPLAY_PQ_T g_Color_Index = {
GLOBAL_SAT:	/* 0~9 */
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},

CONTRAST :	/* 0~9 */
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},

BRIGHTNESS :	/* 0~9 */
	{0x400, 0x400, 0x400, 0x400, 0x400, 0x400, 0x400, 0x400, 0x400, 0x400},

PARTIAL_Y :
	{
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},


PURP_TONE_S :
{			/* hue 0~10 */
	{			/* 0 disable */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 1 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 2 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},
	{			/* 3 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 4 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 5 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 6 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 7 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 8 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 9 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 10 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 11 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 12 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 13 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 14 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 15 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 16 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 17 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 18 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	}
},
SKIN_TONE_S :
{
	{			/* 0 disable */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 1 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 2 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 3 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 4 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 5 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 6 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 7 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 8 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 9 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 10 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 11 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 12 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 13 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 14 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 15 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 16 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 17 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 18 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	}
},
GRASS_TONE_S :
{
	{			/* 0 disable */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 1 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 2 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 3 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 4 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 5 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 6 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 7 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 8 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 9 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 10 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 11 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 12 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 13 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 14 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 15 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 16 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 17 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	},

	{			/* 18 */
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
	}
},
SKY_TONE_S :
{			/* hue 0~10 */
	{			/* 0 disable */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 1 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 2 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},
	{			/* 3 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 4 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 5 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 6 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 7 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 8 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 9 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 10 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 11 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 12 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 13 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 14 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 15 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 16 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 17 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	},

	{			/* 18 */
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80},
		{0x80, 0x80, 0x80}
	}
},

PURP_TONE_H :
{
	/* hue 0~2 */
	{0x80, 0x80, 0x80},	/* 3 */
	{0x80, 0x80, 0x80},	/* 4 */
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},	/* 3 */
	{0x80, 0x80, 0x80},	/* 4 */
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},	/* 4 */
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},	/* 3 */
	{0x80, 0x80, 0x80},	/* 4 */
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80}
},

SKIN_TONE_H :
{
	/* hue 3~16 */
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
},

GRASS_TONE_H :
{
/* hue 17~24 */
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80, 0x80, 0x80, 0x80}
},

SKY_TONE_H :
{
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80},
	{0x80, 0x80, 0x80}
},
CCORR_COEF : /* ccorr feature */
{
	{
		{0x400, 0x0, 0x0},
		{0x0, 0x400, 0x0},
		{0x0, 0x0, 0x400},
	},
	{
		{0x400, 0x0, 0x0},
		{0x0, 0x400, 0x0},
		{0x0, 0x0, 0x400},
	},
	{
		{0x400, 0x0, 0x0},
		{0x0, 0x400, 0x0},
		{0x0, 0x0, 0x400},
	},
	{
		{0x400, 0x0, 0x0},
		{0x0, 0x400, 0x0},
		{0x0, 0x0, 0x400}
	}
},
S_GAIN_BY_Y :
{
	{0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80},

	{0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80},

	{0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80},

	{0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80},

	{0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80,
	 0x80, 0x80, 0x80, 0x80}
},

S_GAIN_BY_Y_EN:0,

LSP_EN:0,

LSP :
{0x0, 0x0, 0x7F, 0x7F, 0x7F, 0x0, 0x7F, 0x7F},
COLOR_3D :
{
	{			/* 0 */
		/* Windows  1 */
		{ 0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF},
		/* Windows  2 */
		{ 0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF},
		/* Windows  3 */
		{ 0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF},
	},
	{			/* 1 */
		/* Windows  1 */
		{ 0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF},
		/* Windows  2 */
		{ 0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF},
		/* Windows  3 */
		{ 0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF},
	},
	{			/* 2 */
		/* Windows  1 */
		{ 0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF},
		/* Windows  2 */
		{ 0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF},
		/* Windows  3 */
		{ 0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF},
	},
	{			/* 3 */
		/* Windows  1 */
		{ 0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF},
		/* Windows  2 */
		{ 0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF},
		/* Windows  3 */
		{ 0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF,
		  0x80,  0x80,  0x80,  0x80,  0x80,  0x80, 0x3FF, 0x3FF,
		 0x000, 0x050, 0x100, 0x200, 0x300, 0x350, 0x3FF},
	},
}
};


static inline struct mtk_disp_color *comp_to_color(struct mtk_ddp_comp *comp)
{
	return container_of(comp, struct mtk_disp_color, ddp_comp);
}


#if 0
static void _color_reg_mask(struct mtk_ddp_comp *comp,
		void *__cmdq, unsigned long offset,
		unsigned int value, unsigned int mask)
{
	struct cmdq_pkt *cmdq = (struct cmdq_pkt *) __cmdq;

	if (cmdq != NULL)
		cmdq_pkt_write(cmdq, comp->cmdq_base,
			comp->regs_pa + offset, value, mask);
	else
		writel(value, comp->regs + offset);
}
#endif

static void ddp_color_cal_split_window(struct mtk_ddp_comp *comp,
	unsigned int *p_split_window_x, unsigned int *p_split_window_y)
{
	unsigned int split_window_x = 0xFFFF0000;
	unsigned int split_window_y = 0xFFFF0000;
	int id = index_of_color(comp->id);

	/* save to global, can be applied on following PQ param updating. */
	if (g_color_dst_w[id] == 0 || g_color_dst_h[id] == 0) {
		DDPINFO("g_color0_dst_w/h not init, return default settings\n");
	} else if (g_split_en) {
		/* TODO: CONFIG_MTK_LCM_PHYSICAL_ROTATION other case */
		split_window_y =
			(g_split_window_y_end << 16) | g_split_window_y_start;
		split_window_x = (g_split_window_x_end << 16) |
			g_split_window_x_start;
	}

	*p_split_window_x = split_window_x;
	*p_split_window_y = split_window_y;
}

bool disp_color_reg_get(struct mtk_ddp_comp *comp,
	unsigned long addr, int *value)
{
	unsigned long flags;

	DDPDBG("%s @ %d......... spin_trylock_irqsave ++ ",
		__func__, __LINE__);
	if (spin_trylock_irqsave(&g_color_clock_lock, flags)) {
		DDPDBG("%s @ %d......... spin_trylock_irqsave -- ",
			__func__, __LINE__);
		*value = readl(comp->regs + addr);
		spin_unlock_irqrestore(&g_color_clock_lock, flags);
	} else {
		DDPINFO("%s @ %d......... Failed to spin_trylock_irqsave ",
			__func__, __LINE__);
	}

	return true;
}

static void ddp_color_set_window(struct mtk_ddp_comp *comp,
	struct DISP_PQ_WIN_PARAM *win_param, struct cmdq_pkt *handle)
{
	unsigned int split_window_x, split_window_y;

	/* save to global, can be applied on following PQ param updating. */
	if (win_param->split_en) {
		g_split_en = 1;
		g_split_window_x_start = win_param->start_x;
		g_split_window_y_start = win_param->start_y;
		g_split_window_x_end = win_param->end_x;
		g_split_window_y_end = win_param->end_y;
	} else {
		g_split_en = 0;
		g_split_window_x_start = 0x0000;
		g_split_window_y_start = 0x0000;
		g_split_window_x_end = 0xFFFF;
		g_split_window_y_end = 0xFFFF;
	}

	DDPINFO("%s: input: id[%d], en[%d], x[0x%x], y[0x%x]\n",
		__func__, comp->id, g_split_en,
		((win_param->end_x << 16) | win_param->start_x),
		((win_param->end_y << 16) | win_param->start_y));

	ddp_color_cal_split_window(comp, &split_window_x, &split_window_y);

	DDPINFO("%s: output: x[0x%x], y[0x%x]", __func__, split_window_x, split_window_y);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_DBG_CFG_MAIN,
		(g_split_en << 3), 0x00000008);
	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_WIN_X_MAIN, split_window_x, ~0);
	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_WIN_Y_MAIN, split_window_y, ~0);
}

struct DISP_PQ_PARAM *get_Color_config(int id)
{
	return &g_Color_Param[id];
}

struct DISPLAY_PQ_T *get_Color_index(void)
{
	return &g_Color_Index;
}

void DpEngine_COLORonInit(struct mtk_ddp_comp *comp, struct cmdq_pkt *handle)
{
	unsigned int split_window_x, split_window_y;
	struct mtk_disp_color *color = comp_to_color(comp);

	ddp_color_cal_split_window(comp, &split_window_x, &split_window_y);

	DDPINFO("%s: id[%d], en[%d], x[0x%x], y[0x%x]\n",
		__func__, comp->id, g_split_en, split_window_x, split_window_y);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_DBG_CFG_MAIN,
		(g_split_en << 3), 0x00000008);
	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_WIN_X_MAIN, split_window_x, ~0);
	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_WIN_Y_MAIN, split_window_y, ~0);

	/* enable interrupt */
	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_INTEN(color),
		0x00000007, 0x00000007);

	/* Set 10bit->8bit Rounding */
	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_OUT_SEL(color), 0x333, 0x00000333);
}

void DpEngine_COLORonConfig(struct mtk_ddp_comp *comp, struct cmdq_pkt *handle)
{
	int index = 0;
	unsigned int u4Temp = 0;
	unsigned int u4SatAdjPurp, u4SatAdjSkin, u4SatAdjGrass, u4SatAdjSky;
	unsigned char h_series[20] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	int id = index_of_color(comp->id);
	struct mtk_disp_color *color = comp_to_color(comp);
	struct DISP_PQ_PARAM *pq_param_p = &g_Color_Param[id];
	int i, j, reg_index;
	unsigned int pq_index;
	int wide_gamut_en = 0;

	if (pq_param_p->u4Brightness >= BRIGHTNESS_SIZE ||
		pq_param_p->u4Contrast >= CONTRAST_SIZE ||
		pq_param_p->u4SatGain >= GLOBAL_SAT_SIZE ||
		pq_param_p->u4HueAdj[PURP_TONE] >= COLOR_TUNING_INDEX ||
		pq_param_p->u4HueAdj[SKIN_TONE] >= COLOR_TUNING_INDEX ||
		pq_param_p->u4HueAdj[GRASS_TONE] >= COLOR_TUNING_INDEX ||
		pq_param_p->u4HueAdj[SKY_TONE] >= COLOR_TUNING_INDEX ||
		pq_param_p->u4SatAdj[PURP_TONE] >= COLOR_TUNING_INDEX ||
		pq_param_p->u4SatAdj[SKIN_TONE] >= COLOR_TUNING_INDEX ||
		pq_param_p->u4SatAdj[GRASS_TONE] >= COLOR_TUNING_INDEX ||
		pq_param_p->u4SatAdj[SKY_TONE] >= COLOR_TUNING_INDEX) {
		DRM_ERROR("[PQ][COLOR] Tuning index range error !\n");
		return;
	}

	if (g_color_bypass[id] == 0) {
		if (color->data->support_color21 == true) {
			cmdq_pkt_write(handle, comp->cmdq_base,
				comp->regs_pa + DISP_COLOR_CFG_MAIN,
				(1 << 21)
				| (g_Color_Index.LSP_EN << 20)
				| (g_Color_Index.S_GAIN_BY_Y_EN << 15)
				| (wide_gamut_en << 8)
				| (0 << 7), 0x003081FF);
		} else {
			/* disable wide_gamut */
			cmdq_pkt_write(handle, comp->cmdq_base,
				comp->regs_pa + DISP_COLOR_CFG_MAIN,
				(0 << 8) | (0 << 7), 0x1FF);
		}

		/* color start */
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_START(color), 0x1, 0x3);

		/* enable R2Y/Y2R in Color Wrapper */
		if (color->data->support_color21 == true) {
			/* RDMA & OVL will enable wide-gamut function */
			/* disable rgb clipping function in CM1 */
			/* to keep the wide-gamut range */
			cmdq_pkt_write(handle, comp->cmdq_base,
				comp->regs_pa + DISP_COLOR_CM1_EN(color),
				0x01, 0x03);
		} else {
			cmdq_pkt_write(handle, comp->cmdq_base,
				comp->regs_pa + DISP_COLOR_CM1_EN(color),
				0x01, 0x01);
		}

		/* also set no rounding on Y2R */
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_CM2_EN(color), 0x11, 0x11);
	} else {
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_CFG_MAIN,
			0x1 << 29, 0x1 << 29);
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_START(color), 0x1, 0x1);
	}

	/* for partial Y contour issue */
	if (wide_gamut_en == 0)
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_LUMA_ADJ, 0x40, 0x7F);
	else if (wide_gamut_en == 1)
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_LUMA_ADJ, 0x0, 0x7F);

	/* config parameter from customer color_index.h */
	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_G_PIC_ADJ_MAIN_1,
		(g_Color_Index.BRIGHTNESS[pq_param_p->u4Brightness] << 16) |
		g_Color_Index.CONTRAST[pq_param_p->u4Contrast], 0x07FF01FF);
	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_G_PIC_ADJ_MAIN_2,
		g_Color_Index.GLOBAL_SAT[pq_param_p->u4SatGain], 0x000001FF);

	/* Partial Y Function */
	for (index = 0; index < 8; index++) {
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_Y_SLOPE_1_0_MAIN + 4 * index,
			(g_Color_Index.PARTIAL_Y
				[pq_param_p->u4PartialY][2 * index] |
			 g_Color_Index.PARTIAL_Y
				[pq_param_p->u4PartialY][2 * index + 1]
			 << 16), 0x00FF00FF);
	}

	if (color->data->support_color21 == false)
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_C_BOOST_MAIN,
			0 << 13, 0x00002000);
	else
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_C_BOOST_MAIN_2,
			0x40 << 24,	0xFF000000);

	/* Partial Saturation Function */
	u4SatAdjPurp = pq_param_p->u4SatAdj[PURP_TONE];
	u4SatAdjSkin = pq_param_p->u4SatAdj[SKIN_TONE];
	u4SatAdjGrass = pq_param_p->u4SatAdj[GRASS_TONE];
	u4SatAdjSky = pq_param_p->u4SatAdj[SKY_TONE];

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN1_0,
		(g_Color_Index.PURP_TONE_S[u4SatAdjPurp][SG1][0] |
		g_Color_Index.PURP_TONE_S[u4SatAdjPurp][SG1][1] << 8 |
		g_Color_Index.PURP_TONE_S[u4SatAdjPurp][SG1][2] << 16 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG1][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN1_1,
		(g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG1][1] |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG1][2] << 8 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG1][3] << 16 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG1][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN1_2,
		(g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG1][5] |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG1][6] << 8 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG1][7] << 16 |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG1][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN1_3,
		(g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG1][1] |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG1][2] << 8 |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG1][3] << 16 |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG1][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN1_4,
		(g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG1][5] |
		g_Color_Index.SKY_TONE_S[u4SatAdjSky][SG1][0] << 8 |
		g_Color_Index.SKY_TONE_S[u4SatAdjSky][SG1][1] << 16 |
		g_Color_Index.SKY_TONE_S[u4SatAdjSky][SG1][2] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN2_0,
		(g_Color_Index.PURP_TONE_S[u4SatAdjPurp][SG2][0] |
		g_Color_Index.PURP_TONE_S[u4SatAdjPurp][SG2][1] << 8 |
		g_Color_Index.PURP_TONE_S[u4SatAdjPurp][SG2][2] << 16 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG2][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN2_1,
		(g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG2][1] |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG2][2] << 8 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG2][3] << 16 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG2][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN2_2,
		(g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG2][5] |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG2][6] << 8 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG2][7] << 16 |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG2][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN2_3,
		(g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG2][1] |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG2][2] << 8 |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG2][3] << 16 |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG2][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN2_4,
		(g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG2][5] |
		g_Color_Index.SKY_TONE_S[u4SatAdjSky][SG2][0] << 8 |
		g_Color_Index.SKY_TONE_S[u4SatAdjSky][SG2][1] << 16 |
		g_Color_Index.SKY_TONE_S[u4SatAdjSky][SG2][2] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN3_0,
		(g_Color_Index.PURP_TONE_S[u4SatAdjPurp][SG3][0] |
		g_Color_Index.PURP_TONE_S[u4SatAdjPurp][SG3][1] << 8 |
		g_Color_Index.PURP_TONE_S[u4SatAdjPurp][SG3][2] << 16 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG3][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN3_1,
		(g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG3][1] |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG3][2] << 8 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG3][3] << 16 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG3][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN3_2,
		(g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG3][5] |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG3][6] << 8 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SG3][7] << 16 |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG3][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN3_3,
		(g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG3][1] |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG3][2] << 8 |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG3][3] << 16 |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG3][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN3_4,
		(g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SG3][5] |
		g_Color_Index.SKY_TONE_S[u4SatAdjSky][SG3][0] << 8 |
		g_Color_Index.SKY_TONE_S[u4SatAdjSky][SG3][1] << 16 |
		g_Color_Index.SKY_TONE_S[u4SatAdjSky][SG3][2] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT1_0,
		(g_Color_Index.PURP_TONE_S[u4SatAdjPurp][SP1][0] |
		g_Color_Index.PURP_TONE_S[u4SatAdjPurp][SP1][1] << 8 |
		g_Color_Index.PURP_TONE_S[u4SatAdjPurp][SP1][2] << 16 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SP1][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT1_1,
		(g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SP1][1] |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SP1][2] << 8 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SP1][3] << 16 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SP1][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT1_2,
		(g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SP1][5] |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SP1][6] << 8 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SP1][7] << 16 |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SP1][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT1_3,
		(g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SP1][1] |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SP1][2] << 8 |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SP1][3] << 16 |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SP1][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT1_4,
		(g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SP1][5] |
		g_Color_Index.SKY_TONE_S[u4SatAdjSky][SP1][0] << 8 |
		g_Color_Index.SKY_TONE_S[u4SatAdjSky][SP1][1] << 16 |
		g_Color_Index.SKY_TONE_S[u4SatAdjSky][SP1][2] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT2_0,
		(g_Color_Index.PURP_TONE_S[u4SatAdjPurp][SP2][0] |
		g_Color_Index.PURP_TONE_S[u4SatAdjPurp][SP2][1] << 8 |
		g_Color_Index.PURP_TONE_S[u4SatAdjPurp][SP2][2] << 16 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SP2][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT2_1,
		(g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SP2][1] |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SP2][2] << 8 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SP2][3] << 16 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SP2][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT2_2,
		(g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SP2][5] |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SP2][6] << 8 |
		g_Color_Index.SKIN_TONE_S[u4SatAdjSkin][SP2][7] << 16 |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SP2][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT2_3,
		(g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SP2][1] |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SP2][2] << 8 |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SP2][3] << 16 |
		g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SP2][4] << 24), ~0);


	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT2_4,
		(g_Color_Index.GRASS_TONE_S[u4SatAdjGrass][SP2][5] |
		g_Color_Index.SKY_TONE_S[u4SatAdjSky][SP2][0] << 8 |
		g_Color_Index.SKY_TONE_S[u4SatAdjSky][SP2][1] << 16 |
		g_Color_Index.SKY_TONE_S[u4SatAdjSky][SP2][2] << 24), ~0);

	for (index = 0; index < 3; index++) {
		u4Temp = pq_param_p->u4HueAdj[PURP_TONE];
		h_series[index + PURP_TONE_START] =
			g_Color_Index.PURP_TONE_H[u4Temp][index];
	}

	for (index = 0; index < 8; index++) {
		u4Temp = pq_param_p->u4HueAdj[SKIN_TONE];
		h_series[index + SKIN_TONE_START] =
		    g_Color_Index.SKIN_TONE_H[u4Temp][index];
	}

	for (index = 0; index < 6; index++) {
		u4Temp = pq_param_p->u4HueAdj[GRASS_TONE];
		h_series[index + GRASS_TONE_START] =
			g_Color_Index.GRASS_TONE_H[u4Temp][index];
	}

	for (index = 0; index < 3; index++) {
		u4Temp = pq_param_p->u4HueAdj[SKY_TONE];
		h_series[index + SKY_TONE_START] =
		    g_Color_Index.SKY_TONE_H[u4Temp][index];
	}

	for (index = 0; index < 5; index++) {
		u4Temp = (h_series[4 * index]) +
		    (h_series[4 * index + 1] << 8) +
		    (h_series[4 * index + 2] << 16) +
		    (h_series[4 * index + 3] << 24);
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_LOCAL_HUE_CD_0 + 4 * index,
			u4Temp, ~0);
	}

	if (color->data->support_color21 == true) {
		/* S Gain By Y */
		u4Temp = 0;

		reg_index = 0;
		for (i = 0; i < S_GAIN_BY_Y_CONTROL_CNT; i++) {
			for (j = 0; j < S_GAIN_BY_Y_HUE_PHASE_CNT; j += 4) {
				u4Temp = (g_Color_Index.S_GAIN_BY_Y[i][j]) +
					(g_Color_Index.S_GAIN_BY_Y[i][j + 1]
					<< 8) +
					(g_Color_Index.S_GAIN_BY_Y[i][j + 2]
					<< 16) +
					(g_Color_Index.S_GAIN_BY_Y[i][j + 3]
					<< 24);

				cmdq_pkt_write(handle, comp->cmdq_base,
					comp->regs_pa +
					DISP_COLOR_S_GAIN_BY_Y0_0 + reg_index,
					u4Temp, ~0);
				reg_index += 4;
			}
		}
		/* LSP */
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_LSP_1,
			(g_Color_Index.LSP[3] << 0) |
			(g_Color_Index.LSP[2] << 7) |
			(g_Color_Index.LSP[1] << 14) |
			(g_Color_Index.LSP[0] << 22), 0x1FFFFFFF);
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_LSP_2,
			(g_Color_Index.LSP[7] << 0) |
			(g_Color_Index.LSP[6] << 8) |
			(g_Color_Index.LSP[5] << 16) |
			(g_Color_Index.LSP[4] << 23), 0x3FFF7F7F);
	}

	/* color window */
	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_TWO_D_WINDOW_1,
		color->data->color_window, ~0);

	if (color->data->support_color30 == true) {
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_CM_CONTROL,
			0x0 |
			(0x3 << 1) |	/* enable window 1 */
			(0x3 << 4) |	/* enable window 2 */
			(0x3 << 7)		/* enable window 3 */
			, 0x1B7);

		pq_index = pq_param_p->u4ColorLUT;
		for (i = 0; i < WIN_TOTAL; i++) {
			reg_index = i * 4 * (LUT_TOTAL * 5);
			for (j = 0; j < LUT_TOTAL; j++) {
				cmdq_pkt_write(handle, comp->cmdq_base,
					comp->regs_pa +
						DISP_COLOR_CM_W1_HUE_0 +
						reg_index,
					g_Color_Index.COLOR_3D[pq_index]
					[i][j*LUT_REG_TOTAL+REG_L] |
					(g_Color_Index.COLOR_3D[pq_index]
					[i][j*LUT_REG_TOTAL+REG_U] << 10) |
					(g_Color_Index.COLOR_3D[pq_index]
					[i][j*LUT_REG_TOTAL+REG_POINT0] << 20),
						~0);
				cmdq_pkt_write(handle, comp->cmdq_base,
					comp->regs_pa + DISP_COLOR_CM_W1_HUE_1
						+ reg_index,
					g_Color_Index.COLOR_3D[pq_index]
					[i][j*LUT_REG_TOTAL+REG_POINT1] |
					(g_Color_Index.COLOR_3D[pq_index]
					[i][j*LUT_REG_TOTAL+REG_POINT2] << 10) |
					(g_Color_Index.COLOR_3D[pq_index]
					[i][j*LUT_REG_TOTAL+REG_POINT3] << 20),
					~0);
				cmdq_pkt_write(handle, comp->cmdq_base,
					comp->regs_pa + DISP_COLOR_CM_W1_HUE_2
						+ reg_index,
					g_Color_Index.COLOR_3D[pq_index]
					[i][j*LUT_REG_TOTAL+REG_POINT4] |
					(g_Color_Index.COLOR_3D[pq_index]
					[i][j*LUT_REG_TOTAL+REG_SLOPE0] << 10) |
					(g_Color_Index.COLOR_3D[pq_index]
					[i][j*LUT_REG_TOTAL+REG_SLOPE1] << 20),
					~0);
				cmdq_pkt_write(handle, comp->cmdq_base,
					comp->regs_pa + DISP_COLOR_CM_W1_HUE_3
						+ reg_index,
					g_Color_Index.COLOR_3D[pq_index]
					[i][j*LUT_REG_TOTAL+REG_SLOPE2] |
					(g_Color_Index.COLOR_3D[pq_index]
					[i][j*LUT_REG_TOTAL+REG_SLOPE3] << 8) |
					(g_Color_Index.COLOR_3D[pq_index]
					[i][j*LUT_REG_TOTAL+REG_SLOPE4] << 16) |
					(g_Color_Index.COLOR_3D[pq_index]
					[i][j*LUT_REG_TOTAL+REG_SLOPE5] << 24),
					~0);
				cmdq_pkt_write(handle, comp->cmdq_base,
					comp->regs_pa + DISP_COLOR_CM_W1_HUE_4
						+ reg_index,
					g_Color_Index.COLOR_3D[pq_index]
					[i][j*LUT_REG_TOTAL+REG_WGT_LSLOPE] |
					(g_Color_Index.COLOR_3D[pq_index]
					[i][j*LUT_REG_TOTAL+REG_WGT_USLOPE]
					<< 16),	~0);

				reg_index += (4 * 5);
			}
		}
	}
}

static void color_write_hw_reg(struct mtk_ddp_comp *comp,
	const struct DISPLAY_COLOR_REG *color_reg, struct cmdq_pkt *handle)
{
	int index = 0;
	unsigned char h_series[20] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
		, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	unsigned int u4Temp = 0;
	int id = index_of_color(comp->id);
	struct mtk_disp_color *color = comp_to_color(comp);
	int i, j, reg_index;
	int wide_gamut_en = 0;

	if (g_color_bypass[id] == 0) {
		if (color->data->support_color21 == true) {
			cmdq_pkt_write(handle, comp->cmdq_base,
				comp->regs_pa + DISP_COLOR_CFG_MAIN,
				(1 << 21)
				| (g_Color_Index.LSP_EN << 20)
				| (g_Color_Index.S_GAIN_BY_Y_EN << 15)
				| (wide_gamut_en << 8)
				| (0 << 7), 0x003081FF);
		} else {
			/* disable wide_gamut */
			cmdq_pkt_write(handle, comp->cmdq_base,
				comp->regs_pa + DISP_COLOR_CFG_MAIN,
				(0 << 8) | (0 << 7), 0x1FF);
		}

		/* color start */
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_START(color), 0x1, 0x3);

		/* enable R2Y/Y2R in Color Wrapper */
		if (color->data->support_color21 == true) {
			/* RDMA & OVL will enable wide-gamut function */
			/* disable rgb clipping function in CM1 */
			/* to keep the wide-gamut range */
			cmdq_pkt_write(handle, comp->cmdq_base,
				comp->regs_pa + DISP_COLOR_CM1_EN(color),
				0x01, 0x03);
		} else {
			cmdq_pkt_write(handle, comp->cmdq_base,
				comp->regs_pa + DISP_COLOR_CM1_EN(color),
				0x01, 0x01);
		}

		/* also set no rounding on Y2R */
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_CM2_EN(color), 0x11, 0x11);
	} else {
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_CFG_MAIN,
			0x1 << 29, 0x1 << 29);
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_START(color), 0x1, 0x1);
	}

	/* for partial Y contour issue */
	if (wide_gamut_en == 0)
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_LUMA_ADJ, 0x40, 0x7F);
	else if (wide_gamut_en == 1)
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_LUMA_ADJ, 0x0, 0x7F);

	/* config parameter from customer color_index.h */
	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_G_PIC_ADJ_MAIN_1,
		(color_reg->BRIGHTNESS << 16) | color_reg->CONTRAST,
		0x07FF01FF);
	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_G_PIC_ADJ_MAIN_2,
		color_reg->GLOBAL_SAT, 0x000001FF);

	/* Partial Y Function */
	for (index = 0; index < 8; index++) {
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_Y_SLOPE_1_0_MAIN + 4 * index,
			(color_reg->PARTIAL_Y[2 * index] |
			 color_reg->PARTIAL_Y[2 * index + 1] << 16),
			 0x00FF00FF);
	}

	if (color->data->support_color21 == false)
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_C_BOOST_MAIN,
			0 << 13, 0x00002000);
	else
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_C_BOOST_MAIN_2,
			0x40 << 24,	0xFF000000);

	/* Partial Saturation Function */

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN1_0,
		(color_reg->PURP_TONE_S[SG1][0] |
		color_reg->PURP_TONE_S[SG1][1] << 8 |
		color_reg->PURP_TONE_S[SG1][2] << 16 |
		color_reg->SKIN_TONE_S[SG1][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN1_1,
		(color_reg->SKIN_TONE_S[SG1][1] |
		color_reg->SKIN_TONE_S[SG1][2] << 8 |
		color_reg->SKIN_TONE_S[SG1][3] << 16 |
		color_reg->SKIN_TONE_S[SG1][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN1_2,
		(color_reg->SKIN_TONE_S[SG1][5] |
		color_reg->SKIN_TONE_S[SG1][6] << 8 |
		color_reg->SKIN_TONE_S[SG1][7] << 16 |
		color_reg->GRASS_TONE_S[SG1][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN1_3,
		(color_reg->GRASS_TONE_S[SG1][1] |
		color_reg->GRASS_TONE_S[SG1][2] << 8 |
		color_reg->GRASS_TONE_S[SG1][3] << 16 |
		color_reg->GRASS_TONE_S[SG1][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN1_4,
		(color_reg->GRASS_TONE_S[SG1][5] |
		color_reg->SKY_TONE_S[SG1][0] << 8 |
		color_reg->SKY_TONE_S[SG1][1] << 16 |
		color_reg->SKY_TONE_S[SG1][2] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN2_0,
		(color_reg->PURP_TONE_S[SG2][0] |
		color_reg->PURP_TONE_S[SG2][1] << 8 |
		color_reg->PURP_TONE_S[SG2][2] << 16 |
		color_reg->SKIN_TONE_S[SG2][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN2_1,
		(color_reg->SKIN_TONE_S[SG2][1] |
		color_reg->SKIN_TONE_S[SG2][2] << 8 |
		color_reg->SKIN_TONE_S[SG2][3] << 16 |
		color_reg->SKIN_TONE_S[SG2][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN2_2,
		(color_reg->SKIN_TONE_S[SG2][5] |
		color_reg->SKIN_TONE_S[SG2][6] << 8 |
		color_reg->SKIN_TONE_S[SG2][7] << 16 |
		color_reg->GRASS_TONE_S[SG2][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN2_3,
		(color_reg->GRASS_TONE_S[SG2][1] |
		color_reg->GRASS_TONE_S[SG2][2] << 8 |
		color_reg->GRASS_TONE_S[SG2][3] << 16 |
		color_reg->GRASS_TONE_S[SG2][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN2_4,
		(color_reg->GRASS_TONE_S[SG2][5] |
		color_reg->SKY_TONE_S[SG2][0] << 8 |
		color_reg->SKY_TONE_S[SG2][1] << 16 |
		color_reg->SKY_TONE_S[SG2][2] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN3_0,
		(color_reg->PURP_TONE_S[SG3][0] |
		color_reg->PURP_TONE_S[SG3][1] << 8 |
		color_reg->PURP_TONE_S[SG3][2] << 16 |
		color_reg->SKIN_TONE_S[SG3][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN3_1,
		(color_reg->SKIN_TONE_S[SG3][1] |
		color_reg->SKIN_TONE_S[SG3][2] << 8 |
		color_reg->SKIN_TONE_S[SG3][3] << 16 |
		color_reg->SKIN_TONE_S[SG3][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN3_2,
		(color_reg->SKIN_TONE_S[SG3][5] |
		color_reg->SKIN_TONE_S[SG3][6] << 8 |
		color_reg->SKIN_TONE_S[SG3][7] << 16 |
		color_reg->GRASS_TONE_S[SG3][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN3_3,
		(color_reg->GRASS_TONE_S[SG3][1] |
		color_reg->GRASS_TONE_S[SG3][2] << 8 |
		color_reg->GRASS_TONE_S[SG3][3] << 16 |
		color_reg->GRASS_TONE_S[SG3][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_GAIN3_4,
		(color_reg->GRASS_TONE_S[SG3][5] |
		color_reg->SKY_TONE_S[SG3][0] << 8 |
		color_reg->SKY_TONE_S[SG3][1] << 16 |
		color_reg->SKY_TONE_S[SG3][2] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT1_0,
		(color_reg->PURP_TONE_S[SP1][0] |
		color_reg->PURP_TONE_S[SP1][1] << 8 |
		color_reg->PURP_TONE_S[SP1][2] << 16 |
		color_reg->SKIN_TONE_S[SP1][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT1_1,
		(color_reg->SKIN_TONE_S[SP1][1] |
		color_reg->SKIN_TONE_S[SP1][2] << 8 |
		color_reg->SKIN_TONE_S[SP1][3] << 16 |
		color_reg->SKIN_TONE_S[SP1][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT1_2,
		(color_reg->SKIN_TONE_S[SP1][5] |
		color_reg->SKIN_TONE_S[SP1][6] << 8 |
		color_reg->SKIN_TONE_S[SP1][7] << 16 |
		color_reg->GRASS_TONE_S[SP1][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT1_3,
		(color_reg->GRASS_TONE_S[SP1][1] |
		color_reg->GRASS_TONE_S[SP1][2] << 8 |
		color_reg->GRASS_TONE_S[SP1][3] << 16 |
		color_reg->GRASS_TONE_S[SP1][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT1_4,
		(color_reg->GRASS_TONE_S[SP1][5] |
		color_reg->SKY_TONE_S[SP1][0] << 8 |
		color_reg->SKY_TONE_S[SP1][1] << 16 |
		color_reg->SKY_TONE_S[SP1][2] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT2_0,
		(color_reg->PURP_TONE_S[SP2][0] |
		color_reg->PURP_TONE_S[SP2][1] << 8 |
		color_reg->PURP_TONE_S[SP2][2] << 16 |
		color_reg->SKIN_TONE_S[SP2][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT2_1,
		(color_reg->SKIN_TONE_S[SP2][1] |
		color_reg->SKIN_TONE_S[SP2][2] << 8 |
		color_reg->SKIN_TONE_S[SP2][3] << 16 |
		color_reg->SKIN_TONE_S[SP2][4] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT2_2,
		(color_reg->SKIN_TONE_S[SP2][5] |
		color_reg->SKIN_TONE_S[SP2][6] << 8 |
		color_reg->SKIN_TONE_S[SP2][7] << 16 |
		color_reg->GRASS_TONE_S[SP2][0] << 24), ~0);

	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT2_3,
		(color_reg->GRASS_TONE_S[SP2][1] |
		color_reg->GRASS_TONE_S[SP2][2] << 8 |
		color_reg->GRASS_TONE_S[SP2][3] << 16 |
		color_reg->GRASS_TONE_S[SP2][4] << 24), ~0);


	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_PART_SAT_POINT2_4,
		(color_reg->GRASS_TONE_S[SP2][5] |
		color_reg->SKY_TONE_S[SP2][0] << 8 |
		color_reg->SKY_TONE_S[SP2][1] << 16 |
		color_reg->SKY_TONE_S[SP2][2] << 24), ~0);

	for (index = 0; index < 3; index++) {
		h_series[index + PURP_TONE_START] =
			color_reg->PURP_TONE_H[index];
	}

	for (index = 0; index < 8; index++) {
		h_series[index + SKIN_TONE_START] =
		    color_reg->SKIN_TONE_H[index];
	}

	for (index = 0; index < 6; index++) {
		h_series[index + GRASS_TONE_START] =
			color_reg->GRASS_TONE_H[index];
	}

	for (index = 0; index < 3; index++) {
		h_series[index + SKY_TONE_START] =
		    color_reg->SKY_TONE_H[index];
	}

	for (index = 0; index < 5; index++) {
		u4Temp = (h_series[4 * index]) +
		    (h_series[4 * index + 1] << 8) +
		    (h_series[4 * index + 2] << 16) +
		    (h_series[4 * index + 3] << 24);
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_LOCAL_HUE_CD_0 + 4 * index,
			u4Temp, ~0);
	}

	if (color->data->support_color21 == true) {
		/* S Gain By Y */
		u4Temp = 0;

		reg_index = 0;
		for (i = 0; i < S_GAIN_BY_Y_CONTROL_CNT; i++) {
			for (j = 0; j < S_GAIN_BY_Y_HUE_PHASE_CNT; j += 4) {
				u4Temp = (g_Color_Index.S_GAIN_BY_Y[i][j]) +
					(g_Color_Index.S_GAIN_BY_Y[i][j + 1]
					<< 8) +
					(g_Color_Index.S_GAIN_BY_Y[i][j + 2]
					<< 16) +
					(g_Color_Index.S_GAIN_BY_Y[i][j + 3]
					<< 24);

				cmdq_pkt_write(handle, comp->cmdq_base,
					comp->regs_pa +
					DISP_COLOR_S_GAIN_BY_Y0_0 +
					reg_index,
					u4Temp, ~0);
				reg_index += 4;
			}
		}
		/* LSP */
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_LSP_1,
			(g_Color_Index.LSP[3] << 0) |
			(g_Color_Index.LSP[2] << 7) |
			(g_Color_Index.LSP[1] << 14) |
			(g_Color_Index.LSP[0] << 22), 0x1FFFFFFF);
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_LSP_2,
			(g_Color_Index.LSP[7] << 0) |
			(g_Color_Index.LSP[6] << 8) |
			(g_Color_Index.LSP[5] << 16) |
			(g_Color_Index.LSP[4] << 23), 0x3FFF7F7F);
	}

	/* color window */
	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_TWO_D_WINDOW_1,
		color->data->color_window, ~0);

	if (color->data->support_color30 == true) {
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_CM_CONTROL,
			0x0 |
			(0x3 << 1) |	/* enable window 1 */
			(0x3 << 4) |	/* enable window 2 */
			(0x3 << 7)		/* enable window 3 */
			, 0x1B7);

		for (i = 0; i < WIN_TOTAL; i++) {
			reg_index = i * 4 * (LUT_TOTAL * 5);
			for (j = 0; j < LUT_TOTAL; j++) {
				cmdq_pkt_write(handle, comp->cmdq_base,
					comp->regs_pa + DISP_COLOR_CM_W1_HUE_0 +
					reg_index,
					color_reg->COLOR_3D[i]
					[j*LUT_REG_TOTAL+REG_L] |
					(color_reg->COLOR_3D[i]
					[j*LUT_REG_TOTAL+REG_U] << 10) |
					(color_reg->COLOR_3D[i]
					[j*LUT_REG_TOTAL+REG_POINT0] << 20),
					~0);
				cmdq_pkt_write(handle, comp->cmdq_base,
					comp->regs_pa + DISP_COLOR_CM_W1_HUE_1 +
					reg_index,
					color_reg->COLOR_3D[i]
					[j*LUT_REG_TOTAL+REG_POINT1] |
					(color_reg->COLOR_3D[i]
					[j*LUT_REG_TOTAL+REG_POINT2] << 10) |
					(color_reg->COLOR_3D[i]
					[j*LUT_REG_TOTAL+REG_POINT3] << 20),
					~0);
				cmdq_pkt_write(handle, comp->cmdq_base,
					comp->regs_pa + DISP_COLOR_CM_W1_HUE_2 +
					reg_index,
					color_reg->COLOR_3D[i]
					[j*LUT_REG_TOTAL+REG_POINT4] |
					(color_reg->COLOR_3D[i]
					[j*LUT_REG_TOTAL+REG_SLOPE0] << 10) |
					(color_reg->COLOR_3D[i]
					[j*LUT_REG_TOTAL+REG_SLOPE1] << 20),
					~0);
				cmdq_pkt_write(handle, comp->cmdq_base,
					comp->regs_pa + DISP_COLOR_CM_W1_HUE_3 +
					reg_index,
					color_reg->COLOR_3D[i]
					[j*LUT_REG_TOTAL+REG_SLOPE2] |
					(color_reg->COLOR_3D[i]
					[j*LUT_REG_TOTAL+REG_SLOPE3] << 8) |
					(color_reg->COLOR_3D[i]
					[j*LUT_REG_TOTAL+REG_SLOPE4] << 16) |
					(color_reg->COLOR_3D[i]
					[j*LUT_REG_TOTAL+REG_SLOPE5] << 24),
					~0);
				cmdq_pkt_write(handle, comp->cmdq_base,
					comp->regs_pa + DISP_COLOR_CM_W1_HUE_4 +
					reg_index,
					color_reg->COLOR_3D[i]
					[j*LUT_REG_TOTAL+REG_WGT_LSLOPE] |
					(color_reg->COLOR_3D[i]
					[j*LUT_REG_TOTAL+REG_WGT_USLOPE] << 16),
					~0);

				reg_index += (4 * 5);
			}
		}
	}
}
static void mtk_color_config(struct mtk_ddp_comp *comp,
			     struct mtk_ddp_config *cfg,
			     struct cmdq_pkt *handle)
{
	struct mtk_disp_color *color = comp_to_color(comp);

	cmdq_pkt_write(handle, comp->cmdq_base,
		       comp->regs_pa + DISP_COLOR_WIDTH(color), cfg->w, ~0);
	cmdq_pkt_write(handle, comp->cmdq_base,
		       comp->regs_pa + DISP_COLOR_HEIGHT(color), cfg->h, ~0);
}

static void ddp_color_bypass_color(struct mtk_ddp_comp *comp, int bypass,
		struct cmdq_pkt *handle)
{

	g_color_bypass[index_of_color(comp->id)] = bypass;

	if (bypass) {
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_CFG_MAIN,
			(1 << 7), 0xFF); /* bypass all */
	} else {
		cmdq_pkt_write(handle, comp->cmdq_base,
			comp->regs_pa + DISP_COLOR_CFG_MAIN,
			(0 << 7), 0xFF); /* resume all */
	}
}

static int color_is_reg_addr_valid(struct mtk_ddp_comp *comp,
	unsigned long addr)
{
	unsigned int i = 0;
	unsigned long reg_addr;
	struct mtk_disp_color *color = comp_to_color(comp);

	if (addr == 0) {
		DDPPR_ERR("addr is NULL\n");
		return 0;
	}

	if ((addr & 0x3) != 0) {
		DDPPR_ERR("addr is not 4-byte aligned!\n");
		return 0;
	}

	for (i = 0; i < color->data->reg_num; i++) {
		reg_addr = color->data->reg_table[i].reg_base;
		if (addr >= reg_addr && addr < reg_addr + 0x1000)
			break;
	}

	if (i < color->data->reg_num) {
		DDPINFO("addr valid, addr=0x%08lx, module=%s\n",
			addr, color->data->reg_table[i].name);
		return i;
	}

	DDPPR_ERR("invalid address! addr=0x%lx!\n", addr);
	return -1;
}

int mtk_drm_ioctl_set_pqparam(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	int ret = 0;
	struct mtk_drm_private *private = dev->dev_private;
	struct mtk_ddp_comp *comp = private->ddp_comp[DDP_COMPONENT_COLOR0];
	struct drm_crtc *crtc = private->crtc[0];
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	int id = index_of_color(comp->id);
	struct DISP_PQ_PARAM *pq_param;

	pq_param = get_Color_config(id);
	memcpy(pq_param, (struct DISP_PQ_PARAM *)data,
		sizeof(struct DISP_PQ_PARAM));

	if (ncs_tuning_mode == 0) {
		/* normal mode */
		ret = mtk_crtc_user_cmd(crtc, comp, SET_PQPARAM, data);
		mtk_crtc_check_trigger(mtk_crtc, false);

		DDPINFO("SET_PQ_PARAM\n");
	} else {
		/* ncs_tuning_mode = 0; */
		DDPINFO
		 ("SET_PQ_PARAM, bypassed by ncs_tuning_mode = 1\n");
	}

	return ret;
}

int mtk_drm_ioctl_set_pqindex(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	int ret = 0;
	struct DISPLAY_PQ_T *pq_index;

	DDPINFO("%s...", __func__);

	pq_index = get_Color_index();
	memcpy(pq_index, (struct DISPLAY_PQ_T *)data,
		sizeof(struct DISPLAY_PQ_T));

	return ret;
}

int mtk_drm_ioctl_set_color_reg(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct mtk_drm_private *private = dev->dev_private;
	struct mtk_ddp_comp *comp = private->ddp_comp[DDP_COMPONENT_COLOR0];
	struct drm_crtc *crtc = private->crtc[0];

	return mtk_crtc_user_cmd(crtc, comp, SET_COLOR_REG, data);
}

int mtk_drm_ioctl_mutex_control(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	int ret = 0;
	unsigned int *value = data;
	struct mtk_drm_private *private = dev->dev_private;
	/* primary display */
	struct drm_crtc *crtc = private->crtc[0];
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);

	DDPINFO("%s...", __func__);

	if (*value == 1) {
		ncs_tuning_mode = 1;
		DDPINFO("ncs_tuning_mode = 1\n");
	} else if (*value == 2) {

		ncs_tuning_mode = 0;
		DDPINFO("ncs_tuning_mode = 0\n");

		mtk_crtc_check_trigger(mtk_crtc, false);
	} else {
		DDPPR_ERR("DISP_IOCTL_MUTEX_CONTROL invalid control\n");
		return -EFAULT;
	}

	return ret;
}

int mtk_drm_ioctl_read_reg(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	int ret = 0;

	struct DISP_READ_REG *rParams = data;
	void __iomem *va = 0;
	unsigned int pa;
	/* TODO: dual pipe */
	struct mtk_drm_private *private = dev->dev_private;
	struct mtk_ddp_comp *comp = private->ddp_comp[DDP_COMPONENT_COLOR0];
	unsigned long flags;

	pa = (unsigned int)rParams->reg;

	if (color_is_reg_addr_valid(comp, pa) < 0) {
		DDPPR_ERR("reg read, addr invalid, pa:0x%x\n", pa);
		return -EFAULT;
	}

	va = ioremap_nocache(pa, sizeof(va));

	DDPDBG("%s @ %d......... spin_trylock_irqsave ++ ",
		__func__, __LINE__);
	if (spin_trylock_irqsave(&g_color_clock_lock, flags)) {
		DDPDBG("%s @ %d......... spin_trylock_irqsave -- ",
			__func__, __LINE__);
		rParams->val = readl(va) & rParams->mask;
		spin_unlock_irqrestore(&g_color_clock_lock, flags);
	} else {
		DDPINFO("%s @ %d......... Failed to spin_trylock_irqsave ",
			__func__, __LINE__);
	}

	DDPINFO("read pa:0x%x(va:0x%lx) = 0x%x (0x%x)\n",
		pa,
		(long)va,
		rParams->val,
		rParams->mask);

	return ret;
}

int mtk_drm_ioctl_write_reg(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct mtk_drm_private *private = dev->dev_private;
	struct mtk_ddp_comp *comp = private->ddp_comp[DDP_COMPONENT_COLOR0];
	struct drm_crtc *crtc = private->crtc[0];
	struct DISP_WRITE_REG *wParams = data;
	unsigned int pa = (unsigned int)wParams->reg;

	if (color_is_reg_addr_valid(comp, pa) < 0) {
		DDPPR_ERR("reg write, addr invalid, pa:0x%x\n", pa);
		return -EFAULT;
	}

	return mtk_crtc_user_cmd(crtc, comp, WRITE_REG, data);
}

int mtk_drm_ioctl_bypass_color(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	int ret = 0;
	struct mtk_drm_private *private = dev->dev_private;
	struct mtk_ddp_comp *comp = private->ddp_comp[DDP_COMPONENT_COLOR0];
	struct drm_crtc *crtc = private->crtc[0];
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);

	ret = mtk_crtc_user_cmd(crtc, comp, BYPASS_COLOR, data);
	mtk_crtc_check_trigger(mtk_crtc, false);

	return ret;
}

int mtk_drm_ioctl_pq_set_window(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	int ret = 0;
	struct mtk_drm_private *private = dev->dev_private;
	struct mtk_ddp_comp *comp = private->ddp_comp[DDP_COMPONENT_COLOR0];
	struct drm_crtc *crtc = private->crtc[0];
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);

	DDPINFO("%s..., id=%d, en=%d, x=0x%x, y=0x%x\n",
		__func__, comp->id, g_split_en,
		((g_split_window_x_end << 16) | g_split_window_x_start),
		((g_split_window_y_end << 16) | g_split_window_y_start));

	ret = mtk_crtc_user_cmd(crtc, comp, PQ_SET_WINDOW, data);
	mtk_crtc_check_trigger(mtk_crtc, false);

	return ret;
}


static void mtk_color_start(struct mtk_ddp_comp *comp, struct cmdq_pkt *handle)
{
	//struct mtk_disp_color *color = comp_to_color(comp);
	int ret;

	ret = pm_runtime_get_sync(comp->dev);
	if (ret < 0)
		DRM_ERROR("Failed to enable power domain: %d\n", ret);

	DpEngine_COLORonInit(comp, handle);

	mutex_lock(&g_color_reg_lock);
	if (g_color_reg_valid) {
		color_write_hw_reg(comp, &g_color_reg, handle);
		mutex_unlock(&g_color_reg_lock);
	} else {
		mutex_unlock(&g_color_reg_lock);
		DpEngine_COLORonConfig(comp, handle);
	}
	/*
	cmdq_pkt_write(handle, comp->cmdq_base,
		       comp->regs_pa + DISP_COLOR_CFG_MAIN,
		       COLOR_BYPASS_ALL | COLOR_SEQ_SEL, ~0);
	cmdq_pkt_write(handle, comp->cmdq_base,
		       comp->regs_pa + DISP_COLOR_START(color), 0x1, ~0);
	*/
}

static void mtk_color_stop(struct mtk_ddp_comp *comp, struct cmdq_pkt *handle)
{
	int ret;

	ret = pm_runtime_put(comp->dev);
	if (ret < 0)
		DRM_ERROR("Failed to disable power domain: %d\n", ret);
}

static void mtk_color_bypass(struct mtk_ddp_comp *comp, struct cmdq_pkt *handle)
{
	struct mtk_disp_color *color = comp_to_color(comp);

	cmdq_pkt_write(handle, comp->cmdq_base,
		       comp->regs_pa + DISP_COLOR_CFG_MAIN,
		       COLOR_BYPASS_ALL | COLOR_SEQ_SEL, ~0);

	/* disable R2Y/Y2R in Color Wrapper */
	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_CM1_EN(color), 0, 0x1);
	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_CM2_EN(color), 0, 0x1);
	cmdq_pkt_write(handle, comp->cmdq_base,
		comp->regs_pa + DISP_COLOR_START(color), 0x3, 0x3);

	/*
	 * writel(0, comp->regs + DISP_COLOR_CM1_EN);
	 * writel(0, comp->regs + DISP_COLOR_CM2_EN);
	 * writel(0x1, comp->regs + DISP_COLOR_START(color));
	 */
}

static int mtk_color_user_cmd(struct mtk_ddp_comp *comp,
	struct cmdq_pkt *handle, unsigned int cmd, void *data)
{
	DDPINFO("%s: cmd: %d\n", __func__, cmd);
	switch (cmd) {
	case SET_PQPARAM:
	{
		/* normal mode */
		DpEngine_COLORonInit(comp, handle);
		DpEngine_COLORonConfig(comp, handle);
	}
	break;
	case SET_COLOR_REG:
	{
		mutex_lock(&g_color_reg_lock);

		if (data != NULL) {
			memcpy(&g_color_reg, (struct DISPLAY_COLOR_REG *)data,
				sizeof(struct DISPLAY_COLOR_REG));

			color_write_hw_reg(comp, &g_color_reg, handle);
		} else {
			DDPINFO("%s: data is NULL", __func__);
		}

		g_color_reg_valid = 1;
		mutex_unlock(&g_color_reg_lock);
	}
	break;
	case WRITE_REG:
	{
		struct DISP_WRITE_REG *wParams = data;
		void __iomem *va = 0;
		unsigned int pa = (unsigned int)wParams->reg;

		cmdq_pkt_write(handle, comp->cmdq_base,
			pa, wParams->val, wParams->mask);

		DDPINFO("write pa:0x%x(va:0x%lx) = 0x%x (0x%x)\n", pa, (long)va,
			wParams->val, wParams->mask);
	}
	break;
	case BYPASS_COLOR:
	{
		unsigned int *value = data;

		ddp_color_bypass_color(comp, *value, handle);
	}
	break;
	case PQ_SET_WINDOW:
	{
		struct DISP_PQ_WIN_PARAM *win_param = data;

		ddp_color_set_window(comp, win_param, handle);
	}
	break;
	default:
		DDPPR_ERR("%s: error cmd: %d\n", __func__, cmd);
		return -EINVAL;
	}
	return 0;
}

static void mtk_color_prepare(struct mtk_ddp_comp *comp)
{
	mtk_ddp_comp_clk_prepare(comp);
	atomic_set(&g_color_is_clock_on[index_of_color(comp->id)], 1);
}

static void mtk_color_unprepare(struct mtk_ddp_comp *comp)
{
	unsigned long flags;

	DDPINFO("%s @ %d......... spin_lock_irqsave ++ ", __func__, __LINE__);
	spin_lock_irqsave(&g_color_clock_lock, flags);
	DDPINFO("%s @ %d......... spin_lock_irqsave -- ", __func__, __LINE__);
	atomic_set(&g_color_is_clock_on[index_of_color(comp->id)], 0);
	spin_unlock_irqrestore(&g_color_clock_lock, flags);
	DDPINFO("%s @ %d......... spin_unlock_irqrestore ", __func__, __LINE__);
	mtk_ddp_comp_clk_unprepare(comp);
}

static const struct mtk_ddp_comp_funcs mtk_disp_color_funcs = {
	.config = mtk_color_config,
	.start = mtk_color_start,
	.stop = mtk_color_stop,
	.bypass = mtk_color_bypass,
	.user_cmd = mtk_color_user_cmd,
	.prepare = mtk_color_prepare,
	.unprepare = mtk_color_unprepare,
};

void mtk_color_dump(struct mtk_ddp_comp *comp)
{
	void __iomem *baddr = comp->regs;

	DDPDUMP("== %s REGS ==\n", mtk_dump_comp_str(comp));
	mtk_serial_dump_reg(baddr, 0x400, 3);
	mtk_serial_dump_reg(baddr, 0xC50, 2);
}

static int mtk_disp_color_bind(struct device *dev, struct device *master,
			       void *data)
{
	struct mtk_disp_color *priv = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	int ret;

	ret = mtk_ddp_comp_register(drm_dev, &priv->ddp_comp);
	if (ret < 0) {
		dev_err(dev, "Failed to register component %s: %d\n",
			dev->of_node->full_name, ret);
		return ret;
	}

	return 0;
}

static void mtk_disp_color_unbind(struct device *dev, struct device *master,
				  void *data)
{
	struct mtk_disp_color *priv = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;

	mtk_ddp_comp_unregister(drm_dev, &priv->ddp_comp);
}

static const struct component_ops mtk_disp_color_component_ops = {
	.bind	= mtk_disp_color_bind,
	.unbind = mtk_disp_color_unbind,
};

static int mtk_disp_color_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_disp_color *priv;
	enum mtk_ddp_comp_id comp_id;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	comp_id = mtk_ddp_comp_get_id(dev->of_node, MTK_DISP_COLOR);
	if ((int)comp_id < 0) {
		dev_err(dev, "Failed to identify by alias: %d\n", comp_id);
		return comp_id;
	}

	ret = mtk_ddp_comp_init(dev, dev->of_node, &priv->ddp_comp, comp_id,
				&mtk_disp_color_funcs);
	if (ret != 0) {
		dev_err(dev, "Failed to initialize component: %d\n", ret);
		return ret;
	}

	priv->data = of_device_get_match_data(dev);

	platform_set_drvdata(pdev, priv);

	pm_runtime_enable(dev);

	ret = component_add(dev, &mtk_disp_color_component_ops);
	if (ret != 0) {
		dev_err(dev, "Failed to add component: %d\n", ret);
		pm_runtime_disable(dev);
	}

	return ret;
}

static int mtk_disp_color_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_color_component_ops);

	pm_runtime_disable(&pdev->dev);
	return 0;
}

static const struct mtk_disp_color_data mt2701_color_driver_data = {
	.color_offset = DISP_COLOR_START_MT2701,
	.support_color21 = false,
	.support_color30 = false,
	.reg_num = 0,
	.color_window = 0x40106051,
};

#if 0
static const struct mtk_pq_reg_table mt6779_pq_reg_table[PQ_MODULE_NUM] = {
	{ "disp_color0", 0x1400E000},
	{ "disp_ccorr0", 0x1400F000},
	{ "disp_aal0", 0x14001000},
	{ "disp_gamma0", 0x14011000},
	{ "disp_dither0", 0x14012000},
	{ "mdp_rsz0", 0x14003000},
	{ "mdp_rsz1", 0x14004000},
	{ "mdp_aal0", 0x1401B000},
	{ "mdp_hdr0", 0x1401C000},
};
#endif

static const struct mtk_disp_color_data mt6779_color_driver_data = {
	.color_offset = DISP_COLOR_START_MT6779,
	.support_color21 = true,
	.support_color30 = true,
	.reg_table = {
		{ "disp_color0", 0x1400E000},
		{ "disp_ccorr0", 0x1400F000},
		{ "disp_aal0", 0x14001000},
		{ "disp_gamma0", 0x14011000},
		{ "disp_dither0", 0x14012000},
		{ "mdp_rsz0", 0x14003000},
		{ "mdp_rsz1", 0x14004000},
		{ "mdp_aal0", 0x1401B000},
		{ "mdp_hdr0", 0x1401C000}
	},
	.reg_num = 9,
	.color_window = 0x40185E57,
};

static const struct mtk_disp_color_data mt8173_color_driver_data = {
	.color_offset = DISP_COLOR_START_MT8173,
	.support_color21 = false,
	.support_color30 = false,
	.reg_num = 0,
	.color_window = 0x40106051,
};

static const struct mtk_disp_color_data mt6885_color_driver_data = {
	.color_offset = DISP_COLOR_START_MT6885,
	.support_color21 = true,
	.support_color30 = true,
	.reg_table = {
		{ "disp_color0", 0x14007000},
		{ "disp_ccorr0", 0x14008000},
		{ "disp_aal0", 0x14009000},
		{ "disp_gamma0", 0x1400A000},
		{ "disp_dither0", 0x1400B000},
		{ "mdp_rsz0", 0x1F012000},
		{ "mdp_rsz1", 0x1F013000},
		{ "mdp_aal0", 0x1F00C000},
		{ "mdp_hdr0", 0x1F010000}
	},
	.reg_num = 9,
	.color_window = 0x40185E57,
};

static const struct of_device_id mtk_disp_color_driver_dt_match[] = {
	{.compatible = "mediatek,mt2701-disp-color",
	 .data = &mt2701_color_driver_data},
	{.compatible = "mediatek,mt6779-disp-color",
	 .data = &mt6779_color_driver_data},
	{.compatible = "mediatek,mt6885-disp-color",
	 .data = &mt6885_color_driver_data},
	{.compatible = "mediatek,mt8173-disp-color",
	 .data = &mt8173_color_driver_data},
	{},
};
MODULE_DEVICE_TABLE(of, mtk_disp_color_driver_dt_match);

struct platform_driver mtk_disp_color_driver = {
	.probe = mtk_disp_color_probe,
	.remove = mtk_disp_color_remove,
	.driver = {
			.name = "mediatek-disp-color",
			.owner = THIS_MODULE,
			.of_match_table = mtk_disp_color_driver_dt_match,
		},
};

