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

#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/device.h>
#ifdef CONFIG_PM_WAKELOCKS
#include <linux/pm_wakeup.h>
#endif

#include "apusys_cmn.h"
#include "apusys_options.h"
#include "apusys_device.h"
#include "apusys_dbg.h"
#include "cmd_parser.h"
#include "resource_mgt.h"
#include "scheduler.h"
#include "cmd_parser_mdla.h"
#include "thread_pool.h"
#include "midware_trace.h"
#include "sched_deadline.h"
#include "mnoc_api.h"
#include "reviser_export.h"

/* init link list head, which link all dev table */
struct apusys_cmd_table {
	struct list_head list;
	struct mutex list_mtx;
};

struct apusys_prio_q_table {
	struct list_head list;
	struct mutex list_mtx;
};

struct pack_cmd {
	int dev_type;
	int sc_num;

	struct list_head sc_list;
	struct apusys_dev_aquire acq;
};

struct pack_cmd_mgr {
	int ready_num;
	struct list_head pc_list;
};

//----------------------------------------------
static struct pack_cmd_mgr g_pack_mgr;
static struct task_struct *sched_task;

//----------------------------------------------
#ifdef CONFIG_PM_WAKELOCKS
static struct wakeup_source *apusys_sched_ws;
static uint32_t ws_count;
static struct mutex ws_mutex;
#endif

static void sched_ws_init(void)
{
#ifdef CONFIG_PM_WAKELOCKS
	ws_count = 0;
	mutex_init(&ws_mutex);
	apusys_sched_ws = wakeup_source_register("apusys_sched");
	if (!apusys_sched_ws)
		LOG_ERR("apusys sched wakelock register fail!\n");
#else
	LOG_DEBUG("not support pm wakelock\n");
#endif
}

static void sched_ws_lock(void)
{
#ifdef CONFIG_PM_WAKELOCKS
	mutex_lock(&ws_mutex);
	//LOG_INFO("wakelock count(%d)\n", ws_count);
	if (apusys_sched_ws && !ws_count) {
		LOG_DEBUG("lock wakelock\n");
		__pm_stay_awake(apusys_sched_ws);
	}
	ws_count++;
	mutex_unlock(&ws_mutex);
#else
	LOG_DEBUG("not support pm wakelock\n");
#endif
}

static void sched_ws_unlock(void)
{
#ifdef CONFIG_PM_WAKELOCKS
	mutex_lock(&ws_mutex);
	//LOG_INFO("wakelock count(%d)\n", ws_count);
	ws_count--;
	if (apusys_sched_ws && !ws_count) {
		LOG_DEBUG("unlock wakelock\n");
		__pm_relax(apusys_sched_ws);
	}
	mutex_unlock(&ws_mutex);
#else
	LOG_DEBUG("not support pm wakelock\n");
#endif
}
struct mem_ctx_mgr {
	struct mutex mtx;
	unsigned long ctx[BITS_TO_LONGS(32)];
};

static struct mem_ctx_mgr g_ctx_mgr;

static int mem_alloc_ctx(void)
{
	int ctx = -1;
	uint32_t request_size = 0x100000;
	uint8_t force = 0;
	unsigned long ctxid = 0;
	uint32_t sys_mem_size = 0;
	int ret = 0;

#if 0

	mutex_lock(&g_ctx_mgr.mtx);
	ctx = find_first_zero_bit(g_ctx_mgr.ctx, 32);
	if (ctx >= 32)
		ctx = -1;
	else
		bitmap_set(g_ctx_mgr.ctx, ctx, 1);

	mutex_unlock(&g_ctx_mgr.mtx);
#else
	ret = reviser_get_vlm(request_size, force, &ctxid, &sys_mem_size);
	if (!ret) {
		LOG_INFO("request(0x%x) force(%u) ctxid(%lu) mem_size(0x%x)\n",
				request_size, force, ctxid, sys_mem_size);
		ctx = ctxid;
	}



#endif


	return ctx;
}

static int mem_free_ctx(int ctx)
{
	int ret = 0;

	if (ctx == VALUE_SUBGRAPH_CTX_ID_NONE)
		return 0;
#if 0
	mutex_lock(&g_ctx_mgr.mtx);
	if (!test_bit(ctx, g_ctx_mgr.ctx))
		LOG_ERR("ctx id confuse, idx(%d) is not set\n", ctx);
	bitmap_clear(g_ctx_mgr.ctx, ctx, 1);
	mutex_unlock(&g_ctx_mgr.mtx);
#else
	ret = reviser_free_vlm((unsigned long)ctx);
	if (!ret)
		LOG_INFO("ctxid(%d)\n", ctx);
#endif
	return 0;
}

