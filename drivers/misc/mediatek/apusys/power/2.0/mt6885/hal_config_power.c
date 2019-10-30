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

#include <linux/delay.h>
#include "hal_config_power.h"
#include "apu_power_api.h"
#include "apusys_power_cust.h"
#include "apusys_power_reg.h"
#include "apu_log.h"
#include <helio-dvfsrc-opp.h>

static int is_apu_power_initilized;
static int force_pwr_on = 1;
static int force_pwr_off;
static int conn_mtcmos_on;
static int buck_already_on;
static int power_on_counter;

void *g_APU_RPCTOP_BASE;
void *g_APU_PCUTOP_BASE;
void *g_APU_VCORE_BASE;
void *g_APU_INFRACFG_AO_BASE;
void *g_APU_INFRA_BCRM_BASE;
void *g_APU_CONN_BASE;
void *g_APU_VPU0_BASE;
void *g_APU_VPU1_BASE;
void *g_APU_VPU2_BASE;
void *g_APU_MDLA0_BASE;
void *g_APU_MDLA1_BASE;
void *g_APU_SPM_BASE;

/************************************
 * platform related power APIs
 ************************************/

static int init_power_resource(void *param);
static int set_power_boot_up(enum DVFS_USER, void *param);
static int set_power_shut_down(enum DVFS_USER, void *param);
static int set_power_voltage(enum DVFS_USER, void *param);
static int set_power_regulator_mode(void *param);
static int set_power_mtcmos(enum DVFS_USER, void *param);
static int set_power_clock(enum DVFS_USER, void *param);
static int set_power_frequency(void *param);
static void get_current_power_info(void *param);
static int uninit_power_resource(void);
static int apusys_power_reg_dump(void);
static void debug_power_mtcmos_on(void);
static void debug_power_mtcmos_off(void);
static void hw_init_setting(void);
static int buck_control(enum DVFS_USER user, int level);
static int rpc_power_status_check(int domain_idx, unsigned int enable);

/************************************
 * common power hal command
 ************************************/

int hal_config_power(enum HAL_POWER_CMD cmd, enum DVFS_USER user, void *param)
{
	int ret = 0;

	LOG_DBG("%s power command : %d, by user : %d\n", __func__, cmd, user);

	if (cmd != PWR_CMD_INIT_POWER && is_apu_power_initilized == 0) {
		LOG_ERR("%s apu power state : %d, force return!\n",
					__func__, is_apu_power_initilized);
		return -1;
	}

	switch (cmd) {
	case PWR_CMD_INIT_POWER:
		ret = init_power_resource(param);
		break;
	case PWR_CMD_SET_BOOT_UP:
		ret = set_power_boot_up(user, param);
		break;
	case PWR_CMD_SET_SHUT_DOWN:
		ret = set_power_shut_down(user, param);
		break;
	case PWR_CMD_SET_VOLT:
		ret = set_power_voltage(user, param);
		break;
	case PWR_CMD_SET_REGULATOR_MODE:
		ret = set_power_regulator_mode(param);
		break;
// do not control mtcmos and clock individually
#if 0
	case PWR_CMD_SET_MTCMOS:
		ret = set_power_mtcmos(user, param);
		break;
	case PWR_CMD_SET_CLK:
		ret = set_power_clock(user, param);
		break;
#endif
	case PWR_CMD_SET_FREQ:
		ret = set_power_frequency(param);
		break;
	case PWR_CMD_GET_POWER_INFO:
		get_current_power_info(param);
		break;
	case PWR_CMD_REG_DUMP:
		apusys_power_reg_dump();
		break;
	case PWR_CMD_UNINIT_POWER:
		ret = uninit_power_resource();
		break;
	case PWR_CMD_DEBUG_MTCMOS_ON:
		debug_power_mtcmos_on();
		break;
	case PWR_CMD_DEBUG_MTCMOS_OFF:
		debug_power_mtcmos_off();
		break;
	default:
		LOG_ERR("%s unknown power command : %d\n", __func__, cmd);
		return -1;
	}

	return ret;
}


/************************************
 * utility function
 ************************************/

// vcore voltage p to vcore opp
static enum vcore_opp volt_to_vcore_opp(int target_volt)
{
	int opp;

	for (opp = 0 ; opp < VCORE_OPP_NUM ; opp++)
		if (vcore_opp_mapping[opp] == target_volt)
			break;

