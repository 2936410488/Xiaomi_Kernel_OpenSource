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

#ifndef _HAL_CONFIG_POWER_H_
#define _HAL_CONFIG_POWER_H_

#include "apusys_power_cust.h"


/************************************
 * command base hal interface
 ************************************/
enum HAL_POWER_CMD {
	PWR_CMD_INIT_POWER,		// 0
	PWR_CMD_SET_BOOT_UP,		// 1
	PWR_CMD_SET_SHUT_DOWN,		// 2
	PWR_CMD_SET_VOLT,		// 3
	PWR_CMD_SET_REGULATOR_MODE,	// 4
	PWR_CMD_SET_MTCMOS,		// 5
	PWR_CMD_SET_CLK,		// 6
	PWR_CMD_SET_FREQ,		// 7
	PWR_CMD_GET_POWER_INFO,		// 8
	PWR_CMD_REG_DUMP,		// 9
	PWR_CMD_UNINIT_POWER,		//10
};


/************************************
 * command base hal param struct
 ************************************/

struct hal_param_init_power {
	struct device *dev;
	void *rpc_base_addr;
	void *pcu_base_addr;
	void *vcore_base_addr;
	void *infracfg_ao_base_addr;
	void *infra_bcrm_base_addr;
	void *conn_base_addr;
	void *vpu0_base_addr;
	void *vpu1_base_addr;
	void *vpu2_base_addr;
	void *mdla0_base_addr;
	void *mdla1_base_addr;
};

// regulator only
struct hal_param_volt {
	enum DVFS_BUCK target_buck;
	enum DVFS_VOLTAGE target_volt;
};

// regulator only, target_mode range : 0 and 1
struct hal_param_regulator_mode {
	enum DVFS_BUCK target_buck;
	int target_mode;
};

// mtcmos only
struct hal_param_mtcmos {
	int enable;
};

// cg and clk
struct hal_param_clk {
	int enable;
};

// freq only
struct hal_param_freq {
	enum DVFS_VOLTAGE_DOMAIN target_volt_domain;
	enum DVFS_FREQ target_freq;
};

struct hal_param_pwr_info {
	uint64_t id;
};

struct hal_param_pwr_mask {
	uint8_t power_bit_mask;
};

/************************************
 * common power config function
 ************************************/
int hal_config_power(enum HAL_POWER_CMD, enum DVFS_USER, void *param);

#endif // _HAL_CONFIG_POWER_H_
