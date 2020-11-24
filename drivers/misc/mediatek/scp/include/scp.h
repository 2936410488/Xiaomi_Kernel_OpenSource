/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#ifndef __SCP_H__
#define __SCP_H__

#include <linux/soc/mediatek/mtk_tinysys_ipi.h>

#define SCP_MBOX_TOTAL 5

/* core1 */
/* definition of slot size for send PINs */
#define PIN_OUT_SIZE_AUDIO_VOW_1         9 /* the following will use mbox 0 */

/* definition of slot size for received PINs */
#define PIN_IN_SIZE_AUDIO_VOW_ACK_1      2 /* the following will use mbox 0 */
#define PIN_IN_SIZE_AUDIO_VOW_1         26 /* the following will use mbox 0 */

/* core0 */
/* definition of slot size for send PINs */
#define PIN_OUT_SIZE_APCCCI_0		 2 /* the following will use mbox 1 */
#define PIN_OUT_SIZE_DVFS_SET_FREQ_0	 1 /* the following will use mbox 1 */
#define PIN_OUT_C_SIZE_SLEEP_0           2 /* the following will use mbox 1 */
#define PIN_OUT_R_SIZE_SLEEP_0           1 /* the following will use mbox 1 */
#define PIN_OUT_SIZE_TEST_0		 1 /* the following will use mbox 1 */

/* definition of slot size for received PINs */
#define PIN_IN_SIZE_APCCCI_0		 2 /* the following will use mbox 1 */
#define PIN_IN_SIZE_SCP_ERROR_INFO_0    10 /* the following will use mbox 1 */
#define PIN_IN_SIZE_SCP_READY_0		 1 /* the following will use mbox 1 */
#define PIN_IN_SIZE_SCP_RAM_DUMP_0	 2 /* the following will use mbox 1 */
/* ============================================================ */

/* core1 */
/* definition of slot size for send PINs */
#define PIN_OUT_SIZE_AUDIO_ULTRA_SND_1	 2 /* the following will use mbox 3 */
#define PIN_OUT_SIZE_DVFS_SET_FREQ_1	 1 /* the following will use mbox 3 */
#define PIN_OUT_C_SIZE_SLEEP_1	         2 /* the following will use mbox 3 */
#define PIN_OUT_R_SIZE_SLEEP_1	         1 /* the following will use mbox 3 */
#define PIN_OUT_SIZE_TEST_1		 1 /* the following will use mbox 3 */
#define PIN_OUT_SIZE_LOGGER_CTRL	 6 /* the following will use mbox 3 */
#define PIN_OUT_SIZE_SCPCTL_1		 2 /* the following will use mbox 3 */

/* definition of slot size for received PINs */
#define PIN_IN_SIZE_AUDIO_ULTRA_SND_1	 2 /* the following will use mbox 3 */
#define PIN_IN_SIZE_SCP_ERROR_INFO_1	10 /* the following will use mbox 3 */
#define PIN_IN_SIZE_LOGGER_CTRL		 6 /* the following will use mbox 3 */
#define PIN_IN_SIZE_LOGGER_INIT_1	 5 /* the following will use mbox 3 */
#define PIN_IN_SIZE_SCP_READY_1		 1 /* the following will use mbox 3 */
#define PIN_IN_SIZE_SCP_RAM_DUMP_1	 2 /* the following will use mbox 3 */
/* ============================================================ */

/* this is mbox pool for 2 cores */
#define PIN_OUT_SIZE_SCP_MPOOL          34 /* the following will use mbox 2,4 */
#define PIN_IN_SIZE_SCP_MPOOL           30 /* the following will use mbox 2,4 */

/* scp Core ID definition */
enum scp_core_id {
	SCP_A_ID = 0,
	SCP_CORE_TOTAL = 1,
};

enum {
/* core1 */
	/* the following will use mbox0 */
	IPI_OUT_AUDIO_VOW_1       =  0,
	IPI_IN_AUDIO_VOW_ACK_1	  =  1,
	IPI_IN_AUDIO_VOW_1        =  2,

/* core0 */
	/* the following will use mbox1 */
	IPI_OUT_APCCCI_0          =  3,
	IPI_OUT_DVFS_SET_FREQ_0	  =  4,
	IPI_OUT_C_SLEEP_0         =  5,
	IPI_OUT_TEST_0            =  6,
	IPI_IN_APCCCI_0           =  7,
	IPI_IN_SCP_ERROR_INFO_0   =  8,
	IPI_IN_SCP_READY_0        =  9,
	IPI_IN_SCP_RAM_DUMP_0     = 10,