	if (opp >= VCORE_OPP_NUM) {
		LOG_ERR("%s failed, force to set opp 0\n", __func__);
		return VCORE_OPP_0;
	}

	LOG_WRN("%s opp = %d\n", __func__, opp);
	return (enum vcore_opp)opp;
}

static void prepare_apu_regulator(struct device *dev, int prepare)
{
	if (prepare) {
		// obtain regulator handle
		prepare_regulator(VCORE_BUCK, dev);
		prepare_regulator(SRAM_BUCK, dev);
		prepare_regulator(VPU_BUCK, dev);
		prepare_regulator(MDLA_BUCK, dev);

		// register pm_qos notifier here,
		// vcore need to use pm_qos for voltage voting
		pm_qos_register();
	} else {
		// release regulator handle
		unprepare_regulator(MDLA_BUCK);
		unprepare_regulator(VPU_BUCK);
		unprepare_regulator(SRAM_BUCK);
		unprepare_regulator(VCORE_BUCK);

		// unregister pm_qos notifier here,
		pm_qos_unregister();
	}
}

/******************************************
 * hal cmd corresponding platform function
 ******************************************/

static void hw_init_setting(void)
{
	uint32_t regValue = 0;

	/* set memory type to PD or sleep */

	// MD32 sleep type
	DRV_WriteReg32(APU_RPC_SW_TYPE0, 0x6F);

	// IMEM_ICACHE sleep type for VPU0
	DRV_WriteReg32(APU_RPC_SW_TYPE2, 0x2);

	// IMEM_ICACHE sleep type for VPU1
	DRV_WriteReg32(APU_RPC_SW_TYPE3, 0x2);

	// IMEM_ICACHE sleep type for VPU2
	DRV_WriteReg32(APU_RPC_SW_TYPE4, 0x2);

	// mask RPC IRQ and bypass WFI
	regValue = DRV_Reg32(APU_RPC_TOP_SEL);
	regValue |= 0x9E;
	regValue |= BIT(10);
	DRV_WriteReg32(APU_RPC_TOP_SEL, regValue);

	udelay(100);

#if !BYPASS_POWER_OFF
	// sleep request enable
	regValue = DRV_Reg32(APU_RPC_TOP_CON);
	regValue |= 0x1;
	DRV_WriteReg32(APU_RPC_TOP_CON, regValue);

	rpc_power_status_check(0, 0);
	LOG_WRN("%s done and request to enter sleep\n", __func__);
#else
	LOG_WRN("%s done\n", __func__);
#endif
}

static int init_power_resource(void *param)
{
	struct hal_param_init_power *init_data = NULL;
	struct device *dev = NULL;

	init_data = (struct hal_param_init_power *)param;

	dev = init_data->dev;
	g_APU_RPCTOP_BASE = init_data->rpc_base_addr;
	g_APU_PCUTOP_BASE = init_data->pcu_base_addr;
	g_APU_VCORE_BASE = init_data->vcore_base_addr;
	g_APU_INFRACFG_AO_BASE = init_data->infracfg_ao_base_addr;
	g_APU_INFRA_BCRM_BASE = init_data->infra_bcrm_base_addr;
	g_APU_SPM_BASE = init_data->spm_base_addr;

	g_APU_CONN_BASE = init_data->conn_base_addr;
	g_APU_VPU0_BASE = init_data->vpu0_base_addr;
	g_APU_VPU1_BASE = init_data->vpu1_base_addr;
	g_APU_VPU2_BASE = init_data->vpu2_base_addr;
	g_APU_MDLA0_BASE = init_data->mdla0_base_addr;
	g_APU_MDLA1_BASE = init_data->mdla1_base_addr;

	LOG_DBG("%s , g_APU_RPCTOP_BASE 0x%p\n", __func__, g_APU_RPCTOP_BASE);
	LOG_DBG("%s , g_APU_PCUTOP_BASE 0x%p\n", __func__, g_APU_PCUTOP_BASE);
	LOG_DBG("%s , g_APU_VCORE_BASE 0x%p\n", __func__, g_APU_VCORE_BASE);
	LOG_DBG("%s , g_APU_INFRACFG_AO_BASE 0x%p\n", __func__,
						g_APU_INFRACFG_AO_BASE);
	LOG_DBG("%s , g_APU_INFRA_BCRM_BASE 0x%p\n", __func__,
						g_APU_INFRA_BCRM_BASE);

	LOG_DBG("%s , g_APU_CONN_BASE 0x%p\n", __func__, g_APU_CONN_BASE);
	LOG_DBG("%s , g_APU_VPU0_BASE 0x%p\n", __func__, g_APU_VPU0_BASE);
	LOG_DBG("%s , g_APU_VPU1_BASE 0x%p\n", __func__, g_APU_VPU1_BASE);
	LOG_DBG("%s , g_APU_VPU2_BASE 0x%p\n", __func__, g_APU_VPU2_BASE);
	LOG_DBG("%s , g_APU_MDLA0_BASE 0x%p\n", __func__, g_APU_MDLA0_BASE);
	LOG_DBG("%s , g_APU_MDLA1_BASE 0x%p\n", __func__, g_APU_MDLA1_BASE);
	LOG_DBG("%s , g_APU_SPM_BASE 0x%p\n", __func__, g_APU_SPM_BASE);

	if (!is_apu_power_initilized) {
		prepare_apu_regulator(dev, 1);
#ifndef MTK_FPGA_PORTING
		prepare_apu_clock(dev);
#endif
		is_apu_power_initilized = 1;
	}
	enable_apu_vcore_clksrc();
	enable_apu_conn_clksrc();
	hw_init_setting();
	set_apu_clock_source(DVFS_FREQ_00_026000_F, V_VCORE);
	disable_apu_conn_clksrc();

	buck_control(VPU0, 3); // buck on
	udelay(100);
	buck_control(VPU0, 0); // buck off

	return 0;
}

