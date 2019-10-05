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

#ifndef _SLBC_OPS_H_
#define _SLBC_OPS_H_

#include <linux/list.h>
#include <linux/bitops.h>

/* error code */
#define EWAIT_RELEASE		1 /* wait for release */
#define ENOT_AVAILABLE		2 /* not available for now */
#define EREQ_DONE		3 /* already requested */
#define EREQ_MASKED		4 /* req madk bit set */
#define EDISABLED		5 /* req madk bit set */

/* call back return value */
#define CB_DONE			0 /* no need to use*/
#define CB_OK			1 /* ready to use/release */

/* slot status */
#define SLOT_AVAILABLE		0 /* slot available*/
#define SLOT_NOT_FOUND		1 /* slot not found */
#define SLOT_USED		2 /* slot used */

/* need to modify slbc_uid_str  */
enum slbc_uid {
	UID_MM_VENC,
	UID_MM_DISP,
	UID_MM_MDP,
	UID_MM_VDEC,
	UID_MD_DPMAIF,
	UID_AI_MDLA,
	UID_AI_ISP,
	UID_GPU,
	UID_HIFI3,
	UID_CPU,
	UID_TEST,
	UID_MAX,
};

#define UID_MM_BITS_1 (BIT(UID_MM_DISP) | BIT(UID_MM_MDP))
#define BIT_IN_MM_BITS_1(x) ((x) & UID_MM_BITS_1)

enum slbc_type {
	TP_BUFFER = 0,
	TP_CACHE,
};

enum slbc_flag {
	FG_SECURE = BIT(0),
	FG_POWER = BIT(1),
};

#define SLBC_TRY_FLAG_BIT(d, bit) (((d)->flag & (bit)) == (bit))

struct slbc_data {
	int uid;
	int type;
	ssize_t size;
	int flag;
	/* below used by slbc driver */
	void __iomem *paddr;
	void __iomem *vaddr;
	int sid;
	int slot_used;
	void *config;
	int ref;
	int pwr_ref;
};

struct slbc_ops {
	struct list_head node;
	struct slbc_data *data;
	int (*activate)(struct slbc_data *data);
	void (*deactivate)(struct slbc_data *data);
};

#ifdef CONFIG_MTK_SLBC
extern int register_slbc_ops(struct slbc_ops *ops);
extern int unregister_slbc_ops(struct slbc_ops *ops);
extern int slbc_request(struct slbc_data *data);
extern int slbc_release(struct slbc_data *data);
extern int slbc_power_on(struct slbc_data *data);
extern int slbc_power_off(struct slbc_data *data);
#else
int register_slbc_ops(struct slbc_ops *ops)
{
	return -EDISABLED;
};
int unregister_slbc_ops(struct slbc_ops *ops)
{
	return -EDISABLED;
};
int slbc_request(struct slbc_data *data)
{
	return -EDISABLED;
};
int slbc_release(struct slbc_data *data)
{
	return -EDISABLED;
};
int slbc_power_on(struct slbc_data *data)
{
	return -EDISABLED;
};
int slbc_power_off(struct slbc_data *data)
{
	return -EDISABLED;
};
#endif /* CONFIG_MTK_SLBC */

#endif /* _SLBC_OPS_H_ */
