/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 MediaTek Inc.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM task_turbo

#if !defined(_TRACE_TASK_TURBO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_TASK_TURBO_H

#include <linux/sched.h>
#include <linux/types.h>
#include <linux/tracepoint.h>
#include <task_turbo.h>

TRACE_EVENT(turbo_set,
	TP_PROTO(struct task_struct *p),
	TP_ARGS(p),

	TP_STRUCT__entry(
		__field(pid_t, pid)
		__field(int, prio)
		__field(unsigned int, turbo)
	),
	TP_fast_assign(
		__entry->pid = p->pid;
		__entry->prio = p->prio;
		__entry->turbo = ((struct task_turbo_t *)&(p)->android_vendor_data1)->turbo;
	),
	TP_printk("pid=%d prio=%d turbo=%d",
		__entry->pid,
		__entry->prio,
		__entry->turbo)
);

TRACE_EVENT(turbo_inherit_failed,
	TP_PROTO(int turbo, unsigned int inherit_types,
		unsigned short inherit_cnt, int return_line),
	TP_ARGS(turbo, inherit_types, inherit_cnt, return_line),

	TP_STRUCT__entry(
		__field(int, turbo)
		__field(unsigned int, inherit_types)
		__field(unsigned short, inherit_cnt)
		__field(int, return_line)
	),
	TP_fast_assign(
		__entry->turbo = turbo;
		__entry->inherit_types = inherit_types;
		__entry->inherit_cnt = inherit_cnt;
		__entry->return_line = return_line;
	),
	TP_printk("turbo=%d inherit_types=%d inherit_cnt=%d line=%d",
			__entry->turbo,
			__entry->inherit_types,
			__entry->inherit_cnt,
			__entry->return_line)
);

TRACE_EVENT(turbo_inherit_start,
	TP_PROTO(struct task_struct *from, struct task_struct *to),
	TP_ARGS(from, to),

	TP_STRUCT__entry(
		__field(pid_t, fpid)
		__field(int, fprio)
		__field(unsigned int, f_turbo)
		__field(unsigned int, f_inherit_types)
		__field(int, tprio)
		__field(unsigned int, t_turbo)
		__field(unsigned int, t_inherit_types)
	),
	TP_fast_assign(
		__entry->fpid		 = from->pid;
		__entry->fprio		 = from->prio;
		__entry->f_turbo	 =
		((struct task_turbo_t *)&(from)->android_vendor_data1)->turbo;
		__entry->f_inherit_types =
		atomic_read(&((struct task_turbo_t *)&(from)->android_vendor_data1)->inherit_types);
		__entry->tprio		 = to->prio;
		__entry->t_turbo	 =
		((struct task_turbo_t *)&(to)->android_vendor_data1)->turbo;
		__entry->t_inherit_types =
		atomic_read(&((struct task_turbo_t *)&(to)->android_vendor_data1)->inherit_types);
	),
	TP_printk("pid=%d prio=%d turbo=%d inh=%d => prio=%d turbo=%d inh=%d",
		__entry->fpid,
		__entry->fprio,
		__entry->f_turbo,
		__entry->f_inherit_types,
		__entry->tprio,
		__entry->t_turbo,
		__entry->t_inherit_types)
);

TRACE_EVENT(turbo_inherit_end,
	TP_PROTO(struct task_struct *p),
	TP_ARGS(p),

	TP_STRUCT__entry(
		__field(pid_t, pid)
		__field(int, prio)
		__field(unsigned int, turbo)
		__field(unsigned int, inherit_types)
	),
	TP_fast_assign(
		__entry->pid		 = p->pid;
		__entry->prio		 = p->prio;
		__entry->turbo		 =
		((struct task_turbo_t *)&(p)->android_vendor_data1)->turbo;
		__entry->inherit_types	 =
		atomic_read(&((struct task_turbo_t *)&(p)->android_vendor_data1)->inherit_types);
	),
	TP_printk("pid=%d prio=%d turbo=%d inherit_types=%d",
		__entry->pid,
		__entry->prio,
		__entry->turbo,
		__entry->inherit_types)
);

TRACE_EVENT(sched_turbo_nice_set,
	TP_PROTO(struct task_struct *task, int old_prio, int new_prio),
	TP_ARGS(task, old_prio, new_prio),
	TP_STRUCT__entry(
		__array(char, comm, TASK_COMM_LEN)
		__field(int, pid)
		__field(int, old_prio)
		__field(int, new_prio)
	),

	TP_fast_assign(
		memcpy(__entry->comm, task->comm, TASK_COMM_LEN);
		__entry->pid	  = task->pid;
		__entry->old_prio = old_prio;
		__entry->new_prio = new_prio;
	),

	TP_printk("comm=%s pid=%d old_prio=%d new_prio=%d",
		__entry->comm,
		__entry->pid,
		__entry->old_prio,
		__entry->new_prio)
);

TRACE_EVENT(sched_set_user_nice,
	TP_PROTO(struct task_struct *task, int prio, int is_turbo),
	TP_ARGS(task, prio, is_turbo),
	TP_STRUCT__entry(
		__field(int, pid)
		__array(char, comm, TASK_COMM_LEN)
		__field(int, prio)
		__field(int, is_turbo)
	),

	TP_fast_assign(
		memcpy(__entry->comm, task->comm, TASK_COMM_LEN);
		__entry->pid	  = task->pid;
		__entry->prio	  = prio;
		__entry->is_turbo = is_turbo;
	),

	TP_printk("comm=%s pid=%d prio=%d is_turbo=%d",
		__entry->comm, __entry->pid, __entry->prio, __entry->is_turbo)
)

TRACE_EVENT(select_turbo_cpu,
	TP_PROTO(int target_cpu),
	TP_ARGS(target_cpu),
	TP_STRUCT__entry(
		__field(int, target_cpu)
	),

	TP_fast_assign(
		__entry->target_cpu = target_cpu;
	),

	TP_printk("target_cpu=%d",
		__entry->target_cpu)
);

#endif /*_TRACE_TASK_TURBO_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_task_turbo
/* This part must be outside protection */
#include <trace/define_trace.h>