static int set_power_voltage(enum DVFS_USER user, void *param)
{
	enum DVFS_BUCK buck = 0;
	int target_volt = 0;
	int ret = 0;

	buck = ((struct hal_param_volt *)param)->target_buck;
	target_volt = ((struct hal_param_volt *)param)->target_volt;

	if (buck < APUSYS_BUCK_NUM) {
		if (buck != VCORE_BUCK) {
			LOG_DBG("%s set buck %d to %d\n", __func__,
						buck, target_volt);

			if (target_volt >= 0) {
				ret = config_normal_regulator(
						buck, target_volt);
			}

		} else {
			ret = config_vcore(user,
					volt_to_vcore_opp(target_volt));
		}
	} else {
		LOG_ERR("%s not support buck : %d\n", __func__, buck);
	}

	return ret;
}

static int set_power_regulator_mode(void *param)
{
	enum DVFS_BUCK buck = 0;
	int is_normal = 0;
	int ret = 0;

	buck = ((struct hal_param_regulator_mode *)param)->target_buck;
	is_normal = ((struct hal_param_regulator_mode *)param)->target_mode;

	ret = config_regulator_mode(buck, is_normal);
	return ret;
}


static void rpc_fifo_check(void)
{
#if 1
	unsigned int regValue = 0;
	unsigned int finished = 1;
	unsigned int check_round = 0;

	do {
		udelay(10);
		regValue = DRV_Reg32(APU_RPC_TOP_CON);
		finished = (regValue & BIT(31));

		if (++check_round >= REG_POLLING_TIMEOUT_ROUNDS) {
			LOG_ERR("%s timeout !\n", __func__);
			break;
		}
	} while (finished);
#else
	udelay(500);
#endif
}


static int rpc_power_status_check(int domain_idx, unsigned int enable)
{
	unsigned int regValue = 0;
#if 1
	unsigned int finished = 0;
	unsigned int check_round = 0;

	do {
		udelay(10);
		regValue = DRV_Reg32(APU_RPC_INTF_PWR_RDY);

		if (enable)
			finished = !((regValue >> domain_idx) & 0x1);
		else
			finished = (regValue >> domain_idx) & 0x1;

		if (++check_round >= REG_POLLING_TIMEOUT_ROUNDS) {
			LOG_ERR("%s APU_RPC_INTF_PWR_RDY = 0x%x, timeout !\n",
							__func__, regValue);
			return -1;
		}

	} while (finished);
#else
	udelay(500);
	regValue = DRV_Reg32(APU_RPC_INTF_PWR_RDY);
#endif
	LOG_WRN("%s APU_RPC_INTF_PWR_RDY = 0x%x\n", __func__, regValue);
	return 0;
}

