/*
 * Copyright (C) 2016 MediaTek Inc.
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

#include "mtk_cpufreq_struct.h"
#include "mtk_cpufreq_config.h"

/* 6885 */
/* for DVFS OPP table L */
#define CPU_DVFS_FREQ0_LL_FY          2000000         /* KHz */
#define CPU_DVFS_FREQ1_LL_FY          1895000         /* KHz */
#define CPU_DVFS_FREQ2_LL_FY		1791000		/* KHz */
#define CPU_DVFS_FREQ3_LL_FY		1708000		/* KHz */
#define CPU_DVFS_FREQ4_LL_FY		1625000		/* KHz */
#define CPU_DVFS_FREQ5_LL_FY		1500000		/* KHz */
#define CPU_DVFS_FREQ6_LL_FY		1393000		/* KHz */
#define CPU_DVFS_FREQ7_LL_FY		1287000		/* KHz */
#define CPU_DVFS_FREQ8_LL_FY		1181000		/* KHz */
#define CPU_DVFS_FREQ9_LL_FY		1048000		/* KHz */
#define CPU_DVFS_FREQ10_LL_FY		968000		/* KHz */
#define CPU_DVFS_FREQ11_LL_FY		862000		/* KHz */
#define CPU_DVFS_FREQ12_LL_FY		756000		/* KHz */
#define CPU_DVFS_FREQ13_LL_FY		703000		/* KHz */
#define CPU_DVFS_FREQ14_LL_FY		650000		/* KHz */
#define CPU_DVFS_FREQ15_LL_FY		500000		/* KHz */

/* for DVFS OPP table B */
#define CPU_DVFS_FREQ0_L_FY		2202000		/* KHz */
#define CPU_DVFS_FREQ1_L_FY		2106000		/* KHz */
#define CPU_DVFS_FREQ2_L_FY		2050000		/* KHz */
#define CPU_DVFS_FREQ3_L_FY		1975000		/* KHz */
#define CPU_DVFS_FREQ4_L_FY		1900000		/* KHz */
#define CPU_DVFS_FREQ5_L_FY		1803000		/* KHz */
#define CPU_DVFS_FREQ6_L_FY		1750000		/* KHz */
#define CPU_DVFS_FREQ7_L_FY		1622000		/* KHz */
#define CPU_DVFS_FREQ8_L_FY		1526000		/* KHz */
#define CPU_DVFS_FREQ9_L_FY		1367000		/* KHz */
#define CPU_DVFS_FREQ10_L_FY		1271000		/* KHz */
#define CPU_DVFS_FREQ11_L_FY		1176000		/* KHz */
#define CPU_DVFS_FREQ12_L_FY		1048000		/* KHz */
#define CPU_DVFS_FREQ13_L_FY		921000		/* KHz */
#define CPU_DVFS_FREQ14_L_FY		825000		/* KHz */
#define CPU_DVFS_FREQ15_L_FY		730000		/* KHz */

/* for DVFS OPP table CCI */
#define CPU_DVFS_FREQ0_CCI_FY		1540000		/* KHz */
#define CPU_DVFS_FREQ1_CCI_FY		1469000		/* KHz */
#define CPU_DVFS_FREQ2_CCI_FY		1426000		/* KHz */
#define CPU_DVFS_FREQ3_CCI_FY		1370000		/* KHz */
#define CPU_DVFS_FREQ4_CCI_FY		1313000		/* KHz */
#define CPU_DVFS_FREQ5_CCI_FY		1256000		/* KHz */
#define CPU_DVFS_FREQ6_CCI_FY		1195000		/* KHz */
#define CPU_DVFS_FREQ7_CCI_FY		1115000		/* KHz */
#define CPU_DVFS_FREQ8_CCI_FY		1030000		/* KHz */
#define CPU_DVFS_FREQ9_CCI_FY		945000		/* KHz */
#define CPU_DVFS_FREQ10_CCI_FY		881000		/* KHz */
#define CPU_DVFS_FREQ11_CCI_FY		817000		/* KHz */
#define CPU_DVFS_FREQ12_CCI_FY		711000		/* KHz */
#define CPU_DVFS_FREQ13_CCI_FY		668000		/* KHz */
#define CPU_DVFS_FREQ14_CCI_FY		583000		/* KHz */
#define CPU_DVFS_FREQ15_CCI_FY		520000		/* KHz */

