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

#include <linux/types.h>
#include <linux/printk.h>
#include "vpu_power.h"
#include "vpu_debug.h"
#include "vpu_algo.h"
#include "apusys_power.h"

static void vpu_pwr_off_locked(struct vpu_device *vd);

/* Get APUSYS DVFS User ID from VPU ID */
static inline int adu(int id)
{
	return (VPU0 + id);
}

static void vpu_pwr_off_timer(struct vpu_device *vd, unsigned long t)
{
	mod_delayed_work(vpu_drv->wq,
		&vd->pw_off_work,
		msecs_to_jiffies(t));
}

static void vpu_pwr_release(struct kref *ref)
{
	struct vpu_device *vd
		= container_of(ref, struct vpu_device, pw_ref);
	unsigned long t = (unsigned long)vd->pw_off_latency;

	if (t) {
		vpu_pwr_debug("%s: vpu%d: vpu_pwr_off_timer: %ld ms\n",
			__func__, vd->id, t);
		vpu_pwr_off_timer(vd, t /* ms */);
	}
}

static void vpu_pwr_param(struct vpu_device *vd, uint8_t boost)
{
	uint8_t opp;

	if (boost == VPU_PWR_NO_BOOST)
		return;

	opp = apusys_boost_value_to_opp(adu(vd->id), boost);

	vpu_pwr_debug("%s: vpu%d: boost: %d, opp: %d\n",
		__func__, vd->id, boost, opp);

	apu_device_set_opp(adu(vd->id), opp);
}

/**
 * vpu_pwr_get_locked() - acquire vpu power and increase power reference
 * @vd: vpu device
 * @boost: boost value: 0~100, given from vpu_request
 *
 * 1. vd->lock or vd->cmd_lock must be locked before calling this function
 * 2. Must paired with vpu_pwr_put_locked()
 */
int vpu_pwr_get_locked(struct vpu_device *vd, uint8_t boost)
{
	if (vd->state == VS_REMOVING) {
		pr_info("%s: vpu%d is going to be removed\n",
			__func__, vd->id);
		return -ENODEV;
	}

	cancel_delayed_work(&vd->pw_off_work);

	if (kref_get_unless_zero(&vd->pw_ref)) {
		vpu_pwr_debug("%s: vpu%d: ref: %d, boost: %d\n",
			__func__, vd->id, kref_read(&vd->pw_ref), boost);
		goto out;
	}

	kref_init(&vd->pw_ref);
	vpu_pwr_debug("%s: vpu%d: ref: 1\n", __func__, vd->id);

	if (vd->state == VS_DOWN) {
		vpu_pwr_debug("%s: vpu%d: apu_device_power_on\n",
			__func__, vd->id);
		apu_device_power_on(adu(vd->id));
	}

out:
	vpu_pwr_param(vd, boost);
	return 0;
}

/**
 * vpu_pwr_put_locked() - decrease power reference
 * @vd: vpu device
 *
 * 1. vd->lock or vd->cmd_lock must be locked before calling this function
 * 2. Must paired with vpu_pwr_get_locked()
 */
void vpu_pwr_put_locked(struct vpu_device *vd)
{
	if (!kref_read(&vd->pw_ref)) {
		vpu_pwr_debug("%s: vpu%d: ref is already zero\n",
			__func__, vd->id);
		return;
	}
	vpu_pwr_debug("%s: vpu%d: ref: %d--\n",
		__func__, vd->id, vpu_pwr_cnt(vd));
	kref_put(&vd->pw_ref, vpu_pwr_release);
}

/**
 * vpu_pwr_up_locked() - unconditionally power up VPU
 * @vd: vpu device
 * @boost: boost value: 0~100, given from vpu_request
 *
 * vd->lock or vd->cmd_lock must be locked before calling this function
 */
int vpu_pwr_up_locked(struct vpu_device *vd, uint8_t boost)
{
	int ret;

	ret = vpu_pwr_get_locked(vd, boost);
	if (!ret)
		return ret;

	vpu_pwr_put_locked(vd);
	vpu_pwr_debug("%s: powered up %d\n", __func__, ret);
	return 0;
}

/**
 * vpu_pwr_down_locked() - unconditionally power down VPU
 * @vd: vpu device
 *
 * vd->lock or vd->cmd_lock must be locked before calling this function
 */
void vpu_pwr_down_locked(struct vpu_device *vd)
{
	refcount_set(&vd->pw_ref.refcount, 0);
	vpu_pwr_off_locked(vd);
	vpu_pwr_debug("%s: vpu%d: powered down\n", __func__, vd->id);
}

/**
 * vpu_pwr_up() - unconditionally power up VPU
 * @vd: vpu device
 * @boost: boost value: 0~100, given from vpu_request
 *
 */
int vpu_pwr_up(struct vpu_device *vd, uint8_t boost)
{
	int ret;

	mutex_lock(&vd->lock);
	mutex_lock(&vd->cmd_lock);
	ret = vpu_pwr_up_locked(vd, boost);
	mutex_unlock(&vd->cmd_lock);
	mutex_unlock(&vd->lock);
	return ret;
}

/**
 * vpu_pwr_down() - unconditionally power down VPU
 * @vd: vpu device
 *
 */
void vpu_pwr_down(struct vpu_device *vd)
{
	mutex_lock(&vd->lock);
	mutex_lock(&vd->cmd_lock);
	vpu_pwr_down_locked(vd);
	mutex_unlock(&vd->cmd_lock);
	mutex_unlock(&vd->lock);
}

static void vpu_pwr_off_locked(struct vpu_device *vd)
{
	vpu_pwr_debug("%s: vpu%d: apu_device_power_off\n",
		__func__, vd->id);
	vpu_alg_unload(vd);
	apu_device_power_off(adu(vd->id));
	vd->state = VS_DOWN;
}

static void vpu_pwr_off(struct work_struct *work)
{
	struct vpu_device *vd
		= container_of(work, struct vpu_device, pw_off_work.work);

	mutex_lock(&vd->lock);
	mutex_lock(&vd->cmd_lock);
	vpu_pwr_off_locked(vd);
	mutex_unlock(&vd->cmd_lock);
	mutex_unlock(&vd->lock);
	wake_up_interruptible(&vd->pw_wait);
}

int vpu_init_dev_pwr(struct platform_device *pdev, struct vpu_device *vd)
{
	int ret = 0;

	INIT_DELAYED_WORK(&vd->pw_off_work, vpu_pwr_off);
	init_waitqueue_head(&vd->pw_wait);
	vd->pw_off_latency = VPU_PWR_OFF_LATENCY;
	refcount_set(&vd->pw_ref.refcount, 0);

	vpu_drv_debug("%s: vpu%d: apu_power_device_register call\n",
		__func__, vd->id);

	ret = apu_power_device_register(adu(vd->id), pdev);

	if (ret)
		dev_info(&pdev->dev, "apu_power_device_register: %d\n",
			ret);

	return ret;
}

void vpu_exit_dev_pwr(struct platform_device *pdev, struct vpu_device *vd)
{
	cancel_delayed_work(&vd->pw_off_work);
	apu_device_power_off(adu(vd->id));
	apu_power_device_unregister(adu(vd->id));
}

