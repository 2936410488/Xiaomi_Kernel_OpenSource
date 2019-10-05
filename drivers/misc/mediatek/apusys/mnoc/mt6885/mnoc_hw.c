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

/*
 *=============================================================
 * Include files
 *=============================================================
 */

/* system includes */
#include <linux/printk.h>
#include <linux/io.h>
#include <linux/spinlock.h>

#include "apusys_device.h"
#include "mnoc_hw.h"
#include "mnoc_drv.h"

static const char * const mni_int_sta_string[] = {
	"MNI_QOS_IRQ_FLAG",
	"ADDR_DEC_ERR_FLAG",
	"MST_PARITY_ERR_FLAG",
	"MST_MISRO_ERR_FLAG",
	"MST_CRDT_ERR_FLAG",
};

static const char * const sni_int_sta_string[] = {
	"SLV_PARITY_ERR_FLA",
	"SLV_MISRO_ERR_FLAG",
	"SLV_CRDT_ERR_FLAG",
};

static const char * const rt_int_sta_string[] = {
	"REQRT_MISRO_ERR_FLAG",
	"RSPRT_MISRO_ERR_FLAG",
	"REQRT_TO_ERR_FLAG",
	"RSPRT_TO_ERR_FLAG",
	"REQRT_CBUF_ERR_FLAG",
	"RSPRT_CBUF_ERR_FLAG",
	"REQRT_CRDT_ERR_FLAG",
	"RSPRT_CRDT_ERR_FLAG",
};

static const char * const mni_map_string[] = {
	"MNI_MDLA0_0",
	"MNI_MDLA0_1",
	"MNI_MDLA1_0",
	"MNI_MDLA1_1",
	"MNI_ADL",
	"MNI_XPU",
	"MNI_VPU0",
	"MNI_EDMA_0",
	"MNI_EDMA_1",
	"MNI_NONE",
	"MNI_VPU1",
	"MNI_VPU2",
	"MNI_MD32",
	"MNI_NONE",
	"MNI_NONE",
	"MNI_NONE",
};

static const char * const sni_map_string[] = {
	"SNI_TCM0",
	"SNI_TCM1",
	"SNI_TCM2",
	"SNI_TCM3",
	"SNI_EMI2",
	"SNI_EMI3",
	"SNI_EMI0",
	"SNI_EMI1",
	"SNI_VPU0",
	"SNI_EXT",
	"SNI_VPU1",
	"SNI_VPU2",
	"SNI_NONE",
	"SNI_MD32",
	"SNI_NONE",
	"SNI_NONE",
};

static const unsigned int mni_int_sta_offset[NR_MNI_INT_STA] = {
	MNI_QOS_IRQ_FLAG,
	ADDR_DEC_ERR_FLAG,
	MST_PARITY_ERR_FLAG,
	MST_MISRO_ERR_FLAG,
	MST_CRDT_ERR_FLAG,
};

static const unsigned int sni_int_sta_offset[NR_SNI_INT_STA] = {
	SLV_PARITY_ERR_FLA,
	SLV_MISRO_ERR_FLAG,
	SLV_CRDT_ERR_FLAG,
};

static const unsigned int rt_int_sta_offset[NR_RT_INT_STA] = {
	REQRT_MISRO_ERR_FLAG,
	RSPRT_MISRO_ERR_FLAG,
	REQRT_TO_ERR_FLAG,
	RSPRT_TO_ERR_FLAG,
	REQRT_CBUF_ERR_FLAG,
	RSPRT_CBUF_ERR_FLAG,
	REQRT_CRDT_ERR_FLAG,
	RSPRT_CRDT_ERR_FLAG,
};

