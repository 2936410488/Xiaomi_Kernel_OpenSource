/*
 * Copyright (C) 2016 MediaTek Inc.
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
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/printk.h>
#include <linux/uaccess.h>
#include <linux/of_irq.h>

/* internal headers */
#include "vpu_drv.h"
#include "vpu_cmn.h"
#include "vpu_mem.h"
#include "vpu_algo.h"
#include "vpu_debug.h"
#include "apusys_power.h"
#include "remoteproc_internal.h"  // TODO: move to drivers/remoteproc/../..

/* remote proc */
#define VPU_FIRMWARE_NAME "mtk_vpu"

/* interface to APUSYS */
int vpu_send_cmd(int op, void *hnd, struct apusys_device *adev)
{
	struct vpu_device *vd;
	struct apusys_cmd_hnd *cmd;
	struct apusys_power_hnd *pw;
	struct apusys_preempt_hnd *pmt;
	struct apusys_firmware_hnd *fw;

	// TODO: implement these sub-functions and remove UNUSED()
#define UNUSED(x) ((void)x)
	UNUSED(pw);
	UNUSED(pmt);
	UNUSED(fw);
#undef UNUSED

	vd = (struct vpu_device *)adev->private;

	vpu_cmd_debug("%s: cmd: %d, hnd: %p\n", __func__, op, hnd);

	switch (op) {
	case APUSYS_CMD_POWERON:
		pw = (struct apusys_power_hnd *)hnd;
		vpu_cmd_debug("%s: APUSYS_CMD_POWERON, boost: %d, opp: %d\n",
			__func__, pw->boost_val, pw->opp);
		break;
	case APUSYS_CMD_POWERDOWN:
		vpu_cmd_debug("%s: APUSYS_CMD_POWERDOWN\n", __func__);
		break;
	case APUSYS_CMD_RESUME:
		vpu_cmd_debug("%s: APUSYS_CMD_RESUME\n", __func__);
		break;
	case APUSYS_CMD_SUSPEND:
		vpu_cmd_debug("%s: APUSYS_CMD_SUSPEND\n", __func__);
		break;
	case APUSYS_CMD_EXECUTE:
		cmd = (struct apusys_cmd_hnd *)hnd;
		vpu_cmd_debug("%s: APUSYS_CMD_EXECUTE, kva: %lx\n",
			__func__, (unsigned long)cmd->kva);
		return vpu_execute(vd, (struct vpu_request *)cmd->kva);
	case APUSYS_CMD_PREEMPT:
		pmt = (struct apusys_preempt_hnd *)hnd;
		vpu_cmd_debug("%s: APUSYS_CMD_PREEMPT, new cmd kva: %lx\n",
			__func__, (unsigned long)pmt->new_cmd->kva);
		break;
	case APUSYS_CMD_FIRMWARE:
		fw = (struct apusys_firmware_hnd *)hnd;
		vpu_cmd_debug("%s: APUSYS_CMD_FIRMWARE, kva: %p\n",
			__func__, fw->kva);
		break;
	default:
		vpu_cmd_debug("%s: unknown command: %d\n", __func__, cmd);
		break;
	}

	return -EINVAL;
}

static int vpu_load(struct rproc *rproc, const struct firmware *fw)
{
	return 0;
}

#if 1
// TODO: move to drivers/remoteproc/../..
static struct resource_table *
vpu_rsc_table(struct rproc *rproc, const struct firmware *fw, int *tablesz)
{
	static struct resource_table table = { .ver = 1, };

	*tablesz = sizeof(table);
	return &table;
}

static const struct rproc_fw_ops vpu_fw_ops = {
	.find_rsc_table = vpu_rsc_table,
	.load = vpu_load,
};
#endif

struct vpu_driver *vpu_drv;

void vpu_drv_release(struct kref *ref)
{
	vpu_drv_debug("%s:\n", __func__);
	kfree(vpu_drv);
	vpu_drv = NULL;
}

