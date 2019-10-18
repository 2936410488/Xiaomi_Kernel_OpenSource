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

#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/module.h>

#include "apusys_dbg.h"
#include "apusys_cmn.h"
#include "resource_mgt.h"
#include "cmd_parser.h"
#include "thread_pool.h"
#include "scheduler.h"
#include "sched_deadline.h"


struct apusys_res_mgr g_res_mgr;

//----------------------------------------------
struct apusys_res_table *res_get_table(int type)
{
	if (type <= APUSYS_DEVICE_NONE || type >= APUSYS_DEVICE_MAX)
		return NULL;

	return g_res_mgr.tab[type];
}

struct apusys_res_mgr *res_get_mgr(void)
{
	return &g_res_mgr;
}

//----------------------------------------------
// cmd related functions
static int _init_prioq(struct prio_q_inst *inst)
{
	int i = 0;

	if (inst == NULL)
		return -EINVAL;

	/* init all priority linked list */
	for (i = 0; i < APUSYS_PRIORITY_MAX; i++)
		INIT_LIST_HEAD(&inst->prio[i]);

	return 0;
}

int res_get_device_num(int dev_type)
{
	struct apusys_res_table *tab = NULL;

	tab = res_get_table(dev_type);
	if (tab == NULL) {
		LOG_ERR("no device(%d) available\n", dev_type);
		return 0;
	}

	return tab->dev_num;
}

uint64_t res_get_dev_support(void)
{
	uint64_t ret = 0;
	int res_max = 0, ret_max = 0;

	res_max = sizeof(g_res_mgr.dev_support);
	ret_max = sizeof(uint64_t);

	if (res_max > ret_max)
		LOG_ERR("device support overflow\n");

	memcpy(&ret, g_res_mgr.dev_support,
		res_max < ret_max ? res_max : ret_max);

	return ret;
}

int res_get_queue_len(int dev_type)
{
	struct apusys_res_table *tab = NULL;
	int busy_num = 0, i = 0;

	tab = res_get_table(dev_type);
	if (tab == NULL) {
		LOG_ERR("no device(%d) available\n", dev_type);
		return 0;
	}

	/* check device busy num */
	for (i = 0; i < tab->dev_num; i++) {
		if (tab->dev_list[i].cur_owner == APUSYS_DEV_OWNER_SCHEDULER &&
			tab->dev_list[i].is_deadline == 0)
			busy_num++;
	}

	return tab->prio_q->normal_len + busy_num;
}

int insert_subcmd(void *isc)
{
	struct apusys_res_table *tab = NULL;
	struct prio_q_inst *inst = NULL;
	struct apusys_subcmd *sc = (struct apusys_subcmd *)isc;
	struct apusys_cmd_hdr *hdr = sc->par_cmd->hdr;
	int priority = hdr->priority;

	tab = res_get_table(sc->type);
	// can't find info, unlock and return fail
	if (tab == NULL) {
		LOG_ERR("no device(%d) available\n", sc->type);
		return -EINVAL;
	}

	LOG_DEBUG("insert 0x%llx-#%d to q(%d/%d/%d/%d)\n",
		sc->par_cmd->cmd_id,
		sc->idx,
		sc->type,
		priority,
		sc->par_cmd->hdr->priority,
		hdr->soft_limit);

	/* get type's queue */
	sc->state = CMD_STATE_READY;
	inst = tab->prio_q;

	if (hdr->soft_limit) { /* Deadline Queue */
		deadline_node_insert(&tab->deadline_q, sc);

		inst->deadline_len++;
		LOG_DEBUG("deadline task(%d)\n",
			inst->deadline_len);

	} else { /* Priority Queue */
		list_add_tail(&sc->q_list, &inst->prio[priority]);
		bitmap_set(inst->node_exist, priority, 1);

		inst->normal_len++;
		LOG_DEBUG("normal task(%d/%d)\n",
			sc->par_cmd->hdr->priority,
			inst->normal_len);
	}

	bitmap_set(g_res_mgr.cmd_exist, sc->type, 1);
	complete(&g_res_mgr.sched_comp);
	return 0;
}

int insert_subcmd_lock(void *isc)
{
	int ret = 0;
	mutex_lock(&g_res_mgr.mtx);
	/* delete subcmd node from ce list */
	ret = insert_subcmd(isc);
	mutex_unlock(&g_res_mgr.mtx);

	return ret;
}

