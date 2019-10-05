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

#include <linux/platform_device.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/syscalls.h>
#include <linux/cdev.h>
#include <linux/kthread.h>

#include "edma_dbgfs.h"
#include "edma_driver.h"
#include "edma_cmd_hnd.h"
#include "edma_queue.h"

#define EDMA_DEV_NAME		"edma"

static int edma_init_queue_task(struct edma_sub *edma_sub)
{
	edma_sub->enque_task = kthread_create(edma_enque_routine_loop,
					      edma_sub,
					      edma_sub->sub_name);
	if (IS_ERR(edma_sub->enque_task)) {
		edma_sub->enque_task = NULL;
		dev_notice(edma_sub->dev, "create enque_task kthread error.\n");
		return -ENOENT;
	}
	wake_up_process(edma_sub->enque_task);

	return 0;
}

int edma_initialize(struct edma_device *edma_device)
{
	int ret = 0;
	int sub_id;

	init_waitqueue_head(&edma_device->req_wait);
	/* init hw and create task */
	for (sub_id = 0; sub_id < edma_device->edma_sub_num; sub_id++) {
		struct edma_sub *edma_sub = edma_device->edma_sub[sub_id];

		if (!edma_sub)
			continue;

		edma_sub->edma_device = edma_device;
		edma_sub->sub = sub_id;
		mutex_init(&edma_sub->cmd_mutex);
		init_waitqueue_head(&edma_sub->cmd_wait);
		sprintf(edma_sub->sub_name, "edma%d", edma_sub->sub);
		ret = edma_init_queue_task(edma_sub);
	}

	edma_device->edma_init_done = true;

	return ret;
}

static int edma_open(struct inode *inode, struct file *flip)
{
	int ret = 0;
	struct edma_user *user;
	struct edma_device *edma_device;

	edma_device =
	    container_of(inode->i_cdev, struct edma_device, edma_chardev);

	if (!edma_device->edma_init_done) {
		ret = edma_initialize(edma_device);
		if (ret) {
			pr_notice("fail to initialize edma");
			return ret;
		}
	}

	edma_create_user(&user, edma_device);
	if (IS_ERR_OR_NULL(user)) {
		pr_notice("fail to create user\n");
		return -ENOMEM;
	}

	flip->private_data = user;

	return ret;
}