void vpu_drv_put(void)
{
	if (!vpu_drv)
		return;

	vpu_drv_debug("%s:\n", __func__);
	kref_put(&vpu_drv->ref, vpu_drv_release);
}

void vpu_drv_get(void)
{
	kref_get(&vpu_drv->ref);
}

static int vpu_start(struct rproc *rproc)
{
	/* enable power and clock */
	return 0;
}

static int vpu_stop(struct rproc *rproc)
{
	/* disable regulator and clock */
	return 0;
}

static void *vpu_da_to_va(struct rproc *rproc, u64 da, int len)
{
	/* convert device address to kernel virtual address */
	return 0;
}

static const struct rproc_ops vpu_ops = {
	.start = vpu_start,
	.stop = vpu_stop,
	.da_to_va = vpu_da_to_va,
};

static int vpu_init_bin(void)
{
	struct device_node *node;
	uint32_t phy_addr;
	uint32_t phy_size;

	/* skip, if vpu firmware had ready been mapped */
	if (vpu_drv && vpu_drv->bin_va)
		return 0;

	node = of_find_compatible_node(NULL, NULL, "mediatek,vpu_core0");

	if (of_property_read_u32(node, "bin-phy-addr", &phy_addr) ||
		of_property_read_u32(node, "bin-size", &phy_size)) {
		pr_info("%s: unable to get vpu firmware.\n", __func__);
		return -ENODEV;
	}

	/* map vpu firmware to kernel virtual address */
	vpu_drv->bin_va = ioremap_wc(phy_addr, phy_size);
	vpu_drv->bin_pa = phy_addr;
	vpu_drv->bin_size = phy_size;

	pr_info("%s: mapped vpu firmware: pa: 0x%lx, size: 0x%x, kva: 0x%lx\n",
		__func__, vpu_drv->bin_pa, vpu_drv->bin_size,
		(unsigned long)vpu_drv->bin_va);

	return 0;
}

static void vpu_shared_release(struct kref *ref)
{
	vpu_drv_debug("%s:\n", __func__);

	if (vpu_drv->mva_algo) {
		vpu_iova_free(vpu_drv->iova_dev, &vpu_drv->iova_algo);
		vpu_drv->mva_algo = 0;
	}

	if (vpu_drv->mva_share) {
		vpu_iova_free(vpu_drv->iova_dev, &vpu_drv->iova_share);
		vpu_drv->mva_share = 0;
	}
}

static int vpu_shared_put(struct platform_device *pdev,
	struct vpu_device *vd)
{
	vpu_drv->iova_dev = &pdev->dev;
	kref_put(&vpu_drv->iova_ref, vpu_shared_release);
	return 0;
}

static int vpu_shared_get(struct platform_device *pdev,
	struct vpu_device *vd)
{
	dma_addr_t iova = 0;

	if (vpu_drv->mva_algo && vpu_drv->mva_share) {
		kref_get(&vpu_drv->iova_ref);
		return 0;
	}

	kref_init(&vpu_drv->iova_ref);

	if (!vpu_drv->mva_algo) {
		if (vpu_iova_dts(pdev, "algo", &vpu_drv->iova_algo))
			goto error;
		iova = vpu_iova_alloc(pdev,	&vpu_drv->iova_algo);
		pr_info("%s: algo: %lx\n",  // TODO: remove debug log
			__func__, (unsigned long)iova);
		if (!iova)
			goto error;
		vpu_drv->mva_algo = iova;
	}

	if (!vpu_drv->mva_share) {
		if (vpu_iova_dts(pdev, "share-data", &vpu_drv->iova_share))
			goto error;
		iova = vpu_iova_alloc(pdev,	&vpu_drv->iova_share);
		pr_info("%s: share data: %lx\n",  // TODO: remove debug log
			__func__, (unsigned long)iova);
		if (!iova)
			goto error;
		vpu_drv->mva_share = iova;
	}

	return 0;

error:
	vpu_shared_put(pdev, vd);
	return -ENOMEM;
}

