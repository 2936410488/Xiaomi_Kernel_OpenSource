// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 MediaTek Inc.
 */

#undef pr_fmt
#define pr_fmt(fmt) "Task-Turbo: " fmt

#include <linux/sched.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <uapi/linux/sched/types.h>
#include <linux/futex.h>
#include <linux/plist.h>
#include <linux/percpu-defs.h>

#include <trace/hooks/vendor_hooks.h>
#include <trace/hooks/sched.h>
#include <trace/hooks/dtask.h>
#include <trace/hooks/net.h>
#include <trace/hooks/binder.h>
#include <trace/hooks/rwsem.h>
#include <trace/hooks/futex.h>
#include <trace/hooks/fpsimd.h>
#include <trace/hooks/topology.h>
#include <trace/hooks/debug.h>
#include <trace/hooks/minidump.h>
#include <trace/hooks/wqlockup.h>
#include <trace/hooks/sysrqcrash.h>
#include <trace/hooks/cgroup.h>
#include <trace/hooks/sys.h>

#include <task_turbo.h>

#define CREATE_TRACE_POINTS
#include <trace_task_turbo.h>

LIST_HEAD(hmp_domains);

#define SCHED_FEAT(name, enabled)	\
	[__SCHED_FEAT_##name] = {0},

struct static_key sched_feat_keys[__SCHED_FEAT_NR] = {
#include "features.h"
};

#undef SCHED_FEAT

#define TOP_APP_GROUP_ID	4
#define TURBO_PID_COUNT		8
#define RENDER_THREAD_NAME	"RenderThread"
#define TURBO_ENABLE		1
#define TURBO_DISABLE		0
#define INHERIT_THRESHOLD	4
#define UTIL_AVG_UNCHANGED	0x1
#define RWSEM_ANONYMOUSLY_OWNED	(1UL << 0)
#define RWSEM_READER_OWNED	((struct task_struct *)RWSEM_ANONYMOUSLY_OWNED)
#define type_offset(type)		 (type * 4)
#define task_turbo_nice(nice) (nice == 0xbeef || nice == 0xbeee)
#define type_number(type)		 (1U << type_offset(type))
#define get_value_with_type(value, type)				\
	(value & ((unsigned int)(0x0000000f) << (type_offset(type))))
#define for_each_hmp_domain_L_first(hmpd) \
		list_for_each_entry_reverse(hmpd, &hmp_domains, hmp_domains)
#define hmp_cpu_domain(cpu)     (per_cpu(hmp_cpu_domain, (cpu)))
#define lsub_positive(_ptr, _val) do {			\
	typeof(_ptr) ptr = (_ptr);			\
	*ptr -= min_t(typeof(*ptr), *ptr, _val);	\
} while (0)

DEFINE_PER_CPU(struct hmp_domain *, hmp_cpu_domain);
DEFINE_PER_CPU(unsigned long, cpu_scale) = SCHED_CAPACITY_SCALE;

static uint32_t latency_turbo = SUB_FEAT_LOCK | SUB_FEAT_BINDER |
				SUB_FEAT_SCHED;
static uint32_t launch_turbo =  SUB_FEAT_LOCK | SUB_FEAT_BINDER |
				SUB_FEAT_SCHED | SUB_FEAT_FLAVOR_BIGCORE;
static DEFINE_MUTEX(TURBO_MUTEX_LOCK);
static pid_t turbo_pid[TURBO_PID_COUNT] = {0};
static unsigned int task_turbo_feats;

static bool is_turbo_task(struct task_struct *p);
static void set_load_weight_turbo(struct task_struct *p);
static void rwsem_stop_turbo_inherit(struct rw_semaphore *sem);
static void rwsem_list_add(struct task_struct *task, struct list_head *entry,
				struct list_head *head);
static bool binder_start_turbo_inherit(struct task_struct *from,
					struct task_struct *to);
static void binder_stop_turbo_inherit(struct task_struct *p);
static void rwsem_start_turbo_inherit(struct rw_semaphore *sem);
static bool sub_feat_enable(int type);
static bool start_turbo_inherit(struct task_struct *task, int type, int cnt);
static bool stop_turbo_inherit(struct task_struct *task, int type);
static inline bool should_set_inherit_turbo(struct task_struct *task);
static inline bool rwsem_owner_is_writer(struct task_struct *owner);
static inline void add_inherit_types(struct task_struct *task, int type);
static inline void sub_inherit_types(struct task_struct *task, int type);
static inline void set_scheduler_tuning(struct task_struct *task);
static inline void unset_scheduler_tuning(struct task_struct *task);
static bool is_inherit_turbo(struct task_struct *task, int type);
static bool test_turbo_cnt(struct task_struct *task);
static int select_turbo_cpu(struct task_struct *p);
static int find_best_turbo_cpu(struct task_struct *p);
static void get_fastest_cpus(struct cpumask *cpus);
static void init_hmp_domains(void);
static void hmp_cpu_mask_setup(void);
static int arch_get_nr_clusters(void);
static void arch_get_cluster_cpus(struct cpumask *cpus, int package_id);
static int hmp_compare(void *priv, struct list_head *a, struct list_head *b);
static inline void fillin_cluster(struct cluster_info *cinfo,
		struct hmp_domain *hmpd);
