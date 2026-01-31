include $(MTK_ROOT_BUILD)/Makefile

$(warning "To build secwidevine--$(MTK_ROOT_BUILD)/Makefile")

$(warning "To build secwidevine")

ifeq ($(CONFIG_TRUSTONIC_TEE_SUPPORT),y)

$(warning "CONFIG_TRUSTONIC_TEE_SUPPORT is enabled")

#ifeq ($(CONFIG_WVDRM_L1_SUPPORT),y)

$(warning "CONFIG_WVDRM_L1_SUPPORT is enabled")
$(warning "MTK_PATH_PLATFORM=$(MTK_PATH_PLATFORM)")

ccflags-y += -I$(MTK_PATH_SOURCE)/../platform/${ARCH_MTK_PLATFORM}/kernel/core/include/trustzone/utils/tlutils
ccflags-y += -I$(MTK_PATH_PLATFORM)/drivers/gud/MobiCoreKernelApi/include \
             -I$(MTK_PATH_PLATFORM)/drivers/gud/MobiCoreKernelApi/public
obj-y += secwidevine.o

#endif

endif