int pop_subcmd(int type, void **isc)
{
	struct apusys_res_table *tab = NULL;
	int priority = 0;
	struct apusys_subcmd *sc = NULL;
	struct apusys_res_mgr *res_mgr = &g_res_mgr;

	tab = res_get_table(type);
	if (tab == NULL) {
		LOG_ERR("no device(%d) available\n", type);
		return -EINVAL;
	}


	sc = deadline_node_pop_first(&tab->deadline_q);
	if (sc) { /* pop cmd from deadline queue */
		LOG_DEBUG("pop 0x%llx-#%d from deadline_q(%d/%llu)\n",
				sc->par_cmd->cmd_id,
				sc->idx,
				sc->type,
				sc->deadline);
		tab->prio_q->deadline_len--;
		LOG_DEBUG("deadline task(%d)\n",
			tab->prio_q->deadline_len);
	} else { /* deadline queue is empty, pop cmd from priority queue */
		priority = find_last_bit(tab->prio_q->node_exist,
				APUSYS_PRIORITY_MAX);
		if (priority >= APUSYS_PRIORITY_MAX) {
			LOG_ERR("can't find cmd in type(%d) priority queue\n",
				type);
			return -EINVAL;
		}

		sc = list_first_entry(&tab->prio_q->prio[priority],
			struct apusys_subcmd, q_list);
		if (sc == NULL) {
			LOG_ERR(
			"get cmd from device(%d) priority queue(%d) fail\n",
			type, priority);
			return -EINVAL;
		}
		list_del(&sc->q_list);
		LOG_DEBUG("pop 0x%llx-#%d from q(%d/%d)\n",
			sc->par_cmd->cmd_id,
			sc->idx,
			sc->type,
			priority);
		tab->prio_q->normal_len--;
		LOG_DEBUG("normal task(%d/%d)\n",
			sc->par_cmd->hdr->priority,
			tab->prio_q->normal_len);

		if (list_empty(&tab->prio_q->prio[priority]))
			bitmap_clear(tab->prio_q->node_exist, priority, 1);

	}

	 /* check if both deadline/priority queue are empty */
	if (bitmap_empty(tab->prio_q->node_exist, APUSYS_PRIORITY_MAX) &&
			deadline_node_empty(&tab->deadline_q)) {
		LOG_DEBUG("device(%d) cmd empty\n", type);
		bitmap_clear(res_mgr->cmd_exist, type, 1);
	}
	*isc = sc;
	return 0;
}

int delete_subcmd(void *isc)
{
	struct list_head *tmp = NULL, *list_ptr = NULL;
	struct apusys_subcmd *sc_node = NULL;
	struct apusys_subcmd *sc = (struct apusys_subcmd *)isc;
	struct apusys_cmd *cmd = NULL;
	struct apusys_res_table *tab = NULL;
	int ret = -EINVAL, i = 0;

	if (isc == NULL)
		return -EINVAL;

	cmd = sc->par_cmd;
	LOG_DEBUG("delete 0x%llx-#%d q(%d/%d)\n",
		sc->par_cmd->cmd_id,
		sc->idx,
		sc->type,
		sc->par_cmd->hdr->priority);

	/* find subcmd from type priority queue */
	tab = res_get_table(sc->type);
	if (tab == NULL) {
		LOG_ERR("no device(%d) available\n", sc->type);
		ret = -EINVAL;
		goto out;
	}

	DEBUG_TAG;

	/* query list to find mem in apusys user */
	for (i = 0; i < APUSYS_PRIORITY_MAX; i++) {
		list_for_each_safe(list_ptr, tmp, &tab->prio_q->prio[i]) {
			sc_node = list_entry(list_ptr,
				struct apusys_subcmd, q_list);
			if (sc_node != sc)
				continue;

			LOG_DEBUG("delete 0x%llx-#%d from q(%d/%d)\n",
				sc->par_cmd->cmd_id,
				sc->idx,
				sc->type,
				i);

			list_del(&sc->q_list);
			if (list_empty(&tab->prio_q->prio[i])) {
				bitmap_clear(tab->prio_q->node_exist, i, 1);

				if (bitmap_empty(tab->prio_q->node_exist,
					APUSYS_PRIORITY_MAX)) {
					LOG_DEBUG("device(%d) cmd empty\n",
						sc->type);
					bitmap_clear(g_res_mgr.cmd_exist,
						sc->type, 1);
				}
			}

			/* setup queue length */
			if (sc->par_cmd->hdr->soft_limit) {
				tab->prio_q->deadline_len--;
				LOG_DEBUG("deadline task(%d)\n",
					tab->prio_q->deadline_len);
			} else {
				tab->prio_q->normal_len--;
				LOG_DEBUG("normal task(%d/%d)\n",
					sc->par_cmd->hdr->priority,
					tab->prio_q->normal_len);
			}

			ret = 0;
			goto out;

		}
	}
	DEBUG_TAG;

out:
	return ret;
}

int delete_subcmd_lock(void *isc)
{
	int ret = 0;

	mutex_lock(&g_res_mgr.mtx);
	ret = delete_subcmd(isc);
	mutex_unlock(&g_res_mgr.mtx);

	return ret;
}

