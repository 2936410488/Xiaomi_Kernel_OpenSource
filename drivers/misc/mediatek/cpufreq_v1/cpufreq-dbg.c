// SPDX-License-Identifier: GPL-2.0
/*
 * cpufreq-dbg.c - CPUFreq debug Driver
 *
 * Copyright (c) 2020 MediaTek Inc.
 * Chienwei Chang <chienwei.chang@mediatek.com>
 */

/* system includes */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/cpufreq.h>
#include <linux/uaccess.h>
#include "mtk_cpu_dbg.h"
#include <linux/delay.h>

#define PLL_CONFIG_PROP_NAME "pll-con"
#define TBL_OFF_PROP_NAME "tbl-off"
#define CLK_DIV_PROP_NAME "clk-div"
#define APMIXED_BASE_PROP_NAME "apmixedsys"
#define MCUCFG_BASE_PROP_NAME "clk-div-base"
#define CSRAM_DVFS_LOG_RANGE "cslog-range"

#define get_volt(offs, repo) ((repo[offs] >> 12) & 0x1FFFF)
#define get_freq(offs, repo) ((repo[offs] & 0xFFF) * 1000)

#define ENTRY_EACH_LOG	5

unsigned int dbg_repo_num;
unsigned int usram_repo_num;
unsigned int repo_i_log_s;
unsigned int repo_i_log_e;

static DEFINE_MUTEX(cpufreq_mutex);
static void __iomem *csram_base;
static void __iomem *usram_base;
static void __iomem *apmixed_base;
static void __iomem *mcucfg_base;

u32 *g_dbg_repo;
u32 *g_usram_repo;
u32 *g_cpufreq_debug;
u32 *g_phyclk;
u32 *g_C0_opp_idx;
u32 *g_C1_opp_idx;
u32 *g_C2_opp_idx;
u32 *g_C3_opp_idx;

int g_num_cluster;//g_num_cluster<=MAX_CLUSTER_NRS
struct pll_addr pll_addr[MAX_CLUSTER_NRS];
unsigned int cluster_off[MAX_CLUSTER_NRS+1];//domain + cci

static unsigned int pll_to_clk(unsigned int pll_f, unsigned int ckdiv1)
{
	unsigned int freq = pll_f;

	switch (ckdiv1) {
	case 8:
		break;
	case 9:
		freq = freq * 3 / 4;
		break;
	case 10:
		freq = freq * 2 / 4;
		break;
	case 11:
		freq = freq * 1 / 4;
		break;
	case 16:
		break;
	case 17:
		freq = freq * 4 / 5;
		break;
	case 18:
		freq = freq * 3 / 5;
		break;
	case 19:
		freq = freq * 2 / 5;
		break;
	case 20:
		freq = freq * 1 / 5;
		break;
	case 24:
		break;
	case 25:
		freq = freq * 5 / 6;
		break;
	case 26:
		freq = freq * 4 / 6;
		break;
	case 27:
		freq = freq * 3 / 6;
		break;
	case 28:
		freq = freq * 2 / 6;
		break;
	case 29:
		freq = freq * 1 / 6;
		break;
	default:
		break;
	}

	return freq;
}

static unsigned int _cpu_freq_calc(unsigned int con1, unsigned int ckdiv1)
{
	unsigned int freq;
	unsigned int posdiv;

	posdiv = _GET_BITS_VAL_(26:24, con1);

	con1 &= _BITMASK_(21:0);
	freq = ((con1 * 26) >> 14) * 1000;

	switch (posdiv) {
	case 0:
		break;
	case 1:
		freq = freq / 2;
		break;
	case 2:
		freq = freq / 4;
		break;
	case 3:
		freq = freq / 8;
		break;
	default:
		freq = freq / 16;
		break;
	};

	return pll_to_clk(freq, ckdiv1);
}

unsigned int get_cur_phy_freq(int cluster)
{
	unsigned int con1;
	unsigned int ckdiv1;
	unsigned int cur_khz;

	con1 = readl(apmixed_base+pll_addr[cluster].reg_addr[0]);
	ckdiv1 = readl(mcucfg_base+pll_addr[cluster].reg_addr[1]);
	ckdiv1 = _GET_BITS_VAL_(21:17, ckdiv1);
	cur_khz = _cpu_freq_calc(con1, ckdiv1);

	return cur_khz;
}

static int dbg_repo_proc_show(struct seq_file *m, void *v)
{
	int i;
	u32 *repo = m->private;
	char ch;

	for (i = 0; i < dbg_repo_num; i++) {
		if (i >= repo_i_log_s && (i - repo_i_log_s) %
						ENTRY_EACH_LOG == 0)
			ch = ':';	/* timestamp */
		else
			ch = '.';
		seq_printf(m, "%4d%c%08x%c",
			i, ch, repo[i], i % 4 == 3 ? '\n' : ' ');
	}

	return 0;
}

PROC_FOPS_RO(dbg_repo);

static int cpufreq_debug_proc_show(struct seq_file *m, void *v)
{
	return 0;
}