//----------------------------------------------
static int alloc_ctx(struct apusys_subcmd *sc, struct apusys_cmd *cmd)
{
	int ctx_id = -1;

	if (sc->c_hdr->mem_ctx == VALUE_SUBGRAPH_CTX_ID_NONE) {
		ctx_id = mem_alloc_ctx();
		if (ctx_id < 0)
			return -EINVAL;
		sc->ctx_id = ctx_id;
		LOG_DEBUG("0x%llx-#%d sc ctx_group(0x%x) ctx(%d)\n",
			cmd->cmd_id, sc->idx,
			sc->c_hdr->mem_ctx, sc->ctx_id);
	} else {
		if (cmd->ctx_list[sc->c_hdr->mem_ctx] ==
			VALUE_SUBGRAPH_CTX_ID_NONE) {
			ctx_id = mem_alloc_ctx();
			if (ctx_id < 0)
				return -EINVAL;

			cmd->ctx_list[sc->c_hdr->mem_ctx] = ctx_id;
			LOG_DEBUG("0x%llx-#%d sc ctx_group(0x%x) ctx(%d)\n",
				cmd->cmd_id, sc->idx,
				sc->c_hdr->mem_ctx,
				cmd->ctx_list[sc->c_hdr->mem_ctx]);
		}
		sc->ctx_id = cmd->ctx_list[sc->c_hdr->mem_ctx];
	}

	LOG_DEBUG("0x%llx-#%d sc ctx_group(0x%x) id(%d)\n",
		cmd->cmd_id, sc->idx,
		sc->c_hdr->mem_ctx, sc->ctx_id);

	return 0;
}

static int free_ctx(struct apusys_subcmd *sc, struct apusys_cmd *cmd)
{
	int ret = 0;

	if (sc->c_hdr->mem_ctx == VALUE_SUBGRAPH_CTX_ID_NONE) {
		LOG_DEBUG("0x%llx-#%d sc ctx_group(0x%x) free id(%d)\n",
			cmd->cmd_id, sc->idx,
			sc->c_hdr->mem_ctx, sc->ctx_id);
		mem_free_ctx(sc->ctx_id);
		sc->ctx_id = VALUE_SUBGRAPH_CTX_ID_NONE;
	} else {
		cmd->ctx_ref[sc->c_hdr->mem_ctx]--;
		if (cmd->ctx_ref[sc->c_hdr->mem_ctx] == 0) {
			LOG_DEBUG("0x%llx-#%d sc ctx_group(0x%x) free id(%d)\n",
				cmd->cmd_id, sc->idx,
				sc->c_hdr->mem_ctx, sc->ctx_id);
			mem_free_ctx(cmd->ctx_list[sc->c_hdr->mem_ctx]);
		}
		sc->ctx_id = VALUE_SUBGRAPH_CTX_ID_NONE;
	}

	return ret;
}