//----------------------------------------------
// device related functions
#if 0
int get_apusys_device(int dev_type, uint64_t owner, struct apusys_device **dev)
{
	struct apusys_res_table *tab = NULL;
	struct apusys_device *ret_dev = NULL;
	unsigned long bit_idx = 0, idx = 0;
	int dev_idx = -1;

	if (dev_type <= APUSYS_DEVICE_NONE || dev_type >= APUSYS_DEVICE_MAX) {
		LOG_DEBUG("request device[%d] invalid\n", dev_type);
		return -EINVAL;
	}

	/* get apusys_res_table*/
	tab = res_get_table(dev_type);
	/* can't find info, unlock and return fail */
	if (tab == NULL) {
		LOG_ERR("no device[%d] available\n", dev_type);
		return -EINVAL;
	}

	/* get idle device from bitmap */
	bit_idx = find_first_zero_bit(tab->dev_status, tab->dev_num);
	LOG_DEBUG("dev[%d] idx[%lu/%u] available\n",
		dev_type, bit_idx, tab->dev_num);
	if (bit_idx < tab->dev_num) {
		ret_dev = tab->dev_list[bit_idx].dev;
		tab->dev_list[bit_idx].cur_owner = owner;
		bitmap_set(tab->dev_status, bit_idx, 1);
		dev_idx = bit_idx;
	}

	/* if no dev available in table, clear dev status in resource mgr */
	idx = find_next_zero_bit(tab->dev_status, tab->dev_num, 0);
	if (idx >= tab->dev_num) {
		bitmap_clear(g_res_mgr.dev_exist, tab->dev_type, 1);
		LOG_DEBUG("clear res mgr dev(%d) status type\n", tab->dev_type);
	}

	tab->available_num--;

	*dev = ret_dev;
	return dev_idx;
}
#endif

int put_apusys_device(struct apusys_dev_info *dev_info)
{
	struct apusys_res_table *tab = NULL;
	struct list_head *tmp = NULL, *list_ptr = NULL;
	struct apusys_dev_aquire *acq = NULL;
	//struct apusys_dev_info *dev_info = NULL;

	if (dev_info == NULL)
		return -EINVAL;

	if (dev_info->dev->dev_type >= APUSYS_DEVICE_MAX) {
		LOG_ERR("request device[%d/%d] invalid\n",
			dev_info->dev->dev_type, APUSYS_DEVICE_MAX);
		return -ENODEV;
	}

	tab = res_get_table(dev_info->dev->dev_type);
	/* can't find info, unlock and return fail */
	if (tab == NULL) {
		LOG_ERR("no device[%d] available\n", dev_info->dev->dev_type);
		return -ENODEV;
	}

	DEBUG_TAG;
	/* query list to find mem in apusys user */
	list_for_each_safe(list_ptr, tmp, &tab->acq_list) {
		acq = list_entry(list_ptr, struct apusys_dev_aquire, tab_list);
		LOG_DEBUG("device(%d) has acquire(%p/%d) waiting, put device\n",
			dev_info->dev->dev_type, acq, acq->is_done);
		if (acq->is_done == 0) {
			DEBUG_TAG;

			dev_info->cmd_id = 0;
			dev_info->sc_idx = 0;
			dev_info->cur_owner = acq->owner;
			acq->acq_num++;
			LOG_DEBUG("device (%d) add #%d dev to acq(%d/%d)\n",
				dev_info->dev->dev_type, dev_info->dev->idx,
				acq->acq_num, acq->target_num);
			if (acq->acq_num == acq->target_num)
				acq->is_done = 1;
			list_add_tail(&dev_info->acq_list, &acq->dev_info_list);
			DEBUG_TAG;
			if (acq->is_done)
				complete(&acq->comp);
			complete(&g_res_mgr.sched_comp);
			return 0;
		}
		DEBUG_TAG;
	}
	DEBUG_TAG;

	/* get idle device from bitmap */
	LOG_DEBUG("put device[%d](%d/%d)\n",
		dev_info->dev->dev_type, dev_info->dev->idx, tab->dev_num);
	dev_info->cur_owner = APUSYS_DEV_OWNER_NONE;
	dev_info->cmd_id = 0;
	dev_info->sc_idx = 0;
	dev_info->is_deadline = 0;
	bitmap_clear(tab->dev_status, dev_info->dev->idx, 1); // clear status
	tab->available_num++;

	bitmap_set(g_res_mgr.dev_exist, tab->dev_type, 1);
	complete(&g_res_mgr.sched_comp);

	DEBUG_TAG;

	return 0;
}

int put_device_lock(struct apusys_dev_info *dev_info)
{
	int ret = 0;

	mutex_lock(&g_res_mgr.mtx);
	ret = put_apusys_device(dev_info);
	mutex_unlock(&g_res_mgr.mtx);

	return ret;
}