static void sys_set_turbo_task(struct task_struct *p);
static unsigned long capacity_of(int cpu);
static unsigned long cpu_runnable_load(struct rq *rq);
static unsigned long cpu_util_without(int cpu, struct task_struct *p);
static void init_turbo_attr(struct task_struct *p);
static inline unsigned long cfs_rq_runnable_load_avg(struct cfs_rq *cfs_rq);
static inline unsigned long cpu_util(int cpu);
static inline unsigned long task_util(struct task_struct *p);
static inline unsigned long _task_util_est(struct task_struct *p);

static void probe_android_rvh_prepare_prio_fork(void *ignore, struct task_struct *p)
{
	struct task_turbo_t *turbo_data;

	init_turbo_attr(p);
	if (unlikely(is_turbo_task(current))) {
		turbo_data = get_task_turbo_t(current);
		set_user_nice(p, turbo_data->nice_backup);
	}
}

static void probe_android_rvh_finish_prio_fork(void *ignore, struct task_struct *p)
{
	if (!dl_prio(p->prio) && !rt_prio(p->prio)) {
		struct task_turbo_t *turbo_data;

		turbo_data = get_task_turbo_t(p);
		/* prio and backup should be aligned */
		turbo_data->nice_backup = PRIO_TO_NICE(p->prio);
	}
}

static void probe_android_rvh_rtmutex_prepare_setprio(void *ignore, struct task_struct *p,
						struct task_struct *pi_task)
{
	/* if rt boost, recover prio with backup */
	if (unlikely(is_turbo_task(p))) {
		if (!dl_prio(p->prio) && !rt_prio(p->prio)) {
			struct task_turbo_t *turbo_data;
			int backup;

			turbo_data = get_task_turbo_t(p);
			backup = turbo_data->nice_backup;

			if (backup >= MIN_NICE && backup <= MAX_NICE) {
				p->static_prio = NICE_TO_PRIO(backup);
				p->prio = p->normal_prio = p->static_prio;
				set_load_weight_turbo(p);
			}
		}
	}
}

static void probe_android_rvh_set_user_nice(void *ignore, struct task_struct *p, long *nice)
{
	struct task_turbo_t *turbo_data;

	if ((*nice < MIN_NICE || *nice > MAX_NICE) && !task_turbo_nice(*nice))
		return;

	turbo_data = get_task_turbo_t(p);
	/* for general use, backup it */
	if (!task_turbo_nice(*nice))
		turbo_data->nice_backup = *nice;

	if (is_turbo_task(p)) {
		*nice = rlimit_to_nice(task_rlimit(p, RLIMIT_NICE));
		if (unlikely(*nice > MAX_NICE)) {
			pr_warning("{name:task-turbo&]pid=%d RLIMIT_NICE=%ld is not set\n",
				p->pid, *nice);
			*nice = turbo_data->nice_backup;
		}
	} else
		*nice = turbo_data->nice_backup;

	trace_sched_set_user_nice(p, *nice, is_turbo_task(p));
}

static void probe_android_rvh_setscheduler(void *ignore, struct task_struct *p)
{
	struct task_turbo_t *turbo_data;

	if (!dl_prio(p->prio) && !rt_prio(p->prio)) {
		turbo_data = get_task_turbo_t(p);
		turbo_data->nice_backup = PRIO_TO_NICE(p->prio);
	}
}

static void probe_android_vh_rwsem_write_finished(void *ignore, struct rw_semaphore *sem)
{
	rwsem_stop_turbo_inherit(sem);
}

static void probe_android_vh_rwsem_init(void *ignore, struct rw_semaphore *sem)
{
	sem->android_vendor_data1 = 0;
}

static void probe_android_vh_alter_rwsem_list_add(void *ignore, struct rwsem_waiter *waiter,
							struct rw_semaphore *sem,
							bool *already_on_list)
{
	rwsem_list_add(waiter->task, &waiter->list, &sem->wait_list);
	*already_on_list = true;
}

static void probe_android_vh_rwsem_wake(void *ignore, struct rw_semaphore *sem)
{
	rwsem_start_turbo_inherit(sem);
}

static void probe_android_vh_binder_transaction_init(void *ignore, struct binder_transaction *t)
{
	t->android_vendor_data1 = 0;
}

static void probe_android_vh_binder_set_priority(void *ignore, struct binder_transaction *t,
							struct task_struct *task)
{
	if (binder_start_turbo_inherit(t->from ?
			t->from->task : NULL, task)) {
		t->android_vendor_data1 = (s64)task;
	}
}

static void probe_android_vh_binder_restore_priority(void *ignore,
		struct binder_transaction *in_reply_to, struct task_struct *cur)
{
	struct task_struct *inherit_task;

	if (in_reply_to) {
		inherit_task = get_inherit_task(in_reply_to);
		if (cur && cur == inherit_task) {
			binder_stop_turbo_inherit(cur);
			in_reply_to->android_vendor_data1 = 0;
		}
	} else
		binder_stop_turbo_inherit(cur);
}

static void probe_android_vh_alter_futex_plist_add(void *ignore, struct plist_node *q_list,
						struct plist_head *hb_chain, bool *already_on_hb)
{
	struct futex_q *this, *next;
	struct plist_node *current_node = q_list;
	struct plist_node *this_node;

