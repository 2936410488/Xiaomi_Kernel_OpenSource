/*
 * Copyright (c) 2015 MediaTek Inc.
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

#include <drm/drmP.h>
#include <drm/drm_gem.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <drm/mediatek_drm.h>

#include "mtk_drm_drv.h"
#include "mtk_drm_gem.h"
#include "mtk_fence.h"
#include "mtk_drm_session.h"
#include "mtk_drm_mmp.h"
#include "ion_drv.h"
#include "ion_priv.h"
#include <soc/mediatek/smi.h>
#if defined(CONFIG_MTK_IOMMU_V2)
#include "mt_iommu.h"
#include "mtk_iommu_ext.h"
#include "mtk/mtk_ion.h"
#endif

static struct mtk_drm_gem_obj *mtk_drm_gem_init(struct drm_device *dev,
						unsigned long size)
{
	struct mtk_drm_gem_obj *mtk_gem_obj;
	int ret;

	size = round_up(size, PAGE_SIZE);

	mtk_gem_obj = kzalloc(sizeof(*mtk_gem_obj), GFP_KERNEL);
	if (!mtk_gem_obj)
		return ERR_PTR(-ENOMEM);

	ret = drm_gem_object_init(dev, &mtk_gem_obj->base, size);
	if (ret < 0) {
		DRM_ERROR("failed to initialize gem object\n");
		kfree(mtk_gem_obj);
		return ERR_PTR(ret);
	}

	return mtk_gem_obj;
}

static void mtk_gem_vmap_pa(phys_addr_t pa, uint size, int cached,
			    struct device *dev, unsigned long *fb_pa)
{
	phys_addr_t pa_align;
	uint sz_align, npages, i;
	struct page **pages;
	pgprot_t pgprot;
	void *va_align;
	struct sg_table *sgt;
	unsigned long attrs;

	pa_align = round_down(pa, PAGE_SIZE);
	sz_align = ALIGN(pa + size - pa_align, PAGE_SIZE);
	npages = sz_align / PAGE_SIZE;

	pages = kmalloc_array(npages, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return;

	pgprot = cached ? PAGE_KERNEL : pgprot_writecombine(PAGE_KERNEL);
	for (i = 0; i < npages; i++)
		pages[i] = phys_to_page(pa_align + i * PAGE_SIZE);

	va_align = vmap(pages, npages, VM_MAP, pgprot);

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt) {
		DDPPR_ERR("sgt creation failed\n");
		return;
	}

	sg_alloc_table_from_pages(sgt, pages, npages, 0, size, GFP_KERNEL);
	attrs = DMA_ATTR_SKIP_CPU_SYNC;
	dma_map_sg_attrs(dev, sgt->sgl, sgt->nents, 0, attrs);
	*fb_pa = sg_dma_address(sgt->sgl);
}

static void mtk_gem_vmap_pa_legacy(phys_addr_t pa, uint size,
				   struct mtk_drm_gem_obj *mtk_gem)
{
#if defined(CONFIG_MTK_IOMMU_V2)
	struct ion_client *client;
	struct ion_handle *handle;
	struct ion_mm_data mm_data;
	size_t mva_size;
	ion_phys_addr_t phy_addr = 0;

	mtk_gem->cookie = (void *)ioremap_nocache(pa, size);
	mtk_gem->kvaddr = mtk_gem->cookie;

	client = mtk_drm_gem_ion_create_client("disp_fb0");
	handle =
		ion_alloc(client, size, (size_t)mtk_gem->kvaddr,
			  ION_HEAP_MULTIMEDIA_MAP_MVA_MASK, 0);
	if (IS_ERR(handle)) {
		DDPPR_ERR("ion alloc failed, handle:0x%p\n", handle);
		return;
	}

	memset((void *)&mm_data, 0, sizeof(struct ion_mm_data));
	mm_data.config_buffer_param.module_id = 0;
	mm_data.config_buffer_param.kernel_handle = handle;
	mm_data.mm_cmd = ION_MM_CONFIG_BUFFER;
	if (ion_kernel_ioctl(client, ION_CMD_MULTIMEDIA,
				 (unsigned long)&mm_data) < 0) {
		DDPPR_ERR("ion config failed, handle:0x%p\n", handle);
		ion_free(client, handle);
		return;
	}

	ion_phys(client, handle, &phy_addr, &mva_size);
	mtk_gem->dma_addr = (unsigned int)phy_addr;
	mtk_gem->size = mva_size;
#endif
}

struct mtk_drm_gem_obj *mtk_drm_fb_gem_insert(struct drm_device *dev,
					      size_t size, phys_addr_t fb_base,
					      unsigned int vramsize)
{
	struct mtk_drm_private *priv = dev->dev_private;
	struct mtk_drm_gem_obj *mtk_gem;
	struct drm_gem_object *obj;
	unsigned long fb_pa;

	DDPINFO("%s+\n", __func__);
	mtk_gem = mtk_drm_gem_init(dev, vramsize);
	if (IS_ERR(mtk_gem))
		return ERR_CAST(mtk_gem);

	obj = &mtk_gem->base;
	if (1) {
		/* TODO: This is a temporary workaorund for get
		 * MVA for LK pre-allocated buffer. The get MVA API of
		 * iommu version return the wrong MVA value. Once the
		 * issue fixed, remove this workaround
		 */
		mtk_gem_vmap_pa_legacy(fb_base, vramsize, mtk_gem);
	} else {
		mtk_gem->dma_attrs = DMA_ATTR_WRITE_COMBINE;
		mtk_gem_vmap_pa(fb_base, vramsize, 0, dev->dev, &fb_pa);

		mtk_gem->dma_addr = (dma_addr_t)fb_pa;
		mtk_gem->size = size;
		mtk_gem->cookie =
			dma_alloc_attrs(priv->dma_dev, size, &mtk_gem->dma_addr,
					GFP_KERNEL, mtk_gem->dma_attrs);
		mtk_gem->kvaddr = mtk_gem->cookie;
	}

	DDPINFO("%s cookie = %p dma_addr = %pad size = %zu\n", __func__,
		mtk_gem->cookie, &mtk_gem->dma_addr, size);
	return mtk_gem;
}