int acq_device_try(struct apusys_dev_aquire *acq)
{
	struct apusys_res_table *tab = NULL;
	unsigned long bit_idx = 0, idx = 0;
	int ret_num = 0, i = 0;

	/* check arguments */
	if (acq == NULL) {
		LOG_ERR("invalid argument\n");
		return -EINVAL;
	}

	acq->acq_num = 0;

	/* get apusys_res_table*/
	tab = res_get_table(acq->dev_type);
	/* can't find info, unlock and return fail */
	if (tab == NULL) {
		LOG_ERR("no device(%d) available\n", acq->dev_type);
		return -EINVAL;
	}

	/* check dev num */
	if (acq->target_num > tab->dev_num) {
		LOG_ERR("alloc wrong num(%d/%d) device(%d)\n",
			acq->target_num, tab->dev_num, acq->dev_type);
		return -EINVAL;
	}

	/* get idle device from bitmap */
	ret_num = acq->target_num < tab->available_num
		? acq->target_num : tab->available_num;
	LOG_DEBUG("allocate device(%d) (%d/%d/%d)\n",
		acq->dev_type, acq->target_num,
		tab->available_num, tab->dev_num);
	for (i = 0; i < ret_num; i++) {
		bit_idx = find_first_zero_bit(tab->dev_status, tab->dev_num);
		LOG_DEBUG("dev(%d) idx(%lu/%u) available\n",
			acq->dev_type, bit_idx, tab->dev_num);
		if (bit_idx < tab->dev_num) {
			list_add_tail(&tab->dev_list[bit_idx].acq_list,
				&acq->dev_info_list);
			tab->dev_list[bit_idx].cur_owner = acq->owner;
			bitmap_set(tab->dev_status, bit_idx, 1);
			tab->available_num--;

			acq->acq_num++;
		} else {
			LOG_ERR("can't find device(%d)\n", acq->dev_type);
		}
	}

	/* if no dev available in table, clear dev status in resource mgr */
	idx = find_next_zero_bit(tab->dev_status, tab->dev_num, 0);
	if (idx >= tab->dev_num) {
		bitmap_clear(g_res_mgr.dev_exist, tab->dev_type, 1);
		LOG_DEBUG("clear res mgr dev(%d) status type\n", tab->dev_type);
	}

	return 0;
}

int acq_device_check(struct apusys_dev_aquire **iacq)
{
	int i = 0;
	struct apusys_res_table *tab = NULL;
	struct apusys_dev_aquire *acq = NULL;

	/* TODO, optimize acq list */
	DEBUG_TAG;
	for (i = 0; i < APUSYS_DEVICE_MAX; i++) {
		tab = res_get_table(i);
		if (tab == NULL)
			continue;

		if (!list_empty(&tab->acq_list)) {
			LOG_DEBUG("res tab(%d) has acq_list\n", i);
			acq = list_first_entry(&tab->acq_list,
				struct apusys_dev_aquire, tab_list);
			if (acq->is_done && acq->target_num == acq->acq_num
				&& acq->owner == APUSYS_DEV_OWNER_SCHEDULER) {
				LOG_DEBUG("res tab(%d) has acq_list done\n", i);
				list_del(&acq->tab_list);
				*iacq = acq;
				return 0;
			}
		}
	}
	DEBUG_TAG;

	return -ENODATA;
}

int acq_device_async(struct apusys_dev_aquire *acq)
{
	struct apusys_res_table *tab = NULL;
	int i = 0, bit_idx = 0, num = 0;

	if (acq == NULL)
		return -EINVAL;

	if (acq->dev_type >= APUSYS_DEVICE_MAX || acq->target_num <= 0 ||
		acq->acq_num != 0) {
		LOG_WARN("invalid arg, dev(%d) num(%d/%d)\n",
			acq->dev_type,
			acq->target_num,
			acq->acq_num);
		return -EINVAL;
	}

	init_completion(&acq->comp);
	INIT_LIST_HEAD(&acq->dev_info_list);

	/* get apusys_res_table*/
	tab = res_get_table(acq->dev_type);
	/* can't find info, unlock and return fail */
	if (tab == NULL) {
		LOG_ERR("no device(%d) available\n", acq->dev_type);
		return -EINVAL;
	}

	/* check dev num */
	if (acq->target_num > tab->dev_num) {
		LOG_ERR("alloc wrong num(%d/%d) device(%d)\n",
			acq->target_num, tab->dev_num, acq->dev_type);
		return -EINVAL;
	}

	DEBUG_TAG;

	/* device table's available device is more than acquire target num */
	num = tab->available_num < (acq->target_num-acq->acq_num)
		? tab->available_num : (acq->target_num-acq->acq_num);
	for (i = 0; i < num; i++) {
		bit_idx = find_first_zero_bit(tab->dev_status, tab->dev_num);
		LOG_DEBUG("dev(%d) idx(%d/%u) available\n",
			acq->dev_type, bit_idx, tab->dev_num);
		if (bit_idx < tab->dev_num) {
			LOG_DEBUG("add to acquire list\n");
			list_add_tail(&tab->dev_list[bit_idx].acq_list,
				&acq->dev_info_list);
			bitmap_set(tab->dev_status, bit_idx, 1);
			tab->dev_list[bit_idx].cur_owner = acq->owner;
			tab->available_num--;

			acq->acq_num++;
		} else {
			LOG_ERR("can't find device(%d)\n", acq->dev_type);
			return -EINVAL;
		}
	}
	DEBUG_TAG;

	if (acq->acq_num < acq->target_num) {
		LOG_INFO("acquire device(%d/%d/%d) async...\n",
			acq->dev_type, acq->acq_num, acq->target_num);
		LOG_DEBUG("add to acquire list\n");
		list_add_tail(&acq->tab_list, &tab->acq_list);
	}
	DEBUG_TAG;

	return acq->acq_num;
}