	if (!sub_feat_enable(SUB_FEAT_LOCK) &&
	    !is_turbo_task(current)) {
		*already_on_hb = false;
		return;
	}

	plist_for_each_entry_safe(this, next, hb_chain, list) {
		if ((!this->pi_state || !this->rt_waiter) && !is_turbo_task(this->task)) {
			this_node = &this->list;
			list_add(&current_node->node_list,
				 this_node->node_list.prev);
			*already_on_hb = true;
			return;
		}
	}

	*already_on_hb = false;
}

static void probe_android_rvh_select_task_rq_fair(void *ignore, struct task_struct *p,
							int prev_cpu, int sd_flag,
							int wake_flags, int *target_cpu)
{
	*target_cpu = select_turbo_cpu(p);
}

static unsigned long cpu_runnable_load(struct rq *rq)
{
	return cfs_rq_runnable_load_avg(&rq->cfs);
}

static inline unsigned long cfs_rq_runnable_load_avg(struct cfs_rq *cfs_rq)
{
	return cfs_rq->avg.runnable_load_avg;
}

static unsigned long capacity_of(int cpu)
{
	return cpu_rq(cpu)->cpu_capacity;
}

static unsigned long cpu_util_without(int cpu, struct task_struct *p)
{
	struct cfs_rq *cfs_rq;
	unsigned int util;

	/* Task has no contribution or is new */
	if (cpu != task_cpu(p) || !READ_ONCE(p->se.avg.last_update_time))
		return cpu_util(cpu);

	cfs_rq = &cpu_rq(cpu)->cfs;
	util = READ_ONCE(cfs_rq->avg.util_avg);

	/* Discount task's util from CPU's util */
	lsub_positive(&util, task_util(p));

	if (sched_feat(UTIL_EST)) {
		unsigned int estimated =
			READ_ONCE(cfs_rq->avg.util_est.enqueued);
		if (unlikely(task_on_rq_queued(p) || current == p))
			lsub_positive(&estimated, _task_util_est(p));

		util = max(util, estimated);
	}

	return min_t(unsigned long, util, capacity_orig_of(cpu));
}

static inline unsigned long cpu_util(int cpu)
{
	struct cfs_rq *cfs_rq;
	unsigned int util;

	cfs_rq = &cpu_rq(cpu)->cfs;
	util = READ_ONCE(cfs_rq->avg.util_avg);

	if (sched_feat(UTIL_EST))
		util = max(util, READ_ONCE(cfs_rq->avg.util_est.enqueued));

	return min_t(unsigned long, util, capacity_orig_of(cpu));
}

static inline unsigned long task_util(struct task_struct *p)
{
	return READ_ONCE(p->se.avg.util_avg);
}

static inline unsigned long _task_util_est(struct task_struct *p)
{
	struct util_est ue = READ_ONCE(p->se.avg.util_est);

	return (max(ue.ewma, ue.enqueued) | UTIL_AVG_UNCHANGED);
}

static void get_fastest_cpus(struct cpumask *cpus)
{
	struct list_head *pos;
	struct hmp_domain *domain;

	pos = hmp_domains.next;
	domain = list_entry(pos, struct hmp_domain, hmp_domains);
	cpumask_copy(cpus, &domain->possible_cpus);
}

int find_best_turbo_cpu(struct task_struct *p)
{
	int iter_cpu;
	int best_cpu = -1;
	const struct cpumask *tsk_cpus_ptr = p->cpus_ptr;
	struct cpumask fast_cpu_mask;
	unsigned long min_load = ULONG_MAX;

	cpumask_clear(&fast_cpu_mask);
	/* only choose big-core cluster */
	get_fastest_cpus(&fast_cpu_mask);
	//cpumask_and(&fast_cpu_mask, &fast_cpu_mask, tsk_cpus_ptr);
	cpumask_andnot(&fast_cpu_mask, tsk_cpus_ptr, &fast_cpu_mask);

	/* task is not allowed in big core */
	if (cpumask_empty(&fast_cpu_mask))
		return -1;

	for_each_cpu(iter_cpu, &fast_cpu_mask) {
		if (!cpu_online(iter_cpu))
			continue;

#if IS_ENABLED(CONFIG_MTK_SCHED_INTEROP)
		if (cpu_rq(iter_cpu)->rt.rt_nr_running &&
			likely(!is_rt_throttle(iter_cpu)))
			continue;
#endif
		/* If utils of fastest cpu is over 90% */
		if (capacity_of(iter_cpu) * 1024 < (cpu_util_without(iter_cpu, p) * 1138))
			continue;

		if (idle_cpu(iter_cpu)) {
			best_cpu = iter_cpu;
			break;
		}

		if (cpu_runnable_load(cpu_rq(iter_cpu)) < min_load) {
			min_load = cpu_runnable_load(cpu_rq(iter_cpu));
			best_cpu = iter_cpu;
		}
	}
	return best_cpu;
}

int select_turbo_cpu(struct task_struct *p)
{
	int target_cpu = -1;

	if (!is_turbo_task(p))
		return -1;

	target_cpu = find_best_turbo_cpu(p);
	trace_select_turbo_cpu(target_cpu);

	return target_cpu;
}