struct mtk_drm_gem_obj *mtk_drm_gem_create(struct drm_device *dev, size_t size,
					   bool alloc_kmap)
{
	struct mtk_drm_private *priv = dev->dev_private;
	struct mtk_drm_gem_obj *mtk_gem;
	struct drm_gem_object *obj;
	int ret;

	mtk_gem = mtk_drm_gem_init(dev, size);
	if (IS_ERR(mtk_gem))
		return ERR_CAST(mtk_gem);

	obj = &mtk_gem->base;

	mtk_gem->dma_attrs = DMA_ATTR_WRITE_COMBINE;

	if (!alloc_kmap)
		mtk_gem->dma_attrs |= DMA_ATTR_NO_KERNEL_MAPPING;
	//	mtk_gem->dma_attrs |= DMA_ATTR_NO_WARN;
	mtk_gem->cookie =
		dma_alloc_attrs(priv->dma_dev, obj->size, &mtk_gem->dma_addr,
				GFP_KERNEL, mtk_gem->dma_attrs);
	if (!mtk_gem->cookie) {
		DRM_ERROR("failed to allocate %zx byte dma buffer", obj->size);
		ret = -ENOMEM;
		goto err_gem_free;
	}
	mtk_gem->size = obj->size;

	if (alloc_kmap)
		mtk_gem->kvaddr = mtk_gem->cookie;

	DRM_DEBUG_DRIVER("cookie = %p dma_addr = %pad size = %zu\n",
			 mtk_gem->cookie, &mtk_gem->dma_addr, mtk_gem->size);

	return mtk_gem;

err_gem_free:
	drm_gem_object_release(obj);
	kfree(mtk_gem);
	return ERR_PTR(ret);
}