static int check_mtcmos_all_power_state(void)
{
	unsigned int regValue = DRV_Reg32(APU_RPC_INTF_PWR_RDY);

	// check if all mtcmos bit equals to zero
	if (((regValue >> 2) & 0x1) == 0x0 &&
			((regValue >> 3) & 0x1) == 0x0 &&
			((regValue >> 4) & 0x1) == 0x0 &&
			((regValue >> 6) & 0x1) == 0x0 &&
			((regValue >> 7) & 0x1) == 0x0)
		return 1;
	else
		return 0;
}

static int set_power_mtcmos(enum DVFS_USER user, void *param)
{
	unsigned int enable = ((struct hal_param_mtcmos *)param)->enable;
	unsigned int domain_idx = 0;
	unsigned int regValue = 0;
	int retry = 0;
	int ret = 0;

	LOG_INF("%s , user: %d , enable: %d\n", __func__, user, enable);

	if (user == EDMA)
		domain_idx = 0;
	else if (user == VPU0)
		domain_idx = 2;
	else if (user == VPU1)
		domain_idx = 3;
	else if (user == VPU2)
		domain_idx = 4;
	else if (user == MDLA0)
		domain_idx = 6;
	else if (user == MDLA1)
		domain_idx = 7;
	else
		LOG_WRN("%s not support user : %d\n", __func__, user);

	if (enable) {
		// call spm api to enable wake up signal for apu_conn/apu_vcore
		if (force_pwr_on ||
			(DRV_Reg32(APU_RPC_INTF_PWR_RDY) & BIT(0)) == 0x0) {
			LOG_WRN("%s enable wakeup signal\n", __func__);


			ret |= enable_apu_conn_clksrc();
			ret |= set_apu_clock_source(DVFS_FREQ_00_208000_F,
								V_VCORE);

			// CCF API assist to enable clock source of apu conn
			ret |= enable_apu_mtcmos(1);

			// wait for conn mtcmos enable ready
			ret |= rpc_power_status_check(0, 1);

			// clear inner dummy CG (true enable but bypass disable)
			ret |= enable_apu_conn_vcore_clock();

			force_pwr_on = 0;
			conn_mtcmos_on = 1;
		}

		// EDMA do not need to control mtcmos by rpc
		if (user < APUSYS_DVFS_USER_NUM && !ret) {
			// enable clock source of this device first
			ret |= enable_apu_device_clksrc(user);

			do {
				rpc_fifo_check();
				// BIT(4) to Power on
				DRV_WriteReg32(APU_RPC_SW_FIFO_WE,
					(domain_idx | BIT(4)));
				LOG_WRN("%s APU_RPC_SW_FIFO_WE write 0x%lx\n",
					__func__, (domain_idx | BIT(4)));

				if (retry >= 3) {
					LOG_ERR("%s fail (user:%d, mode:%d)\n",
							__func__, user, enable);
					disable_apu_device_clksrc(user);
					return -1;
				}
				retry++;
			} while (rpc_power_status_check(domain_idx, enable));
		}

	} else {

		// EDMA do not need to control mtcmos by rpc
		if (user < APUSYS_DVFS_USER_NUM) {
			do {
				rpc_fifo_check();
				DRV_WriteReg32(APU_RPC_SW_FIFO_WE, domain_idx);
				LOG_WRN("%s APU_RPC_SW_FIFO_WE write %u\n",
					__func__, domain_idx);

				if (retry >= 3) {
					LOG_ERR("%s fail (user:%d, mode:%d)\n",
							__func__, user, enable);
					return -1;
				}
				retry++;
			} while (rpc_power_status_check(domain_idx, enable));

			// disable clock source of this device
			disable_apu_device_clksrc(user);
		}

		// only remained apu_top is power on
		if (force_pwr_off || check_mtcmos_all_power_state()) {
		/*
		 * call spm api to disable wake up signal
		 * for apu_conn/apu_vcore
		 */
			// inner dummy cg won't be gated when you call disable
			disable_apu_conn_vcore_clock();

			ret |= enable_apu_mtcmos(0);
			//udelay(100);

			// mask RPC IRQ and bypass WFI
			regValue = DRV_Reg32(APU_RPC_TOP_SEL);
			regValue |= 0x9E;
			DRV_WriteReg32(APU_RPC_TOP_SEL, regValue);

			// sleep request enable
			regValue = DRV_Reg32(APU_RPC_TOP_CON);
			regValue |= 0x1;
			DRV_WriteReg32(APU_RPC_TOP_CON, regValue);

			ret |= rpc_power_status_check(0, 0);

			ret |= set_apu_clock_source(DVFS_FREQ_00_026000_F,
								V_VCORE);
			disable_apu_conn_clksrc();

			force_pwr_off = 0;
			conn_mtcmos_on = 0;
		}
	}

	return ret;
}

