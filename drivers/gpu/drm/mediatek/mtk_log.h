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

#ifndef __MTKFB_LOG_H
#define __MTKFB_LOG_H

#include <linux/kernel.h>

enum DPREC_LOGGER_PR_TYPE {
	DPREC_LOGGER_ERROR,
	DPREC_LOGGER_FENCE,
	DPREC_LOGGER_DEBUG,
	DPREC_LOGGER_DUMP,
	DPREC_LOGGER_STATUS,
	DPREC_LOGGER_PR_NUM
};

int mtk_dprec_logger_pr(unsigned int type, char *fmt, ...);

#define DDPINFO(fmt, arg...)                                                   \
	do {                                                                   \
		mtk_dprec_logger_pr(DPREC_LOGGER_DEBUG, fmt, ##arg);           \
		if (g_mobile_log)                                              \
			pr_info(pr_fmt(fmt), ##arg);     \
	} while (0)

#define DDPDBG(fmt, arg...)                                                    \
	do {                                                                   \
		if (!g_detail_log)                                             \
			break;                                                 \
		mtk_dprec_logger_pr(DPREC_LOGGER_DEBUG, fmt, ##arg);           \
		if (g_mobile_log)                                              \
			pr_info(pr_fmt(fmt), ##arg);     \
	} while (0)

#define DDPMSG(fmt, arg...)                                                    \
	do {                                                                   \
		mtk_dprec_logger_pr(DPREC_LOGGER_DEBUG, fmt, ##arg);           \
		pr_info(pr_fmt(fmt), ##arg);             \
	} while (0)

#define DDPDUMP(fmt, arg...)                                                   \
	do {                                                                   \
		mtk_dprec_logger_pr(DPREC_LOGGER_DUMP, fmt, ##arg);            \
		if (g_mobile_log)                                              \
			pr_info(pr_fmt(fmt), ##arg);     \
	} while (0)

#define DDPFENCE(fmt, arg...)                                                  \
	do {                                                                   \
		mtk_dprec_logger_pr(DPREC_LOGGER_FENCE, fmt, ##arg);           \
		if (g_fence_log)                                               \
			pr_info(pr_fmt(fmt), ##arg);     \
	} while (0)

#define DDPPR_ERR(fmt, arg...)                                                 \
	do {                                                                   \
		mtk_dprec_logger_pr(DPREC_LOGGER_ERROR, fmt, ##arg);           \
		pr_err(pr_fmt(fmt), ##arg);              \
	} while (0)

#define DDPIRQ(fmt, arg...)                                                    \
	do {                                                                   \
		if (g_irq_log)                                                 \
			mtk_dprec_logger_pr(DPREC_LOGGER_DEBUG, fmt, ##arg);   \
	} while (0)

#ifdef CONFIG_MTK_AEE_FEATURE
#define DDPAEE(string, args...)                                                \
	do {                                                                   \
		char str[200];                                                 \
		snprintf(str, 199, "DDP:" string, ##args);                     \
		aee_kernel_warning_api(__FILE__, __LINE__,                     \
				       DB_OPT_DEFAULT |                        \
					       DB_OPT_MMPROFILE_BUFFER,        \
				       str, string, ##args);                   \
		DDPPR_ERR("[DDP Error]" string, ##args);                       \
	} while (0)
#else /* !CONFIG_MTK_AEE_FEATURE */
#define DDPAEE(string, args...)                                                \
	do {                                                                   \
		char str[200];                                                 \
		snprintf(str, 199, "DDP:" string, ##args);                     \
		pr_err("[DDP Error]" string, ##args);                          \
	} while (0)
#endif /* CONFIG_MTK_AEE_FEATURE */

extern bool g_mobile_log;
extern bool g_fence_log;
extern bool g_irq_log;
extern bool g_detail_log;
#endif