static void set_load_weight_turbo(struct task_struct *p)
{
	int prio = p->static_prio - MAX_RT_PRIO;
	struct load_weight *load = &p->se.load;

	/*
	 * SCHED_IDLE tasks get minimal weight:
	 */
	if (idle_policy(p->policy)) {
		load->weight = scale_load(WEIGHT_IDLEPRIO);
		load->inv_weight = WMULT_IDLEPRIO;
		p->se.runnable_weight = load->weight;
		return;
	}

	load->weight = scale_load(sched_prio_to_weight[prio]);
	load->inv_weight = sched_prio_to_wmult[prio];
	p->se.runnable_weight = load->weight;
}

int idle_cpu(int cpu)
{
	struct rq *rq = cpu_rq(cpu);

	if (rq->curr != rq->idle)
		return 0;

	if (rq->nr_running)
		return 0;

#if IS_ENABLED(CONFIG_SMP)
	if (!llist_empty(&rq->wake_list))
		return 0;
#endif

	return -1;
}

static void rwsem_stop_turbo_inherit(struct rw_semaphore *sem)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&sem->wait_lock, flags);
	if ((struct task_struct *)&(sem)->android_vendor_data1 == current) {
		stop_turbo_inherit(current, RWSEM_INHERIT);
		sem->android_vendor_data1 = 0;
		trace_turbo_inherit_end(current);
	}
	raw_spin_unlock_irqrestore(&sem->wait_lock, flags);
}

static void rwsem_list_add(struct task_struct *task,
			   struct list_head *entry,
			   struct list_head *head)
{
	if (!sub_feat_enable(SUB_FEAT_LOCK)) {
		list_add_tail(entry, head);
		return;
	}

	if (is_turbo_task(task)) {
		struct list_head *pos = NULL;
		struct list_head *n = NULL;
		struct rwsem_waiter *waiter = NULL;

		/* insert turbo task pior to first non-turbo task */
		list_for_each_safe(pos, n, head) {
			waiter = list_entry(pos,
					struct rwsem_waiter, list);
			if (!is_turbo_task(waiter->task)) {
				list_add(entry, waiter->list.prev);
				return;
			}
		}
	}
	list_add_tail(entry, head);
}

static void rwsem_start_turbo_inherit(struct rw_semaphore *sem)
{
	bool should_inherit;
	struct task_struct *owner, *turbo_owner;
	struct task_struct *cur = current;
	struct task_turbo_t *turbo_data;
	unsigned long read_owner;

	if (!sub_feat_enable(SUB_FEAT_LOCK))
		return;

	read_owner = atomic_long_read(&sem->owner);
	should_inherit = should_set_inherit_turbo(cur);
	if (should_inherit) {
		turbo_owner = get_inherit_task(sem);
		turbo_data = get_task_turbo_t(cur);
		owner = (struct task_struct *)read_owner;
		if (rwsem_owner_is_writer(owner) &&
				!is_turbo_task(owner) &&
				!turbo_owner) {
			start_turbo_inherit(owner,
					    RWSEM_INHERIT,
					    turbo_data->inherit_cnt);
			sem->android_vendor_data1 = read_owner;
			trace_turbo_inherit_start(cur, owner);
		}
	}
}

static inline bool rwsem_owner_is_writer(struct task_struct *owner)
{
	return owner && owner != RWSEM_READER_OWNED;
}

static bool start_turbo_inherit(struct task_struct *task,
				int type,
				int cnt)
{
	struct task_turbo_t *turbo_data;

	if (type <= START_INHERIT && type >= END_INHERIT)
		return false;

	add_inherit_types(task, type);
	turbo_data = get_task_turbo_t(task);
	if (turbo_data->inherit_cnt < cnt + 1)
		turbo_data->inherit_cnt = cnt + 1;

	/* scheduler tuning start */
	set_scheduler_tuning(task);
	return true;
}

static bool stop_turbo_inherit(struct task_struct *task,
				int type)
{
	unsigned int inherit_types;
	bool ret = false;
	struct task_turbo_t *turbo_data;

	if (type <= START_INHERIT && type >= END_INHERIT)
		goto done;

	turbo_data = get_task_turbo_t(task);
	inherit_types = atomic_read(&turbo_data->inherit_types);
	if (inherit_types == 0)
		goto done;

	sub_inherit_types(task, type);
	turbo_data = get_task_turbo_t(task);
	inherit_types = atomic_read(&turbo_data->inherit_types);
	if (inherit_types > 0)
		goto done;

	/* scheduler tuning stop */
	unset_scheduler_tuning(task);
	turbo_data->inherit_cnt = 0;
	ret = true;
done:
	return ret;
}

static inline void set_scheduler_tuning(struct task_struct *task)
{
	int cur_nice = task_nice(task);

	if (!fair_policy(task->policy))
		return;

	if (!sub_feat_enable(SUB_FEAT_SCHED))
		return;

	/* trigger renice for turbo task */
	set_user_nice(task, 0xbeef);

	trace_sched_turbo_nice_set(task, NICE_TO_PRIO(cur_nice), task->prio);
}