static int insert_pack_cmd(struct apusys_subcmd *sc, struct pack_cmd **ipc)
{
	struct pack_cmd *pc = NULL;
	struct apusys_subcmd *tmp_sc = NULL;
	struct list_head *tmp = NULL, *list_ptr = NULL;
	int next_idx = 0, ret = 0, bit0 = 0, bit1 = 0;
	unsigned int pack_idx = VALUE_SUBGRAPH_PACK_ID_NONE;

	if (sc == NULL)
		return -EINVAL;

	pack_idx = get_pack_idx(sc);

	LOG_DEBUG("0x%llx-#%d sc: before pack(%u)(%d/%d)\n",
		sc->par_cmd->cmd_id,
		sc->idx,
		pack_idx,
		test_bit(sc->idx, sc->par_cmd->pc_col.pack_status),
		test_bit(pack_idx, sc->par_cmd->pc_col.pack_status));

	list_add_tail(&sc->pc_list, &sc->par_cmd->pc_col.sc_list);

	/* packed cmd collect done */
	bit0 = test_and_change_bit(sc->idx, sc->par_cmd->pc_col.pack_status);
	bit1 = test_and_change_bit(pack_idx, sc->par_cmd->pc_col.pack_status);
	if (bit0 && bit1) {
		LOG_INFO("pack cmd satified(0x%llx/0x%llx)(%d/%d)\n",
			sc->par_cmd->hdr->uid,
			sc->par_cmd->cmd_id,
			sc->idx,
			pack_idx);
		pc = kzalloc(sizeof(struct pack_cmd), GFP_KERNEL);
		if (pc == NULL) {
			LOG_ERR("alloc packcmd(0x%llx/%d) for execute fail\n",
				sc->par_cmd->cmd_id, sc->idx);
			/* TODO, error handling for cmd list added already */
			return -ENOMEM;
		}

		INIT_LIST_HEAD(&pc->acq.dev_info_list);
		INIT_LIST_HEAD(&pc->sc_list);
		pc->dev_type = sc->type;

		/* find all packed subcmd id */
		/* TODO, guarntee can find all packed cmd*/
		/* now just query one round, user must set in order */
		next_idx = pack_idx;
		list_for_each_safe(list_ptr, tmp,
			&sc->par_cmd->pc_col.sc_list) {
			tmp_sc = list_entry(list_ptr,
				struct apusys_subcmd, pc_list);
			LOG_DEBUG("find pack idx(%d/%d)\n",
				next_idx, tmp_sc->idx);
			if (tmp_sc->idx == next_idx) {
				next_idx = get_pack_idx(tmp_sc);
				list_del(&tmp_sc->pc_list);
				list_add_tail(&tmp_sc->pc_list, &pc->sc_list);
				tmp_sc->state = CMD_STATE_RUN;
				pc->sc_num++;
			}
		}

		/* TODO: don't show fake error msg */
		if (tmp_sc->idx != pack_idx) {
			LOG_DEBUG("pack idx, (%d->%d)\n",
				tmp_sc->idx, pack_idx);
		}

		/* return pack cmd after all packed sc ready */
		*ipc = pc;
		ret = pc->sc_num;
	}

	LOG_DEBUG("0x%llx-#%d sc: after pack(%u)(%d/%d)\n",
		sc->par_cmd->cmd_id,
		sc->idx,
		pack_idx,
		test_bit(sc->idx, sc->par_cmd->pc_col.pack_status),
		test_bit(pack_idx, sc->par_cmd->pc_col.pack_status));

	return ret;
}

static int exec_pack_cmd(void *iacq)
{
	struct apusys_dev_aquire *acq = NULL;
	struct pack_cmd *pc = NULL;
	struct apusys_subcmd *sc = NULL;
	struct apusys_dev_info *info = NULL;
	struct list_head *tmp = NULL, *list_ptr = NULL;
	int count = 0;

	acq = (struct apusys_dev_aquire *)iacq;
	if (acq == NULL)
		return -EINVAL;

	pc = (struct pack_cmd *)acq->user;
	if (pc == NULL)
		return -EINVAL;

	list_for_each_safe(list_ptr, tmp, &acq->dev_info_list) {
		info = list_entry(list_ptr, struct apusys_dev_info, acq_list);
		list_del(&info->acq_list);
		if (list_empty(&pc->sc_list)) {
			LOG_ERR("pack cmd and device(%d) is not same number!\n",
				info->dev->dev_type);
			if (put_apusys_device(info)) {
				LOG_ERR("put device(%d/%d) fail\n",
					info->dev->dev_type, info->dev->idx);
			}
		} else {
			sc = (struct apusys_subcmd *)list_first_entry
				(&pc->sc_list, struct apusys_subcmd, pc_list);
			list_del(&sc->pc_list);
			count++;
			info->cmd_id = sc->par_cmd->cmd_id;
			info->sc_idx = sc->idx;
			sc->state = CMD_STATE_RUN;
			/* mark device execute deadline task */
			if (sc->par_cmd->hdr->soft_limit)
				info->is_deadline = 1;
			/* trigger device by thread pool */
			if (thread_pool_trigger(sc, info)) {
				LOG_ERR("tp cmd(0x%llx/%d)dev(%d/%d) fail\n",
					sc->par_cmd->cmd_id, sc->idx,
					info->dev->dev_type, info->dev->idx);
			}
		}
	}

	if (count != pc->sc_num) {
		LOG_ERR("execute pack cmd(%d/%d) number issue\n",
		count, pc->sc_num);
	}

	/* destroy pack_cmd and dev_acquire */
	kfree(pc);

	return 0;
}

static int clear_pack_cmd(struct apusys_cmd *cmd)
{
	//struct list_head *tmp = NULL, *list_ptr = NULL;
	//struct pack_cmd *pc = NULL;

	if (cmd == NULL)
		return -EINVAL;

	LOG_INFO("0x%llx cmd clear pack list\n",
		cmd->cmd_id);

	//list_del(&tmp_sc->pc_list);
	//list_add_tail(&tmp_sc->pc_list, &pc->sc_list);

	bitmap_clear(cmd->pc_col.pack_status, 0, cmd->hdr->num_sc);
	INIT_LIST_HEAD(&cmd->pc_col.sc_list);

	return 0;
}