/* for DVFS OPP table L */
#define CPU_DVFS_VOLT0_VPROC1_FY	100000		/* 10uV */
#define CPU_DVFS_VOLT1_VPROC1_FY	 96875		/* 10uV */
#define CPU_DVFS_VOLT2_VPROC1_FY	 93125		/* 10uV */
#define CPU_DVFS_VOLT3_VPROC1_FY	 90625		/* 10uV */
#define CPU_DVFS_VOLT4_VPROC1_FY	 88125		/* 10uV */
#define CPU_DVFS_VOLT5_VPROC1_FY	 83750		/* 10uV */
#define CPU_DVFS_VOLT6_VPROC1_FY	 80625		/* 10uV */
#define CPU_DVFS_VOLT7_VPROC1_FY	 77500		/* 10uV */
#define CPU_DVFS_VOLT8_VPROC1_FY	 75000		/* 10uV */
#define CPU_DVFS_VOLT9_VPROC1_FY	 71250		/* 10uV */
#define CPU_DVFS_VOLT10_VPROC1_FY	 68750		/* 10uV */
#define CPU_DVFS_VOLT11_VPROC1_FY	 65625		/* 10uV */
#define CPU_DVFS_VOLT12_VPROC1_FY	 63125		/* 10uV */
#define CPU_DVFS_VOLT13_VPROC1_FY	 61250		/* 10uV */
#define CPU_DVFS_VOLT14_VPROC1_FY	 60000		/* 10uV */
#define CPU_DVFS_VOLT15_VPROC1_FY	 60000		/* 10uV */

/* for DVFS OPP table B */
#define CPU_DVFS_VOLT0_VPROC2_FY	100000		/* 10uV	*/
#define CPU_DVFS_VOLT1_VPROC2_FY	96250		/* 10uV	*/
#define CPU_DVFS_VOLT2_VPROC2_FY	94375		/* 10uV	*/
#define CPU_DVFS_VOLT3_VPROC2_FY	91875		/* 10uV	*/
#define CPU_DVFS_VOLT4_VPROC2_FY	89375		/* 10uV	*/
#define CPU_DVFS_VOLT5_VPROC2_FY	85625		/* 10uV	*/
#define CPU_DVFS_VOLT6_VPROC2_FY	83750		/* 10uV	*/
#define CPU_DVFS_VOLT7_VPROC2_FY	80625		/* 10uV	*/
#define CPU_DVFS_VOLT8_VPROC2_FY	78750		/* 10uV	*/
#define CPU_DVFS_VOLT9_VPROC2_FY	75000		/* 10uV	*/
#define CPU_DVFS_VOLT10_VPROC2_FY	72500		/* 10uV	*/
#define CPU_DVFS_VOLT11_VPROC2_FY	70625		/* 10uV	*/
#define CPU_DVFS_VOLT12_VPROC2_FY	67500		/* 10uV	*/
#define CPU_DVFS_VOLT13_VPROC2_FY	64375		/* 10uV	*/
#define CPU_DVFS_VOLT14_VPROC2_FY	62500		/* 10uV	*/
#define CPU_DVFS_VOLT15_VPROC2_FY	60000		/* 10uV	*/