static inline void unset_scheduler_tuning(struct task_struct *task)
{
	int cur_prio = task->prio;

	if (!fair_policy(task->policy))
		return;

	set_user_nice(task, 0xbeee);

	trace_sched_turbo_nice_set(task, cur_prio, task->prio);
}

static inline void add_inherit_types(struct task_struct *task, int type)
{
	struct task_turbo_t *turbo_data;

	turbo_data = get_task_turbo_t(task);
	atomic_add(type_number(type), &turbo_data->inherit_types);
}

static inline void sub_inherit_types(struct task_struct *task, int type)
{
	struct task_turbo_t *turbo_data;

	turbo_data = get_task_turbo_t(task);
	atomic_sub(type_number(type), &turbo_data->inherit_types);
}

static bool binder_start_turbo_inherit(struct task_struct *from,
					struct task_struct *to)
{
	bool ret = false;
	struct task_turbo_t *from_turbo_data;

	if (!sub_feat_enable(SUB_FEAT_BINDER))
		goto done;

	if (!from || !to)
		goto done;

	if (!is_turbo_task(from) ||
		!test_turbo_cnt(from)) {
		from_turbo_data = get_task_turbo_t(from);
		trace_turbo_inherit_failed(from_turbo_data->turbo,
					atomic_read(&from_turbo_data->inherit_types),
					from_turbo_data->inherit_cnt, __LINE__);
		goto done;
	}

	if (!is_turbo_task(to)) {
		from_turbo_data = get_task_turbo_t(from);
		ret = start_turbo_inherit(to, BINDER_INHERIT,
					from_turbo_data->inherit_cnt);
		trace_turbo_inherit_start(from, to);
	}
done:
	return ret;
}

static void binder_stop_turbo_inherit(struct task_struct *p)
{
	if (is_inherit_turbo(p, BINDER_INHERIT))
		stop_turbo_inherit(p, BINDER_INHERIT);
	trace_turbo_inherit_end(p);
}

static bool is_inherit_turbo(struct task_struct *task, int type)
{
	unsigned int inherit_types;
	struct task_turbo_t *turbo_data;

	if (!task)
		return false;

	turbo_data = get_task_turbo_t(task);
	inherit_types = atomic_read(&turbo_data->inherit_types);

	if (inherit_types == 0)
		return false;

	return get_value_with_type(inherit_types, type) > 0;
}

static bool is_turbo_task(struct task_struct *p)
{
	struct task_turbo_t *turbo_data;

	if (!p)
		return false;

	turbo_data = get_task_turbo_t(p);
	return turbo_data->turbo || atomic_read(&turbo_data->inherit_types);
}

static bool test_turbo_cnt(struct task_struct *task)
{
	struct task_turbo_t *turbo_data;

	turbo_data = get_task_turbo_t(task);
	/* TODO:limit number should be discuss */
	return turbo_data->inherit_cnt < INHERIT_THRESHOLD;
}

static inline bool should_set_inherit_turbo(struct task_struct *task)
{
	return is_turbo_task(task) && test_turbo_cnt(task);
}

inline bool latency_turbo_enable(void)
{
	return task_turbo_feats == latency_turbo;
}

inline bool launch_turbo_enable(void)
{
	return task_turbo_feats == launch_turbo;
}

static void init_turbo_attr(struct task_struct *p)
{
	struct task_turbo_t *turbo_data = get_task_turbo_t(p);

	turbo_data->turbo = TURBO_DISABLE;
	turbo_data->render = 0;
	atomic_set(&(turbo_data->inherit_types), 0);
	turbo_data->inherit_cnt = 0;
}

int get_turbo_feats(void)
{
	return task_turbo_feats;
}

static bool sub_feat_enable(int type)
{
	return get_turbo_feats() & type;
}

/*
 * set task to turbo by pid
 */
static int set_turbo_task(int pid, int val)
{
	struct task_struct *p;
	struct task_turbo_t *turbo_data;
	int retval = 0;

	if (pid < 0 || pid > PID_MAX_DEFAULT)
		return -EINVAL;

	if (val < 0 || val > 1)
		return -EINVAL;

	rcu_read_lock();
	p = find_task_by_vpid(pid);

	if (p != NULL) {
		get_task_struct(p);
		turbo_data = get_task_turbo_t(p);
		turbo_data->turbo = val;
		/*TODO: scheduler tuning */
		if (turbo_data->turbo == TURBO_ENABLE)
			set_scheduler_tuning(p);
		else
			unset_scheduler_tuning(p);
		trace_turbo_set(p);
		put_task_struct(p);
	} else
		retval = -ESRCH;
	rcu_read_unlock();

	return retval;
}

static int unset_turbo_task(int pid)
{
	return set_turbo_task(pid, TURBO_DISABLE);
}