static void subcmd_done(void *isc)
{
	struct apusys_subcmd *sc = NULL;
	struct apusys_subcmd *scr = NULL;
	struct apusys_cmd *cmd = NULL;
	int ret = 0, done_idx = 0, state = 0, i = 0;
	struct apusys_res_mgr *res_mgr = res_get_mgr();

	sc = (struct apusys_subcmd *)isc;
	if (sc == NULL) {
		LOG_ERR("invalid sc(%p)\n", sc);
		return;
	}

	cmd = sc->par_cmd;
	if (cmd == NULL) {
		LOG_ERR("invalid cmd(%p)\n", cmd);
		return;
	}

	mutex_lock(&cmd->mtx);

	/* check sc state and delete */
	mutex_lock(&sc->mtx);
	if (free_ctx(sc, cmd))
		LOG_ERR("free memory ctx id fail\n");
	done_idx = sc->idx;
	sc->state = CMD_STATE_DONE;
	mutex_unlock(&sc->mtx);

	mutex_lock(&cmd->sc_mtx);
	/* should insert subcmd which dependency satisfied */
	for (i = 0; i < sc->scr_num; i++) {
		/* decreate pdr count of sc */
		decrease_pdr_cnt(cmd, sc->scr_list[i]);

		if (check_sc_ready(cmd, sc->scr_list[i]) != 0)
			continue;

		scr = cmd->sc_list[sc->scr_list[i]];
		if (scr != NULL) {
			LOG_DEBUG("0x%llx-#%d sc: dp satified, ins q(%d-#%d)\n",
				cmd->cmd_id,
				scr->idx,
				scr->type,
				cmd->hdr->priority);

			mutex_lock(&res_mgr->mtx);
			mutex_lock(&scr->mtx);
			ret = insert_subcmd(scr);
			if (ret) {
				LOG_ERR("ins 0x%llx-#%d sc to q(%d-#%d) fail\n",
					cmd->cmd_id,
					scr->idx,
					scr->type,
					cmd->hdr->priority);
			}
			mutex_unlock(&scr->mtx);
			mutex_unlock(&res_mgr->mtx);
		}
	}

	if (apusys_subcmd_delete(sc)) {
		LOG_ERR("delete sc(0x%llx/%d) fail\n",
			cmd->cmd_id,
			sc->idx);
	}

	mutex_unlock(&cmd->sc_mtx);

	/* clear subcmd bit in cmd entry's status */
	if (check_cmd_done(cmd) == 0)
		state = cmd->state;

	/* if whole apusys cmd done, wakeup user context thread */
	if (state == CMD_STATE_DONE) {
		LOG_DEBUG("apusys cmd(0x%llx) done\n",
			cmd->cmd_id);
		LOG_DEBUG("wakeup user context thread\n");
		complete(&cmd->comp);
	}

	mutex_unlock(&cmd->mtx);
}