/**
 * MNI offset 0 -> MNI05_QOS_CTRL0
 * MNI offset 1 -> MNI06_QOS_CTRL0
 * MNI offset 2 -> MNI07_QOS_CTRL0
 * MNI offset 3 -> MNI08_QOS_CTRL0
 * MNI offset 4 -> MNI13_QOS_CTRL0
 * MNI offset 5 -> MNI15_QOS_CTRL0
 * MNI offset 6 -> MNI00_QOS_CTRL0
 * MNI offset 7 -> MNI09_QOS_CTRL0
 * MNI offset 8 -> MNI10_QOS_CTRL0
 * MNI offset 9 -> MNI03_QOS_CTRL0
 * MNI offset 10 -> MNI01_QOS_CTRL0
 * MNI offset 11 -> MNI02_QOS_CTRL0
 * MNI offset 12 -> MNI04_QOS_CTRL0
 * MNI offset 13 -> MNI11_QOS_CTRL0
 * MNI offset 14 -> MNI12_QOS_CTRL0
 * MNI offset 15 -> MNI14_QOS_CTRL0
 * VPU0		-> MNI00 -> offset 6
 * VPU1		-> MNI01 -> offset 10
 * VPU2		-> MNI02 -> offset 11
 * MDLA0_0	-> MNI05 -> offset 0
 * MDLA0_1	-> MNI06 -> offset 1
 * MDLA1_0	-> MNI07 -> offset 2
 * MDLA1_1	-> MNI08 -> offset 3
 * EDMA_0	-> MNI09 -> offset 7
 * EDMA_1	-> MNI10 -> offset 8
 * MD32		-> MNI04 -> offset 12
 */
static char mni_map[NR_APU_QOS_MNI] = {6, 10, 11, 0, 1, 2, 3, 7, 8, 12};

static bool arr_mni_pre_ultra[NR_APU_QOS_MNI] = {0};
static bool arr_mni_lt_guardian_pre_ultra[NR_APU_QOS_MNI] = {0};


int apusys_dev_to_core_id(int dev_type, int dev_core)
{
	int ret = -1;

	switch (dev_type) {
	case APUSYS_DEVICE_VPU:
		if (dev_core >= 0 && dev_core < NR_APU_ENGINE_VPU)
			ret = dev_core;
		break;
	case APUSYS_DEVICE_MDLA:
		if (dev_core >= 0 && dev_core < NR_APU_ENGINE_MDLA)
			ret = NR_APU_ENGINE_VPU + dev_core;
		break;
	case APUSYS_DEVICE_EDMA:
		if (dev_core >= 0 && dev_core < NR_APU_ENGINE_EDMA)
			ret = NR_APU_ENGINE_VPU + NR_APU_ENGINE_MDLA + dev_core;
		break;
	default:
		ret = -1;
		break;
	}

	return ret;
}

/* register to apusys power on callback */
static void mnoc_qos_reg_init(void)
{
	int i;
	unsigned long flags;

	LOG_DEBUG("+\n");

	spin_lock_irqsave(&mnoc_spinlock, flags);

	/* time slot setting */
	for (i = 0; i < NR_APU_QOS_MNI; i++) {
		/* QoS watcher BW time slot set to 1.26 ms */
		mnoc_write_field(MNI_QOS_REG(MNI_QOS_CTRL_BASE,
			2, mni_map[i]), 1:0, 0x1);
		/* QoS guardian BW time slot set to 1.26 ms */
		mnoc_write_field(MNI_QOS_REG(MNI_QOS_CTRL_BASE,
			16, mni_map[i]), 1:0, 0x1);

		/* 26M cycle count = {QW_LT_PRD,8’h0} << QW_LT_PRD_SHF */
		/* QW_LT_PRD = 0x80, QW_LT_PRD_SHF = 0x0 */
		/* QoS watcher LT time slot set to 1.26 ms */
		mnoc_write_field(MNI_QOS_REG(MNI_QOS_CTRL_BASE,
			5, mni_map[i]), 7:0, 0x80);
		mnoc_write_field(MNI_QOS_REG(MNI_QOS_CTRL_BASE,
			5, mni_map[i]), 10:8, 0x0);
		/* QoS guardian LT time slot set to 1.26 ms */
		mnoc_write_field(MNI_QOS_REG(MNI_QOS_CTRL_BASE,
			19, mni_map[i]), 7:0, 0x80);
		mnoc_write_field(MNI_QOS_REG(MNI_QOS_CTRL_BASE,
			19, mni_map[i]), 10:8, 0x0);

		/* MNI to SNI path setting */
		/* set QoS guardian to monitor DRAM only */
		mnoc_write_field(MNI_QOS_REG(MNI_QOS_CTRL_BASE,
			31, mni_map[i]), 31:16, 0xF000);
		/* set QoS watcher to monitor DRAM+TCM */
		mnoc_write_field(MNI_QOS_REG(MNI_QOS_CTRL_BASE,
			31, mni_map[i]), 15:0, 0xFF00);

		/* set QW_BW_INT_EN = 1 to enable monitor */
		mnoc_write_field(MNI_QOS_REG(MNI_QOS_CTRL_BASE,
			1, mni_map[i]), 2:2, 0x1);
		/* set QG_BW_INT_EN = 1 to enable monitor */
		mnoc_write_field(MNI_QOS_REG(MNI_QOS_CTRL_BASE,
			15, mni_map[i]), 2:2, 0x1);
		/* set QW_LT_INT_EN = 1 to enable monitor */
		mnoc_write_field(MNI_QOS_REG(MNI_QOS_CTRL_BASE,
			1, mni_map[i]), 4:4, 0x1);
		/* set QG_LT_INT_EN = 1 to enable monitor */
		mnoc_write_field(MNI_QOS_REG(MNI_QOS_CTRL_BASE,
			15, mni_map[i]), 4:4, 0x1);
	}

	spin_unlock_irqrestore(&mnoc_spinlock, flags);

	LOG_DEBUG("-\n");
}

