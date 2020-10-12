// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/module.h>       /* needed by all modules */
#include <linux/init.h>         /* needed by module macros */
#include <linux/device.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include "adsp_clk.h"
#include "adsp_platform.h"
#include "adsp_platform_driver.h"
#include "adsp_core.h"
#include "adsp_mbox.h"

int (*ipi_queue_send_msg_handler)(
	uint32_t core_id, /* enum adsp_core_id */
	uint32_t ipi_id,  /* enum adsp_ipi_id */
	void *buf,
	uint32_t len,
	uint32_t wait_ms);

/* protect access tcm if set reset flag */
rwlock_t access_rwlock;

/* timesync */
struct timesync_t adsp_timesync_dram;
void *adsp_timesync_ptr = &adsp_timesync_dram; /* extern to adsp_help.h */

/* notifier */
static BLOCKING_NOTIFIER_HEAD(adsp_notifier_list);

void hook_ipi_queue_send_msg_handler(
	int (*send_msg_handler)(
		uint32_t core_id, /* enum adsp_core_id */
		uint32_t ipi_id,  /* enum adsp_ipi_id */
		void *buf,
		uint32_t len,
		uint32_t wait_ms))
{
	ipi_queue_send_msg_handler = send_msg_handler;
}
EXPORT_SYMBOL(hook_ipi_queue_send_msg_handler);

void unhook_ipi_queue_send_msg_handler(void)
{
	ipi_queue_send_msg_handler = NULL;
}
EXPORT_SYMBOL(unhook_ipi_queue_send_msg_handler);

struct adsp_priv *_get_adsp_core(void *ptr, int id)
{
	if (ptr)
		return container_of(ptr, struct adsp_priv, mdev);

	if (id < ADSP_CORE_TOTAL && id >= 0)
		return adsp_cores[id];

	return NULL;
}

void set_adsp_state(struct adsp_priv *pdata, int state)
{
	pdata->state = state;
}

int get_adsp_state(struct adsp_priv *pdata)
{
	return pdata->state;
}

int is_adsp_ready(u32 cid)
{
	if (unlikely(cid >= ADSP_CORE_TOTAL))
		return -EINVAL;

	if (unlikely(!adsp_cores[cid]))
		return -EINVAL;

	return (adsp_cores[cid]->state == ADSP_RUNNING);
}
EXPORT_SYMBOL(is_adsp_ready);

uint32_t get_adsp_core_total(void)
{
	return ADSP_CORE_TOTAL;
}
EXPORT_SYMBOL(get_adsp_core_total);

bool is_adsp_system_running(void)
{
	unsigned int id;

	for (id = 0; id < ADSP_CORE_TOTAL; id++) {
		if (is_adsp_ready(id) > 0)
			return true;
	}
	return false;
}

int adsp_copy_to_sharedmem(struct adsp_priv *pdata, int id, const void *src,
			   int count)
{
	void __iomem *dst = NULL;
	const struct sharedmem_info *item;

	if (unlikely(id >= ADSP_SHAREDMEM_NUM))
		return 0;

	item = pdata->mapping_table + id;
	if (item->offset)
		dst = pdata->dtcm + pdata->dtcm_size - item->offset;

	if (unlikely(!dst || !src))
		return 0;

	if (count > item->size)
		count = item->size;

	memcpy_toio(dst, src, count);

	return count;
}

int adsp_copy_from_sharedmem(struct adsp_priv *pdata, int id, void *dst,
			     int count)
{
	void __iomem *src = NULL;
	const struct sharedmem_info *item;

	if (unlikely(id >= ADSP_SHAREDMEM_NUM))
		return 0;

	item = pdata->mapping_table + id;
	if (item->offset)
		src = pdata->dtcm + pdata->dtcm_size - item->offset;

	if (unlikely(!dst || !src))
		return 0;

	if (count > item->size)
		count = item->size;

	memcpy_fromio(dst, src, count);

	return count;
}

enum adsp_ipi_status adsp_push_message(enum adsp_ipi_id id, void *buf,
			unsigned int len, unsigned int wait_ms,
			unsigned int core_id)
{
	int ret = -1;

	/* send msg to queue */
	if (ipi_queue_send_msg_handler)
		ret = ipi_queue_send_msg_handler(core_id, id, buf, len, wait_ms);

	/* send to mbox directly if handler or queue not inited */
	if (ret != 0) {
		pr_notice("%s, ipi queue not ready. id=%d", __func__, id);
		ret = adsp_send_message(id, buf, len, wait_ms, core_id);
	}

	return (ret == 0) ? ADSP_IPI_DONE : ADSP_IPI_ERROR;
}