static int vpu_exit_dev_mem(struct platform_device *pdev,
	struct vpu_device *vd)
{
	vpu_iova_free(&pdev->dev, &vd->iova_reset);
	vpu_iova_free(&pdev->dev, &vd->iova_main);
	vpu_iova_free(&pdev->dev, &vd->iova_kernel);
	vpu_iova_free(&pdev->dev, &vd->iova_work);
	vpu_iova_free(&pdev->dev, &vd->iova_iram);
	vpu_shared_put(pdev, vd);

	return 0;
}

static int vpu_init_dev_mem(struct platform_device *pdev,
	struct vpu_device *vd)
{
	struct resource *res;
	dma_addr_t iova = 0;
	int ret = 0;

	/* reg_base */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_info(&pdev->dev, "unable to get resource\n");
		return -ENODEV;
	}
	vd->reg_base = devm_ioremap_resource(&pdev->dev, res); /* IPU_BASE */

	if (!vd->reg_base) {
		dev_info(&pdev->dev, "unable to map register base\n");
		return -ENODEV;
	}

	pr_info("%s: vpu%d: mapped reg_base: 0x%lx\n",
		__func__, vd->id, (unsigned long)vd->reg_base);

	/* iova */
	if (vpu_iova_dts(pdev, "reset-vector", &vd->iova_reset) ||
		vpu_iova_dts(pdev, "main-prog", &vd->iova_main) ||
		vpu_iova_dts(pdev, "kernel-lib", &vd->iova_kernel) ||
		vpu_iova_dts(pdev, "iram-data", &vd->iova_iram) ||
		vpu_iova_dts(pdev, "work-buf", &vd->iova_work)) {
		goto error;
	}

	ret = vpu_shared_get(pdev, vd);
	if (ret)
		goto error;
	iova = vpu_iova_alloc(pdev, &vd->iova_reset);
	pr_info("%s: vpu%d: reset vector: %lx\n",  // TODO: remove debug log
		__func__, vd->id, (unsigned long)iova);
	if (!iova)
		goto free;
	iova = vpu_iova_alloc(pdev, &vd->iova_main);
	pr_info("%s: vpu%d: main prog: %lx\n",  // TODO: remove debug log
		__func__, vd->id, (unsigned long)iova);
	if (!iova)
		goto free;
	iova = vpu_iova_alloc(pdev, &vd->iova_kernel);
	pr_info("%s: vpu%d: kernel lib: %lx\n",
		__func__, vd->id, (unsigned long)iova);
	if (!iova)
		goto free;
	iova = vpu_iova_alloc(pdev, &vd->iova_work);
	pr_info("%s: vpu%d: work buf: %lx\n",  // TODO: remove debug log
		__func__, vd->id, (unsigned long)iova);
	if (!iova)
		goto free;
	iova = vpu_iova_alloc(pdev, &vd->iova_iram);
	pr_info("%s: vpu%d: iram data: %lx\n",  // TODO: remove debug log
		__func__, vd->id, (unsigned long)iova);
	vd->mva_iram = iova;

	return 0;

free:
	vpu_exit_dev_mem(pdev, vd);
error:
	return -ENOMEM;
}


static int vpu_init_dev_irq(struct platform_device *pdev,
	struct vpu_device *vd)
{
	vd->irq_num = irq_of_parse_and_map(pdev->dev.of_node, 0);

	if (vd->irq_num <= 0) {
		pr_info("%s: %s: invalid IRQ: %d\n",
			__func__, vd->name, vd->irq_num);
		return -ENODEV;
	}

	pr_info("%s: %s: IRQ: %d\n",
		__func__, vd->name, vd->irq_num);

	return 0;
}

