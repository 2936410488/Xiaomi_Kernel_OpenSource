# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2019 MediaTek Inc.

LOCAL_PATH := $(call my-dir)

ifeq ($(notdir $(LOCAL_PATH)),$(strip $(LINUX_KERNEL_VERSION)))

include $(LOCAL_PATH)/kenv.mk

ifeq ($(wildcard $(TARGET_PREBUILT_KERNEL)),)
KERNEL_MAKE_DEPENDENCIES := $(shell find $(KERNEL_DIR) -name .git -prune -o -type f | sort)

$(GEN_KERNEL_BUILD_CONFIG): $(KERNEL_DIR)/scripts/gen_build_config.py $(KERNEL_DIR)/build.config.common $(build_config_file) $(KERNEL_CONFIG_FILE) $(LOCAL_PATH)/Android.mk
	$(hide) python $< --kernel-defconfig $(KERNEL_DEFCONFIG) -m $(TARGET_BUILD_VARIANT) -o $@

$(TARGET_KERNEL_CONFIG): PRIVATE_KERNEL_OUT := $(REL_KERNEL_OUT)
$(TARGET_KERNEL_CONFIG): PRIVATE_DIST_DIR := $(patsubst %/,%,$(dir $(KERNEL_OUT)))
$(TARGET_KERNEL_CONFIG): PRIVATE_CC_WRAPPER := $(CCACHE_EXEC)
$(TARGET_KERNEL_CONFIG): PRIVATE_KERNEL_BUILD_CONFIG := $(REL_GEN_KERNEL_BUILD_CONFIG)
$(TARGET_KERNEL_CONFIG): kernel/build/build.sh $(GEN_KERNEL_BUILD_CONFIG) $(KERNEL_MAKE_DEPENDENCIES)
	$(hide) mkdir -p $(dir $@)
	$(hide) CC_WRAPPER=$(PRIVATE_CC_WRAPPER) SKIP_MRPROPER=1 BUILD_CONFIG=$(PRIVATE_KERNEL_BUILD_CONFIG) OUT_DIR=$(PRIVATE_KERNEL_OUT) DIST_DIR=$(PRIVATE_DIST_DIR) POST_DEFCONFIG_CMDS="exit 0" ./kernel/build/build.sh

ifeq (yes,$(strip $(BUILD_KERNEL)))
.KATI_RESTAT: $(KERNEL_ZIMAGE_OUT)
$(KERNEL_ZIMAGE_OUT): PRIVATE_DIR := $(KERNEL_DIR)
$(KERNEL_ZIMAGE_OUT): PRIVATE_KERNEL_OUT := $(REL_KERNEL_OUT)
$(KERNEL_ZIMAGE_OUT): PRIVATE_DIST_DIR := $(patsubst %/,%,$(dir $(KERNEL_OUT)))
$(KERNEL_ZIMAGE_OUT): PRIVATE_CC_WRAPPER := $(CCACHE_EXEC)
$(KERNEL_ZIMAGE_OUT): PRIVATE_KERNEL_BUILD_CONFIG := $(REL_GEN_KERNEL_BUILD_CONFIG)
$(KERNEL_ZIMAGE_OUT): $(TARGET_KERNEL_CONFIG) $(KERNEL_MAKE_DEPENDENCIES)
	$(hide) mkdir -p $(dir $@)
	$(hide) CC_WRAPPER=$(PRIVATE_CC_WRAPPER) SKIP_MRPROPER=1 BUILD_CONFIG=$(PRIVATE_KERNEL_BUILD_CONFIG) OUT_DIR=$(PRIVATE_KERNEL_OUT) DIST_DIR=$(PRIVATE_DIST_DIR) SKIP_DEFCONFIG=1 ./kernel/build/build.sh
	$(hide) $(call fixup-kernel-cmd-file,$(KERNEL_OUT)/arch/$(KERNEL_TARGET_ARCH)/boot/compressed/.piggy.xzkern.cmd)
	cat $(IMAGE_GZ_PATH) $(MTK_APPEND_DTB_PATH) > $(MTK_IMAGE_GZ_DTB_PATH)

$(BUILT_KERNEL_TARGET): $(KERNEL_ZIMAGE_OUT) $(TARGET_KERNEL_CONFIG) $(LOCAL_PATH)/Android.mk | $(ACP)
	$(copy-file-to-target)

$(TARGET_PREBUILT_KERNEL): $(BUILT_KERNEL_TARGET) $(LOCAL_PATH)/Android.mk | $(ACP)
	$(copy-file-to-new-target)

endif#BUILD_KERNEL
endif #TARGET_PREBUILT_KERNEL is empty

ifeq (yes,$(strip $(BUILD_KERNEL)))
ifneq ($(strip $(TARGET_NO_KERNEL)),true)
$(INSTALLED_KERNEL_TARGET): $(BUILT_KERNEL_TARGET) $(LOCAL_PATH)/Android.mk | $(ACP)
	$(copy-file-to-target)
endif#TARGET_NO_KERNEL
endif#BUILD_KERNEL

.PHONY: kernel save-kernel kernel-savedefconfig kernel-menuconfig menuconfig-kernel savedefconfig-kernel clean-kernel
kernel: $(INSTALLED_KERNEL_TARGET)
save-kernel: $(TARGET_PREBUILT_KERNEL)

kernel-savedefconfig: $(TARGET_KERNEL_CONFIG)
	cp $(TARGET_KERNEL_CONFIG) $(KERNEL_CONFIG_FILE)

kernel-menuconfig:
	$(hide) mkdir -p $(KERNEL_OUT)
	$(MAKE) -C $(KERNEL_DIR) $(KERNEL_MAKE_OPTION) menuconfig

menuconfig-kernel savedefconfig-kernel:
	$(hide) mkdir -p $(KERNEL_OUT)
	$(MAKE) -C $(KERNEL_DIR) $(KERNEL_MAKE_OPTION) $(patsubst %config-kernel,%config,$@)

clean-kernel:
	$(hide) rm -rf $(KERNEL_OUT) $(INSTALLED_KERNEL_TARGET)

### DTB build template
MTK_DTBIMAGE_DTS := $(addsuffix .dts,$(addprefix $(KERNEL_DIR)/arch/$(KERNEL_TARGET_ARCH)/boot/dts/,$(PLATFORM_DTB_NAME)))
include device/mediatek/build/core/build_dtbimage.mk

MTK_DTBOIMAGE_DTS := $(addsuffix .dts,$(addprefix $(KERNEL_DIR)/arch/$(KERNEL_TARGET_ARCH)/boot/dts/,$(PROJECT_DTB_NAMES)))
include device/mediatek/build/core/build_dtboimage.mk

endif #LINUX_KERNEL_VERSION