enum adsp_ipi_status adsp_send_message(enum adsp_ipi_id id, void *buf,
			unsigned int len, unsigned int wait,
			unsigned int core_id)
{
	struct adsp_priv *pdata = NULL;
	struct mtk_ipi_msg msg;

	if (core_id >= ADSP_CORE_TOTAL)
		return ADSP_IPI_ERROR;
	pdata = get_adsp_core_by_id(core_id);

	if (get_adsp_state(pdata) != ADSP_RUNNING) {
		pr_notice("%s, adsp not enabled, id=%d", __func__, id);
		return ADSP_IPI_ERROR;
	}

	if (len > (SHARE_BUF_SIZE - 16) || buf == NULL) {
		pr_info("%s(), %s buffer error", __func__, "adsp");
		return ADSP_IPI_ERROR;
	}

	msg.ipihd.id = id;
	msg.ipihd.len = len;
	msg.ipihd.options = 0xffff0000;
	msg.ipihd.reserved = 0xdeadbeef;
	msg.data = buf;

	return adsp_mbox_send(pdata->send_mbox, &msg, wait);
}
EXPORT_SYMBOL(adsp_send_message);

static irqreturn_t adsp_irq_dispatcher(int irq, void *data)
{
	struct irq_t *pdata = (struct irq_t *)data;

	adsp_mt_clr_spm(pdata->cid);
	if (!pdata->irq_cb || !pdata->clear_irq)
		return IRQ_NONE;
	pdata->clear_irq(pdata->cid);
	pdata->irq_cb(irq, pdata->data, pdata->cid);
	return IRQ_HANDLED;
}

int adsp_irq_registration(u32 core_id, u32 irq_id, void *handler, void *data)
{
	int ret;
	struct adsp_priv *pdata = get_adsp_core_by_id(core_id);

	if (unlikely(!pdata))
		return -EACCES;

	pdata->irq[irq_id].cid = core_id;
	pdata->irq[irq_id].irq_cb = handler;
	pdata->irq[irq_id].data = data;
	ret = request_irq(pdata->irq[irq_id].seq,
			  (irq_handler_t)adsp_irq_dispatcher,
			  IRQF_TRIGGER_HIGH,
			  pdata->name,
			  &pdata->irq[irq_id]);
	return ret;
}

void adsp_register_notify(struct notifier_block *nb)
{
	blocking_notifier_chain_register(&adsp_notifier_list, nb);
}
EXPORT_SYMBOL(adsp_register_notify);

void adsp_unregister_notify(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&adsp_notifier_list, nb);
}
EXPORT_SYMBOL(adsp_unregister_notify);

void adsp_extern_notify_chain(enum ADSP_NOTIFY_EVENT event)
{
#ifdef CFG_RECOVERY_SUPPORT
	blocking_notifier_call_chain(&adsp_notifier_list, event, NULL);
#endif
}

void switch_adsp_power(bool on)
{
	if (on) {
		adsp_enable_clock();
		adsp_set_clock_freq(CLK_DEFAULT_INIT_CK);
	} else {
		adsp_set_clock_freq(CLK_DEFAULT_26M_CK);
		adsp_disable_clock();
	}
}

void timesync_to_adsp(struct adsp_priv *pdata, u32 fz)
{
	adsp_timesync_dram.freeze = fz;
	adsp_timesync_dram.version++;
	adsp_timesync_dram.version &= 0xff;
	adsp_copy_to_sharedmem(pdata, ADSP_SHAREDMEM_TIMESYNC,
			&adsp_timesync_dram, sizeof(adsp_timesync_dram));
}

void adsp_sram_restore_snapshot(struct adsp_priv *pdata)
{
	if (!pdata->itcm || !pdata->itcm_snapshot || !pdata->itcm_size ||
	    !pdata->dtcm || !pdata->dtcm_snapshot || !pdata->dtcm_size)
		return;

	memcpy_toio(pdata->itcm, pdata->itcm_snapshot, pdata->itcm_size);
	memcpy_toio(pdata->dtcm, pdata->dtcm_snapshot, pdata->dtcm_size);
}

void adsp_sram_provide_snapshot(struct adsp_priv *pdata)
{
	if (!pdata->itcm || !pdata->dtcm)
		return;

	if (!pdata->itcm_snapshot)
		pdata->itcm_snapshot = vmalloc(pdata->itcm_size);

	if (!pdata->dtcm_snapshot)
		pdata->dtcm_snapshot = vmalloc(pdata->dtcm_size);

	if (!pdata->itcm_snapshot || !pdata->dtcm_snapshot)
		return;

	memcpy_fromio(pdata->itcm_snapshot, pdata->itcm, pdata->itcm_size);
	memcpy_fromio(pdata->dtcm_snapshot, pdata->dtcm, pdata->dtcm_size);
}