int acq_device_sync(struct apusys_dev_aquire *acq)
{
	int ret = 0;

	if (acq == NULL)
		return -EINVAL;

	acq->acq_num = 0;
	DEBUG_TAG;

	mutex_lock(&g_res_mgr.mtx);


	ret = acq_device_async(acq);
	if (ret < 0) {
		mutex_unlock(&g_res_mgr.mtx);
		return ret;
	}
	DEBUG_TAG;

	/* if acquire == target, return ok */
	if (acq->acq_num == acq->target_num) {
		DEBUG_TAG;
		mutex_unlock(&g_res_mgr.mtx);
		return acq->acq_num;
	}
	DEBUG_TAG;

	mutex_unlock(&g_res_mgr.mtx);
	while (acq->acq_num != acq->target_num) {
		ret = wait_for_completion_interruptible(&acq->comp);
		if (ret) {
			LOG_ERR("acquire device(%d/%d/%d) interrupt, ret(%d)\n",
				acq->dev_type, acq->acq_num,
				acq->target_num, ret);
			break;
		}
	}

	if (acq->acq_num == acq->target_num) {
		LOG_DEBUG("acquire dev(%d/%d/%d) sync ok\n",
		acq->dev_type, acq->acq_num, acq->target_num);
		list_del(&acq->tab_list);
	}

	return acq->acq_num;
}

int res_power_on(int dev_type, uint32_t idx,
	uint32_t boost_val, uint32_t timeout)
{
	struct apusys_res_table *tab = NULL;
	struct apusys_dev_info *info = NULL;
	struct apusys_power_hnd pwr;
	int ret = 0;

	LOG_INFO("poweron res(%d/%u) boost(%u) timeout(%u)\n",
		dev_type, idx, boost_val, timeout);

	if (boost_val > 100) {
		LOG_ERR("invalid boost val(%d)\n", boost_val);
		return -EINVAL;
	}

	tab = res_get_table(dev_type);
	if (tab == NULL) {
		LOG_ERR("invalid device type(%d)\n", dev_type);
		return -EINVAL;
	}

	if (idx >= tab->dev_num) {
		LOG_ERR("invalid device idx(%u/%d)\n",
			idx, tab->dev_num);
		return -EINVAL;
	}

	info = &tab->dev_list[idx];
	if (info == NULL) {
		LOG_ERR("can't find device info(%p) (%d/%d)\n",
			info, dev_type, idx);
		return -EINVAL;
	}

	memset(&pwr, 0, sizeof(struct apusys_power_hnd));
	pwr.opp = 0;
	pwr.boost_val = boost_val;
	if (timeout > APUSYS_SETPOWER_TIMEOUT)
		pwr.timeout = APUSYS_SETPOWER_TIMEOUT;
	else
		pwr.timeout = timeout;
	ret = info->dev->send_cmd(APUSYS_CMD_POWERON, &pwr, info->dev);
	if (ret) {
		LOG_ERR("poweron res(%d/%d) boostval(%d) fail(%d)\n",
			dev_type, idx, boost_val, ret);
	}

	return ret;
}

int res_power_off(int dev_type, uint32_t idx)
{
	struct apusys_res_table *tab = NULL;
	struct apusys_dev_info *info = NULL;
	struct apusys_power_hnd pwr;
	int ret = 0;

	LOG_INFO("powerdown res(%d/%u)\n", dev_type, idx);

	tab = res_get_table(dev_type);
	if (tab == NULL) {
		LOG_ERR("invalid device type(%d)\n", dev_type);
		return -EINVAL;
	}

	if (idx >= tab->dev_num) {
		LOG_ERR("invalid device idx(%u/%d)\n",
			idx, tab->dev_num);
		return -EINVAL;
	}

	info = &tab->dev_list[idx];
	if (info == NULL) {
		LOG_ERR("can't find device info(%p) (%d/%u)\n",
			info, dev_type, idx);
		return -EINVAL;
	}

	memset(&pwr, 0, sizeof(struct apusys_power_hnd));
	ret = info->dev->send_cmd(APUSYS_CMD_POWERDOWN, &pwr, info->dev);
	if (ret) {
		LOG_ERR("powerdown res(%d/%u) fail(%d)\n",
			dev_type, idx, ret);
	}

	return ret;
}


int res_load_firmware(int dev_type, uint32_t magic, const char *name,
	int idx, uint64_t kva, uint32_t iova, uint32_t size, int op)
{
	struct apusys_res_table *tab = NULL;
	struct apusys_dev_info *info = NULL;
	struct apusys_firmware_hnd hnd;
	int ret = 0;

	if (name == NULL)
		return -EINVAL;

	tab = res_get_table(dev_type);
	if (tab == NULL) {
		LOG_ERR("invalid device type(%d)\n", dev_type);
		return -EINVAL;
	}

	if (idx >= tab->dev_num) {
		LOG_ERR("invalid device idx(%d/%d)\n", idx, tab->dev_num);
		return -EINVAL;
	}

	info = &tab->dev_list[idx];
	if (info == NULL) {
		LOG_ERR("can't find device info(%p) (%d/%d)\n",
			info, dev_type, idx);
		return -EINVAL;
	}

	memset(&hnd, 0, sizeof(struct apusys_firmware_hnd));
	hnd.magic = magic;
	hnd.kva = kva;
	hnd.iova = iova;
	hnd.size = size;
	hnd.op = op;
	strncpy(hnd.name, name, sizeof(hnd.name)-1);

	ret = info->dev->send_cmd(APUSYS_CMD_FIRMWARE, &hnd, info->dev);
	if (ret) {
		LOG_ERR("load(%d) dev(%d/%d) fw(0x%x/0x%x) fail(%d)",
			op, dev_type, idx, iova, size, ret);
	}

	return ret;
}