static int set_task_turbo_feats(const char *buf,
				const struct kernel_param *kp)
{
	int ret = 0, i;
	unsigned int val;

	ret = kstrtouint(buf, 0, &val);


	mutex_lock(&TURBO_MUTEX_LOCK);
	if (val == latency_turbo ||
	    val == launch_turbo  || val == 0)
		ret = param_set_uint(buf, kp);
	else
		ret = -EINVAL;

	/* if disable turbo, remove all turbo tasks */
	/* mutex_lock(&TURBO_MUTEX_LOCK); */
	if (val == 0) {
		for (i = 0; i < TURBO_PID_COUNT; i++) {
			if (turbo_pid[i]) {
				unset_turbo_task(turbo_pid[i]);
				turbo_pid[i] = 0;
			}
		}
	}
	mutex_unlock(&TURBO_MUTEX_LOCK);

	if (!ret)
		pr_info("task_turbo_feats is change to %d successfully",
				task_turbo_feats);
	return ret;
}

static struct kernel_param_ops task_turbo_feats_param_ops = {
	.set = set_task_turbo_feats,
	.get = param_get_uint,
};

param_check_uint(feats, &task_turbo_feats);
module_param_cb(feats, &task_turbo_feats_param_ops, &task_turbo_feats, 0644);
MODULE_PARM_DESC(feats, "enable task turbo features if needed");

static bool add_turbo_list_locked(pid_t pid);
static void remove_turbo_list_locked(pid_t pid);

/*
 * use pid set turbo task
 */
static int add_turbo_list_by_pid(pid_t pid)
{
	int retval = -EINVAL;

	if (!task_turbo_feats)
		return retval;

	if (pid < 0 || pid > PID_MAX_DEFAULT)
		return retval;

	mutex_lock(&TURBO_MUTEX_LOCK);
	if (!add_turbo_list_locked(pid))
		goto unlock;

	retval = set_turbo_task(pid, TURBO_ENABLE);
unlock:
	mutex_unlock(&TURBO_MUTEX_LOCK);
	return retval;
}

static pid_t turbo_pid_param;
static int set_turbo_task_param(const char *buf,
				const struct kernel_param *kp)
{
	int retval = 0;
	pid_t pid;

	retval = kstrtouint(buf, 0, &pid);

	if (!retval)
		retval = add_turbo_list_by_pid(pid);

	if (!retval)
		turbo_pid_param = pid;

	return retval;
}

static struct kernel_param_ops turbo_pid_param_ops = {
	.set = set_turbo_task_param,
	.get = param_get_int,
};

param_check_uint(turbo_pid, &turbo_pid_param);
module_param_cb(turbo_pid, &turbo_pid_param_ops, &turbo_pid_param, 0644);
MODULE_PARM_DESC(turbo_pid, "set turbo task by pid");

static int unset_turbo_list_by_pid(pid_t pid)
{
	int retval = -EINVAL;

	if (pid < 0 || pid > PID_MAX_DEFAULT)
		return retval;

	mutex_lock(&TURBO_MUTEX_LOCK);
	remove_turbo_list_locked(pid);
	retval = unset_turbo_task(pid);
	mutex_unlock(&TURBO_MUTEX_LOCK);
	return retval;
}

static pid_t unset_turbo_pid_param;
static int unset_turbo_task_param(const char *buf,
				  const struct kernel_param *kp)
{
	int retval = 0;
	pid_t pid;

	retval = kstrtouint(buf, 0, &pid);

	if (!retval)
		retval = unset_turbo_list_by_pid(pid);

	if (!retval)
		unset_turbo_pid_param = pid;

	return retval;
}

static struct kernel_param_ops unset_turbo_pid_param_ops = {
	.set = unset_turbo_task_param,
	.get = param_get_int,
};

param_check_uint(unset_turbo_pid, &unset_turbo_pid_param);
module_param_cb(unset_turbo_pid, &unset_turbo_pid_param_ops,
		&unset_turbo_pid_param, 0644);
MODULE_PARM_DESC(unset_turbo_pid, "unset turbo task by pid");

static inline int get_st_group_id(struct task_struct *task)
{
#if IS_ENABLED(CONFIG_CGROUP_SCHED)
	const int subsys_id = cpu_cgrp_id;
	struct cgroup *grp;

	rcu_read_lock();
	grp = task_cgroup(task, subsys_id);
	rcu_read_unlock();
	return grp->id;
#else
	return 0;
#endif
}

static inline bool cgroup_check_set_turbo(struct task_struct *p)
{
	struct task_turbo_t *turbo_data;

	turbo_data = get_task_turbo_t(p);

	if (!launch_turbo_enable())
		return false;

	if (turbo_data->turbo)
		return false;

	/* set critical tasks for UI or UX to turbo */
	return (turbo_data->render ||
	       (p == p->group_leader &&
		p->real_parent->pid != 1));
}

/*
 * record task to turbo list
 */
static bool add_turbo_list_locked(pid_t pid)
{
	int i, free_idx = -1;
	bool ret = false;

	if (unlikely(!get_turbo_feats()))
		goto done;

	for (i = 0; i < TURBO_PID_COUNT; i++) {
		if (free_idx < 0 && !turbo_pid[i])
			free_idx = i;

		if (unlikely(turbo_pid[i] == pid)) {
			free_idx = i;
			break;
		}
	}

	if (free_idx >= 0) {
		turbo_pid[free_idx] = pid;
		ret = true;
	}
done:
	return ret;
}