static int exec_cmd_func(void *isc, void *idev_info)
{
	struct apusys_dev_info *dev_info = (struct apusys_dev_info *)idev_info;
	struct apusys_cmd_hnd cmd_hnd;
	struct apusys_subcmd *sc = (struct apusys_subcmd *)isc;
	int i = 0, ret = 0;

	if (isc == NULL || idev_info == NULL) {
		ret = -EINVAL;
		goto out;
	}

	memset(&cmd_hnd, 0, sizeof(cmd_hnd));

	/* get subcmd information */
	cmd_hnd.kva = (uint64_t)sc->codebuf;
	cmd_hnd.size = sc->c_hdr->cb_info_size;
	cmd_hnd.boost_val = deadline_task_boost(sc);
	cmd_hnd.cmd_id = sc->par_cmd->cmd_id;
	cmd_hnd.subcmd_idx = sc->idx;
	cmd_hnd.priority = sc->par_cmd->hdr->priority;
	cmd_hnd.cmd_entry = (uint64_t)sc->par_cmd->hdr;
	if (cmd_hnd.kva == 0 || cmd_hnd.size == 0) {
		LOG_ERR("invalid sc(%d)(0x%llx/%d)\n",
			i, cmd_hnd.kva, cmd_hnd.size);
		ret = -EINVAL;
		goto out;
	}

	/* fill specific subcmd info */
	if (sc->type == APUSYS_DEVICE_MDLA) {
		if (parse_mdla_sg(sc, &cmd_hnd))
			LOG_ERR("fill mdla specific info fail\n");
	}

	/* call execute */
	midware_trace_begin("apusys_scheduler|dev: %d_%d, cmd_id: 0x%08llx",
			dev_info->dev->dev_type,
			dev_info->dev->idx,
			sc->par_cmd->cmd_id);

	LOG_DEBUG("0x%llx-#%d sc: exec hnd(%d/0x%llx/%d)\n",
		sc->par_cmd->cmd_id,
		sc->idx,
		sc->type,
		cmd_hnd.kva,
		cmd_hnd.size);

	/* 1. allocate memory ctx id*/
	mutex_lock(&sc->par_cmd->mtx);
	if (alloc_ctx(sc, sc->par_cmd))
		LOG_ERR("allocate memory ctx id(%d) fail\n", sc->ctx_id);
	mutex_unlock(&sc->par_cmd->mtx);

	mutex_lock(&sc->mtx);

	/* execute reviser to switch VLM */
	reviser_set_context(dev_info->dev->dev_type,
			dev_info->dev->idx, sc->ctx_id);

#ifdef APUSYS_OPTIONS_INF_MNOC
	/* 2. start count cmd qos */
	LOG_DEBUG("mnoc: cmd qos start 0x%llx-#%d dev(%d-#%d)\n",
		sc->par_cmd->cmd_id, sc->idx,
		sc->type, dev_info->dev->idx);

	/* count qos start */
	if (apu_cmd_qos_start(sc->par_cmd->cmd_id, sc->idx,
		sc->type, dev_info->dev->idx)) {
		LOG_DEBUG("start qos for 0x%llx-#%d sc fail\n",
			sc->par_cmd->cmd_id, sc->idx);
	}
#endif

	/* 3. get driver time start */
	LOG_INFO("exec 0x%llx/0x%llx-#%d(%d) sc: dev(%d-#%d) boost(%u)\n",
		sc->par_cmd->hdr->uid,
		sc->par_cmd->cmd_id,
		sc->idx, sc->type,
		dev_info->dev->dev_type,
		dev_info->dev->idx,
		cmd_hnd.boost_val);

	get_time_from_system(&sc->duration);

	/* 4. execute subcmd */
	ret = dev_info->dev->send_cmd(APUSYS_CMD_EXECUTE,
		(void *)&cmd_hnd, dev_info->dev);
	if (ret) {
		LOG_ERR("exec 0x%llx/0x%llx-%d sc on dev(%d-#%d) fail(%d)\n",
			sc->par_cmd->hdr->uid,
			sc->par_cmd->cmd_id,
			sc->idx,
			dev_info->dev->dev_type,
			dev_info->dev->idx,
			ret);

		sc->par_cmd->cmd_ret = ret;
	} else {
		LOG_INFO("exec 0x%llx/0x%llx-#%d(%d) sc: dev(%d-#%d) done\n",
			sc->par_cmd->hdr->uid,
			sc->par_cmd->cmd_id,
			sc->idx,
			sc->type,
			dev_info->dev->dev_type,
			dev_info->dev->idx);
	}

	/* 5. get driver time and ip time */
	get_time_from_system(&sc->duration);
	sc->ip_time = cmd_hnd.ip_time;

#ifdef APUSYS_OPTIONS_INF_MNOC
	/* 6. count qos end */
	sc->bw = apu_cmd_qos_end(sc->par_cmd->cmd_id, sc->idx);
	LOG_DEBUG("mnoc: cmd qos end 0x%llx-#%d dev(%d/%d) bw(%d)\n",
		sc->par_cmd->cmd_id, dev_info->dev->idx, sc->type,
		dev_info->dev->idx, sc->bw);
#endif

	mutex_unlock(&sc->mtx);

	/* 8. put device back */
	if (put_device_lock(dev_info)) {
		LOG_ERR("return dev(%d-#%d) fail\n",
			dev_info->dev->dev_type,
			dev_info->dev->idx);
		ret = -EINVAL;
		goto out;
	}

out:
	midware_trace_end("apusys_scheduler|dev: %d_%d, cmd_id: 0x%08llx, ret:%d",
			dev_info->dev->dev_type,
			dev_info->dev->idx,
			sc->par_cmd->cmd_id, ret);

	subcmd_done(sc);

	return ret;
}