static int set_power_clock(enum DVFS_USER user, void *param)
{
	int ret = 0;
#ifndef MTK_FPGA_PORTING
	int enable = ((struct hal_param_clk *)param)->enable;

	LOG_DBG("%s , user: %d , enable: %d\n", __func__, user, enable);

	if (enable)
		ret = enable_apu_device_clock(user);
	else
		// inner dummy cg won't be gated when you call disable
		disable_apu_device_clock(user);
#endif
	return ret;
}

static int set_power_frequency(void *param)
{
	enum DVFS_VOLTAGE_DOMAIN domain = 0;
	enum DVFS_FREQ freq = 0;
	int ret = 0;

	freq = ((struct hal_param_freq *)param)->target_freq;
	domain = ((struct hal_param_freq *)param)->target_volt_domain;

	if (domain < APUSYS_BUCK_DOMAIN_NUM) {
		if (domain == V_MDLA0 || domain == V_MDLA1)
			ret = config_apupll(freq, domain);
		else
			ret = set_apu_clock_source(freq, domain);
	} else {
		LOG_ERR("%s not support power domain : %d\n", __func__, domain);
	}

	return ret;
}

static void get_current_power_info(void *param)
{
	struct apu_power_info *info = ((struct apu_power_info *)param);
	char log_str[60];

	info->dump_div = 1000;

	// including APUsys related buck
	dump_voltage(info);

	// including APUsys related freq
	dump_frequency(info);

	snprintf(log_str, sizeof(log_str),
			"v[%u,%u,%u,%u]f[%u,%u,%u,%u,%u,%u,%u,%u]%llu",
			info->vvpu, info->vmdla, info->vcore, info->vsram,
			info->dsp_freq, info->dsp1_freq, info->dsp2_freq,
			info->dsp3_freq, info->dsp6_freq, info->dsp7_freq,
			info->apupll_freq, info->ipuif_freq, info->id);

	// TODO: return value to MET

	LOG_WRN("APUPWR %s\n", log_str);
}

static int uninit_power_resource(void)
{
	if (is_apu_power_initilized) {
		buck_control(VPU0, 0); // buck off
		buck_already_on = 0;
		udelay(100);
#ifndef MTK_FPGA_PORTING
		unprepare_apu_clock();
#endif
		prepare_apu_regulator(NULL, 0);
		is_apu_power_initilized = 0;
	}

	return 0;
}

/*
 * control buck to four different levels -
 *	level 3 : buck ON
 *	level 2 : buck to default voltage
 *	level 1 : buck to low voltage
 *	level 0 : buck OFF
 */