static ssize_t cpufreq_debug_proc_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *pos)
{
	int cpu = 0, min = 0, max = 0;
	unsigned long MHz;
	unsigned long mHz;
	char *buf = (char *) __get_free_page(GFP_USER);
	struct device *cpu_dev = get_cpu_device(cpu);
	struct cpufreq_policy *policy;

	if (copy_from_user(buf, buffer, count)) {
		free_page((unsigned long)buf);
		return -EINVAL;
	}

	if (sscanf(buf, "%d %d %d", &cpu, &min, &max) != 3) {
		free_page((unsigned long)buf);
		return -EINVAL;
	}

	free_page((unsigned long)buf);
	if (min <= 0 || max <= 0)
		return -EINVAL;
	MHz = max;
	mHz = min;
	MHz = MHz * 1000;
	mHz = mHz * 1000;
	dev_pm_opp_find_freq_floor(cpu_dev, &MHz);
	dev_pm_opp_find_freq_floor(cpu_dev, &mHz);

	pr_info("[DVFS] core%d: (%d %d) -- (%ld %ld)\n", cpu, min, max, mHz, MHz);

	max = (unsigned int)(MHz / 1000);
	min = (unsigned int)(mHz / 1000);

	policy = cpufreq_cpu_get(cpu);
	down_write(&policy->rwsem);
	policy->cpuinfo.max_freq = max;
	policy->cpuinfo.min_freq = min;
	up_write(&policy->rwsem);
	cpufreq_cpu_put(policy);
	cpufreq_update_limits(cpu);

	return count;
}


PROC_FOPS_RW(cpufreq_debug);


static int usram_repo_proc_show(struct seq_file *m, void *v)
{
	int i;
	u32 *repo = m->private;
	char ch;

	for (i = 0; i < usram_repo_num; i++) {
		ch = '.';
		seq_printf(m, "%4d%c%08x%c",
				i, ch, repo[i], i % 4 == 3 ? '\n' : ' ');
	}

	return 0;
}

PROC_FOPS_RO(usram_repo);

static int opp_idx_show(struct seq_file *m, void *v, u32 pos)
{
	u32 *repo = m->private;
	u32 opp = 0;
	u32 prev_freq = 0x0; //some invalid freq value

	while (pos < dbg_repo_num && get_freq(pos, repo) != prev_freq) {
		prev_freq = get_freq(pos, repo);
		seq_printf(m, "\t%-2d (%u, %u)\n", opp,
			get_freq(pos, repo), get_volt(pos, repo));
		pos++;
		opp++;
	}

	return 0;

}

static int C0_opp_idx_proc_show(struct seq_file *m, void *v)
{
	return opp_idx_show(m, v, cluster_off[0]);
}

PROC_FOPS_RO(C0_opp_idx);

static int C1_opp_idx_proc_show(struct seq_file *m, void *v)
{
	return opp_idx_show(m, v, cluster_off[1]);
}

PROC_FOPS_RO(C1_opp_idx);

static int C2_opp_idx_proc_show(struct seq_file *m, void *v)
{
	return opp_idx_show(m, v, cluster_off[2]);
}

PROC_FOPS_RO(C2_opp_idx);

static int C3_opp_idx_proc_show(struct seq_file *m, void *v)
{
	return opp_idx_show(m, v, cluster_off[3]);
}

PROC_FOPS_RO(C3_opp_idx);

static int phyclk_proc_show(struct seq_file *m, void *v)
{
	int i;
	static const char * const name_arr[] = {"C0", "C1", "C2"};

	for (i = 0; i < g_num_cluster; i++)
		seq_printf(m, "old cluster: %s, freq = %d\n", name_arr[i], get_cur_phy_freq(i));
	return 0;
}

PROC_FOPS_RO(phyclk);

static int create_cpufreq_debug_fs(void)
{
	struct proc_dir_entry *dir = NULL;
	int i;

	struct pentry {
		const char *name;
		const struct proc_ops *fops;
		void *data;
	};

	const struct pentry entries[] = {
		PROC_ENTRY_DATA(dbg_repo),
		PROC_ENTRY_DATA(cpufreq_debug),
		PROC_ENTRY_DATA(phyclk),
		PROC_ENTRY_DATA(usram_repo),
	};
	const struct pentry clusters[MAX_CLUSTER_NRS+1] = {
		PROC_ENTRY_DATA(C0_opp_idx),//L  or LL
		PROC_ENTRY_DATA(C1_opp_idx),//BL or L
		PROC_ENTRY_DATA(C2_opp_idx),//B  or CCI
		PROC_ENTRY_DATA(C3_opp_idx),//CCI
	};

	/* create /proc/cpuhvfs */
	dir = proc_mkdir("cpuhvfs", NULL);
	if (!dir) {
		pr_info("fail to create /proc/cpuhvfs @ %s()\n",
								__func__);
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(entries)-1; i++) {
		if (!proc_create_data
			(entries[i].name, 0664,
			dir, entries[i].fops, csram_base))
			pr_info("%s(), create /proc/cpuhvfs/%s failed\n",
						__func__, entries[0].name);
	}
	i = ARRAY_SIZE(entries)-1;
	proc_create_data(entries[i].name, 0664, dir, entries[i].fops, usram_base);

	if (g_num_cluster > MAX_CLUSTER_NRS) {
		pr_info("fail to create CX_opp_idx @ %s()\n", __func__);
		return -EINVAL;
	}
	// CPU Clusters + CCI cluster
	for (i = 0; i < g_num_cluster+1; i++) {
		if (!proc_create_data(clusters[i].name, 0664, dir, clusters[i].fops, csram_base))
			pr_info("%s(), create /proc/cpuhvfs/%s failed\n",
				__func__, entries[0].name);
	}

	return 0;
}