/* register to apusys power on callback */
static void mnoc_reg_init(void)
{
	int i;
	unsigned long flags;

	LOG_DEBUG("+\n");

	spin_lock_irqsave(&mnoc_spinlock, flags);

	/* EMI fine tune: SLV12_QOS ~ SLV15_QOS = 0x7 */
	mnoc_write_field(MNOC_REG(SLV_QOS_CTRL1), 31:16, 0x7777);

	/* enable mnoc interrupt */
	mnoc_write_field(MNOC_INT_EN, 1:0, 3);

	/* set request router timeout interrupt */
	for (i = 0; i < NR_MNOC_RT; i++) {
		/* all VC enabled */
		mnoc_write(MNOC_RT_PMU_REG(REQ_RT_PMU_BASE, 3, i), 0xFFFFFFFF);
		mnoc_write(MNOC_RT_PMU_REG(REQ_RT_PMU_BASE, 4, i), 0xFFFFFFFF);
		/* set timeout threshold to 510 cycles */
		mnoc_write_field(MNOC_RT_PMU_REG(REQ_RT_PMU_BASE, 2, i),
			8:0, 510);
		/* enable timeout counting */
		mnoc_write_field(MNOC_RT_PMU_REG(REQ_RT_PMU_BASE, 2, i),
			31:31, 1);
	}

	/* set response router timeout interrupt */
	for (i = 0; i < NR_MNOC_RT; i++) {
		/* all VC enabled */
		mnoc_write(MNOC_RT_PMU_REG(RSP_RT_PMU_BASE, 3, i), 0xFFFFFFFF);
		mnoc_write(MNOC_RT_PMU_REG(RSP_RT_PMU_BASE, 4, i), 0xFFFFFFFF);
		/* set timeout threshold to 510 cycles */
		mnoc_write_field(MNOC_RT_PMU_REG(RSP_RT_PMU_BASE, 2, i),
			8:0, 510);
		/* enable timeout counting */
		mnoc_write_field(MNOC_RT_PMU_REG(RSP_RT_PMU_BASE, 2, i),
			31:31, 1);
	}

	spin_unlock_irqrestore(&mnoc_spinlock, flags);

	LOG_DEBUG("-\n");
}

/*
 * todo: extinguish mnoc irq0 and irq1 for better efficiency?
 * GIC SPI IRQ 406 is shared, need to return IRQ_NONE
 * if not triggered by mnoc
 */