int res_send_ucmd(int dev_type, int idx,
	uint64_t kva, uint32_t iova, uint32_t size)
{
	struct apusys_usercmd_hnd hnd;
	struct apusys_res_table *tab = NULL;
	struct apusys_dev_info *info = NULL;
	int ret = 0;

	tab = res_get_table(dev_type);
	if (tab == NULL) {
		LOG_ERR("invalid device type(%d)\n", dev_type);
		return -EINVAL;
	}

	if (idx >= tab->dev_num) {
		LOG_ERR("invalid device idx(%d/%d)\n", idx, tab->dev_num);
		return -EINVAL;
	}

	info = &tab->dev_list[idx];
	if (info == NULL) {
		LOG_ERR("can't find device info(%p) (%d/%d)\n",
			info, dev_type, idx);
		return -EINVAL;
	}

	memset(&hnd, 0, sizeof(struct apusys_usercmd_hnd));
	hnd.kva = kva;
	hnd.iova = iova;
	hnd.size = size;

	ret = info->dev->send_cmd(APUSYS_CMD_USER, &hnd, info->dev);
	if (ret) {
		LOG_ERR("send user cmd(0x%llx/0x%x/%d) to dev(%d/%d) fail(%d)",
			kva, iova, size,
			dev_type, idx, ret);
	}

	return ret;
}

void res_sched_enable(int enable)
{
	mutex_lock(&g_res_mgr.mtx);
	if (enable) {
		LOG_INFO("start apusys sched\n");
		g_res_mgr.sched_pause = 0;
	} else {
		LOG_INFO("pause apusys sched\n");
		g_res_mgr.sched_pause = 1;
	}
	mutex_unlock(&g_res_mgr.mtx);
}

int res_secure_on(int dev_type)
{
	struct apusys_res_table *tab = NULL;
	int ret = 0;

	tab = res_get_table(dev_type);
	if (tab == NULL) {
		LOG_ERR("secure control can't find dev(%d)\n", dev_type);
		return -ENODEV;
	}

	switch (dev_type) {
	case APUSYS_DEVICE_SAMPLE:
		LOG_INFO("dev(%d) secure mode on(%d)\n", dev_type, ret);
		break;

#ifdef CONFIG_MTK_GZ_SUPPORT_SDSP
	case APUSYS_DEVICE_VPU:
		ret = mtee_sdsp_enable(1);
		LOG_INFO("dev(%d) secure mode on(%d)\n", dev_type, ret);
		break;
#endif
	default:
		LOG_ERR("dev(%d) not support secure mode\n", dev_type);
		ret = -ENODEV;
		break;
	}

	return ret;
}

int res_secure_off(int dev_type)
{
	struct apusys_res_table *tab = NULL;
	int ret = 0;

	tab = res_get_table(dev_type);
	if (tab == NULL) {
		LOG_ERR("secure control can't find dev(%d)\n", dev_type);
		return -ENODEV;
	}

	switch (dev_type) {
	case APUSYS_DEVICE_SAMPLE:
		LOG_INFO("dev(%d) secure mode off(%d)\n", dev_type, ret);
		break;

#ifdef CONFIG_MTK_GZ_SUPPORT_SDSP
	case APUSYS_DEVICE_VPU:
		ret = mtee_sdsp_enable(0);
		LOG_INFO("dev(%d) secure mode off(%d)\n", dev_type, ret);
		break;
#endif
	default:
		LOG_ERR("dev(%d) not support secure mode\n", dev_type);
		ret = -ENODEV;
		break;
	}

	return ret;
}