/* for DVFS OPP table CCI */
#define CPU_DVFS_VOLT0_VPROC3_FY	100000		/* 10uV */
#define CPU_DVFS_VOLT1_VPROC3_FY	 96875		/* 10uV */
#define CPU_DVFS_VOLT2_VPROC3_FY	 94375		/* 10uV */
#define CPU_DVFS_VOLT3_VPROC3_FY	 91875		/* 10uV */
#define CPU_DVFS_VOLT4_VPROC3_FY	 89375		/* 10uV */
#define CPU_DVFS_VOLT5_VPROC3_FY	 86875		/* 10uV */
#define CPU_DVFS_VOLT6_VPROC3_FY	 83750		/* 10uV */
#define CPU_DVFS_VOLT7_VPROC3_FY	 80625		/* 10uV */
#define CPU_DVFS_VOLT8_VPROC3_FY	 78125		/* 10uV */
#define CPU_DVFS_VOLT9_VPROC3_FY	 75000		/* 10uV */
#define CPU_DVFS_VOLT10_VPROC3_FY	 72500		/* 10uV */
#define CPU_DVFS_VOLT11_VPROC3_FY	 70625		/* 10uV */
#define CPU_DVFS_VOLT12_VPROC3_FY	 66875		/* 10uV */
#define CPU_DVFS_VOLT13_VPROC3_FY	 65000		/* 10uV */
#define CPU_DVFS_VOLT14_VPROC3_FY	 62500		/* 10uV */
#define CPU_DVFS_VOLT15_VPROC3_FY	 60000		/* 10uV */

/* 6885_TB */
/* for DVFS OPP table L */
#define CPU_DVFS_FREQ0_LL_TB            2000000         /* KHz */
#define CPU_DVFS_FREQ1_LL_TB            1895000         /* KHz */
#define CPU_DVFS_FREQ2_LL_TB		1791000		/* KHz */
#define CPU_DVFS_FREQ3_LL_TB		1708000		/* KHz */
#define CPU_DVFS_FREQ4_LL_TB		1625000		/* KHz */
#define CPU_DVFS_FREQ5_LL_TB		1500000		/* KHz */
#define CPU_DVFS_FREQ6_LL_TB		1393000		/* KHz */
#define CPU_DVFS_FREQ7_LL_TB		1287000		/* KHz */
#define CPU_DVFS_FREQ8_LL_TB		1181000		/* KHz */
#define CPU_DVFS_FREQ9_LL_TB		1048000		/* KHz */
#define CPU_DVFS_FREQ10_LL_TB		968000		/* KHz */
#define CPU_DVFS_FREQ11_LL_TB		862000		/* KHz */
#define CPU_DVFS_FREQ12_LL_TB		756000		/* KHz */
#define CPU_DVFS_FREQ13_LL_TB		703000		/* KHz */
#define CPU_DVFS_FREQ14_LL_TB		650000		/* KHz */
#define CPU_DVFS_FREQ15_LL_TB		500000		/* KHz */

/* for DVFS OPP table B */
#define CPU_DVFS_FREQ0_L_TB		2300000		/* KHz */
#define CPU_DVFS_FREQ1_L_TB		2202000		/* KHz */
#define CPU_DVFS_FREQ2_L_TB		2106000		/* KHz */
#define CPU_DVFS_FREQ3_L_TB		1993000		/* KHz */
#define CPU_DVFS_FREQ4_L_TB		1900000		/* KHz */
#define CPU_DVFS_FREQ5_L_TB		1825000		/* KHz */
#define CPU_DVFS_FREQ6_L_TB		1750000		/* KHz */
#define CPU_DVFS_FREQ7_L_TB		1622000		/* KHz */
#define CPU_DVFS_FREQ8_L_TB		1463000		/* KHz */
#define CPU_DVFS_FREQ9_L_TB		1367000		/* KHz */
#define CPU_DVFS_FREQ10_L_TB		1271000		/* KHz */
#define CPU_DVFS_FREQ11_L_TB		1176000		/* KHz */
#define CPU_DVFS_FREQ12_L_TB		1048000		/* KHz */
#define CPU_DVFS_FREQ13_L_TB		921000		/* KHz */
#define CPU_DVFS_FREQ14_L_TB		825000		/* KHz */
#define CPU_DVFS_FREQ15_L_TB		730000		/* KHz */