void mtk_drm_gem_free_object(struct drm_gem_object *obj)
{
	struct mtk_drm_gem_obj *mtk_gem = to_mtk_gem_obj(obj);
	struct mtk_drm_private *priv = obj->dev->dev_private;

	if (mtk_gem->sg)
		drm_prime_gem_destroy(obj, mtk_gem->sg);
	else if (!mtk_gem->sec)
		dma_free_attrs(priv->dma_dev, obj->size, mtk_gem->cookie,
			       mtk_gem->dma_addr, mtk_gem->dma_attrs);

	if (mtk_gem->handle && priv->client)
		mtk_drm_gem_ion_free_handle(priv->client, mtk_gem->handle);
	else
		DDPPR_ERR("invaild ion handle or client\n");
	/* release file pointer to gem object. */
	drm_gem_object_release(obj);

	kfree(mtk_gem);
}

int mtk_drm_gem_dumb_create(struct drm_file *file_priv, struct drm_device *dev,
			    struct drm_mode_create_dumb *args)
{
	struct mtk_drm_gem_obj *mtk_gem;
	int ret;

	args->pitch = DIV_ROUND_UP(args->width * args->bpp, 8);
	args->size = (__u64)args->pitch * (__u64)args->height;

	mtk_gem = mtk_drm_gem_create(dev, args->size, false);
	if (IS_ERR(mtk_gem))
		return PTR_ERR(mtk_gem);

	/*
	 * allocate a id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file_priv, &mtk_gem->base, &args->handle);
	if (ret)
		goto err_handle_create;

	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_unreference_unlocked(&mtk_gem->base);

	return 0;

err_handle_create:
	mtk_drm_gem_free_object(&mtk_gem->base);
	return ret;
}

int mtk_drm_gem_dumb_map_offset(struct drm_file *file_priv,
				struct drm_device *dev, uint32_t handle,
				uint64_t *offset)
{
	struct drm_gem_object *obj;
	int ret;

	obj = drm_gem_object_lookup(file_priv, handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		return -EINVAL;
	}

	ret = drm_gem_create_mmap_offset(obj);
	if (ret)
		goto out;

	*offset = drm_vma_node_offset_addr(&obj->vma_node);
	DRM_DEBUG_KMS("offset = 0x%llx\n", *offset);

out:
	drm_gem_object_unreference_unlocked(obj);
	return ret;
}

static int mtk_drm_gem_object_mmap(struct drm_gem_object *obj,
				   struct vm_area_struct *vma)

{
	int ret;
	struct mtk_drm_gem_obj *mtk_gem = to_mtk_gem_obj(obj);
	struct mtk_drm_private *priv = obj->dev->dev_private;

	/*
	 * dma_alloc_attrs() allocated a struct page table for mtk_gem, so clear
	 * VM_PFNMAP flag that was set by drm_gem_mmap_obj()/drm_gem_mmap().
	 */
	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_pgoff = 0;

	ret = dma_mmap_attrs(priv->dma_dev, vma, mtk_gem->cookie,
			     mtk_gem->dma_addr, obj->size, mtk_gem->dma_attrs);
	if (ret)
		drm_gem_vm_close(vma);

	return ret;
}

int mtk_drm_gem_mmap_buf(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap_obj(obj, obj->size, vma);
	if (ret)
		return ret;

	return mtk_drm_gem_object_mmap(obj, vma);
}

int mtk_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_gem_object *obj;
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	obj = vma->vm_private_data;

	return mtk_drm_gem_object_mmap(obj, vma);
}

#if defined(CONFIG_MTK_IOMMU_V2)
struct ion_client *mtk_drm_gem_ion_create_client(const char *name)
{
	struct ion_client *disp_ion_client = NULL;

	if (g_ion_device)
		disp_ion_client = ion_client_create(g_ion_device, name);
	else
		DDPPR_ERR("invalid g_ion_device\n");

	if (!disp_ion_client) {
		DDPPR_ERR("create ion client failed!\n");
		return NULL;
	}

	return disp_ion_client;
}

