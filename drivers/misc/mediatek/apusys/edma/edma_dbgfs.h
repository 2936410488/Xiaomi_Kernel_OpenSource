/*
 * Copyright (c) 2019 MediaTek Inc.
 * Author: JB Tsai <jb.tsai@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __EDMA_DBGFS_H__
#define __EDMA_DBGFS_H__

int edma_create_sysfs(struct device *dev);
void edma_remove_sysfs(struct device *dev);
int edma_dbg_check_ststus(int check_status);

#endif /* __EDMA_DBGFS_H__ */