/* for DVFS OPP table CCI */
#define CPU_DVFS_FREQ0_CCI_TB		1540000		/* KHz */
#define CPU_DVFS_FREQ1_CCI_TB		1469000		/* KHz */
#define CPU_DVFS_FREQ2_CCI_TB		1426000		/* KHz */
#define CPU_DVFS_FREQ3_CCI_TB		1370000		/* KHz */
#define CPU_DVFS_FREQ4_CCI_TB		1313000		/* KHz */
#define CPU_DVFS_FREQ5_CCI_TB		1256000		/* KHz */
#define CPU_DVFS_FREQ6_CCI_TB		1195000		/* KHz */
#define CPU_DVFS_FREQ7_CCI_TB		1115000		/* KHz */
#define CPU_DVFS_FREQ8_CCI_TB		1030000		/* KHz */
#define CPU_DVFS_FREQ9_CCI_TB		945000		/* KHz */
#define CPU_DVFS_FREQ10_CCI_TB		881000		/* KHz */
#define CPU_DVFS_FREQ11_CCI_TB		817000		/* KHz */
#define CPU_DVFS_FREQ12_CCI_TB		711000		/* KHz */
#define CPU_DVFS_FREQ13_CCI_TB		668000		/* KHz */
#define CPU_DVFS_FREQ14_CCI_TB		583000		/* KHz */
#define CPU_DVFS_FREQ15_CCI_TB		520000		/* KHz */

/* for DVFS OPP table L */
#define CPU_DVFS_VOLT0_VPROC1_TB	100000		/* 10uV */
#define CPU_DVFS_VOLT1_VPROC1_TB	 96875		/* 10uV */
#define CPU_DVFS_VOLT2_VPROC1_TB	 93125		/* 10uV */
#define CPU_DVFS_VOLT3_VPROC1_TB	 90625		/* 10uV */
#define CPU_DVFS_VOLT4_VPROC1_TB	 88125		/* 10uV */
#define CPU_DVFS_VOLT5_VPROC1_TB	 83750		/* 10uV */
#define CPU_DVFS_VOLT6_VPROC1_TB	 80625		/* 10uV */
#define CPU_DVFS_VOLT7_VPROC1_TB	 77500		/* 10uV */
#define CPU_DVFS_VOLT8_VPROC1_TB	 75000		/* 10uV */
#define CPU_DVFS_VOLT9_VPROC1_TB	 71250		/* 10uV */
#define CPU_DVFS_VOLT10_VPROC1_TB	 68750		/* 10uV */
#define CPU_DVFS_VOLT11_VPROC1_TB	 65625		/* 10uV */
#define CPU_DVFS_VOLT12_VPROC1_TB	 63125		/* 10uV */
#define CPU_DVFS_VOLT13_VPROC1_TB	 61250		/* 10uV */
#define CPU_DVFS_VOLT14_VPROC1_TB	 60000		/* 10uV */
#define CPU_DVFS_VOLT15_VPROC1_TB	 60000		/* 10uV */

/* for DVFS OPP table B */
#define CPU_DVFS_VOLT0_VPROC2_TB	100000		/* 10uV	*/
#define CPU_DVFS_VOLT1_VPROC2_TB	96250		/* 10uV	*/
#define CPU_DVFS_VOLT2_VPROC2_TB	93125		/* 10uV	*/
#define CPU_DVFS_VOLT3_VPROC2_TB	89375		/* 10uV	*/
#define CPU_DVFS_VOLT4_VPROC2_TB	85625		/* 10uV	*/
#define CPU_DVFS_VOLT5_VPROC2_TB	83125		/* 10uV	*/
#define CPU_DVFS_VOLT6_VPROC2_TB	80625		/* 10uV	*/
#define CPU_DVFS_VOLT7_VPROC2_TB	78125		/* 10uV	*/
#define CPU_DVFS_VOLT8_VPROC2_TB	75000		/* 10uV	*/
#define CPU_DVFS_VOLT9_VPROC2_TB	73125		/* 10uV	*/
#define CPU_DVFS_VOLT10_VPROC2_TB	71250		/* 10uV	*/
#define CPU_DVFS_VOLT11_VPROC2_TB	68750		/* 10uV	*/
#define CPU_DVFS_VOLT12_VPROC2_TB	66250		/* 10uV	*/
#define CPU_DVFS_VOLT13_VPROC2_TB	63750		/* 10uV	*/
#define CPU_DVFS_VOLT14_VPROC2_TB	61875		/* 10uV	*/
#define CPU_DVFS_VOLT15_VPROC2_TB	60000		/* 10uV	*/