static int mtk_cpuhvfs_init(void)
{
	int ret = 0, i;
	struct device_node *hvfs_node;
	struct device_node *apmixed_node;
	struct device_node *mcucfg_node;
	struct platform_device *pdev;
	struct resource *csram_res, *usram_res;

	hvfs_node = of_find_node_by_name(NULL, "cpuhvfs");
	if (hvfs_node == NULL) {
		pr_notice("[cpuhvfs] failed to find node @ %s\n", __func__);
		return -ENODEV;
	}

	pdev = of_device_alloc(hvfs_node, NULL, NULL);
	usram_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	usram_base = ioremap(usram_res->start, resource_size(usram_res));
	usram_repo_num = (resource_size(usram_res) / sizeof(u32));

	csram_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	csram_base = ioremap(csram_res->start, resource_size(csram_res));
	dbg_repo_num = (resource_size(csram_res) / sizeof(u32));
	//Start address from csram
	of_property_read_u32_index(hvfs_node, CSRAM_DVFS_LOG_RANGE, 0, &repo_i_log_s);
	//End address from csram
	of_property_read_u32_index(hvfs_node, CSRAM_DVFS_LOG_RANGE, 1, &repo_i_log_e);
	//only used for having a pretty dump format
	repo_i_log_s /= sizeof(u32);
	repo_i_log_e /= sizeof(u32);

	/* get PLL config & CLKDIV for every cluster */
	ret = of_property_count_u32_elems(hvfs_node, PLL_CONFIG_PROP_NAME);
	if (ret < 0) {
		pr_notice("[cpuhvfs] failed to get num_cluster @ %s\n", __func__);
		return -EINVAL;
	}
	g_num_cluster = ret;
	ret = of_property_count_u32_elems(hvfs_node, CLK_DIV_PROP_NAME);
	if (ret != g_num_cluster) {
		pr_notice("[cpuhvfs] clk-div size is not aligned@ %s\n", __func__);
		return -EINVAL;
	}

	//get APMIXED_BASE & MCUCFG_BASE
	apmixed_node = of_parse_phandle(hvfs_node, APMIXED_BASE_PROP_NAME, 0);
	if (apmixed_node == NULL) {
		pr_notice("[cpuhvfs] failed to get apmixed base @ %s\n", __func__);
		return -EINVAL;
	}
	apmixed_base = of_iomap(apmixed_node, 0);

	mcucfg_node = of_parse_phandle(hvfs_node, MCUCFG_BASE_PROP_NAME, 0);
	if (mcucfg_node == NULL) {
		pr_notice("[cpuhvfs] failed to get mcucfg base @ %s\n", __func__);
		return -EINVAL;
	}
	mcucfg_base = of_iomap(mcucfg_node, 0);

	for (i = 0; i < g_num_cluster; i++) {
		of_property_read_u32_index(hvfs_node,
			PLL_CONFIG_PROP_NAME, i, &pll_addr[i].reg_addr[0]);
		of_property_read_u32_index(hvfs_node,
			CLK_DIV_PROP_NAME, i, &pll_addr[i].reg_addr[1]);
	}

	//Offsets used to fetch OPP table
	ret = of_property_count_u32_elems(hvfs_node, TBL_OFF_PROP_NAME);
	if (ret != g_num_cluster+1) {
		pr_notice("[cpuhvfs] only get %d opp offset@ %s\n", ret, __func__);
		return -EINVAL;
	}
	for (i = 0; i < g_num_cluster+1; i++)
		of_property_read_u32_index(hvfs_node, TBL_OFF_PROP_NAME, i, &cluster_off[i]);


	create_cpufreq_debug_fs();
#ifdef EEM_DBG
	ret = mtk_eem_init();
	if (ret)
		pr_info("eem dbg init fail\n");
#endif
	return ret;
}
module_init(mtk_cpuhvfs_init)

static void mtk_cpuhvfs_exit(void)
{
}
module_exit(mtk_cpuhvfs_exit);

MODULE_DESCRIPTION("MTK CPU DVFS Platform Driver v0.1.1");
MODULE_AUTHOR("Chienwei Chang <chiewei.chang@mediatek.com>");
MODULE_LICENSE("GPL v2");