int sched_routine(void *arg)
{
	int ret = 0, type = 0, dev_num = 0;
	unsigned long available[BITS_TO_LONGS(APUSYS_DEV_TABLE_MAX)];
	struct apusys_subcmd *sc = NULL;
	struct apusys_res_mgr *res_mgr = NULL;
	struct apusys_dev_aquire acq, *acq_async = NULL;
	struct apusys_device *dev = NULL;
	struct list_head *tmp = NULL, *list_ptr = NULL;
	struct apusys_dev_info *info = NULL;
	struct pack_cmd *pc = NULL;

	res_mgr = (struct apusys_res_mgr *)arg;

	while (!kthread_should_stop() && !res_mgr->sched_stop) {
		ret = wait_for_completion_interruptible(&res_mgr->sched_comp);
		if (ret)
			LOG_WARN("sched thread(%d)\n", ret);

		if (res_mgr->sched_pause != 0) {
			LOG_DEBUG("sched pause(%d)\n",
				res_mgr->sched_pause);
			continue;
		}

		memset(&available, 0, sizeof(available));
		bitmap_and(available, res_mgr->cmd_exist,
			res_mgr->dev_exist, APUSYS_DEV_TABLE_MAX);
		/* if dev/cmd available or */
		while (!bitmap_empty(available, APUSYS_DEV_TABLE_MAX)
			|| acq_device_check(&acq_async) == 0) {

			mutex_lock(&res_mgr->mtx);

			/* check any device acq ready for packcmd */
			if (acq_async != NULL) {
				LOG_DEBUG("get pack cmd(%d) ready",
					acq_async->dev_type);
				/* exec packcmd */
				if (exec_pack_cmd(acq_async))
					LOG_ERR("execute pack cmd fail\n");

				goto sched_retrigger;
			}

			/* cmd/dev not same bit available, continue */
			if (bitmap_empty(available, APUSYS_DEV_TABLE_MAX))
				goto sched_retrigger;

			/* get type from available */
			type = find_first_bit(available, APUSYS_DEV_TABLE_MAX);
			if (type >= APUSYS_DEV_TABLE_MAX) {
				LOG_WARN("find first bit for type(%d) fail\n",
					type);
				goto sched_retrigger;
			}

			/* pop cmd from priority queue */
			ret = pop_subcmd(type, &sc);
			if (ret) {
				LOG_ERR("pop subcmd for dev(%d) fail\n", type);
				goto sched_retrigger;
			}

			/* if sc is packed, collect all packed cmd and send */
			if (get_pack_idx(sc) != VALUE_SUBGRAPH_PACK_ID_NONE) {
				if (insert_pack_cmd(sc, &pc) <= 0)
					goto sched_retrigger;

				LOG_DEBUG("packcmd done, insert acq(%d/%d)\n",
					pc->dev_type,
					pc->sc_num);
				pc->acq.target_num = pc->sc_num;
				pc->acq.owner = APUSYS_DEV_OWNER_SCHEDULER;
				pc->acq.dev_type = pc->dev_type;
				pc->acq.user = (void *)pc;
				pc->acq.is_done = 0;
				ret = acq_device_async(&pc->acq);
				if (ret < 0) {
					LOG_ERR("acq dev(%d/%d) fail\n",
						pc->acq.dev_type,
						pc->acq.target_num);
				} else {
					if (pc->acq.acq_num ==
						pc->acq.target_num) {
						LOG_INFO("execute pack cmd\n");
						exec_pack_cmd(&pc->acq);
					}
				}
				goto sched_retrigger;
			}

			/* check queue empty for multicore */

			/* get device */
			dev_num = 1;
			memset(&acq, 0, sizeof(acq));
			acq.target_num = dev_num;
			acq.dev_type = type;
			acq.owner = APUSYS_DEV_OWNER_SCHEDULER;
			INIT_LIST_HEAD(&acq.dev_info_list);
			INIT_LIST_HEAD(&acq.tab_list);
			ret = acq_device_try(&acq);
			if (ret < 0 || acq.acq_num <= 0) {
				LOG_WARN("no dev(%d) available\n", type);
				mutex_lock(&sc->mtx);
				/* can't get device, insert sc back */
				if (insert_subcmd(sc)) {
					LOG_ERR("re 0x%llx-#%d sc q(%d-#%d)\n",
						sc->par_cmd->cmd_id,
						sc->idx,
						type,
						sc->par_cmd->hdr->priority);
				}
				mutex_unlock(&sc->mtx);
				goto sched_retrigger;
			}

			list_for_each_safe(list_ptr, tmp, &acq.dev_info_list) {
				info = list_entry(list_ptr,
					struct apusys_dev_info, acq_list);
				dev = info->dev;
				info->cmd_id = sc->par_cmd->cmd_id;
				info->sc_idx = sc->idx;
				mutex_lock(&sc->mtx);
				sc->state = CMD_STATE_RUN;
				mutex_unlock(&sc->mtx);
				/* mark device execute deadline task */
				if (sc->par_cmd->hdr->soft_limit)
					info->is_deadline = 1;
				/* trigger device by thread pool */
				ret = thread_pool_trigger(sc, info);
				if (ret)
					LOG_ERR("trigger thread pool fail\n");
			}
sched_retrigger:
			bitmap_and(available, res_mgr->cmd_exist,
				res_mgr->dev_exist, APUSYS_DEV_TABLE_MAX);
			acq_async = NULL;
			mutex_unlock(&res_mgr->mtx);
		}
	}

	LOG_WARN("scheduling thread stop\n");

	return 0;
}