/* for DVFS OPP table CCI */
#define CPU_DVFS_VOLT0_VPROC3_TB	100000		/* 10uV */
#define CPU_DVFS_VOLT1_VPROC3_TB	 96875		/* 10uV */
#define CPU_DVFS_VOLT2_VPROC3_TB	 94375		/* 10uV */
#define CPU_DVFS_VOLT3_VPROC3_TB	 91875		/* 10uV */
#define CPU_DVFS_VOLT4_VPROC3_TB	 89375		/* 10uV */
#define CPU_DVFS_VOLT5_VPROC3_TB	 86875		/* 10uV */
#define CPU_DVFS_VOLT6_VPROC3_TB	 83750		/* 10uV */
#define CPU_DVFS_VOLT7_VPROC3_TB	 80625		/* 10uV */
#define CPU_DVFS_VOLT8_VPROC3_TB	 78125		/* 10uV */
#define CPU_DVFS_VOLT9_VPROC3_TB	 75000		/* 10uV */
#define CPU_DVFS_VOLT10_VPROC3_TB	 72500		/* 10uV */
#define CPU_DVFS_VOLT11_VPROC3_TB	 70625		/* 10uV */
#define CPU_DVFS_VOLT12_VPROC3_TB	 66875		/* 10uV */
#define CPU_DVFS_VOLT13_VPROC3_TB	 65000		/* 10uV */
#define CPU_DVFS_VOLT14_VPROC3_TB	 62500		/* 10uV */
#define CPU_DVFS_VOLT15_VPROC3_TB	 60000		/* 10uV */


/* DVFS OPP table */
#define OPP_TBL(cluster, seg, lv, vol)	\
static struct mt_cpu_freq_info opp_tbl_##cluster##_e##lv##_0[] = {        \
	OP                                                                \