static void add_turbo_list(struct task_struct *p)
{
	struct task_turbo_t *turbo_data;

	mutex_lock(&TURBO_MUTEX_LOCK);
	if (add_turbo_list_locked(p->pid)) {
		turbo_data = get_task_turbo_t(p);
		turbo_data->turbo = TURBO_ENABLE;
		/* TODO: scheduler tuninng */
		set_scheduler_tuning(p);
		trace_turbo_set(p);
	}
	mutex_unlock(&TURBO_MUTEX_LOCK);
}

/*
 * remove task from turbo list
 */
static void remove_turbo_list_locked(pid_t pid)
{
	int i;

	for (i = 0; i < TURBO_PID_COUNT; i++) {
		if (turbo_pid[i] == pid) {
			turbo_pid[i] = 0;
			break;
		}
	}
}

static void remove_turbo_list(struct task_struct *p)
{
	struct task_turbo_t *turbo_data;

	mutex_lock(&TURBO_MUTEX_LOCK);
	turbo_data = get_task_turbo_t(p);
	remove_turbo_list_locked(p->pid);
	turbo_data->turbo = TURBO_DISABLE;
	unset_scheduler_tuning(p);
	trace_turbo_set(p);
	mutex_unlock(&TURBO_MUTEX_LOCK);
}

static void probe_android_vh_cgroup_set_task(void *ignore, int ret, struct task_struct *p)
{
	struct task_turbo_t *turbo_data;

	if (ret)
		return;

	if (get_st_group_id(p) == TOP_APP_GROUP_ID) {
		if (!cgroup_check_set_turbo(p))
			return;
		add_turbo_list(p);
	} else {
		turbo_data = get_task_turbo_t(p);
		if (turbo_data->turbo)
			remove_turbo_list(p);
	}
}

static void probe_android_vh_sys_set_task(void *ignore, struct task_struct *p)
{
	sys_set_turbo_task(p);
}

static void probe_android_vh_nice_check(void *ignore, long *nice, bool *allowed)
{
	if ((*nice < MIN_NICE || *nice > MAX_NICE) && !task_turbo_nice(*nice))
		*allowed = false;
	else
		*allowed = true;
}

static inline void fillin_cluster(struct cluster_info *cinfo,
		struct hmp_domain *hmpd)
{
	int cpu;
	unsigned long cpu_perf;

	cinfo->hmpd = hmpd;
	cinfo->cpu = cpumask_any(&cinfo->hmpd->possible_cpus);

	for_each_cpu(cpu, &hmpd->possible_cpus) {
		cpu_perf = arch_scale_cpu_capacity(cpu);
		if (cpu_perf > 0)
			break;
	}
	cinfo->cpu_perf = cpu_perf;

	if (cpu_perf == 0)
		pr_info("Uninitialized CPU performance (CPU mask: %lx)",
				cpumask_bits(&hmpd->possible_cpus)[0]);
}

int hmp_compare(void *priv, struct list_head *a, struct list_head *b)
{
	struct cluster_info ca;
	struct cluster_info cb;

	fillin_cluster(&ca, list_entry(a, struct hmp_domain, hmp_domains));
	fillin_cluster(&cb, list_entry(b, struct hmp_domain, hmp_domains));

	return (ca.cpu_perf > cb.cpu_perf) ? -1 : 1;
}

void init_hmp_domains(void)
{
	struct hmp_domain *domain;
	struct cpumask cpu_mask;
	int id, maxid;

	cpumask_clear(&cpu_mask);
	maxid = arch_get_nr_clusters();

	/*
	 * Initialize hmp_domains
	 * Must be ordered with respect to compute capacity.
	 * Fastest domain at head of list.
	 */
	for (id = 0; id < maxid; id++) {
		arch_get_cluster_cpus(&cpu_mask, id);
		domain = (struct hmp_domain *)
			kmalloc(sizeof(struct hmp_domain), GFP_KERNEL);
		if (domain) {
			cpumask_copy(&domain->possible_cpus, &cpu_mask);
			cpumask_and(&domain->cpus, cpu_online_mask,
				&domain->possible_cpus);
			list_add(&domain->hmp_domains, &hmp_domains);
		}
	}

	/*
	 * Sorting HMP domain by CPU capacity
	 */
	list_sort(NULL, &hmp_domains, &hmp_compare);
	pr_info("Sort hmp_domains from little to big:\n");
	for_each_hmp_domain_L_first(domain) {
		pr_info("    cpumask: 0x%02lx\n",
				*cpumask_bits(&domain->possible_cpus));
	}
	hmp_cpu_mask_setup();
}

void hmp_cpu_mask_setup(void)
{
	struct hmp_domain *domain;
	struct list_head *pos;
	int cpu;

	pr_info("Initializing HMP scheduler:\n");

	/* Initialize hmp_domains using platform code */
	if (list_empty(&hmp_domains)) {
		pr_info("HMP domain list is empty!\n");
		return;
	}

	/* Print hmp_domains */
	list_for_each(pos, &hmp_domains) {
		domain = list_entry(pos, struct hmp_domain, hmp_domains);

		for_each_cpu(cpu, &domain->possible_cpus)
			per_cpu(hmp_cpu_domain, cpu) = domain;
	}
	pr_info("Initializing HMP scheduler done\n");
}