void mtk_drm_gem_ion_destroy_client(struct ion_client *client)
{
	if (!client) {
		DDPPR_ERR("invalid ion client!\n");
		return;
	}

	ion_client_destroy(client);
}

void mtk_drm_gem_ion_free_handle(struct ion_client *client,
	struct ion_handle *handle)
{
	if (!client) {
		DDPPR_ERR("invalid ion client!\n");
		return;
	}
	if (!handle) {
		DDPPR_ERR("invalid ion handle!\n");
		return;
	}

	ion_free(client, handle);
}

/**
 * Import ion handle and configure this buffer
 * @client
 * @fd ion shared fd
 * @return ion handle
 */
struct ion_handle *mtk_drm_gem_ion_import_handle(struct ion_client *client,
	int fd)
{
	struct ion_handle *handle = NULL;
	struct ion_mm_data mm_data;

	if (!client) {
		DDPPR_ERR("invalid ion client!\n");
		return handle;
	}
	handle = ion_import_dma_buf_fd(client, fd);
	if (IS_ERR(handle)) {
		DDPPR_ERR("import ion handle failed!\n");
		return NULL;
	}
	memset((void *)&mm_data, 0, sizeof(struct ion_mm_data));
	mm_data.mm_cmd = ION_MM_CONFIG_BUFFER;
	mm_data.config_buffer_param.kernel_handle = handle;
	mm_data.config_buffer_param.module_id = 0;
	mm_data.config_buffer_param.security = 0;
	mm_data.config_buffer_param.coherent = 0;

	if (ion_kernel_ioctl(client, ION_CMD_MULTIMEDIA,
		(unsigned long)&mm_data)) {
		DDPPR_ERR("configure ion buffer failed!\n");
		ion_free(client, handle);
		return NULL;
	}

	return handle;
}
#endif

struct drm_gem_object *
mtk_gem_prime_import(struct drm_device *dev, struct dma_buf *dma_buf)
{
	struct drm_gem_object *obj;
	struct mtk_drm_gem_obj *mtk_gem;// = to_mtk_gem_obj(obj);
#if defined(CONFIG_MTK_IOMMU_V2)
	struct mtk_drm_private *priv = dev->dev_private;
	struct ion_client *client;
	struct ion_handle *handle;
	struct ion_mm_data mm_data;
	int ret, retry_cnt = 0;

	DRM_MMP_EVENT_START(prime_import, (unsigned long)priv,
			(unsigned long)dma_buf);
	client = priv->client;
	DRM_MMP_MARK(prime_import, 1, 0);
	handle = ion_import_dma_buf(client, dma_buf);
	if (IS_ERR(handle)) {
		DDPPR_ERR("ion import failed, client:0x%p, dmabuf:0x%p\n",
				client, dma_buf);
		DRM_MMP_MARK(prime_import, 0, 0);
		DRM_MMP_EVENT_END(prime_import, (unsigned long)handle,
				(unsigned long)client);
		return ERR_PTR(-EINVAL);
	}

retry:
	DRM_MMP_MARK(prime_import, 1, 1);
	memset((void *)&mm_data, 0, sizeof(struct ion_mm_data));
	DRM_MMP_MARK(prime_import, 1, 2);
	mm_data.config_buffer_param.module_id = 0;
	mm_data.config_buffer_param.kernel_handle = handle;
	mm_data.mm_cmd = ION_MM_CONFIG_BUFFER;
	ret = ion_kernel_ioctl(client, ION_CMD_MULTIMEDIA,
				 (unsigned long)&mm_data);

	/* try 10s */
	if (ret == -ION_ERROR_CONFIG_CONFLICT && retry_cnt < 10000) {
		retry_cnt++;
		DDPPR_ERR("ion config conflict, retry:%d, handle:0x%p\n",
			retry_cnt, handle);
		usleep_range(1000, 1500); /* delay 1ms */
		goto retry;
	} else if (ret < 0) {
		DDPPR_ERR("ion config failed, handle:0x%p, ret:%d\n",
			handle, ret);
		ion_free(client, handle);
		DRM_MMP_MARK(prime_import, 0, 1);
		DRM_MMP_EVENT_END(prime_import, (unsigned long)handle,
				(unsigned long)client);
		return ERR_PTR(ret);
	}
#endif
	DRM_MMP_MARK(prime_import, 1, 3);
	obj = drm_gem_prime_import(dev, dma_buf);
	if (IS_ERR(obj)) {
		DRM_MMP_MARK(prime_import, 0, 2);
		DRM_MMP_EVENT_END(prime_import, (unsigned long)dev,
				(unsigned long)obj);
		return obj;
	}

	DRM_MMP_MARK(prime_import, 1, 4);
	mtk_gem = to_mtk_gem_obj(obj);
	mtk_gem->handle = handle;
	DRM_MMP_EVENT_END(prime_import, (unsigned long)obj,
			(unsigned long)handle);

	return obj;
}