bool mnoc_check_int_status(void)
{
	unsigned long flags;
	bool mnoc_irq_triggered = false;
	unsigned int val;
	int int_idx, ni_idx;

	LOG_DEBUG("+\n");

	spin_lock_irqsave(&mnoc_spinlock, flags);

	/* prevent register access if apusys power off */
	if (!mnoc_reg_valid) {
		spin_unlock_irqrestore(&mnoc_spinlock, flags);
		return mnoc_irq_triggered;
	}

	for (int_idx = 0; int_idx < NR_MNI_INT_STA; int_idx++) {
		val = mnoc_read(MNOC_REG(mni_int_sta_offset[int_idx]));
		if ((val & 0xFFFF) != 0) {
			LOG_ERR("%s = 0x%x\n",
				mni_int_sta_string[int_idx], val);
			for (ni_idx = 0; ni_idx < NR_MNOC_MNI; ni_idx++)
				if ((val & (1 << ni_idx)) != 0)
					LOG_ERR("From %s\n",
						mni_map_string[ni_idx]);
			mnoc_write_field(
				MNOC_REG(mni_int_sta_offset[int_idx]),
				15:0, 0xFFFF);
			mnoc_irq_triggered = true;
		}
	}

	for (int_idx = 0; int_idx < NR_SNI_INT_STA; int_idx++) {
		val = mnoc_read(MNOC_REG(sni_int_sta_offset[int_idx]));
		if ((val & 0xFFFF) != 0) {
			LOG_ERR("%s = 0x%x\n",
				sni_int_sta_string[int_idx], val);
			for (ni_idx = 0; ni_idx < NR_MNOC_SNI; ni_idx++)
				if ((val & (1 << ni_idx)) != 0)
					LOG_ERR("From %s\n",
						sni_map_string[ni_idx]);
			mnoc_write_field(
				MNOC_REG(sni_int_sta_offset[int_idx]),
				15:0, 0xFFFF);
			mnoc_irq_triggered = true;
		}
	}

	for (int_idx = 0; int_idx < NR_MNI_INT_STA; int_idx++) {
		val = mnoc_read(MNOC_REG(rt_int_sta_offset[int_idx]));
		if ((val & 0x1F) != 0) {
			LOG_ERR("%s = 0x%x\n",
				rt_int_sta_string[int_idx], val);
			for (ni_idx = 0; ni_idx < NR_MNOC_RT; ni_idx++)
				if ((val & (1 << ni_idx)) != 0)
					LOG_ERR("From RT %d\n", ni_idx);
			mnoc_write_field(
				MNOC_REG(rt_int_sta_offset[int_idx]),
				4:0, 0x1F);
			mnoc_irq_triggered = true;
		}
	}

	spin_unlock_irqrestore(&mnoc_spinlock, flags);

	LOG_DEBUG("-\n");

	return mnoc_irq_triggered;
}

/* read PMU_COUNTER_OUT 0~15 value to pmu buffer */
void mnoc_get_pmu_counter(unsigned int *buf)
{
	int i;
	unsigned long flags;

	LOG_DEBUG("+\n");

	spin_lock_irqsave(&mnoc_spinlock, flags);
	if (mnoc_reg_valid)
		for (i = 0; i < NR_MNOC_PMU_CNTR; i++)
			buf[i] = mnoc_read(PMU_COUNTER0_OUT + 4*i);
	spin_unlock_irqrestore(&mnoc_spinlock, flags);

	LOG_DEBUG("-\n");
}

void mnoc_tcm_hash_set(unsigned int sel, unsigned int en0, unsigned int en1)
{
	unsigned long flags;

	LOG_DEBUG("+\n");

	spin_lock_irqsave(&mnoc_spinlock, flags);
	mnoc_write_field(APU_TCM_HASH_TRUNCATE_CTRL0, 2:0, sel);
	mnoc_write_field(APU_TCM_HASH_TRUNCATE_CTRL0, 6:3, en0);
	mnoc_write_field(APU_TCM_HASH_TRUNCATE_CTRL0, 10:7, en1);
	spin_unlock_irqrestore(&mnoc_spinlock, flags);

	LOG_DEBUG("APU_TCM_HASH_TRUNCATE_CTRL0 = 0x%x\n",
		mnoc_read(APU_TCM_HASH_TRUNCATE_CTRL0));

	LOG_DEBUG("-\n");
}

