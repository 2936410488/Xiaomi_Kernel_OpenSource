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

#ifndef ___MT_GPUFREQ_INTERNAL_PLAT_H___
#define ___MT_GPUFREQ_INTERNAL_PLAT_H___

/**************************************************
 *  0:     all on when mtk probe init (freq/ Vgpu/ Vsram_gpu)
 *         disable DDK power on/off callback
 **************************************************/
#define MT_GPUFREQ_POWER_CTL_ENABLE	1

/**************************************************
 * (DVFS_ENABLE, CUST_CONFIG)
 * (1, 1) -> DVFS enable and init to CUST_INIT_OPP
 * (1, 0) -> DVFS enable
 * (0, 1) -> DVFS disable but init to CUST_INIT_OPP (do DVFS only onces)
 * (0, 0) -> DVFS disable
 **************************************************/
#define MT_GPUFREQ_DVFS_ENABLE          1
#define MT_GPUFREQ_CUST_CONFIG          0
#define MT_GPUFREQ_CUST_INIT_OPP        (g_opp_table_segment[0].gpufreq_khz)

/**************************************************
 * DVFS Setting
 **************************************************/
#define NUM_OF_OPP_IDX (sizeof(g_opp_table_segment) / \
			sizeof(g_opp_table_segment[0]))
#define FIXED_VSRAM_VOLT                (75000)
#define FIXED_VSRAM_VOLT_THSRESHOLD     (65000)

/**************************************************
 * PMIC Setting
 **************************************************/
/*vgpu      0.3 ~ 1.19375 V*/
/*vsram_gpu 0.5 ~ 1.29375 V*/
#define VGPU_MAX_VOLT                   (119375)        /* mV x 100 */
#define VGPU_MIN_VOLT                   (30000)         /* mV x 100 */
#define VSRAM_GPU_MAX_VOLT              (129375)        /* mV x 100 */
#define VSRAM_GPU_MIN_VOLT              (50000)         /* mV x 100 */
#define PMIC_STEP                       (625)           /* mV x 100 */
#define BUCK_DIFF_MAX                   (35000)         /* mV x 100 */
#define BUCK_DIFF_MIN                   (00000)         /* mV x 100 */
#define BUCK_UDELAY_BUFFER              (52)            /* us */

/**************************************************
 * Clock Setting
 **************************************************/
#define MFGPLL_CK_DEFAULT_FREQ          (620000)        /* KHz */
#define UNIVPLL_D3_DEFAULT_FREQ         (416000)        /* KHz */
#define MAINPLL_D3_DEFAULT_FREQ         (218400)        /* KHz */
#define CLK26M_DEFAULT_FREQ             (26000)         /* KHz */
#define POSDIV_2_MAX_FREQ               (1900000)       /* KHz */
#define POSDIV_2_MIN_FREQ               (750000)        /* KHz */
#define POSDIV_4_MAX_FREQ               (950000)        /* KHz */
#define POSDIV_4_MIN_FREQ               (375000)        /* KHz */
#define POSDIV_8_MAX_FREQ               (475000)        /* KHz */
#define POSDIV_8_MIN_FREQ               (187500)        /* KHz */
#define POSDIV_16_MAX_FREQ              (237500)        /* KHz */
#define POSDIV_16_MIN_FREQ              (125000)        /* KHz */
#define POSDIV_SHIFT                    (24)            /* bit */
#define DDS_SHIFT                       (14)            /* bit */
#define TO_MHZ_HEAD                     (100)
#define TO_MHZ_TAIL                     (10)
#define ROUNDING_VALUE                  (5)
#define MFGPLL_FIN                      (26)            /* MHz */
#define MFGPLL_FH_PLL                   FH_PLL4
#define MFGPLL_CON0                     (g_apmixed_base + 0x250)
#define MFGPLL_CON1                     (g_apmixed_base + 0x254)
#define MFGPLL_CON2                     (g_apmixed_base + 0x258)
#define MFGPLL_PWR_CON0                 (g_apmixed_base + 0x25C)

/**************************************************
 * Reference Power Setting
 **************************************************/
#define GPU_ACT_REF_POWER               (1285)                /* mW  */
#define GPU_ACT_REF_FREQ                (900000)              /* KHz */
#define GPU_ACT_REF_VOLT                (90000)               /* mV x 100 */
#define PTPOD_DISABLE_VOLT              (80000)

/**************************************************
 * Battery Over Current Protect
 **************************************************/
#ifdef MT_GPUFREQ_BATT_OC_PROTECT
#define MT_GPUFREQ_BATT_OC_LIMIT_FREQ           (485000)        /* KHz */
#endif