int apusys_sched_del_cmd(struct apusys_cmd *cmd)
{
	int i = 0, ret = 0, times = 30, wait_ms = 200;
	struct apusys_subcmd *sc = NULL;
	struct apusys_res_mgr *res_mgr = res_get_mgr();

	if (cmd->state == CMD_STATE_DONE) {
		LOG_DEBUG("cmd done already\n");
		return 0;
	}

	LOG_WARN("abort cmd(0x%llx)\n",
		cmd->cmd_id);

	/* delete all subcmd in cmd */
	mutex_lock(&cmd->mtx);
	mutex_lock(&cmd->sc_mtx);
	if (clear_pack_cmd(cmd))
		LOG_WARN("clear pack cmd list fail\n");

	for (i = 0; i < cmd->hdr->num_sc; i++) {
		sc = cmd->sc_list[i];
		if (sc == NULL)
			continue;

		LOG_DEBUG("check 0x%llx-#%d sc status\n",
			cmd->cmd_id,
			sc->idx);

		mutex_lock(&res_mgr->mtx);
		mutex_lock(&sc->mtx);
		if (sc->state < CMD_STATE_RUN) {
			if (free_ctx(sc, cmd))
				LOG_ERR("free memory ctx id fail\n");

			if (sc->state == CMD_STATE_READY) {
				/* delete subcmd from q */
				if (delete_subcmd(sc)) {
					LOG_ERR(
					"delete 0x%llx-#%d from q fail\n",
					sc->par_cmd->cmd_id,
					sc->idx);
				} else {
					sc->state = CMD_STATE_DONE;
				}
			}

			mutex_unlock(&sc->mtx);
			mutex_unlock(&res_mgr->mtx);

			if (apusys_subcmd_delete(sc)) {
				LOG_ERR("delete 0x%llx-#%d sc fail\n",
					cmd->cmd_id, sc->idx);
			}


			LOG_DEBUG("delete 0x%llx-#%d sc\n",
					cmd->cmd_id, i);

		} else {
			LOG_DEBUG("0x%llx-#%d sc already execute(%d)\n",
				cmd->cmd_id,
				i,
				sc->state);
			mutex_unlock(&sc->mtx);
			mutex_unlock(&res_mgr->mtx);
		}
	}
	mutex_unlock(&cmd->sc_mtx);
	mutex_unlock(&cmd->mtx);

	LOG_DEBUG("wait 0x%llx cmd done...\n",
		cmd->cmd_id);

	/* final polling */
	for (i = 0; i < times; i++) {
		if (check_cmd_done(cmd) == 0) {
			LOG_INFO("delete cmd safely\n");
			break;
		}
		LOG_WARN("sleep 200ms to wait sc done\n");
		msleep(wait_ms);
	}

	if (i >= times) {
		LOG_ERR("cmd busy\n");
		ret = -EBUSY;
	}

	return ret;
}

