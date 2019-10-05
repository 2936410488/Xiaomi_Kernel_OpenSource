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

#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/device.h>

#include <trace/events/mtk_events.h>

#include <slbc.h>
#define CREATE_TRACE_POINTS
#include <slbc_events.h>

#include <linux/kthread.h>

#ifdef CONFIG_MTK_SLBC_MMSRAM
#include <mmsram.h>

static struct mmsram_data mmsram;
#endif /* CONFIG_MTK_SLBC_MMSRAM */

static struct task_struct *slbc_request_task;
static struct task_struct *slbc_release_task;

static struct wakeup_source slbc_lock;

static int slbc_enable = 1;
static int buffer_ref;
static int cache_ref;
static int slbc_ref;

static LIST_HEAD(slbc_ops_list);
static DEFINE_MUTEX(slbc_ops_lock);
static unsigned long slbc_status;
static unsigned long slbc_mask_status;
static unsigned long slbc_req_status;
static unsigned long slbc_release_status;
static unsigned long slbc_slot_status;

#define SLBC_CHECK_TIME msecs_to_jiffies(1000)
static struct timer_list slbc_deactivate_timer;

static struct slbc_config p_config[] = {
	/* SLBC_ENTRY(id, sid, max, fix, p, extra, res, cache) */
	SLBC_ENTRY(UID_MM_VENC, 0, 0, 1408, 0, 0x0, 0x1, 0),
	SLBC_ENTRY(UID_MM_DISP, 1, 0, 1383, 0, 0x0, 0x1, 0),
	SLBC_ENTRY(UID_MM_MDP, 2, 0, 1383, 0, 0x0, 0x1, 0),
	SLBC_ENTRY(UID_MD_DPMAIF, 3, 0, 1408, 1, 0x0, 0x1, 0),
	SLBC_ENTRY(UID_AI_MDLA, 4, 0, 1408, 1, 0x0, 0x1, 0),
	SLBC_ENTRY(UID_AI_ISP, 5, 0, 1408, 1, 0x0, 0x1, 0),
	SLBC_ENTRY(UID_TEST, 6, 0, 1408, 1, 0x0, 0x1, 0),
};

char *slbc_uid_str[] = {
	"UID_MM_VENC",
	"UID_MM_DISP",
	"UID_MM_MDP",
	"UID_MM_VDEC",
	"UID_MD_DPMAIF",
	"UID_AI_MDLA",
	"UID_AI_ISP",
	"UID_GPU",
	"UID_HIFI3",
	"UID_CPU",
	"UID_TEST",
	"UID_MAX",
};

#ifdef CONFIG_MTK_SLBC_MMSRAM
static void slbc_set_mmsram_data(struct slbc_data *d)
{
	d->paddr = mmsram.paddr;
	d->vaddr = mmsram.vaddr;
}
static void slbc_clr_mmsram_data(struct slbc_data *d)
{
	d->paddr = 0;
	d->vaddr = 0;
}

static int slbc_check_mmsram(void)
{
	if (!mmsram.size) {
		mmsram_get_info(&mmsram);
		if (!mmsram.size) {
			pr_info("#@# %s(%d) mmsram is wrong !!!\n",
					__func__, __LINE__);

			return -EINVAL;
		}
	}

	return 0;
}
#endif /* CONFIG_MTK_SLBC_MMSRAM */

static void slbc_debug_log(const char *fmt, ...)
{
#ifdef SLBC_DEBUG
	static char buf[1024];
	va_list va;

	va_start(va, fmt);
	vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);
	pr_info("#@# %s\n", buf);
#endif /* SLBC_DEBUG */
}

static int get_slbc_sid_by_uid(enum slbc_uid uid)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(p_config); i++) {
		if (p_config[i].uid == uid)
			return p_config[i].slot_id;
	}

	return 0;
}

/**
 * register_slbc_ops - Register a set of slbc operations.
 * @ops: slbc operations to register.
 */