int adsp_reset(void)
{
#ifdef CFG_RECOVERY_SUPPORT
	int ret = 0, cid = 0;
	struct adsp_priv *pdata;

	if (!is_adsp_axibus_idle()) {
		pr_info("%s, adsp_axibus busy try again", __func__);
		return -EAGAIN;
	}

	/* clear adsp cfg */
	adsp_mt_clear();

	/* choose default clk mux */
	adsp_set_clock_freq(CLK_TOP_CLK26M);
	adsp_set_clock_freq(CLK_DEFAULT_INIT_CK);

	/* restore tcm to initial state */
	for (cid = 0; cid < ADSP_CORE_TOTAL; cid++) {
		pdata = adsp_cores[cid];
		adsp_sram_restore_snapshot(pdata);
	}

	/* restart adsp */
	for (cid = 0; cid < ADSP_CORE_TOTAL; cid++) {
		pdata = adsp_cores[cid];

		adsp_mt_sw_reset(cid);
		timesync_to_adsp(pdata, APTIME_UNFREEZE);
		reinit_completion(&pdata->done);

		adsp_mt_run(cid);

		ret = wait_for_completion_timeout(&pdata->done, HZ);

		if (unlikely(ret == 0)) {
			pr_warn("%s, core %d reset timeout\n", __func__, cid);
			return -ETIME;
		}
	}

	for (cid = 0; cid < ADSP_CORE_TOTAL; cid++) {
		pdata = adsp_cores[cid];

		if (pdata->ops->after_bootup)
			pdata->ops->after_bootup(pdata);
	}

	pr_info("[ADSP] reset adsp done\n");
#endif
	return 0;
}

static void adsp_ready_ipi_handler(int id, void *data, unsigned int len)
{
	unsigned int cid = *(unsigned int *)data;
	struct adsp_priv *pdata = get_adsp_core_by_id(cid);

	if (unlikely(!pdata))
		return;

	if (get_adsp_state(pdata) != ADSP_RUNNING) {
		set_adsp_state(pdata, ADSP_RUNNING);
		complete(&pdata->done);
	}
}

static void adsp_suspend_ipi_handler(int id, void *data, unsigned int len)
{
	unsigned int cid = *(unsigned int *)data;
	struct adsp_priv *pdata = get_adsp_core_by_id(cid);

	if (unlikely(!pdata))
		return;

	complete(&pdata->done);
}

static int adsp_system_bootup(void)
{
	int ret = 0, cid = 0;
	struct adsp_priv *pdata;

	switch_adsp_power(true);

	for (cid = 0; cid < ADSP_CORE_TOTAL; cid++) {
		pdata = adsp_cores[cid];

		if (unlikely(!pdata)) {
			ret = -EFAULT;
			goto ERROR;
		}

		adsp_mt_sw_reset(cid);

		ret = pdata->ops->initialize(pdata);
		if (unlikely(ret)) {
			pr_warn("%s, initialize %d is fail\n", __func__, cid);
			goto ERROR;
		}

		adsp_sram_provide_snapshot(pdata);
		adsp_mt_run(cid);

		ret = wait_for_completion_timeout(&pdata->done, HZ);

		if (unlikely(ret == 0)) {
			pr_warn("%s, core %d boot_up timeout\n", __func__, cid);
			ret = -ETIME;
			goto ERROR;
		}
	}

	adsp_register_feature(SYSTEM_FEATURE_ID); /* regi for trigger suspend */

	for (cid = 0; cid < ADSP_CORE_TOTAL; cid++) {
		pdata = adsp_cores[cid];

		if (pdata->ops->after_bootup)
			pdata->ops->after_bootup(pdata);
	}

	adsp_deregister_feature(SYSTEM_FEATURE_ID);
	pr_info("%s done\n", __func__);
	return ret;

ERROR:
	pr_info("%s fail ret(%d)\n", __func__, ret);
	return ret;
}

static int __init adsp_init(void)
{
	int ret = 0;

	ret = create_adsp_drivers();
	if (ret)
		goto DONE;

	rwlock_init(&access_rwlock);
	adsp_platform_init();

	//register_syscore_ops(&adsp_syscore_ops);
	adsp_ipi_registration(ADSP_IPI_ADSP_A_READY,
			      adsp_ready_ipi_handler,
			      "adsp_ready");
	adsp_ipi_registration(ADSP_IPI_DVFS_SUSPEND,
			      adsp_suspend_ipi_handler,
			      "adsp_suspend_ack");

	ret = adsp_system_bootup();
DONE:
	pr_info("%s(), done, ret = %d\n", __func__, ret);
	return 0;
}

static void __exit adsp_exit(void)
{
	pr_info("%s\n", __func__);
}

module_init(adsp_init);
module_exit(adsp_exit);

MODULE_AUTHOR("Chien-Wei Hsu <Chien-Wei.Hsu@mediatek.com>");
MODULE_DESCRIPTION("MTK AUDIO DSP Device Driver");
MODULE_LICENSE("GPL v2");