static int buck_control(enum DVFS_USER user, int level)
{
	struct hal_param_volt vpu_volt_data;
	struct hal_param_volt mdla_volt_data;
	struct hal_param_volt vcore_volt_data;
	struct hal_param_volt sram_volt_data;
	struct apu_power_info info;
	int ret = 0;

	LOG_WRN("%s begin, level = %d\n", __func__, level);

	if (level == 3) { // buck ON
		// just turn on buck
		enable_regulator(VPU_BUCK);
		enable_regulator(MDLA_BUCK);
		enable_regulator(SRAM_BUCK);

		// release buck isolation
		DRV_ClearBitReg32(BUCK_ISOLATION, (BIT(0) | BIT(5)));

	} else if (level == 2) { // default voltage

		vcore_volt_data.target_buck = VCORE_BUCK;
		vcore_volt_data.target_volt = VCORE_DEFAULT_VOLT;
		ret |= set_power_voltage(VPU0, (void *)&vcore_volt_data);

		vpu_volt_data.target_buck = VPU_BUCK;
		vpu_volt_data.target_volt = VSRAM_TRANS_VOLT;
		ret |= set_power_voltage(user, (void *)&vpu_volt_data);

		mdla_volt_data.target_buck = MDLA_BUCK;
		mdla_volt_data.target_volt = VSRAM_TRANS_VOLT;
		ret |= set_power_voltage(user, (void *)&mdla_volt_data);

		sram_volt_data.target_buck = SRAM_BUCK;
		sram_volt_data.target_volt = VSRAM_DEFAULT_VOLT;
		ret |= set_power_voltage(user, (void *)&sram_volt_data);

		vpu_volt_data.target_buck = VPU_BUCK;
		vpu_volt_data.target_volt = VVPU_DEFAULT_VOLT;
		ret |= set_power_voltage(user, (void *)&vpu_volt_data);

		mdla_volt_data.target_buck = MDLA_BUCK;
		mdla_volt_data.target_volt = VMDLA_DEFAULT_VOLT;
		ret |= set_power_voltage(user, (void *)&mdla_volt_data);

	} else {

		/*
		 * to avoid vmdla/vvpu constraint,
		 * adjust to transition voltage first.
		 */
		mdla_volt_data.target_buck = MDLA_BUCK;
		mdla_volt_data.target_volt = VSRAM_TRANS_VOLT;
		ret |= set_power_voltage(user, (void *)&mdla_volt_data);

		vpu_volt_data.target_buck = VPU_BUCK;
		vpu_volt_data.target_volt = VSRAM_TRANS_VOLT;
		ret |= set_power_voltage(user, (void *)&vpu_volt_data);

		sram_volt_data.target_buck = SRAM_BUCK;
		sram_volt_data.target_volt = VSRAM_TRANS_VOLT;
		ret |= set_power_voltage(user, (void *)&sram_volt_data);

		vcore_volt_data.target_buck = VCORE_BUCK;
		vcore_volt_data.target_volt = VCORE_SHUTDOWN_VOLT;
		ret |= set_power_voltage(VPU0,
				(void *)&vcore_volt_data);

		if (level == 1) { // buck adjust to low voltage
			/*
			 * then adjust vmdla/vvpu again to real default voltage
			 */
			mdla_volt_data.target_buck = MDLA_BUCK;
			mdla_volt_data.target_volt = VMDLA_SHUTDOWN_VOLT;
			ret |= set_power_voltage(user, (void *)&mdla_volt_data);

			vpu_volt_data.target_buck = VPU_BUCK;
			vpu_volt_data.target_volt = VVPU_SHUTDOWN_VOLT;
			ret |= set_power_voltage(user, (void *)&vpu_volt_data);

		} else { // buck OFF
			// enable buck isolation
			DRV_SetBitReg32(BUCK_ISOLATION, (BIT(0) | BIT(5)));

			// just turn off buck and don't release regulator handle
			disable_regulator(SRAM_BUCK);
			disable_regulator(VPU_BUCK);
			disable_regulator(MDLA_BUCK);
		}
	}

	info.dump_div = 1000;
	info.id = 0;
	dump_voltage(&info);

	LOG_WRN("%s end, level = %d\n", __func__, level);
	return ret;
}

static int set_power_boot_up(enum DVFS_USER user, void *param)
{
	struct hal_param_mtcmos mtcmos_data;
	struct hal_param_clk clk_data;
	int ret = 0;

	if (!buck_already_on) {
		buck_control(user, 3); // buck on
		buck_already_on = 1;
		udelay(100);
	}

	if (power_on_counter == 0) {

		buck_control(user, 2); // default voltage

		// FIXME: Set mtcmos disable first to avoid conflict with iommu
		force_pwr_on = 1;
	}

	// Set mtcmos enable
	mtcmos_data.enable = 1;
	ret = set_power_mtcmos(user, (void *)&mtcmos_data);

	if (!ret && user < APUSYS_DVFS_USER_NUM) {
		// Set cg enable
		clk_data.enable = 1;
		ret |= set_power_clock(user, (void *)&clk_data);
	}

	if (ret)
		LOG_ERR("%s fail, ret = %d\n", __func__, ret);
	else
		LOG_DBG("%s pass, ret = %d\n", __func__, ret);

	power_on_counter++;
	return ret;
}


static int set_power_shut_down(enum DVFS_USER user, void *param)
{
	struct hal_param_mtcmos mtcmos_data;
	struct hal_param_clk clk_data;
	int ret = 0;

	if (user < APUSYS_DVFS_USER_NUM) {
		// inner dummy cg won't be gated when you call disable
		clk_data.enable = 0;
		ret = set_power_clock(user, (void *)&clk_data);
	}

	if (power_on_counter == 1)
		force_pwr_off = 1;

	// Set mtcmos disable
	mtcmos_data.enable = 0;
	ret |= set_power_mtcmos(user, (void *)&mtcmos_data);

	if (power_on_counter == 1 && buck_already_on) {
		buck_control(user, 0); // buck off
		buck_already_on = 0;
	}

	if (ret)
		LOG_ERR("%s fail, ret = %d\n", __func__, ret);
	else
		LOG_DBG("%s pass, ret = %d\n", __func__, ret);

	power_on_counter--;
	return ret;
}

