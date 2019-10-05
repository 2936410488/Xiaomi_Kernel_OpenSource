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

#ifndef __APUSYS_MEMORY_MGT_H__
#define __APUSYS_MEMORY_MGT_H__

#include <ion.h>
#include <mtk/ion_drv.h>
#include <mtk/mtk_ion.h>

struct apusys_mem_mgr {
	struct ion_client *client;

	struct list_head list;
	struct mutex list_mtx;
	struct device *dev;
	uint8_t is_init;
};

int apusys_mem_alloc(struct apusys_mem *mem);
int apusys_mem_free(struct apusys_mem *mem);
int apusys_mem_flush(void);
int apusys_mem_invalidate(void);
int apusys_mem_init(struct device *dev);
int apusys_mem_destroy(void);
int apusys_mem_ctl(struct apusys_mem *mem);
int apusys_mem_map_iova(struct apusys_mem *mem);
int apusys_mem_map_kva(struct apusys_mem *mem);
int apusys_mem_unmap_iova(struct apusys_mem *mem);
int apusys_mem_unmap_kva(struct apusys_mem *mem);
#endif
