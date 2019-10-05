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

#ifndef __MTK_GPU_SWPM_PLATFORM_H__
#define __MTK_GPU_SWPM_PLATFORM_H__

/* numbers of unsigned int */
#define GPU_SWPM_RESERVED_SIZE 10

enum gpu_power_counter {
	gfreq,
	gvolt,
	galu_urate,
	gtex_urate,
	glsc_urate,
	gl2c_urate,
	gvary_urate,
	gtiler_urate,
	gloading,

	GPU_POWER_COUNTER_LAST
};

struct gpu_swpm_rec_data {
	/* 4(int) * 10 = 40 bytes */
	unsigned int gpu_enable;
	unsigned int gpu_counter[GPU_POWER_COUNTER_LAST];
};

#endif