static int vpu_probe(struct platform_device *pdev)
{
	struct vpu_device *vd;
	struct rproc *rproc;
	int ret;

	rproc = rproc_alloc(&pdev->dev, pdev->name, &vpu_ops,
		VPU_FIRMWARE_NAME, sizeof(*vd));

	if (!rproc) {
		dev_info(&pdev->dev, "failed to allocate rproc\n");
		return -ENOMEM;
	}

	/* initialize device (core specific) data */
	rproc->fw_ops = &vpu_fw_ops;
	vd = (struct vpu_device *)rproc->priv;
	vd->dev = &pdev->dev;
	vd->rproc = rproc;
	platform_set_drvdata(pdev, vd);

	if (of_property_read_u32(pdev->dev.of_node, "id", &vd->id)) {
		dev_info(&pdev->dev, "unable to get core id from dts\n");
		ret = -ENODEV;
		goto free_rproc;
	}

	snprintf(vd->name, sizeof(vd->name), "vpu%d", vd->id);

	/* put efuse judgement at beginning */
	if (vpu_is_disabled(vd)) {
		ret = -ENODEV;
		vd->state = VS_DISALBED;
		goto free_rproc;
	} else {
		vd->state = VS_DOWN;
	}

	/* allocate resources */
	ret = vpu_init_dev_mem(pdev, vd);
	if (ret)
		goto free_rproc;

	ret = vpu_init_dev_irq(pdev, vd);
	if (ret)
		goto free_rproc;

	/* device hw initialization */
	ret = vpu_init_dev_hw(pdev, vd);
	if (ret)
		goto free_rproc;

	/* power initialization */
	vpu_drv_debug("%s: apu_power_device_register call\n", __func__);
	if (vd->id != 0) { // we just need to take pdev of core0 to init power
//		ret = apu_power_device_register(VPU0 + vd->id, NULL);
	} else {
//		ret = apu_power_device_register(VPU0 + vd->id, pdev);
	}
	if (ret) {
		dev_info(&pdev->dev, "apu_power_device_register: %d\n",
			ret);
		goto free_rproc;
	}

	vpu_drv_debug("%s: apu_device_power_on call\n", __func__);
//	ret = apu_device_power_on((VPU0 + vd->id));
	if (ret) {
		dev_info(&pdev->dev, "apu_device_power_on: %d\n", ret);
		goto free_rproc;
	}

	/* device algo initialization */
	INIT_LIST_HEAD(&vd->algo);
	ret = vpu_init_dev_algo(pdev, vd);
	if (ret)
		goto free_rproc;

	/* register device to APUSYS */
	vd->adev.dev_type = APUSYS_DEVICE_VPU;
	vd->adev.preempt_type = APUSYS_PREEMPT_WAITCOMPLETED;
	vd->adev.private = vd;
	vd->adev.send_cmd = vpu_send_cmd;

	ret = apusys_register_device(&vd->adev);
	if (ret) {
		dev_info(&pdev->dev, "apusys_register_device: %d\n",
			ret);
		goto free_rproc;
	}

	/* register debugfs nodes */
	ret = vpu_init_dev_debug(pdev, vd);
	if (ret)
		goto free_rproc;

	/* add to remoteproc */
	ret = rproc_add(rproc);
	if (ret) {
		dev_info(&pdev->dev, "rproc_add: %d\n", ret);
		goto free_rproc;
	}

	/* add to vd list */
	mutex_lock(&vpu_drv->lock);
	vpu_drv_get();
	list_add_tail(&vd->list, &vpu_drv->devs);
	mutex_unlock(&vpu_drv->lock);

	dev_info(&pdev->dev, "%s: succeed\n", __func__);
	return 0;

	// TODO: add error handling free algo

free_rproc:
	rproc_free(rproc);
	dev_info(&pdev->dev, "%s: failed\n", __func__);
	return ret;
}

static int vpu_remove(struct platform_device *pdev)
{
	struct vpu_device *vd = platform_get_drvdata(pdev);

	vpu_exit_dev_debug(pdev, vd);
	vpu_exit_dev_hw(pdev, vd);
	vpu_exit_dev_algo(pdev, vd);
	vpu_exit_dev_mem(pdev, vd);
	disable_irq(vd->irq_num);
	apusys_unregister_device(&vd->adev);
	rproc_del(vd->rproc);
	rproc_free(vd->rproc);
//	apu_device_power_off(VPU0 + vd->id);
//	apu_power_device_unregister(VPU0 + vd->id);
	vpu_drv_put();

	return 0;
}