int arch_get_nr_clusters(void)
{
	int __arch_nr_clusters = -1;
	int max_id = 0;
	unsigned int cpu;

	/* assume socket id is monotonic increasing without gap. */
	for_each_possible_cpu(cpu) {
		struct cpu_topology *cpu_topo = &cpu_topology[cpu];

		if (cpu_topo->package_id > max_id)
			max_id = cpu_topo->package_id;
	}
	__arch_nr_clusters = max_id + 1;
	return __arch_nr_clusters;
}

void arch_get_cluster_cpus(struct cpumask *cpus, int package_id)
{
	unsigned int cpu;

	cpumask_clear(cpus);
	for_each_possible_cpu(cpu) {
		struct cpu_topology *cpu_topo = &cpu_topology[cpu];

		if (cpu_topo->package_id == package_id)
			cpumask_set_cpu(cpu, cpus);
	}
}

static void sys_set_turbo_task(struct task_struct *p)
{
	struct task_turbo_t *turbo_data;

	if (strcmp(p->comm, RENDER_THREAD_NAME))
		return;

	if (!launch_turbo_enable())
		return;

	if (get_st_group_id(p) != TOP_APP_GROUP_ID)
		return;

	turbo_data = get_task_turbo_t(p);
	turbo_data->render = 1;
	add_turbo_list(p);
}

static int __init init_task_turbo(void)
{
	int ret, ret_erri_line;

	ret = register_trace_android_rvh_rtmutex_prepare_setprio(
			probe_android_rvh_rtmutex_prepare_setprio, NULL);
	if (ret) {
		ret_erri_line = __LINE__;
		goto failed;
	}

	ret = register_trace_android_rvh_prepare_prio_fork(
			probe_android_rvh_prepare_prio_fork, NULL);
	if (ret) {
		ret_erri_line = __LINE__;
		goto failed;
	}

	ret = register_trace_android_rvh_finish_prio_fork(
			probe_android_rvh_finish_prio_fork, NULL);
	if (ret) {
		ret_erri_line = __LINE__;
		goto failed;
	}

	ret = register_trace_android_rvh_set_user_nice(
			probe_android_rvh_set_user_nice, NULL);
	if (ret) {
		ret_erri_line = __LINE__;
		goto failed;
	}

	ret = register_trace_android_rvh_setscheduler(
			probe_android_rvh_setscheduler, NULL);
	if (ret) {
		ret_erri_line = __LINE__;
	goto failed;
	}

	ret = register_trace_android_vh_binder_transaction_init(
			probe_android_vh_binder_transaction_init, NULL);
	if (ret) {
		ret_erri_line = __LINE__;
		goto failed;
	}

	ret = register_trace_android_vh_binder_set_priority(
			probe_android_vh_binder_set_priority, NULL);
	if (ret) {
		ret_erri_line = __LINE__;
	goto failed;
	}

	ret = register_trace_android_vh_binder_restore_priority(
			probe_android_vh_binder_restore_priority, NULL);
	if (ret) {
		ret_erri_line = __LINE__;
		goto failed;
	}

	ret = register_trace_android_vh_rwsem_init(
			probe_android_vh_rwsem_init, NULL);
	if (ret) {
		ret_erri_line = __LINE__;
		goto failed;
	}

	ret = register_trace_android_vh_rwsem_wake(
			probe_android_vh_rwsem_wake, NULL);
	if (ret) {
		ret_erri_line = __LINE__;
		goto failed;
	}

	ret = register_trace_android_vh_rwsem_write_finished(
			probe_android_vh_rwsem_write_finished, NULL);
	if (ret) {
		ret_erri_line = __LINE__;
		goto failed;
	}

	ret = register_trace_android_vh_alter_rwsem_list_add(
			probe_android_vh_alter_rwsem_list_add, NULL);
	if (ret) {
		ret_erri_line = __LINE__;
		goto failed;
	}

	ret = register_trace_android_vh_alter_futex_plist_add(
			probe_android_vh_alter_futex_plist_add, NULL);
	if (ret) {
		ret_erri_line = __LINE__;
		goto failed;
	}

	ret = register_trace_android_rvh_select_task_rq_fair(
			probe_android_rvh_select_task_rq_fair, NULL);
	if (ret) {
		ret_erri_line = __LINE__;
		goto failed;
	}

	ret = register_trace_android_vh_cgroup_set_task(
			probe_android_vh_cgroup_set_task, NULL);
	if (ret) {
		ret_erri_line = __LINE__;
		goto failed;
	}

	ret = register_trace_android_vh_sys_set_task(
			probe_android_vh_sys_set_task, NULL);
	if (ret) {
		ret_erri_line = __LINE__;
		goto failed;
	}

	ret = register_trace_android_vh_nice_check(
			probe_android_vh_nice_check, NULL);
	if (ret) {
		ret_erri_line = __LINE__;
		goto failed;
	}

	init_hmp_domains();

failed:
	if (ret)
		pr_err("register hooks failed, ret %d line %d\n", ret, ret_erri_line);

	return ret;
}
static void  __exit exit_task_turbo(void)
{
	/*
	 * vendor hook cannot unregister, please check vendor_hook.h
	 */
}
module_init(init_task_turbo);
module_exit(exit_task_turbo);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MediaTek Inc.");
MODULE_DESCRIPTION("MediaTek task-turbo");