/**************************************************
 * Battery Percentage Protect
 **************************************************/
#ifdef MT_GPUFREQ_BATT_PERCENT_PROTECT
#define MT_GPUFREQ_BATT_PERCENT_LIMIT_FREQ      (485000)        /* KHz */
#endif

/**************************************************
 * Low Battery Volume Protect
 **************************************************/
#ifdef MT_GPUFREQ_LOW_BATT_VOLT_PROTECT
#define MT_GPUFREQ_LOW_BATT_VOLT_LIMIT_FREQ     (485000)        /* KHz */
#endif

/**************************************************
 * Register Manipulations
 **************************************************/
#define READ_REGISTER_UINT32(reg)	\
	(*(unsigned int * const)(reg))
#define WRITE_REGISTER_UINT32(reg, val)	\
	((*(unsigned int * const)(reg)) = (val))
#define INREG32(x)	\
	READ_REGISTER_UINT32((unsigned int *)((void *)(x)))
#define OUTREG32(x, y)	\
	WRITE_REGISTER_UINT32((unsigned int *)((void *)(x)), (unsigned int)(y))
#define SETREG32(x, y)	\
	OUTREG32(x, INREG32(x)|(y))
#define CLRREG32(x, y)	\
	OUTREG32(x, INREG32(x)&~(y))
#define MASKREG32(x, y, z)	\
	OUTREG32(x, (INREG32(x)&~(y))|(z))
#define DRV_Reg32(addr)				INREG32(addr)
#define DRV_WriteReg32(addr, data)	OUTREG32(addr, data)
#define DRV_SetReg32(addr, data)	SETREG32(addr, data)
#define DRV_ClrReg32(addr, data)	CLRREG32(addr, data)

/**************************************************
 * Proc Node Definition
 **************************************************/
#ifdef CONFIG_PROC_FS
#define PROC_FOPS_RW(name)	\
	static int mt_ ## name ## _proc_open(	\
			struct inode *inode,	\
			struct file *file)	\
	{	\
		return single_open(	\
				file,	\
				mt_ ## name ## _proc_show,	\
				PDE_DATA(inode));	\
	}	\
	static const struct file_operations mt_ ## name ## _proc_fops =	\
	{	\
		.owner = THIS_MODULE,	\
		.open = mt_ ## name ## _proc_open,	\
		.read = seq_read,	\
		.llseek = seq_lseek,	\
		.release = single_release,	\
		.write = mt_ ## name ## _proc_write,	\
	}
#define PROC_FOPS_RO(name)	\
	static int mt_ ## name ## _proc_open(	\
			struct inode *inode,	\
			struct file *file)	\
	{	\
		return single_open(	\
				file,	\
				mt_ ## name ## _proc_show,	\
				PDE_DATA(inode));	\
	}	\
	static const struct file_operations mt_ ## name ## _proc_fops =	\
	{	\
		.owner = THIS_MODULE,	\
		.open = mt_ ## name ## _proc_open,	\
		.read = seq_read,	\
		.llseek = seq_lseek,	\
		.release = single_release,	\
	}