	/* the following will use mbox2 */
	IPI_OUT_SCP_MPOOL_0       = 11,
	IPI_IN_SCP_MPOOL_0        = 12,

/* core1 */
	/* the following will use mbox3 */
	IPI_OUT_AUDIO_ULTRA_SND_1 = 13,
	IPI_OUT_DVFS_SET_FREQ_1   = 14,
	IPI_OUT_C_SLEEP_1         = 15,
	IPI_OUT_TEST_1            = 16,
	IPI_OUT_LOGGER_CTRL       = 17,
	IPI_OUT_SCPCTL_1          = 18,
	IPI_IN_AUDIO_ULTRA_SND_1  = 19,
	IPI_IN_SCP_ERROR_INFO_1   = 20,
	IPI_IN_LOGGER_CTRL        = 21,
	IPI_IN_SCP_READY_1        = 22,
	IPI_IN_SCP_RAM_DUMP_1     = 23,
	/* the following will use mbox4 */
	IPI_OUT_SCP_MPOOL_1       = 24,
	IPI_IN_SCP_MPOOL_1        = 25,
	SCP_IPI_COUNT
};

enum scp_ipi_status {
	SCP_IPI_NOT_READY = -2,
	SCP_IPI_ERROR = -1,
	SCP_IPI_DONE,
	SCP_IPI_BUSY,
};

/* scp notify event */
enum SCP_NOTIFY_EVENT {
	SCP_EVENT_READY = 0,
	SCP_EVENT_STOP,
};
/* the order of ipi_id should be consistent with IPI_LEGACY_GROUP */
enum ipi_id {
	IPI_MPOOL,
	IPI_CHRE,
	IPI_CHREX,
	IPI_SENSOR,
	IPI_SENSOR_INIT_START,
	SCP_NR_IPI,
};

/* scp reserve memory ID definition*/
enum scp_reserve_mem_id_t {
	VOW_MEM_ID,
	SENS_MEM_ID,
	SCP_A_LOGGER_MEM_ID,
	AUDIO_IPI_MEM_ID,
	VOW_BARGEIN_MEM_ID,
	SCP_DRV_PARAMS_MEM_ID,
	NUMS_MEM_ID,
};

/* scp feature ID list */
enum feature_id {
	VOW_FEATURE_ID,
	SENS_FEATURE_ID,
	FLP_FEATURE_ID,
	RTOS_FEATURE_ID,
	SPEAKER_PROTECT_FEATURE_ID,
	VCORE_TEST_FEATURE_ID,
	VOW_BARGEIN_FEATURE_ID,
	VOW_DUMP_FEATURE_ID,
	VOW_VENDOR_M_FEATURE_ID,
	VOW_VENDOR_A_FEATURE_ID,
	VOW_VENDOR_G_FEATURE_ID,
	NUM_FEATURE_ID,
};

extern struct mtk_mbox_device scp_mboxdev;
extern struct mtk_ipi_device scp_ipidev;


/* An API to get scp status */
extern unsigned int is_scp_ready(enum scp_core_id scp_id);

/* APIs to register new IPI handlers */
extern enum scp_ipi_status scp_ipi_registration(enum ipi_id id,
	void (*ipi_handler)(int id, void *data, unsigned int len),
	const char *name);
extern enum scp_ipi_status scp_ipi_unregistration(enum ipi_id id);

/* A common API to send message to SCP */
extern enum scp_ipi_status scp_ipi_send(enum ipi_id id, void *buf,
	unsigned int len, unsigned int wait, enum scp_core_id scp_id);


/* APIs to lock scp and make scp awaken */
extern int scp_awake_lock(void *_scp_id);
extern int scp_awake_unlock(void *_scp_id);

/* APIs for register notification */
extern void scp_A_register_notify(struct notifier_block *nb);
extern void scp_A_unregister_notify(struct notifier_block *nb);

/* APIs for hardware semaphore */
extern int get_scp_semaphore(int flag);
extern int release_scp_semaphore(int flag);
extern int scp_get_semaphore_3way(int flag);
extern int scp_release_semaphore_3way(int flag);

/* APIs for reserved memory */
extern phys_addr_t scp_get_reserve_mem_phys(enum scp_reserve_mem_id_t id);
extern phys_addr_t scp_get_reserve_mem_virt(enum scp_reserve_mem_id_t id);
extern phys_addr_t scp_get_reserve_mem_size(enum scp_reserve_mem_id_t id);

/* APIs for registering function of features */
extern void scp_register_feature(enum feature_id id);
extern void scp_deregister_feature(enum feature_id id);


#endif