(CPU_DVFS_FREQ0_##cluster##_##seg, CPU_DVFS_VOLT0_VPROC##vol##_##seg),   \
	OP                                                               \
(CPU_DVFS_FREQ1_##cluster##_##seg, CPU_DVFS_VOLT1_VPROC##vol##_##seg),   \
	OP                                                               \
(CPU_DVFS_FREQ2_##cluster##_##seg, CPU_DVFS_VOLT2_VPROC##vol##_##seg),   \
	OP                                                               \
(CPU_DVFS_FREQ3_##cluster##_##seg, CPU_DVFS_VOLT3_VPROC##vol##_##seg),   \
	OP                                                               \
(CPU_DVFS_FREQ4_##cluster##_##seg, CPU_DVFS_VOLT4_VPROC##vol##_##seg),   \
	OP                                                               \
(CPU_DVFS_FREQ5_##cluster##_##seg, CPU_DVFS_VOLT5_VPROC##vol##_##seg),   \
	OP                                                               \
(CPU_DVFS_FREQ6_##cluster##_##seg, CPU_DVFS_VOLT6_VPROC##vol##_##seg),   \
	OP                                                               \
(CPU_DVFS_FREQ7_##cluster##_##seg, CPU_DVFS_VOLT7_VPROC##vol##_##seg),   \
	OP                                                               \
(CPU_DVFS_FREQ8_##cluster##_##seg, CPU_DVFS_VOLT8_VPROC##vol##_##seg),   \
	OP                                                               \
(CPU_DVFS_FREQ9_##cluster##_##seg, CPU_DVFS_VOLT9_VPROC##vol##_##seg),   \
	OP                                                                \
(CPU_DVFS_FREQ10_##cluster##_##seg, CPU_DVFS_VOLT10_VPROC##vol##_##seg), \
	OP                                                               \
(CPU_DVFS_FREQ11_##cluster##_##seg, CPU_DVFS_VOLT11_VPROC##vol##_##seg), \
	OP                                                               \
(CPU_DVFS_FREQ12_##cluster##_##seg, CPU_DVFS_VOLT12_VPROC##vol##_##seg), \
	OP                                                               \
(CPU_DVFS_FREQ13_##cluster##_##seg, CPU_DVFS_VOLT13_VPROC##vol##_##seg), \
	OP                                                               \
(CPU_DVFS_FREQ14_##cluster##_##seg, CPU_DVFS_VOLT14_VPROC##vol##_##seg), \
	OP                                                               \
(CPU_DVFS_FREQ15_##cluster##_##seg, CPU_DVFS_VOLT15_VPROC##vol##_##seg), \
}

OPP_TBL(LL,   FY, 0, 1); /* opp_tbl_LL_e0_0   */
OPP_TBL(L,  FY, 0, 2); /* opp_tbl_L_e0_0  */
OPP_TBL(CCI, FY, 0, 3); /* opp_tbl_CCI_e0_0 */

OPP_TBL(LL,   TB, 1, 1); /* opp_tbl_LL_e1_0   */
OPP_TBL(L,  TB, 1, 2); /* opp_tbl_L_e1_0  */
OPP_TBL(CCI, TB, 1, 3); /* opp_tbl_CCI_e1_0 */

/* v1.3 */
static struct opp_tbl_info opp_tbls[NR_MT_CPU_DVFS][NUM_CPU_LEVEL] = {
	/* LL */
	{
		[CPU_LEVEL_0] = { opp_tbl_LL_e0_0,
			ARRAY_SIZE(opp_tbl_LL_e0_0) },
		[CPU_LEVEL_1] = { opp_tbl_LL_e1_0,
			ARRAY_SIZE(opp_tbl_LL_e1_0) },

	},
	/* L */
	{
		[CPU_LEVEL_0] = { opp_tbl_L_e0_0,
			ARRAY_SIZE(opp_tbl_L_e0_0) },
		[CPU_LEVEL_1] = { opp_tbl_L_e1_0,
			ARRAY_SIZE(opp_tbl_L_e1_0) },


	},
	/* CCI */
	{
		[CPU_LEVEL_0] = { opp_tbl_CCI_e0_0,
			ARRAY_SIZE(opp_tbl_CCI_e0_0) },
		[CPU_LEVEL_1] = { opp_tbl_CCI_e1_0,
			ARRAY_SIZE(opp_tbl_CCI_e1_0) },

	},
};

/* 16 steps OPP table */
static struct mt_cpu_freq_method opp_tbl_method_LL_FY[] = {	/* 6885 */
	/* POS,	CLK */
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(4,	1),
	FP(4,	1),
	FP(4,	1),
};

static struct mt_cpu_freq_method opp_tbl_method_L_FY[] = {	/* 6885 */
	/* POS,	CLK */
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(4,	1),
};

static struct mt_cpu_freq_method opp_tbl_method_CCI_FY[] = {	/* 6885 */
	/* POS,	CLK */
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(4,	1),
	FP(4,	1),
	FP(4,	1),
	FP(4,	1),
};

static struct mt_cpu_freq_method opp_tbl_method_LL_TB[] = {	/* 6885 */
	/* POS,	CLK */
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(4,	1),
	FP(4,	1),
	FP(4,	1),
};

static struct mt_cpu_freq_method opp_tbl_method_L_TB[] = {	/* 6885 */
	/* POS,	CLK */
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(1,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(4,	1),
};

static struct mt_cpu_freq_method opp_tbl_method_CCI_TB[] = {	/* 6885 */
	/* POS,	CLK */
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(2,	1),
	FP(4,	1),
	FP(4,	1),
	FP(4,	1),
	FP(4,	1),
};

struct opp_tbl_m_info opp_tbls_m[NR_MT_CPU_DVFS][NUM_CPU_LEVEL] = {
	/* LL */
	{
		[CPU_LEVEL_0] = { opp_tbl_method_LL_FY },
		[CPU_LEVEL_1] = { opp_tbl_method_LL_FY },

	},
	/* L */
	{
		[CPU_LEVEL_0] = { opp_tbl_method_L_FY },
		[CPU_LEVEL_1] = { opp_tbl_method_L_FY },

	},
	/* CCI */
	{
		[CPU_LEVEL_0] = { opp_tbl_method_CCI_FY },
		[CPU_LEVEL_1] = { opp_tbl_method_CCI_FY },

	},
};
