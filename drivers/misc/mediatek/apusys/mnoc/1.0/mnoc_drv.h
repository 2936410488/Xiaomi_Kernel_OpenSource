/*
 * Copyright (C) 2019 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */
#ifndef __APUSYS_MNOC_DRV_H__
#define __APUSYS_MNOC_DRV_H__

#define MNOC_TIME_PROFILE (1)
#define MNOC_INT_ENABLE (1)
#define MNOC_QOS_ENABLE (0)
#define MNOC_DBG_ENABLE (1)

#define APUSYS_MNOC_DEV_NAME "apusys_mnoc"

#define APUSYS_MNOC_LOG_PREFIX "[apusys][mnoc]"
#define LOG_ERR(x, args...) \
pr_info(APUSYS_MNOC_LOG_PREFIX "[error] %s " x, __func__, ##args)
#define LOG_WARN(x, args...) \
pr_info(APUSYS_MNOC_LOG_PREFIX "[warn] %s " x, __func__, ##args)
#define LOG_INFO(x, args...) \
pr_info(APUSYS_MNOC_LOG_PREFIX "[info] %s " x, __func__, ##args)
#define DEBUG_TAG LOG_DEBUG("\n")

#define LOG_DEBUG(x, args...) \
	{ \
		if (mnoc_log_level > 0) \
			pr_info(APUSYS_MNOC_LOG_PREFIX "[debug] %s/%d "\
			x, __func__, __LINE__, ##args); \
	}

extern void __iomem *mnoc_base;
extern void __iomem *mnoc_int_base;
extern void __iomem *mnoc_apu_conn_base;
extern void __iomem *mnoc_slp_prot_base1;
extern void __iomem *mnoc_slp_prot_base2;
extern spinlock_t mnoc_spinlock;
extern bool mnoc_reg_valid;

extern int mnoc_log_level;


#endif
