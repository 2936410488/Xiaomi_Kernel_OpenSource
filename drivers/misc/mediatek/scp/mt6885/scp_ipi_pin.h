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

#ifndef _SCP_IPI_PIN_H_
#define _SCP_IPI_PIN_H_

#include <mt-plat/mtk_tinysys_ipi.h>

/* scp awake timeout count definition */
#define SCP_AWAKE_TIMEOUT 100000
/* scp Core ID definition */
enum scp_core_id {
	SCP_A_ID = 0,
	SCP_CORE_TOTAL = 1,
};

enum {
/* core0 */
	/* the following will use mbox0 */
	IPI_OUT_CHRE_0		   =  0,
	IPI_OUT_CHREX_0		   =  1,
	IPI_OUT_SENSOR_0	   =  2,
	/* the following will use mbox1 */
	IPI_OUT_APCCCI_0	   =  3,
	IPI_OUT_DVFS_SLEEP_0	   =  4,
	IPI_OUT_DVFS_SET_FREQ_0	   =  5,
	IPI_OUT_TEST_0		   =  6,
	IPI_OUT_LOGGER_ENABLE_0    =  7,
	IPI_OUT_LOGGER_WAKEUP_0    =  8,
	IPI_OUT_LOGGER_INIT_0	   =  9,
	IPI_OUT_SCPCTL_0	   = 10,
	IPI_OUT_SCP_LOG_FILTER_0   = 11,
	IPI_IN_CHRE_0		   = 12,
	IPI_IN_SENSOR_0		   = 13,
	IPI_IN_APCCCI_0		   = 14,
	IPI_IN_SCP_ERROR_INFO_0	   = 15,
	IPI_IN_LOGGER_WAKEUP_0	   = 16,
	IPI_IN_LOGGER_INIT_0	   = 17,
	IPI_IN_SCP_READY_0	   = 18,
	IPI_IN_SCP_RAM_DUMP_0	   = 19,

/* core1 */
	/* the following will use mbox3 */
	IPI_OUT_AUDIO_VOW_1	   = 20,
	IPI_OUT_AUDIO_ULTRA_SND_1  = 21,
	IPI_OUT_DVFS_SLEEP_1	   = 22,
	IPI_OUT_DVFS_SET_FREQ_1	   = 23,
	IPI_OUT_TEST_1		   = 24,
	IPI_IN_AUDIO_VOW_1	   = 25,
	IPI_IN_AUDIO_ULTRA_SND_1   = 26,
	IPI_IN_SCP_READY_1	   = 27,
	IPI_IN_SCP_RAM_DUMP_1	   = 28,
	SCP_IPI_COUNT
};

extern struct mtk_mbox_device scp_mboxdev;
extern struct mtk_ipi_device scp_ipidev;

extern char *core_ids[SCP_CORE_TOTAL];

extern void scp_reset_awake_counts(void);
extern int scp_awake_lock(enum scp_core_id scp_id);
extern int scp_awake_unlock(enum scp_core_id scp_id);
extern int scp_awake_counts[];

extern unsigned int is_scp_ready(enum scp_core_id scp_id);

#endif