void res_mgt_dump(void *s_file)
{
	struct apusys_res_table *tab = NULL;
	struct apusys_dev_info *info = NULL;
	struct seq_file *s = (struct seq_file *)s_file;
	int i = 0, j = 0;

#define LINEBAR \
	"|-----------------------------------------------------------|\n"
#define S_LINEBAR \
	"|-----------------------------------------|\n"

	LOG_CON(s, LINEBAR);
	LOG_CON(s, "| %-58s|\n",
		"apusys device table");
	LOG_CON(s, LINEBAR);

	mutex_lock(&g_res_mgr.mtx);

	/* query list to find apusys device table by type */
	for (i = 0; i < APUSYS_DEVICE_MAX; i++) {
		tab = res_get_table(i);
		if (tab == NULL)
			continue;

		for (j = 0; j < tab->dev_num; j++) {
			info = &tab->dev_list[j];
			if (info->cur_owner == APUSYS_DEV_OWNER_DISABLE) {
				if (j != 0) {
					LOG_CON(s, "|%17s| %-8s#%-4d>%26s|\n",
						"",
						"<device ",
						j,
						"");
					LOG_CON(s, "|%17s| -%40s|\n",
						"",
						"status       disable");
					LOG_CON(s, LINEBAR);
					continue;
				}

				LOG_CON(s, "| %9s(%4d) | %-8s#%-4d>%-26s|\n",
					"dev type ",
					tab->dev_type,
					"<device ",
					j,
					"");
				LOG_CON(s, "| %9s(%4d) | -%40s|\n",
					"core num ",
					tab->dev_num,
					"status       disable");
				LOG_CON(s, "| %-9s(%4d) | %-17s= -%21s|\n",
					"available",
					tab->available_num,
					"cmd id",
					"none");
				LOG_CON(s, LINEBAR);

				continue;
			}

			if (j == 0) {
				LOG_CON(s, "| %9s(%4d) | %-8s#%-4d>%26s|\n",
					"dev type ",
					tab->dev_type,
					"<device ",
					j,
					"");
				LOG_CON(s, "| %9s(%4d) | %-17s= %-21p|\n",
					"core num ",
					tab->dev_num,
					"device ptr",
					info->dev);
				LOG_CON(s, "| %9s(%4d) | %-17s= %-21d|\n",
					"available",
					tab->available_num,
					"device idx",
					info->dev->idx);
			} else {
				LOG_CON(s, "|%-17s| %-8s#%-4d>%-26s|\n",
					"",
					"<device ",
					j,
					"");
				LOG_CON(s, "|%-17s| %-17s= %-21p|\n",
					"",
					"device ptr",
					info->dev);
				LOG_CON(s, "|%-17s| %-17s= %-21d|\n",
					"",
					"device idx",
					info->dev->idx);
			}
			LOG_CON(s, "|%-17s| %-17s= 0x%-19llx|\n",
				"",
				"cmd id",
				info->cmd_id);
			LOG_CON(s, "|%-17s| %-17s= 0x%-19d|\n",
				"",
				"subcmd idx",
				info->sc_idx);
			LOG_CON(s, "|%-17s| %-17s= %-21llu|\n",
				"",
				"current owner",
				info->cur_owner);

			if (j >= tab->dev_num-1) {
				LOG_CON(s, LINEBAR);
			} else {
				LOG_CON(s, "|%-17s%s",
					"",
					S_LINEBAR);
			}
		}
	}

	mutex_unlock(&g_res_mgr.mtx);

#undef LINEBAR
#undef S_LINEBAR
}

//----------------------------------------------
int res_mgt_init(void)
{
	LOG_INFO("%s +\n", __func__);

	/* clear global variable */
	memset(&g_res_mgr, 0, sizeof(g_res_mgr));
	mutex_init(&g_res_mgr.mtx);
	init_completion(&g_res_mgr.sched_comp);

	LOG_INFO("%s done -\n", __func__);

	return 0;
}

int res_mgt_destroy(void)
{
	LOG_INFO("%s +\n", __func__);

	/* clear global variable */
	memset(&g_res_mgr, 0, sizeof(g_res_mgr));

	LOG_INFO("%s done -\n", __func__);
	return 0;
}