/*
 * Allocate a sg_table for this GEM object.
 * Note: Both the table's contents, and the sg_table itself must be freed by
 *       the caller.
 * Returns a pointer to the newly allocated sg_table, or an ERR_PTR() error.
 */
struct sg_table *mtk_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct mtk_drm_gem_obj *mtk_gem = to_mtk_gem_obj(obj);
	struct mtk_drm_private *priv = obj->dev->dev_private;
	struct sg_table *sgt;
	int ret;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	ret = dma_get_sgtable_attrs(priv->dma_dev, sgt, mtk_gem->cookie,
				    mtk_gem->dma_addr, obj->size,
				    mtk_gem->dma_attrs);
	if (ret) {
		DRM_ERROR("failed to allocate sgt, %d\n", ret);
		kfree(sgt);
		return ERR_PTR(ret);
	}

	return sgt;
}

struct drm_gem_object *
mtk_gem_prime_import_sg_table(struct drm_device *dev,
			      struct dma_buf_attachment *attach,
			      struct sg_table *sg)
{
	struct mtk_drm_gem_obj *mtk_gem;
	int ret;
	struct scatterlist *s;
	unsigned int i;
	dma_addr_t expected;

	DRM_MMP_EVENT_START(prime_import_sg, (unsigned long)attach,
			(unsigned long)sg);
	mtk_gem = mtk_drm_gem_init(dev, attach->dmabuf->size);

	DRM_MMP_MARK(prime_import_sg, 1, 0);
	if (IS_ERR(mtk_gem)) {
		DRM_MMP_MARK(prime_import_sg, 0, 0);
		DRM_MMP_EVENT_END(prime_import_sg, 0, 0);
		return ERR_PTR(PTR_ERR(mtk_gem));
	}

	expected = sg_dma_address(sg->sgl);
	for_each_sg(sg->sgl, s, sg->nents, i) {
		if (sg_dma_address(s) != expected) {
			DRM_ERROR("sg_table is not contiguous");
			ret = -EINVAL;
			DRM_MMP_MARK(prime_import_sg, 0, 1);
			DRM_MMP_EVENT_END(prime_import_sg, 0, 0);
			goto err_gem_free;
		}
		expected = sg_dma_address(s) + sg_dma_len(s);
	}

	mtk_gem->dma_addr = sg_dma_address(sg->sgl);
	mtk_gem->size = attach->dmabuf->size;
	mtk_gem->sg = sg;
	DRM_MMP_EVENT_END(prime_import_sg, (unsigned long)attach,
			(unsigned long)sg);

	return &mtk_gem->base;

err_gem_free:
	kfree(mtk_gem);
	return ERR_PTR(ret);
}

int mtk_gem_map_offset_ioctl(struct drm_device *drm, void *data,
			     struct drm_file *file_priv)
{
	struct drm_mtk_gem_map_off *args = data;

	return mtk_drm_gem_dumb_map_offset(file_priv, drm, args->handle,
					   &args->offset);
}