static void set_mni_pre_ultra_locked(unsigned int idx, bool endis)
{
	unsigned int map, val;

	LOG_DEBUG("+\n");

	/* bit 24 : force AW urgent enable
	 * bit 25 : force AR urgent enable
	 * bit 29 : AW pre-urgent value
	 * bit 31 : AR pre-urgent value
	 */
	map = (1 << 24) | (1 << 25) | (1 << 29) | (1 << 31);

	val = mnoc_read(MNI_QOS_REG(MNI_QOS_CTRL_BASE,
		15, mni_map[idx]));
	if (endis)
		mnoc_write(MNI_QOS_REG(MNI_QOS_CTRL_BASE,
			15, mni_map[idx]), (val | map));
	else
		mnoc_write(MNI_QOS_REG(MNI_QOS_CTRL_BASE,
			15, mni_map[idx]), (val & (~map)));

	LOG_DEBUG("-\n");
}

void mnoc_set_mni_pre_ultra(int dev_type, int dev_core, bool endis)
{
	unsigned long flags;
	unsigned int idx;
	int core;

	LOG_DEBUG("+\n");

	core = apusys_dev_to_core_id(dev_type, dev_core);

	if (core == -1) {
		LOG_ERR("illegal dev_type(%d), dev_core(%d)\n",
			dev_type, dev_core);
		return;
	}

	spin_lock_irqsave(&mnoc_spinlock, flags);

	switch (core) {
	case APU_QOS_ENGINE_VPU0:
	case APU_QOS_ENGINE_VPU1:
	case APU_QOS_ENGINE_VPU2:
		idx = core;
		arr_mni_pre_ultra[idx] = endis;
		if (mnoc_reg_valid)
			set_mni_pre_ultra_locked(idx, endis);
		break;
	case APU_QOS_ENGINE_MDLA0:
	case APU_QOS_ENGINE_MDLA1:
		idx = (core - APU_QOS_ENGINE_MDLA0) * 2 + MNI_MDLA0_0;
		arr_mni_pre_ultra[idx] = endis;
		arr_mni_pre_ultra[idx+1] = endis;
		if (mnoc_reg_valid) {
			set_mni_pre_ultra_locked(idx, endis);
			set_mni_pre_ultra_locked(idx+1, endis);
		}
		break;
	case APU_QOS_ENGINE_EDMA0:
	case APU_QOS_ENGINE_EDMA1:
	case APU_QOS_ENGINE_MD32:
		idx = core - APU_QOS_ENGINE_EDMA0 + MNI_EDMA0;
		arr_mni_pre_ultra[idx] = endis;
		if (mnoc_reg_valid)
			set_mni_pre_ultra_locked(idx, endis);
		break;
	default:
		break;
	}

	spin_unlock_irqrestore(&mnoc_spinlock, flags);

	LOG_DEBUG("-\n");
}

