ifeq ($(CONFIG_MTK_SEC_TRACE),y)
ccflags-y += -I$(MTK_PATH_PLATFORM)/drivers/gud/MobiCoreKernelApi/include \
             -I$(MTK_PATH_PLATFORM)/drivers/gud/MobiCoreKernelApi/public

ccflags-y += -I$(MTK_PATH_SOURCE)/../platform/${ARCH_MTK_PLATFORM}/kernel/core/include/trustzone/sectrace/TlProxy

obj-y += sectrace.o proxy.o

obj-y += test/
endif