//----------------------------------------------
// export function
int apusys_register_device(struct apusys_device *dev)
{
	struct apusys_res_table *tab = NULL;
	struct prio_q_inst *q_inst = NULL;
	int i = 0, ret = 0;

	LOG_DEBUG("register apusys device +\n");

	/* check argument ptr valid */
	if (dev == NULL) {
		LOG_ERR("register dev nullptr\n");
		return -EINVAL;
	}

	/* check dev type valid */
	if (dev->dev_type < 0 || dev->dev_type >= APUSYS_DEVICE_MAX) {
		LOG_ERR("register dev wrong type[%d]\n", dev->dev_type);
		return -EINVAL;
	};

	mutex_lock(&g_res_mgr.mtx);
	tab = res_get_table(dev->dev_type);
	if (tab != NULL) {
		/* device type already registered, add directly */
		LOG_DEBUG("add dev to type[%d] list\n", dev->dev_type);
		for (i = 0; i < APUSYS_DEV_TABLE_MAX; i++) {
			if (tab->dev_list[i].cur_owner
				!= APUSYS_DEV_OWNER_DISABLE)
				continue;

			tab->dev_list[i].dev = dev; // setup dev
			bitmap_clear(tab->dev_status, i, 1); // setup status

			if (i >= tab->dev_num)
				tab->dev_num++;

			/* rewrite device's idx */
			if (dev->idx != i) {
				LOG_WARN("over write dev idx(%d/%d)",
					dev->idx,
					i);
				dev->idx = i;
			}

			tab->dev_list[i].cur_owner = APUSYS_DEV_OWNER_NONE;
			LOG_DEBUG("register dev(%p) success (%d/%d)\n",
				dev, i, tab->dev_num);

			break;
		}

		if (i >= APUSYS_DEV_TABLE_MAX) {
			LOG_ERR("register dev(%d) fail(%d/%d), dev list full\n",
				dev->dev_type, i, tab->dev_num);
			mutex_unlock(&g_res_mgr.mtx);
			return -ENODEV;
		}
	} else {
		/* device type not registered yet, allocate table first */
		LOG_DEBUG("register device(%d)\n", dev->dev_type);
		tab = kzalloc(sizeof(struct apusys_res_table), GFP_KERNEL);
		tab->dev_list[tab->dev_num].dev = dev; // dev
		tab->dev_list[tab->dev_num].cur_owner = APUSYS_DEV_OWNER_NONE;
		bitmap_clear(tab->dev_status, tab->dev_num, 1); //status
		tab->dev_type = dev->dev_type;// type
		if (dev->idx != tab->dev_num) {
			LOG_WARN("dev(%d) idx not match(%d/%d)\n",
				dev->dev_type, dev->idx, tab->dev_num);
			dev->idx = tab->dev_num;
		}
		tab->dev_num++; //dev number
		for (i = 0; i < APUSYS_DEV_TABLE_MAX; i++)
			INIT_LIST_HEAD(&tab->dev_list[i].acq_list);

		INIT_LIST_HEAD(&tab->acq_list);

		deadline_node_init(&tab->deadline_q);/* Init deadline queue */
		g_res_mgr.tab[tab->dev_type] = tab;

		bitmap_set(g_res_mgr.dev_support, tab->dev_type, 1);

		q_inst = kzalloc(sizeof(struct prio_q_inst), GFP_KERNEL);
		if (q_inst == NULL) {
			LOG_ERR("allocate q inst fail\n");
			ret = -EINVAL;
			mutex_unlock(&g_res_mgr.mtx);
			goto allc_q_fail;
		}
		/* priority queue */
		if (_init_prioq(q_inst)) {
			LOG_ERR("init q inst(%p) fail\n", q_inst);
			ret = -EINVAL;
			mutex_unlock(&g_res_mgr.mtx);
			goto init_q_fail;
		}

		/* set dev's priority queue */
		tab->prio_q = q_inst;

		/* create queue length for query */
		if (apusys_dbg_create_queue(&tab->dev_type))
			LOG_ERR("create queue length node(%d) fail\n",
			tab->dev_type);

		LOG_DEBUG("register device(%d)(%d/%d) done,\n",
			dev->dev_type, dev->idx, tab->dev_num);
	}

	tab->available_num++;

	/* mark mgr type dev is available */
	bitmap_set(g_res_mgr.dev_exist, dev->dev_type, 1);

	mutex_unlock(&g_res_mgr.mtx);

	/* create thread to thread pool */
	for (i = 0; i <= dev->preempt_level; i++) {
		if (thread_pool_add_once()) {
			LOG_ERR("create thread(%d) to pool fail\n", i);
			if (thread_pool_delete(i))
				LOG_ERR("delete thread(%d) fail\n", i);

			ret = -EINVAL;
			goto init_q_fail;
		}
	}

	LOG_DEBUG("register apusys device -\n");

	return 0;

init_q_fail:
	/* release queue inst */
	kfree(q_inst);
	tab->prio_q = NULL;
allc_q_fail:
	/* release dev table*/
	g_res_mgr.tab[tab->dev_type] = NULL;
	kfree(tab);

	return ret;

}
EXPORT_SYMBOL(apusys_register_device);

int apusys_unregister_device(struct apusys_device *dev)
{
	struct apusys_res_table *tab = NULL;
	int ret = 0, i = 0, idx = 0;

	DEBUG_TAG;
	if (dev == NULL) {
		LOG_ERR("invalid argument\n");
		return -EINVAL;
	}

	mutex_lock(&g_res_mgr.mtx);

	/* TODO: should flush cmd from dev */
	tab = res_get_table(dev->dev_type);
	if (tab == NULL) {
		LOG_ERR("can't find dev table(%d)\n", dev->dev_type);
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < APUSYS_DEV_TABLE_MAX; i++) {
		if (tab->dev_list[i].dev == dev) {
			LOG_DEBUG("get dev(%p)\n", dev);
			if (tab->dev_list[i].cur_owner ==
				APUSYS_DEV_OWNER_NONE) {
				LOG_DEBUG("dev(%p) is free, can release\n",
					tab->dev_list[i].dev);

				/* clear dev info */
				memset(&tab->dev_list[i], 0,
					sizeof(struct apusys_dev_info));
				/* delete from dev table */
				/* set status let schdeuler don't dispatch */
				bitmap_set(tab->dev_status, i, 1);
				tab->available_num--;
			} else {
				LOG_ERR("dev(%p) is busy, release fail\n",
					dev);
				ret = -EINVAL;
			}
			break;
		}
	}

	if (i >= APUSYS_DEV_TABLE_MAX) {
		LOG_ERR("can't find dev, release device fail\n");
		ret = -EINVAL;
	}

	/* if any dev available in table, clear dev status in resource mgr*/
	idx = find_next_zero_bit(tab->dev_status, tab->dev_num, 0);
	if (idx >= tab->dev_num) {
		bitmap_clear(g_res_mgr.dev_exist, tab->dev_type, 1);
		LOG_DEBUG("clear res mgr dev(%d) status type\n", tab->dev_type);
	}

out:
	mutex_unlock(&g_res_mgr.mtx);
	return ret;
}
EXPORT_SYMBOL(apusys_unregister_device);