#define PROC_ENTRY(name) \
	{__stringify(name), &mt_ ## name ## _proc_fops}
#endif

/**************************************************
 * Operation Definition
 **************************************************/
#define VOLT_NORMALIZATION(volt)	\
	((volt % 625) ? (volt - (volt % 625) + 625) : volt)
#ifndef MIN
#define MIN(x, y)	(((x) < (y)) ? (x) : (y))
#endif
#define GPUOP(khz, volt, vsram)	\
	{	\
		.gpufreq_khz = khz,	\
		.gpufreq_volt = volt,	\
		.gpufreq_vsram = vsram,	\
	}

/**************************************************
 * Enumerations
 **************************************************/
enum g_segment_id_enum {
	MT6785T_SEGMENT = 1,
	MT6785_SEGMENT,
	MT6783_SEGMENT,
};
enum g_posdiv_power_enum  {
	POSDIV_POWER_1 = 0,
	POSDIV_POWER_2,
	POSDIV_POWER_4,
	POSDIV_POWER_8,
	POSDIV_POWER_16,
};
enum g_clock_source_enum  {
	CLOCK_MAIN = 0,
	CLOCK_SUB,
};
enum g_limited_idx_enum {
	IDX_THERMAL_PROTECT_LIMITED = 0,
	IDX_LOW_BATT_LIMITED,
	IDX_BATT_PERCENT_LIMITED,
	IDX_BATT_OC_LIMITED,
	IDX_PBM_LIMITED,
	NUMBER_OF_LIMITED_IDX,
};

/**************************************************
 * Structures
 **************************************************/
struct opp_table_info {
	unsigned int gpufreq_khz;
	unsigned int gpufreq_volt;
	unsigned int gpufreq_vsram;
};
struct g_clk_info {
	/* main clock for mfg setting */
	struct clk *clk_mux;
	/* substitution clock for mfg transient mux setting */
	struct clk *clk_main_parent;
	/* substitution clock for mfg transient parent setting */
	struct clk *clk_sub_parent;
	/* clock gate, which has only two state with ON or OFF */
	struct clk *subsys_mfg_cg;
	struct clk *mtcmos_mfg_async;
	/* mtcmos_mfg dependent on mtcmos_mfg_async */
	struct clk *mtcmos_mfg;
	/* mtcmos_mfg_core0 dependent on mtcmos_mfg0 */
	struct clk *mtcmos_mfg_core0;
	/* mtcmos_mfg_core1_2 dependent on mtcmos_mfg1/0 */
	struct clk *mtcmos_mfg_core1_2;
	/* mtcmos_mfg_core3_4 dependent on mtcmos_mfg1 */
	struct clk *mtcmos_mfg_core3_4;
	/* mtcmos_mfg_core5_6 dependent on mtcmos_mfg1 */
	struct clk *mtcmos_mfg_core5_6;
	/* mtcmos_mfg_core7_8 dependent on mtcmos_mfg1 */
	struct clk *mtcmos_mfg_core7_8;
};
struct g_pmic_info {
	struct regulator *reg_vgpu;
	struct regulator *reg_vsram_gpu;
};

/**************************************************
 * External functions declaration
 **************************************************/
extern bool mtk_get_gpu_loading(unsigned int *pLoading);
extern unsigned int mt_get_ckgen_freq(unsigned int idx);

/**************************************************
 * global value definition
 **************************************************/
struct opp_table_info *g_opp_table;
struct opp_table_info *g_opp_table_default;

/**************************************************
 * PTPOD definition
 **************************************************/
unsigned int g_ptpod_opp_idx_table_segment[] = {
	0, 3, 6, 8,
	10, 12, 14, 16,
	18, 20, 22, 24,
	26, 28, 30, 32
};

/**************************************************
 * GPU OPP table definition
 **************************************************/
struct opp_table_info g_opp_table_segment[] = {
    /* frequency,  vgpu, vsram*/
    /*       KHz, 100mV, 100mV*/
	GPUOP(836000, 75000, 75000), /* 0 sign off */
	GPUOP(825000, 74375, 75000), /* 1 */
	GPUOP(815000, 73750, 75000), /* 2 */
	GPUOP(805000, 73125, 75000), /* 3 */
	GPUOP(795000, 72500, 75000), /* 4 */
	GPUOP(785000, 71875, 75000), /* 5 */
	GPUOP(775000, 71250, 75000), /* 6 */
	GPUOP(765000, 70625, 75000), /* 7 */
	GPUOP(755000, 70000, 75000), /* 8 */
	GPUOP(745000, 69375, 75000), /* 9 */
	GPUOP(735000, 68750, 75000), /*10 */
	GPUOP(725000, 68125, 75000), /*11 */
	GPUOP(715000, 67500, 75000), /*12 */
	GPUOP(705000, 66875, 75000), /*13 */
	GPUOP(695000, 66250, 75000), /*14 */
	GPUOP(685000, 65625, 75000), /*15 */
	GPUOP(675000, 65000, 75000), /*16 sign off */
	GPUOP(654000, 64375, 75000), /*17 */
	GPUOP(634000, 63750, 75000), /*18 */
	GPUOP(614000, 63125, 75000), /*19 */
	GPUOP(593000, 62500, 75000), /*20 */
	GPUOP(573000, 61875, 75000), /*21 */
	GPUOP(553000, 61250, 75000), /*22 */
	GPUOP(532000, 60625, 75000), /*23 */
	GPUOP(512000, 60000, 75000), /*24 */
	GPUOP(492000, 59375, 75000), /*25 */
	GPUOP(471000, 58750, 75000), /*26 */
	GPUOP(451000, 58125, 75000), /*27 */
	GPUOP(431000, 57500, 75000), /*28 */
	GPUOP(410000, 56875, 75000), /*29 */
	GPUOP(390000, 56250, 75000), /*30 */
	GPUOP(370000, 55625, 75000), /*31 */
	GPUOP(350000, 55000, 75000), /*32 sign off */
};

#endif /* ___MT_GPUFREQ_INTERNAL_PLAT_H___ */