static int vpu_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	return 0;
}

static int vpu_resume(struct platform_device *pdev)
{
	return 0;
}

/* device power management */
#ifdef CONFIG_PM
int vpu_pm_suspend(struct device *device)
{
	struct platform_device *pdev = to_platform_device(device);

	WARN_ON(pdev == NULL);
	return vpu_suspend(pdev, PMSG_SUSPEND);  // TODO: inplement vpu_suspend
}

int vpu_pm_resume(struct device *device)
{
	struct platform_device *pdev = to_platform_device(device);

	WARN_ON(pdev == NULL);
	return vpu_resume(pdev);  // TODO: implement vpu resume
}

int vpu_pm_restore_noirq(struct device *device)
{
	return 0;
}

static const struct dev_pm_ops vpu_pm_ops = {
	.suspend = vpu_pm_suspend,
	.resume = vpu_pm_resume,
	.freeze = vpu_pm_suspend,
	.thaw = vpu_pm_resume,
	.poweroff = vpu_pm_suspend,
	.restore = vpu_pm_resume,
	.restore_noirq = vpu_pm_restore_noirq,
};
#endif

static const struct of_device_id vpu_of_ids[] = {
	{.compatible = "mediatek,vpu_core0",},
	{.compatible = "mediatek,vpu_core1",},
	{.compatible = "mediatek,vpu_core2",},
	{}
};

static struct platform_driver vpu_plat_drv = {
	.probe   = vpu_probe,
	.remove  = vpu_remove,
	.suspend = vpu_suspend,
	.resume  = vpu_resume,
	.driver  = {
	.name = "vpu",
	.owner = THIS_MODULE,
	.of_match_table = vpu_of_ids,
#ifdef CONFIG_PM
	.pm = &vpu_pm_ops,
#endif
	}
};

static int __init vpu_init(void)
{
	int ret;

	vpu_drv = kzalloc(sizeof(struct vpu_driver), GFP_KERNEL);

	if (!vpu_drv)
		return -ENOMEM;

	kref_init(&vpu_drv->ref);

	ret = vpu_init_bin();
	if (ret)
		goto error_out;

	ret = vpu_init_algo();
	if (ret)
		goto error_out;

	vpu_init_debug();

	INIT_LIST_HEAD(&vpu_drv->devs);
	mutex_init(&vpu_drv->lock);

	vpu_drv->mva_algo = 0;
	vpu_drv->mva_share = 0;

	vpu_init_drv_hw();

	ret = platform_driver_register(&vpu_plat_drv);

	return ret;

error_out:
	kfree(vpu_drv);
	vpu_drv = NULL;
	return ret;
}

static void __exit vpu_exit(void)
{
	struct vpu_device *vd;
	struct list_head *ptr, *tmp;

	/* notify all devices that we are going to be removed
	 *  wait and stop all on-going requests
	 **/
	mutex_lock(&vpu_drv->lock);
	list_for_each_safe(ptr, tmp, &vpu_drv->devs) {
		vd = list_entry(ptr, struct vpu_device, list);
		list_del(ptr);
		mutex_lock(&vd->cmd_lock);
		vd->state = VS_REMOVING;
		mutex_unlock(&vd->cmd_lock);
	}
	mutex_unlock(&vpu_drv->lock);

	vpu_exit_debug();
	vpu_exit_drv_hw();

	if (vpu_drv) {
		vpu_drv_debug("%s: iounmap\n", __func__);
		if (vpu_drv->bin_va) {
			iounmap(vpu_drv->bin_va);
			vpu_drv->bin_va = NULL;
		}

		vpu_drv_put();
	}

	vpu_drv_debug("%s: platform_driver_unregister\n", __func__);
	platform_driver_unregister(&vpu_plat_drv);
}

// module_init(vpu_init);
late_initcall(vpu_init);
module_exit(vpu_exit);
MODULE_DESCRIPTION("Mediatek VPU Driver");
MODULE_LICENSE("GPL");