static void set_lt_guardian_pre_ultra_locked(unsigned int idx, bool endis)
{
	LOG_DEBUG("+\n");

	if (endis) {
		/* set QG_LT_THH */
		mnoc_write_field(
			MNI_QOS_REG(MNI_QOS_CTRL_BASE, 20, mni_map[idx]),
			12:0, QG_LT_THH_PRE_ULTRA);
		/* set QG_LT_THL */
		mnoc_write_field(
			MNI_QOS_REG(MNI_QOS_CTRL_BASE, 20, mni_map[idx]),
			28:16, QG_LT_THL_PRE_ULTRA);
		/* set QCC_LT_LV_DIS[3:0] = 4’b1001 */
		mnoc_write_field(
			MNI_QOS_REG(MNI_QOS_CTRL_BASE, 29, mni_map[idx]),
			11:8, 0x9);
		/* set STM mode QCC_LT_TH_MODE = 1 */
		mnoc_write_field(
			MNI_QOS_REG(MNI_QOS_CTRL_BASE, 29, mni_map[idx]),
			16:16, 0x1);
		/* set QCC_TOP_URGENT_EN = 0 */
		mnoc_write_field(
			MNI_QOS_REG(MNI_QOS_CTRL_BASE, 29, mni_map[idx]),
			19:19, 0x0);
	} else {
		/* set QG_LT_THH */
		mnoc_write_field(
			MNI_QOS_REG(MNI_QOS_CTRL_BASE, 20, mni_map[idx]),
			12:0, 0x0);
		/* set QG_LT_THL */
		mnoc_write_field(
			MNI_QOS_REG(MNI_QOS_CTRL_BASE, 20, mni_map[idx]),
			28:16, 0x0);
		/* set QCC_LT_LV_DIS[3:0] = 4’b0000 */
		mnoc_write_field(
			MNI_QOS_REG(MNI_QOS_CTRL_BASE, 29, mni_map[idx]),
			11:8, 0x0);
		/* set STM mode QCC_LT_TH_MODE = 0 */
		mnoc_write_field(
			MNI_QOS_REG(MNI_QOS_CTRL_BASE, 29, mni_map[idx]),
			16:16, 0x0);
		/* set QCC_TOP_URGENT_EN = 1 */
		mnoc_write_field(
			MNI_QOS_REG(MNI_QOS_CTRL_BASE, 29, mni_map[idx]),
			19:19, 0x1);
	}

	LOG_DEBUG("-\n");
}

void mnoc_set_lt_guardian_pre_ultra(int dev_type, int dev_core, bool endis)
{
	unsigned long flags;
	unsigned int idx;
	int core;

	LOG_DEBUG("+\n");

	core = apusys_dev_to_core_id(dev_type, dev_core);

	if (core == -1) {
		LOG_ERR("illegal dev_type(%d), dev_core(%d)\n",
			dev_type, dev_core);
		return;
	}

	spin_lock_irqsave(&mnoc_spinlock, flags);

	switch (core) {
	case APU_QOS_ENGINE_VPU0:
	case APU_QOS_ENGINE_VPU1:
	case APU_QOS_ENGINE_VPU2:
		idx = core;
		arr_mni_lt_guardian_pre_ultra[idx] = endis;
		if (mnoc_reg_valid)
			set_lt_guardian_pre_ultra_locked(idx, endis);
		break;
	case APU_QOS_ENGINE_MDLA0:
	case APU_QOS_ENGINE_MDLA1:
		idx = (core - APU_QOS_ENGINE_MDLA0) * 2 + MNI_MDLA0_0;
		arr_mni_lt_guardian_pre_ultra[idx] = endis;
		arr_mni_lt_guardian_pre_ultra[idx+1] = endis;
		if (mnoc_reg_valid) {
			set_lt_guardian_pre_ultra_locked(idx, endis);
			set_lt_guardian_pre_ultra_locked(idx+1, endis);
		}
		break;
	case APU_QOS_ENGINE_EDMA0:
	case APU_QOS_ENGINE_EDMA1:
	case APU_QOS_ENGINE_MD32:
		idx = core - APU_QOS_ENGINE_EDMA0 + MNI_EDMA0;
		arr_mni_lt_guardian_pre_ultra[idx] = endis;
		if (mnoc_reg_valid)
			set_lt_guardian_pre_ultra_locked(idx, endis);
		break;
	default:
		break;
	}

	spin_unlock_irqrestore(&mnoc_spinlock, flags);

	LOG_DEBUG("-\n");
}

void mnoc_hw_reinit(void)
{
	unsigned long flags;
	int idx;

	mnoc_qos_reg_init();
	mnoc_reg_init();

	spin_lock_irqsave(&mnoc_spinlock, flags);
	for (idx = 0; idx < NR_APU_QOS_MNI; idx++) {
		if (arr_mni_pre_ultra[idx])
			set_mni_pre_ultra_locked(idx, 1);
		if (arr_mni_lt_guardian_pre_ultra[idx])
			set_lt_guardian_pre_ultra_locked(idx, 1);
	}
	spin_unlock_irqrestore(&mnoc_spinlock, flags);
}