static int apusys_power_reg_dump(void)
{
	unsigned int regVal = 0x0;

	// keep 26M vcore clk make we can dump reg directly
#if 0
	if (conn_mtcmos_on == 0) {
		LOG_ERR("APUREG APU_RPC_INTF_PWR_RDY dump fail (mtcmos off)\n");
		return -1;
	}
#endif
	regVal = DRV_Reg32(APU_RPC_INTF_PWR_RDY);

	// dump mtcmos status
	LOG_WRN("APUREG APU_RPC_INTF_PWR_RDY = 0x%x, conn_mtcmos_on = %d\n",
							regVal, conn_mtcmos_on);

	if (((regVal & BIT(0))) == 0x1) {
		LOG_WRN("APUREG APU_VCORE_CG_CON = 0x%x\n",
					DRV_Reg32(APU_VCORE_CG_CON));
		LOG_WRN("APUREG APU_CONN_CG_CON = 0x%x\n",
					DRV_Reg32(APU_CONN_CG_CON));
	} else {
		LOG_WRN("APUREG conn_vcore mtcmos not ready, bypass CG dump\n");
		return -1;
	}

	if (((regVal & BIT(2)) >> 2) == 0x1)
		LOG_WRN("APUREG APU0_APU_CG_CON = 0x%x\n",
					DRV_Reg32(APU0_APU_CG_CON));
	else
		LOG_WRN("APUREG vpu0 mtcmos not ready, bypass CG dump\n");

	if (((regVal & BIT(3)) >> 3) == 0x1)
		LOG_WRN("APUREG APU1_APU_CG_CON = 0x%x\n",
					DRV_Reg32(APU1_APU_CG_CON));
	else
		LOG_WRN("APUREG vpu1 mtcmos not ready, bypass CG dump\n");

	if (((regVal & BIT(4)) >> 4) == 0x1)
		LOG_WRN("APUREG APU2_APU_CG_CON = 0x%x\n",
					DRV_Reg32(APU2_APU_CG_CON));
	else
		LOG_WRN("APUREG vpu2 mtcmos not ready, bypass CG dump\n");

	if (((regVal & BIT(6)) >> 6) == 0x1)
		LOG_WRN("APUREG APU_MDLA0_APU_MDLA_CG_CON = 0x%x\n",
					DRV_Reg32(APU_MDLA0_APU_MDLA_CG_CON));
	else
		LOG_WRN("APUREG mdla0 mtcmos not ready, bypass CG dump\n");

	if (((regVal & BIT(7)) >> 7) == 0x1)
		LOG_WRN("APUREG APU_MDLA1_APU_MDLA_CG_CON = 0x%x\n",
					DRV_Reg32(APU_MDLA1_APU_MDLA_CG_CON));
	else
		LOG_WRN("APUREG mdla1 mtcmos not ready, bypass CG dump\n");

	return 0;
}

static void debug_power_mtcmos_on(void)
{
	LOG_WRN("%s begin +++\n", __func__);
#if 0
	buck_control(VPU0, 2); // buck ON

	enable_apu_mtcmos(1);
	enable_apu_conn_vcore_clock();
#endif
	buck_control(VPU0, 3); // buck on
	buck_control(VPU0, 2); // default voltage
	LOG_WRN("%s end ---\n", __func__);
}

static void debug_power_mtcmos_off(void)
{
//	unsigned int regValue = 0;

	LOG_WRN("%s begin +++\n", __func__);

	buck_control(VPU0, 1); // low voltage
	buck_control(VPU0, 0); // buck off
#if 0
	disable_apu_conn_vcore_clock();
	enable_apu_mtcmos(0);

	// mask RPC IRQ and bypass WFI
	regValue = DRV_Reg32(APU_RPC_TOP_SEL);
	regValue |= 0x9E;
	DRV_WriteReg32(APU_RPC_TOP_SEL, regValue);

	// sleep request enable
	regValue = DRV_Reg32(APU_RPC_TOP_CON);
	regValue |= 0x1;
	DRV_WriteReg32(APU_RPC_TOP_CON, regValue);

	buck_control(VPU0, 0); // buck OFF
#endif
	LOG_WRN("%s end ---\n", __func__);
}
