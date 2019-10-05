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

#ifndef _APU_POWER_API_H_
#define _APU_POWER_API_H_

#include "apusys_power_cust.h"

// FIXME: should match config of 6885
struct apu_power_info {
	unsigned int dump_div;
	unsigned int vvpu;
	unsigned int vmdla;
	unsigned int vcore;
	unsigned int vsram;
	unsigned int dsp_freq;		// dsp conn
	unsigned int dsp1_freq;		// vpu core0
	unsigned int dsp2_freq;		// vpu core1
	unsigned int dsp3_freq;		// mdla core
	unsigned int ipuif_freq;	// ipu intf.
	unsigned int max_opp_limit;
	unsigned int min_opp_limit;
	unsigned int thermal_cond;
	unsigned int power_lock;
	unsigned long long id;
};

//APU
void pm_qos_register(void);
void pm_qos_unregister(void);
int prepare_regulator(enum DVFS_BUCK buck, struct device *dev);
int unprepare_regulator(enum DVFS_BUCK buck);
int config_normal_regulator(enum DVFS_BUCK buck, enum DVFS_VOLTAGE voltage_mV);
int config_regulator_mode(enum DVFS_BUCK buck, int is_normal);
int config_vcore(enum DVFS_USER user, int vcore_opp);
void dump_voltage(struct apu_power_info *info);
void dump_frequency(struct apu_power_info *info);
int set_apu_clock_source(enum DVFS_FREQ freq, enum DVFS_VOLTAGE_DOMAIN domain);
int prepare_apu_clock(struct device *dev);
void unprepare_apu_clock(void);
void disable_apu_clock(enum DVFS_USER);
void enable_apu_clock(enum DVFS_USER);

#endif // _APU_POWER_API_H_