int register_slbc_ops(struct slbc_ops *ops)
{
	int uid;
	int sid;
	struct slbc_data *d;

#ifdef CONFIG_MTK_SLBC_MMSRAM
	if (!slbc_check_mmsram()) {
		pr_info("#@# %s(%d) mmsram is wrong !!!\n",
				__func__, __LINE__);

		return -EINVAL;
	}
#endif /* CONFIG_MTK_SLBC_MMSRAM */

	if (ops && ops->data) {
		d = ops->data;
		uid = d->uid;

		trace_slbc_api((void *)__func__, slbc_uid_str[uid]);
		slbc_debug_log("%s: %s", __func__, slbc_uid_str[uid]);
	} else {
		pr_info("#@# %s(%d) data is wrong !!!\n", __func__, __LINE__);

		return -EINVAL;
	}

	sid = get_slbc_sid_by_uid(uid);
	if (sid) {
		d->sid = sid;
		d->config = &p_config[sid];
		d->slot_used = 0;
		d->ref = 0;
	} else {
		pr_info("#@# %s(%d) slot is wrong !!!\n", __func__, __LINE__);

		return -EINVAL;
	}

	mutex_lock(&slbc_ops_lock);
	list_add_tail(&ops->node, &slbc_ops_list);
	mutex_unlock(&slbc_ops_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(register_slbc_ops);

/**
 * unregister_slbc_ops - Unregister a set of slbc operations.
 * @ops: slbc operations to unregister.
 */
int unregister_slbc_ops(struct slbc_ops *ops)
{
	int uid;
	struct slbc_data *d;

	if (ops && ops->data) {
		d = ops->data;
		uid = d->uid;

		trace_slbc_api((void *)__func__, slbc_uid_str[uid]);
		slbc_debug_log("%s: %s", __func__, slbc_uid_str[uid]);
	} else {
		pr_info("#@# %s(%d) data is wrong !!!\n", __func__, __LINE__);

		return -EINVAL;
	}

	d->sid = 0;
	d->config = 0;
	d->slot_used = 0;
	d->ref = 0;

	mutex_lock(&slbc_ops_lock);
	list_del(&ops->node);
	mutex_unlock(&slbc_ops_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(unregister_slbc_ops);

static void slbc_deactivate_timer_fn(unsigned long data)
{
	struct slbc_ops *ops;
	int ref = 0;

	slbc_debug_log("%s: slbc_status %x", __func__, slbc_status);
	slbc_debug_log("%s: slbc_release_status %x", __func__,
			slbc_release_status);

	list_for_each_entry(ops, &slbc_ops_list, node) {
		struct slbc_data *d = ops->data;
		int uid = d->uid;

		if (test_bit(uid, &slbc_release_status)) {
			trace_slbc_api((void *)__func__, slbc_uid_str[uid]);
			slbc_debug_log("%s: %s", __func__, slbc_uid_str[uid]);

			if (test_bit(uid, &slbc_status)) {
				ref++;

				pr_info("#@# %s(%d) %s not released !!!\n",
						__func__, __LINE__,
						slbc_uid_str[uid]);
			} else {
				clear_bit(uid, &slbc_release_status);
				slbc_debug_log("%s: slbc_release_status %x",
						__func__, slbc_release_status);

				pr_info("#@# %s(%d) %s released !!!\n",
						__func__, __LINE__,
						slbc_uid_str[uid]);
			}
		}
	}

	if (ref) {
		unsigned long expires;

		expires = jiffies + SLBC_CHECK_TIME;
		mod_timer(&slbc_deactivate_timer, expires);
	}
}

#if 1
int slbc_activate(struct slbc_data *d)
{
	struct slbc_ops *ops;
	int uid = d->uid;
	int ret;

	if (slbc_enable == 0)
		return -EDISABLED;

	trace_slbc_api((void *)__func__, slbc_uid_str[uid]);
	slbc_debug_log("%s: %s", __func__, slbc_uid_str[uid]);

	ops = container_of(&d, struct slbc_ops, data);
	if (ops && ops->activate) {
		ret = slbc_request(d);
		if (ret) {
			pr_info("#@# %s(%d) %s request fail !!!\n",
					__func__, __LINE__, slbc_uid_str[uid]);

			return ret;
		}

		if (ops->activate(d) == CB_DONE) {
			pr_info("#@# %s(%d) %s activate fail !!!\n",
					__func__, __LINE__, slbc_uid_str[uid]);

			ret = slbc_release(d);
			if (ret) {
				pr_info("#@# %s(%d) %s release fail !!!\n",
						__func__, __LINE__,
						slbc_uid_str[uid]);

				return ret;
			}
		} else {
			trace_slbc_data((void *)__func__, d);
			slbc_debug_log("%s: %s %s", __func__, slbc_uid_str[uid],
					"done");
		}

		return 0;
	}

	pr_info("#@# %s(%d) %s data not found !!!\n",
			__func__, slbc_uid_str[uid]);

	return -EFAULT;
}
EXPORT_SYMBOL_GPL(slbc_activate);

int slbc_deactivate(struct slbc_data *d)
{
	struct slbc_ops *ops;
	int uid = d->uid;

	if (slbc_enable == 0)
		return -EDISABLED;

	trace_slbc_api((void *)__func__, slbc_uid_str[uid]);
	slbc_debug_log("%s: %s", __func__, slbc_uid_str[uid]);

	ops = container_of(&d, struct slbc_ops, data);
	if (ops && ops->deactivate) {
		unsigned long expires;

		ops->deactivate(d);

		trace_slbc_data((void *)__func__, d);
		slbc_debug_log("%s: %s %s", __func__, slbc_uid_str[uid],
				"done");

		set_bit(uid, &slbc_release_status);
		slbc_debug_log("%s: slbc_release_status %x", __func__,
				slbc_release_status);
		expires = jiffies + SLBC_CHECK_TIME;
		mod_timer(&slbc_deactivate_timer, expires);

		return 0;
	}

	pr_info("#@# %s(%d) %s data not found !!!\n", __func__, __LINE__,
			slbc_uid_str[uid]);

	return -EFAULT;
}
EXPORT_SYMBOL_GPL(slbc_deactivate);
#else
int slbc_activate(struct slbc_data *d)
{
	struct slbc_ops *ops;
	int uid = d->uid;
	int ret;

	if (slbc_enable == 0)
		return -EDISABLED;

	trace_slbc_api((void *)__func__, slbc_uid_str[uid]);
	slbc_debug_log("%s: %s", __func__, slbc_uid_str[uid]);

	list_for_each_entry(ops, &slbc_ops_list, node) {
		struct slbc_data *d_new = ops->data;
		int uid_new = d_new->uid;

		if (uid_new == uid && ops->activate) {
			ret = slbc_request(d_new);
			if (ret) {
				pr_info("#@# %s(%d) %s request fail !!!\n",
						__func__, __LINE__,
						slbc_uid_str[uid]);

				return ret;
			}

			if (ops->activate(d_new) == CB_DONE) {
				pr_info("#@# %s(%d) %s activate fail !!!\n",
						__func__, __LINE__,
						slbc_uid_str[uid_new]);

				ret = slbc_release(d_new);
				if (ret) {
					pr_info("#@# %s(%d) %s release fail !!!\n",
							__func__, __LINE__,
							slbc_uid_str[uid]);

					return ret;
				}
			} else {
				trace_slbc_data((void *)__func__, d_new);
				slbc_debug_log("%s: %s %s", __func__,
						slbc_uid_str[uid], "done");
			}

			return 0;
		}
	}

	pr_info("#@# %s(%d) %s data not found !!!\n",
			__func__, slbc_uid_str[uid]);

	return -EFAULT;
}
EXPORT_SYMBOL_GPL(slbc_activate);

int slbc_deactivate(struct slbc_data *d)
{
	struct slbc_ops *ops;
	int uid = d->uid;

	if (slbc_enable == 0)
		return -EDISABLED;

	trace_slbc_api((void *)__func__, slbc_uid_str[uid]);
	slbc_debug_log("%s: %s", __func__, slbc_uid_str[uid]);

	list_for_each_entry(ops, &slbc_ops_list, node) {
		struct slbc_data *d_new = ops->data;
		int uid_new = d_new->uid;

		if (uid_new == uid && ops->deactivate) {
			unsigned long expires;

			ops->deactivate(d_new);

			trace_slbc_data((void *)__func__, d_new);
			slbc_debug_log("%s: %s %s", __func__, slbc_uid_str[uid],
					"done");

			set_bit(uid_new, &slbc_release_status);
			slbc_debug_log("%s: slbc_release_status %x",
					__func__, slbc_release_status);
			expires = jiffies + SLBC_CHECK_TIME;
			mod_timer(&slbc_deactivate_timer, expires);

			return 0;
		}
	}

	pr_info("#@# %s(%d) %s data not found !!!\n",
			__func__, __LINE__,
			slbc_uid_str[uid]);

	return -EFAULT;
}
EXPORT_SYMBOL_GPL(slbc_deactivate);
#endif

static struct slbc_data *slbc_find_next_low_used(struct slbc_data *d_old)
{
	struct slbc_ops *ops;
	struct slbc_config *config_old = d_old->config;
	unsigned int p_old = config_old->priority;
	unsigned int res_old = config_old->res_slot;
	struct slbc_data *d_used = NULL;

	slbc_debug_log("%s: slbc_status %x", __func__, slbc_status);

	list_for_each_entry(ops, &slbc_ops_list, node) {
		struct slbc_data *d = ops->data;
		struct slbc_config *config = d->config;
		int uid = d->uid;
		unsigned int p = config->priority;

		if (test_bit(uid, &slbc_status) && (p > p_old) &&
				(d->slot_used & res_old)) {
			d_used = d;

			break;
		}
	}

	if (d_used) {
		trace_slbc_data((void *)__func__, d_used);
		slbc_debug_log("%s: %s", __func__,
				slbc_uid_str[d_used->uid]);
	}

	return d_used;

}

static struct slbc_data *slbc_find_next_high_req(struct slbc_data *d_old)
{
	struct slbc_ops *ops;
	struct slbc_config *config_old = d_old->config;
	unsigned int p_old = config_old->priority;
	unsigned int res_old = config_old->res_slot;
	struct slbc_data *d_req = NULL;

	slbc_debug_log("%s: slbc_req_status %x", __func__, slbc_req_status);

	list_for_each_entry(ops, &slbc_ops_list, node) {
		struct slbc_data *d = ops->data;
		struct slbc_config *config = d->config;
		unsigned int res = config->res_slot;
		int uid = d->uid;
		unsigned int p = config->priority;

		if (test_bit(uid, &slbc_req_status) && (p <= p_old) &&
				(res & res_old)) {
			p_old = p;
			d_req = d;

			if (!p)
				break;
		}
	}

	if (d_req) {
		trace_slbc_data((void *)__func__, d_req);
		slbc_debug_log("%s: %s", __func__, slbc_uid_str[d_req->uid]);
	}

	return d_req;
}

static int slbc_activate_thread(void *arg)
{
	struct slbc_data *d = arg;
	int uid = d->uid;

	trace_slbc_api((void *)__func__, slbc_uid_str[uid]);
	slbc_debug_log("%s: %s", __func__, slbc_uid_str[uid]);

	/* FIXME: */
	/* check return value */
	slbc_activate(d);

	return 0;
}

static int slbc_deactivate_thread(void *arg)
{
	struct slbc_data *d = arg;
	int uid = d->uid;
	struct slbc_data *d_used;

	trace_slbc_api((void *)__func__, slbc_uid_str[uid]);
	slbc_debug_log("%s: %s", __func__, slbc_uid_str[uid]);

	while ((d_used = slbc_find_next_low_used(d))) {
		/* FIXME: */
		/* check return value */
		slbc_deactivate(d_used);
	};

	return 0;
}

static void check_slot_by_data(struct slbc_data *d)
{
	struct slbc_config *config = d->config;
	unsigned int res = config->res_slot;
	int uid = d->uid;

	trace_slbc_api((void *)__func__, slbc_uid_str[uid]);
	slbc_debug_log("%s: %s", __func__, slbc_uid_str[uid]);

	/* FIXME: */
	/* check all need slot */
	d->slot_used = res;
}

static int find_slbc_slot_by_data(struct slbc_data *d)
{
	int uid = d->uid;

	trace_slbc_api((void *)__func__, slbc_uid_str[uid]);
	slbc_debug_log("%s: %s", __func__, slbc_uid_str[uid]);

	slbc_debug_log("%s: slbc_slot_status %x", __func__, slbc_slot_status);
	if (!(slbc_slot_status & d->slot_used))
		return SLOT_AVAILABLE;

	return SLOT_USED;
}

static void set_slbc_slot_by_data(struct slbc_data *d)
{
	slbc_slot_status |= d->slot_used;
	slbc_debug_log("%s: slbc_slot_status %x", __func__, slbc_slot_status);
}

static void clr_slbc_slot_by_data(struct slbc_data *d)
{
	slbc_slot_status &= ~d->slot_used;
	slbc_debug_log("%s: slbc_slot_status %x", __func__, slbc_slot_status);
}

int slbc_request(struct slbc_data *d)
{
	int ret = 0;
	int uid;

	if (slbc_enable == 0)
		return -EDISABLED;

	if (d == 0)
		return -EINVAL;

	if (d->uid == 0)
		return -EINVAL;

	uid = d->uid;

	trace_slbc_api((void *)__func__, slbc_uid_str[uid]);
	slbc_debug_log("%s: %s", __func__, slbc_uid_str[uid]);

	slbc_debug_log("%s: slbc_mask_status %x", __func__, slbc_mask_status);
	ret = test_bit(uid, &slbc_mask_status);
	if (ret == 1)
		return -EREQ_MASKED;

	set_bit(uid, &slbc_req_status);
	slbc_debug_log("%s: slbc_req_status %x", __func__, slbc_req_status);

	slbc_debug_log("%s: slbc_status %x", __func__, slbc_status);
	ret = test_bit(uid, &slbc_status);
	if (ret == 1) {
		slbc_set_mmsram_data(d);

		goto request_done;
	}

	if (BIT_IN_MM_BITS_1(BIT(uid)) &&
			!BIT_IN_MM_BITS_1(slbc_status)) {
		slbc_set_mmsram_data(d);

		goto request_done;
	}

	check_slot_by_data(d);

	if (find_slbc_slot_by_data(d) != SLOT_AVAILABLE) {
		struct slbc_data *d_used;

		d_used = slbc_find_next_low_used(d);
		if (d_used) {
			slbc_release_task = kthread_run(slbc_deactivate_thread,
					d, "slbc_deactivate_thread");

			return -EWAIT_RELEASE;
		}

		return -ENOT_AVAILABLE;
	}

	mutex_lock(&slbc_ops_lock);
	if ((d->type) == TP_BUFFER) {
		slbc_debug_log("%s: TP_BUFFER\n", __func__, __LINE__);
#ifdef CONFIG_MTK_SLBC_MMSRAM
		if (!buffer_ref)
			enable_mmsram();

		slbc_set_mmsram_data(d);
#endif /* CONFIG_MTK_SLBC_MMSRAM */

		buffer_ref++;
	}
	if ((d->type) == TP_CACHE) {
		slbc_debug_log("%s: TP_CACHE\n", __func__, __LINE__);
		if (cache_ref++ == 0) {
			/* FIXME: */
			/* acp_enable(); */
		}
	}
	mutex_unlock(&slbc_ops_lock);

	set_slbc_slot_by_data(d);

	trace_slbc_data((void *)__func__, d);

request_done:
	if (slbc_ref++ == 0)
		__pm_stay_awake(&slbc_lock);

	d->ref++;

	set_bit(uid, &slbc_status);
	slbc_debug_log("%s: slbc_status %x", __func__, slbc_status);
	clear_bit(uid, &slbc_req_status);
	slbc_debug_log("%s: slbc_req_status %x", __func__, slbc_req_status);

	return ret;
}
EXPORT_SYMBOL_GPL(slbc_request);

int slbc_release(struct slbc_data *d)
{
	int ret = 0;
	struct slbc_data *d_req;
	int uid;

	if (slbc_enable == 0)
		return -EDISABLED;

	if (d == 0)
		return -EINVAL;

	if (d->uid == 0)
		return -EINVAL;

	uid = d->uid;

	trace_slbc_api((void *)__func__, slbc_uid_str[uid]);
	slbc_debug_log("%s: %s", __func__, slbc_uid_str[uid]);

	slbc_debug_log("%s: slbc_mask_status %x", __func__, slbc_mask_status);
	ret = test_bit(uid, &slbc_mask_status);
	if (ret == 1)
		return -EREQ_MASKED;

	slbc_debug_log("%s: slbc_status %x", __func__, slbc_status);
	ret = test_bit(uid, &slbc_status);
	if (ret == 1) {
		slbc_clr_mmsram_data(d);

		goto release_done;
	}

	if (BIT_IN_MM_BITS_1(BIT(uid)) &&
			!BIT_IN_MM_BITS_1(slbc_status & !BIT(uid))) {
		slbc_clr_mmsram_data(d);

		goto release_done;
	}

	mutex_lock(&slbc_ops_lock);
	if ((d->type) == TP_BUFFER) {
		slbc_debug_log("%s: TP_BUFFER\n", __func__, __LINE__);

		buffer_ref--;
		WARN_ON(buffer_ref < 0);

#ifdef CONFIG_MTK_SLBC_MMSRAM
		slbc_clr_mmsram_data(d);

		if (!buffer_ref)
			disable_mmsram();
#endif /* CONFIG_MTK_SLBC_MMSRAM */
	}
	if ((d->type) == TP_CACHE) {
		slbc_debug_log("%s: TP_CACHE\n", __func__, __LINE__);
		if (--cache_ref == 0) {
			/* FIXME: */
			/* acp_disable(); */
		}
		WARN_ON(cache_ref < 0);
	}
	mutex_unlock(&slbc_ops_lock);

	clr_slbc_slot_by_data(d);

	trace_slbc_data((void *)__func__, d);

	d_req = slbc_find_next_high_req(d);
	if (d_req) {
		slbc_request_task = kthread_run(slbc_activate_thread,
				d_req, "slbc_activate_thread");
	}

release_done:
	if (--slbc_ref == 0)
		__pm_relax(&slbc_lock);

	d->ref--;
	WARN(d->ref < 0, "%s: release %s fail !!!\n",
			__func__, slbc_uid_str[uid]);

	clear_bit(uid, &slbc_status);
	slbc_debug_log("%s: slbc_status %x", __func__, slbc_status);

	return 0;
}
EXPORT_SYMBOL_GPL(slbc_release);

int slbc_power_on(struct slbc_data *d)
{
	int uid = d->uid;

	trace_slbc_api((void *)__func__, slbc_uid_str[uid]);
	slbc_debug_log("%s: %s flag %x", __func__, slbc_uid_str[uid], d->flag);

#ifdef CONFIG_MTK_SLBC_MMSRAM
	if (IS_ENABLED(CONFIG_MTK_SLBC_MMSRAM) &&
			SLBC_TRY_FLAG_BIT(d, FG_POWER))
		return mmsram_power_on();
#endif /* CONFIG_MTK_SLBC_MMSRAM */

	return 0;
}
EXPORT_SYMBOL_GPL(slbc_power_on);

int slbc_power_off(struct slbc_data *d)
{
	int uid = d->uid;

	trace_slbc_api((void *)__func__, slbc_uid_str[uid]);
	slbc_debug_log("%s: %s flag %x", __func__, slbc_uid_str[uid], d->flag);

#ifdef CONFIG_MTK_SLBC_MMSRAM
	if (IS_ENABLED(CONFIG_MTK_SLBC_MMSRAM) &&
			SLBC_TRY_FLAG_BIT(d, FG_POWER))
		mmsram_power_off();
#endif /* CONFIG_MTK_SLBC_MMSRAM */

	return 0;
}
EXPORT_SYMBOL_GPL(slbc_power_off);

static void slbc_dump_data(struct seq_file *m, struct slbc_data *d)
{
	int uid = d->uid;

	seq_printf(m, "\nID %s", slbc_uid_str[uid]);

	if (test_bit(uid, &slbc_status))
		seq_puts(m, " activate\n");
	else
		seq_puts(m, " deactivate\n");

	seq_printf(m, "\t%d\t", uid);
	seq_printf(m, "%x\t", d->type);
	seq_printf(m, "%d\n", d->size);
	seq_printf(m, "%p\t", d->paddr);
	seq_printf(m, "%p\t", d->vaddr);
	seq_printf(m, "%d\t", d->sid);
	seq_printf(m, "%x\n", d->slot_used);
	seq_printf(m, "%p\n", d->config);
	seq_printf(m, "%d\n", d->ref);
}

static int dbg_slbc_proc_show(struct seq_file *m, void *v)
{
	struct slbc_ops *ops;

	seq_printf(m, "slbc_enable %x\n", slbc_enable);
	seq_printf(m, "slbc_status %lx\n", slbc_status);
	seq_printf(m, "slbc_mask_status %lx\n", slbc_mask_status);
	seq_printf(m, "slbc_req_status %x\n", slbc_req_status);
	seq_printf(m, "slbc_release_status %x\n", slbc_release_status);
	seq_printf(m, "slbc_slot_status %x\n", slbc_slot_status);
	seq_printf(m, "buffer_ref %x\n", buffer_ref);
	seq_printf(m, "cache_ref %x\n", cache_ref);

	mutex_lock(&slbc_ops_lock);
	list_for_each_entry(ops, &slbc_ops_list, node) {
		struct slbc_data *d = ops->data;

		slbc_dump_data(m, d);
	}
	mutex_unlock(&slbc_ops_lock);

	seq_puts(m, "\n");

	return 0;
}

static ssize_t dbg_slbc_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *pos)
{
	int ret;
	char *buf = (char *) __get_free_page(GFP_USER);
	char cmd[64];
	unsigned long val_1;
	unsigned long val_2;

	if (!buf)
		return -ENOMEM;

	ret = -EINVAL;

	if (count >= PAGE_SIZE)
		goto out;

	ret = -EFAULT;

	if (copy_from_user(buf, buffer, count))
		goto out;

	buf[count] = '\0';

	ret = sscanf(buf, "%63s %d %d", cmd, &val_1, &val_2);
	if (ret < 1) {
		ret = -EPERM;
		goto out;
	}

	if (!strcmp(cmd, "slbc_enable")) {
		slbc_enable = val_1;
		if (slbc_enable == 0) {
			struct slbc_ops *ops;

			mutex_lock(&slbc_ops_lock);
			list_for_each_entry(ops, &slbc_ops_list, node) {
				struct slbc_data *d = ops->data;
				int uid = d->uid;

				if (test_bit(uid, &slbc_status))
					ops->deactivate(d);
			}
			mutex_unlock(&slbc_ops_lock);
		}
	} else if (!strcmp(cmd, "slbc_status"))
		slbc_status = val_1;
	else if (!strcmp(cmd, "slbc_mask_status"))
		slbc_mask_status = val_1;
	else if (!strcmp(cmd, "slbc_req_status"))
		slbc_req_status = val_1;
	else if (!strcmp(cmd, "slbc_release_status"))
		slbc_release_status = val_1;
	else if (!strcmp(cmd, "slbc_slot_status"))
		slbc_slot_status = val_1;

out:
	free_page((unsigned long)buf);

	if (ret < 0)
		return ret;

	return count;
}

PROC_FOPS_RW(dbg_slbc);

static int slbc_create_debug_fs(void)
{
	int i;
	struct proc_dir_entry *dir = NULL;

	struct pentry {
		const char *name;
		const struct file_operations *fops;
		void *data;
	};

	const struct pentry entries[] = {
		PROC_ENTRY(dbg_slbc),
	};

	/* create /proc/slbc */
	dir = proc_mkdir("slbc", NULL);
	if (!dir) {
		pr_info("fail to create /proc/slbc @ %s()\n", __func__);

		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(entries); i++) {
		if (!proc_create_data(entries[i].name, 0664,
					dir, entries[i].fops, entries[i].data))
			pr_info("%s(), create /proc/slbc/%s failed\n",
					__func__, entries[i].name);
	}

	return 0;
}

int __init slbc_module_init(void)
{
	struct device_node *node;
	int ret;
	const char *buf;

	node = of_find_compatible_node(NULL, NULL,
			"mediatek,slbc");
	if (node) {
		ret = of_property_read_string(node,
				"status", (const char **)&buf);

		if (ret == 0) {
			if (!strcmp(buf, "enable"))
				slbc_enable = 1;
			else
				slbc_enable = 0;
		}
		pr_info("#@# %s(%d) slbc_enable %d\n", __func__, __LINE__,
				slbc_enable);
	} else
		pr_info("find slbc node failed\n");

	ret = slbc_create_debug_fs();
	if (ret) {
		pr_info("FAILED TO CREATE DEBUG FILESYSTEM (%d)\n", ret);

		return ret;
	}

	init_timer_deferrable(&slbc_deactivate_timer);
	slbc_deactivate_timer.data = 0;
	slbc_deactivate_timer.function = slbc_deactivate_timer_fn;

	return 0;
}

late_initcall(slbc_module_init);

MODULE_DESCRIPTION("SLBC Driver v0.1");