static int edma_release(struct inode *inode, struct file *flip)
{
	struct edma_user *user = flip->private_data;
	struct edma_device *edma_device;

	if (user) {
		edma_device = dev_get_drvdata(user->dev);
		edma_delete_user(user, edma_device);
	} else {
		pr_notice("delete empty user!\n");
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations edma_fops = {
	.owner = THIS_MODULE,
	.open = edma_open,
	.release = edma_release,
#ifdef CONFIG_COMPAT
	.unlocked_ioctl = edma_ioctl
#endif
};

static inline void edma_unreg_chardev(struct edma_device *edma_device)
{
	cdev_del(&edma_device->edma_chardev);
	unregister_chrdev_region(edma_device->edma_devt, 1);
}

static inline int edma_reg_chardev(struct edma_device *edma_device)
{
	int ret = 0;

	ret = alloc_chrdev_region(&edma_device->edma_devt, 0, 1, EDMA_DEV_NAME);
	if ((ret) < 0) {
		pr_notice("alloc_chrdev_region failed, %d\n", ret);
		return ret;
	}

	/* Attatch file operation. */
	cdev_init(&edma_device->edma_chardev, &edma_fops);
	edma_device->edma_chardev.owner = THIS_MODULE;

	/* Add to system */
	ret = cdev_add(&edma_device->edma_chardev, edma_device->edma_devt, 1);
	if ((ret) < 0) {
		pr_notice("Attatch file operation failed, %d\n", ret);
		goto out;
	}

out:
	if (ret < 0)
		edma_unreg_chardev(edma_device);

	return ret;
}

static const struct of_device_id mtk_edma_sub_of_ids[] = {
	{.compatible = "mtk,edma-sub",    NULL},
	{}
};

static int mtk_edma_sub_probe(struct platform_device *pdev)
{
	int irq, ret;
	struct resource *mem;
	struct edma_sub *edma_sub;
	struct device *dev = &pdev->dev;

	edma_sub = devm_kzalloc(dev, sizeof(*edma_sub), GFP_KERNEL);
	if (!edma_sub)
		return -ENOMEM;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	edma_sub->base_addr = devm_ioremap_resource(dev, mem);
	if (IS_ERR((const void *)(edma_sub->base_addr))) {
		dev_notice(dev, "cannot get ioremap\n");
		return -ENOENT;
	}

	/* interrupt resource */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, edma_isr_handler,
			       IRQF_TRIGGER_NONE,
			       dev_name(dev),
			       edma_sub);
	if (ret < 0) {
		dev_notice(dev, "Failed to request irq %d: %d\n", irq, ret);
		return ret;
	}

	edma_sub->dev = &pdev->dev;
	platform_set_drvdata(pdev, edma_sub);
	dev_set_drvdata(dev, edma_sub);

	return 0;
}

static int mtk_edma_sub_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver mtk_edma_sub_driver = {
	.probe = mtk_edma_sub_probe,
	.remove = mtk_edma_sub_remove,
	.driver = {
		   .name = "mtk,edma-sub",
		   .of_match_table = mtk_edma_sub_of_ids,
		   .pm = NULL,
	}
};

static int edma_setup_resource(struct platform_device *pdev,
			      struct edma_device *edma_device)
{
	struct device *dev = &pdev->dev;
	struct device_node *sub_node;
	struct platform_device *sub_pdev;
	int i, ret;

	ret = of_property_read_u32(dev->of_node, "sub_nr",
				   &edma_device->edma_sub_num);
	if (ret) {
		dev_notice(dev, "parsing sub_nr error: %d\n", ret);
		return -EINVAL;
	}

	if (edma_device->edma_sub_num > EDMA_SUB_NUM)
		return -EINVAL;

	for (i = 0; i < edma_device->edma_sub_num; i++) {
		struct edma_sub *edma_sub = NULL;

		sub_node = of_parse_phandle(dev->of_node,
					     "mediatek,edma-sub", i);
		if (!sub_node) {
			dev_notice(dev,
				"Missing <mediatek,edma-sub> phandle\n");
			return -EINVAL;
		}

		sub_pdev = of_find_device_by_node(sub_node);
		if (sub_pdev)
			edma_sub = platform_get_drvdata(sub_pdev);

		if (!edma_sub) {
			dev_notice(dev, "Waiting for edma sub %s\n",
				 sub_node->full_name);
			of_node_put(sub_node);
			return -EPROBE_DEFER;
		}
		of_node_put(sub_node);
		/* attach edma_sub */
		edma_device->edma_sub[i] = edma_sub;
	}

	return 0;
}

static int edma_probe(struct platform_device *pdev)
{
	struct edma_device *edma_device;
	struct device *dev = &pdev->dev;
	int ret;

	edma_device = devm_kzalloc(dev, sizeof(*edma_device), GFP_KERNEL);
	if (!edma_device)
		return -ENOMEM;

	ret = edma_setup_resource(pdev, edma_device);
	if (ret)
		return ret;

	edma_device->edma_init_done = false;

	INIT_LIST_HEAD(&edma_device->user_list);
	mutex_init(&edma_device->user_mutex);
	edma_device->edma_num_users = 0;
	edma_device->dev = &pdev->dev;

	if (edma_reg_chardev(edma_device) == 0) {
		/* Create class register */
		edma_class = class_create(THIS_MODULE, EDMA_DEV_NAME);
		if (IS_ERR(edma_class)) {
			ret = PTR_ERR(edma_class);
			dev_notice(dev, "Unable to create class, err = %d\n",
									ret);
			goto dev_out;
		}

		dev = device_create(edma_class, NULL, edma_device->edma_devt,
				    NULL, EDMA_DEV_NAME);
		if (IS_ERR(dev)) {
			ret = PTR_ERR(dev);
			dev_notice(dev,
				"Failed to create device: /dev/%s, err = %d",
				EDMA_DEV_NAME, ret);
			goto dev_out;
		}

		platform_set_drvdata(pdev, edma_device);
		dev_set_drvdata(dev, edma_device);
		edma_create_sysfs(dev);
	}
	pr_notice("edma probe done\n");

	return 0;

dev_out:
	edma_unreg_chardev(edma_device);

	return ret;
}

static int edma_remove(struct platform_device *pdev)
{
	struct edma_device *edma_device = platform_get_drvdata(pdev);

	edma_unreg_chardev(edma_device);
	device_destroy(edma_class, edma_device->edma_devt);
	class_destroy(edma_class);

	edma_remove_sysfs(&pdev->dev);

	return 0;
}


static const struct of_device_id edma_of_ids[] = {
	{.compatible = "mtk,edma",},
	{}
};

static struct platform_driver edma_driver = {
	.probe = edma_probe,
	.remove = edma_remove,
	.driver = {
		   .name = EDMA_DEV_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = edma_of_ids,
	}
};

static int __init EDMA_INIT(void)
{
	int ret = 0;

	ret = platform_driver_register(&mtk_edma_sub_driver);
	if (ret != 0) {
		pr_notice("Failed to register edma sub driver\n");
		return -ENODEV;
	}

	ret = platform_driver_register(&edma_driver);
	if (ret != 0) {
		pr_notice("failed to register edma driver");
		goto err_unreg_edma_sub;
	}

	return ret;
err_unreg_edma_sub:
	platform_driver_unregister(&mtk_edma_sub_driver);
	return ret;
}

static void __exit EDMA_EXIT(void)
{
	platform_driver_unregister(&edma_driver);
	platform_driver_unregister(&mtk_edma_sub_driver);
}

module_init(EDMA_INIT);
module_exit(EDMA_EXIT);
MODULE_LICENSE("GPL");