int mtk_gem_create_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct mtk_drm_gem_obj *mtk_gem;
	struct drm_mtk_gem_create *args = data;
	int ret;

	mtk_gem = mtk_drm_gem_create(dev, args->size, false);
	if (IS_ERR(mtk_gem))
		return PTR_ERR(mtk_gem);

	/*
	 * allocate a id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file_priv, &mtk_gem->base, &args->handle);
	if (ret)
		goto err_handle_create;

	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_unreference_unlocked(&mtk_gem->base);

	return 0;

err_handle_create:
	mtk_drm_gem_free_object(&mtk_gem->base);
	return ret;
}

static void prepare_output_buffer(struct drm_device *dev,
				  struct drm_mtk_gem_submit *buf,
				  struct mtk_fence_buf_info *output_buf)
{

	if (!(mtk_drm_session_mode_is_decouple_mode(dev) &&
	      mtk_drm_session_mode_is_mirror_mode(dev))) {
		buf->interface_fence_fd = MTK_INVALID_FENCE_FD;
		buf->interface_index = 0;
		return;
	}

	/* create second fence for WDMA when decouple mirror mode */
	buf->layer_id = mtk_fence_get_interface_timeline_id();
	output_buf = mtk_fence_prepare_buf(dev, buf);
	if (output_buf) {
		buf->interface_fence_fd = output_buf->fence;
		buf->interface_index = output_buf->idx;
	} else {
		DDPPR_ERR("P+ FAIL /%s%d/L%d/e%d/fd%d/id%d/ffd%d\n",
			  mtk_fence_session_mode_spy(buf->session_id),
			  MTK_SESSION_DEV(buf->session_id), buf->layer_id,
			  buf->layer_en, buf->index, buf->fb_id, buf->fence_fd);
		buf->interface_fence_fd = MTK_INVALID_FENCE_FD;
		buf->interface_index = 0;
	}
}

int mtk_gem_submit_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	int ret = 0;
	struct drm_mtk_gem_submit *args = data;
	struct mtk_fence_buf_info *buf, *buf2 = NULL;

	if (args->type == MTK_SUBMIT_OUT_FENCE)
		args->layer_id = mtk_fence_get_output_timeline_id();

	if (args->layer_en) {
		buf = mtk_fence_prepare_buf(dev, args);
		if (buf != NULL) {
			args->fence_fd = buf->fence;
			args->index = buf->idx;
		} else {
			DDPPR_ERR("P+ FAIL /%s%d/L%d/e%d/fd%d/id%d/ffd%d\n",
				  mtk_fence_session_mode_spy(args->session_id),
				  MTK_SESSION_DEV(args->session_id),
				  args->layer_id, args->layer_en, args->fb_id,
				  args->index, args->fence_fd);
			args->fence_fd = MTK_INVALID_FENCE_FD; /* invalid fd */
			args->index = 0;
		}
		if (args->type == MTK_SUBMIT_OUT_FENCE)
			prepare_output_buffer(dev, args, buf2);
	} else {
		DDPPR_ERR("P+ FAIL /%s%d/l%d/e%d/fd%d/id%d/ffd%d\n",
			  mtk_fence_session_mode_spy(args->session_id),
			  MTK_SESSION_DEV(args->session_id), args->layer_id,
			  args->layer_en, args->fb_id, args->index,
			  args->fence_fd);
		args->fence_fd = MTK_INVALID_FENCE_FD; /* invalid fd */
		args->index = 0;
	}

	return ret;
}

int mtk_drm_sec_hnd_to_gem_hnd(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_mtk_sec_gem_hnd *args = data;
	struct mtk_drm_gem_obj *mtk_gem_obj;

	if (!drm_core_check_feature(dev, DRIVER_PRIME))
		return -EINVAL;

	mtk_gem_obj = kzalloc(sizeof(*mtk_gem_obj), GFP_KERNEL);
	if (!mtk_gem_obj)
		return -ENOMEM;

	mtk_gem_obj->sec = true;
	mtk_gem_obj->dma_addr = args->sec_hnd;
	drm_gem_private_object_init(dev, &mtk_gem_obj->base, 0);
	drm_gem_handle_create(file_priv, &mtk_gem_obj->base, &args->gem_hnd);

	return 0;
}
