/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef __CMDQ_SEC_H__
#define __CMDQ_SEC_H__

#include <linux/kernel.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#include "cmdq-sec-iwc-common.h"

enum CMDQ_SEC_SCENARIO {
	CMDQ_SEC_JPEG_DEC = 0,
	CMDQ_SEC_PRIMARY_DISP = 1,
	CMDQ_SEC_PRIMARY_MEMOUT = 2,
	CMDQ_SEC_PRIMARY_ALL = 3,
	CMDQ_SEC_SUB_DISP = 4,
	CMDQ_SEC_SUB_MEMOUT = 5,
	CMDQ_SEC_SUB_ALL = 6,
	CMDQ_SEC_MHL_DISP = 7,
	CMDQ_SEC_RDMA0_DISP = 8,
	CMDQ_SEC_RDMA0_COLOR0_DISP = 9,
	CMDQ_SEC_RDMA1_DISP = 10,

	/* Trigger loop scenario does not enable HWs */
	CMDQ_SEC_TRIGGER_LOOP = 11,

	/* client from user space, so the cmd buffer is in user space. */
	CMDQ_SEC_USER_MDP = 12,

	CMDQ_SEC_DEBUG = 13,
	CMDQ_SEC_DEBUG_PREFETCH = 14,

	/* ESD check */
	CMDQ_SEC_DISP_ESD_CHECK = 15,
	/* for screen capture to wait for RDMA-done
	 * without blocking config thread
	 */
	CMDQ_SEC_DISP_SCREEN_CAPTURE = 16,

	CMDQ_SEC_DISP_PRIMARY_DISABLE_SECURE_PATH = 18,
	CMDQ_SEC_DISP_SUB_DISABLE_SECURE_PATH = 19,

	/* color path request from kernel */
	CMDQ_SEC_DISP_COLOR = 20,
	/* color path request from user sapce */
	CMDQ_SEC_USER_DISP_COLOR = 21,

	/* [phased out]client from user space,
	 * so the cmd buffer is in user space.
	 */
	CMDQ_SEC_USER_SPACE = 22,

	CMDQ_SEC_DISP_MIRROR_MODE = 23,

	CMDQ_SEC_DISP_CONFIG_AAL = 24,
	CMDQ_SEC_DISP_CONFIG_PRIMARY_GAMMA = 25,
	CMDQ_SEC_DISP_CONFIG_SUB_GAMMA = 26,
	CMDQ_SEC_DISP_CONFIG_PRIMARY_DITHER = 27,
	CMDQ_SEC_DISP_CONFIG_SUB_DITHER = 28,
	CMDQ_SEC_DISP_CONFIG_PRIMARY_PWM = 29,
	CMDQ_SEC_DISP_CONFIG_SUB_PWM = 30,
	CMDQ_SEC_DISP_CONFIG_PRIMARY_PQ = 31,
	CMDQ_SEC_DISP_CONFIG_SUB_PQ = 32,
	CMDQ_SEC_DISP_CONFIG_OD = 33,
	CMDQ_SEC_DISP_VFP_CHANGE = 34,

	CMDQ_SEC_RDMA2_DISP = 35,

	/* for primary trigger loop enable pre-fetch usage */
	CMDQ_SEC_HIGHP_TRIGGER_LOOP = 36,
	/* for low priority monitor loop to polling bus status */
	CMDQ_SEC_LOWP_TRIGGER_LOOP = 37,

	CMDQ_SEC_KERNEL_CONFIG_GENERAL = 38,

	CMDQ_SEC_TIMER_LOOP = 39,
	CMDQ_SEC_MOVE = 40,
	CMDQ_SEC_SRAM_LOOP = 41,

	/* debug scenario use mdp flush */
	CMDQ_SEC_DEBUG_MDP = 42,

	/* for ISP kernel driver */
	CMDQ_SEC_ISP_RSC = 43,
	CMDQ_SEC_ISP_FDVT = 44,
	CMDQ_SEC_ISP_DPE = 45,

	CMDQ_MAX_SEC_COUNT	/* ALWAYS keep at the end */
};

struct cmdq_sec_addr_meta {
	/* [IN]_d, index of instruction.
	 * Update its arg_b value to real PA/MVA in secure world
	 */
	uint32_t instrIndex;

	/*
	 * Note: Buffer and offset
	 *
	 *   -------------
	 *   |     |     |
	 *   -------------
	 *   ^     ^  ^  ^
	 *   A     B  C  D
	 *
	 * A: baseHandle
	 * B: baseHandle + blockOffset
	 * C: baseHandle + blockOffset + offset
	 * A~B or B~D: size
	 */

	uint32_t type;		/* [IN] addr handle type */
	uint64_t baseHandle;	/* [IN]_h, secure address handle */
	/* [IN]_b, block offset from handle(PA) to current block(plane) */
	uint32_t blockOffset;
	uint32_t offset;	/* [IN]_b, buffser offset to secure handle */
	uint32_t size;		/* buffer size */
	uint32_t port;		/* hw port id (i.e. M4U port id) */
};

struct cmdq_sec_data {
	/* address metadata, used to translate secure buffer PA
	 * related instruction in secure world
	 */
	uint32_t addrMetadataCount;	/* [IN] count of element in addrList */
	/* [IN] array of cmdq_sec_addr_meta */
	uint64_t addrMetadatas;
	uint32_t addrMetadataMaxCount;	/*[Reserved] */

	enum CMDQ_SEC_SCENARIO scenario;

	uint64_t enginesNeedDAPC;
	uint64_t enginesNeedPortSecurity;

	/* [Reserved] This is for CMDQ driver usage itself. Not for client.
	 * task index in thread's tasklist. -1 for not in tasklist.
	 */
	int32_t waitCookie;
	/* reset HW thread in SWd */
	bool resetExecCnt;

	enum cmdq_sec_meta_type client_meta_type;

	u32 client_meta_size[4];
	void *client_meta[4];
};

/* implementation in cmdq-sec-helper.c */
s32 cmdq_sec_pkt_set_data(struct cmdq_pkt *pkt, const u64 dapc_engine,
	const u64 port_sec_engine, const enum CMDQ_SEC_SCENARIO scenario,
	const enum cmdq_sec_meta_type meta_type);
s32 cmdq_sec_pkt_set_payload(struct cmdq_pkt *pkt, u8 idx,
	const u32 meta_size, u32 *meta);
s32 cmdq_sec_pkt_write_reg(struct cmdq_pkt *pkt, u32 addr, u64 base,
	const enum CMDQ_IWC_ADDR_METADATA_TYPE type,
	const u32 offset, const u32 size, const u32 port);
s32 cmdq_sec_pkt_assign_metadata(struct cmdq_pkt *pkt,
	u32 count, void *meta_array);
void cmdq_sec_dump_secure_data(struct cmdq_pkt *pkt);
int cmdq_sec_pkt_wait_complete(struct cmdq_pkt *pkt,
	struct completion *cmplt);
void cmdq_sec_err_dump(struct cmdq_pkt *pkt, struct cmdq_client *client);

#endif