int apusys_sched_wait_cmd(struct apusys_cmd *cmd)
{
	int ret = 0, state = -1;
	int retry = 20, retry_time = 50;
	unsigned long timeout = usecs_to_jiffies(APUSYS_PARAM_WAIT_TIMEOUT);

	if (cmd == NULL)
		return -EINVAL;

	/* wait all subcmd completed */
	mutex_lock(&cmd->sc_mtx);
	state = cmd->state;
	mutex_unlock(&cmd->sc_mtx);

	if (state == CMD_STATE_DONE)
		return ret;

	sched_ws_lock();

start:
	ret = wait_for_completion_interruptible_timeout(&cmd->comp, timeout);
	if (ret == -ERESTARTSYS) {
		LOG_WARN("user ctx interrupt(%d) cmd(0x%llx)\n",
			ret, cmd->cmd_id);
		if (retry) {
			LOG_INFO("retry cmd(0x%llx)(%d)...\n",
				cmd->cmd_id,
				retry);
			retry--;
			msleep(retry_time);
			goto start;
		}
	} else if (ret <= 0) {
		LOG_ERR("user ctx interrupt(%d) cmd(0x%llx)\n",
			ret, cmd->cmd_id);
		cmd->cmd_ret = ret;
	} else {
		ret = 0;
	}

	sched_ws_unlock();

	return ret;
}

int apusys_sched_add_cmd(struct apusys_cmd *cmd)
{
	int ret = 0, i = 0;
	struct apusys_subcmd *sc = NULL;
	uint32_t dp_ofs = 0;
	uint32_t dp_num = 0;

	if (cmd == NULL) {
		ret = -EINVAL;
		goto out;
	}

	/* 1. add all independent subcmd to corresponding queue */
	while (i < cmd->hdr->num_sc) {
		dp_num = *(uint32_t *)(cmd->dp_entry + dp_ofs);

		/* allocate subcmd struct */
		if (apusys_subcmd_create(i, cmd, &sc, dp_ofs)) {
			LOG_ERR("create sc for cmd(%d/%p) fail\n", i, cmd);
			ret = -EINVAL;
			break;
		}

		mutex_lock(&cmd->sc_mtx);

		/* add sc to cmd's sc_list*/
		if (check_sc_ready(cmd, i) == 0) {
			ret = insert_subcmd_lock(sc);
			if (ret) {
				LOG_ERR("ins 0x%llx-#%d sc(%p) q(%d-#%d)\n",
					cmd->cmd_id,
					sc->idx,
					sc,
					sc->type,
					cmd->hdr->priority);
				mutex_unlock(&cmd->sc_mtx);
				goto out;
			}
		}

		mutex_unlock(&cmd->sc_mtx);

		dp_ofs += (SIZE_CMD_PREDECCESSOR_CMNT_ELEMENT *
			(dp_num + 1));
		i++;
	}

out:
	return ret;
}

int apusys_sched_pause(void)
{
	struct apusys_res_mgr *res_mgr = res_get_mgr();

	if (res_mgr == NULL)
		return -EINVAL;

	/* pause scheduler */
	if (res_mgr->sched_pause == 0) {
		LOG_INFO("scheduler pause\n");
		res_mgr->sched_pause = 1;
	} else {
		LOG_WARN("scheduler already pause\n");
	}

	/* check all device free */
	res_suspend_dev();

	return 0;
}

int apusys_sched_restart(void)
{
	struct apusys_res_mgr *res_mgr = res_get_mgr();

	if (res_mgr == NULL)
		return -EINVAL;

	if (res_mgr->sched_pause == 1) {
		LOG_INFO("scheduler restart\n");
		res_mgr->sched_pause = 0;
	} else {
		LOG_WARN("scheduler already resume\n");
	}

	res_resume_dev();
	/* trigger sched thread */
	complete(&res_mgr->sched_comp);

	return 0;
}


//----------------------------------------------
/* init function */
int apusys_sched_init(void)
{
	int ret = 0;

	LOG_INFO("%s +\n", __func__);
	sched_ws_init();

	memset(&g_ctx_mgr, 0, sizeof(struct mem_ctx_mgr));
	mutex_init(&g_ctx_mgr.mtx);

	memset(&g_pack_mgr, 0, sizeof(struct pack_cmd_mgr));
	INIT_LIST_HEAD(&g_pack_mgr.pc_list);

	ret = thread_pool_init(exec_cmd_func);
	if (ret) {
		LOG_ERR("init thread pool fail(%d)\n", ret);
		return ret;
	}

	sched_task = kthread_run(sched_routine,
		(void *)res_get_mgr(), "apusys_sched");
	if (sched_task == NULL) {
		LOG_ERR("create kthread(sched) fail\n");
		return -ENOMEM;
	}

	LOG_INFO("%s done -\n", __func__);
	return 0;
}

int apusys_sched_destroy(void)
{
	int ret = 0;

	LOG_INFO("%s +\n", __func__);

	/* destroy thread pool */
	ret = thread_pool_destroy();
	if (ret)
		LOG_ERR("destroy thread pool fail(%d)\n", ret);

	LOG_INFO("%s done -\n", __func__);
	return 0;
}
