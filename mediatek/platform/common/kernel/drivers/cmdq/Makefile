# common driver code for CMDQ

# drivers/cmdq
ccflags-y += -I$(MTK_PATH_PLATFORM)/drivers/cmdq \
             -I$(MTK_PATH_PLATFORM)/core/include \
             -I$(MTK_PATH_PLATFORM)/drivers/dispsys 
# Enable MET
ccflags-y += -DMET_USER_EVENT_SUPPORT


# secure path
ifeq ($(strip $(CONFIG_MTK_SEC_VIDEO_PATH_SUPPORT)),y)
ifeq ($(strip $(CONFIG_TRUSTONIC_TEE_SUPPORT)),y)
ccflags-y += -I$(MTK_PATH_PLATFORM)/drivers/gud/MobiCoreKernelApi/include \
             -I$(MTK_PATH_PLATFORM)/drivers/gud/MobiCoreKernelApi/public

ccflags-y += -I$(MTK_PATH_PLATFORM)/core/include/trustzone/cmdq/cmdq_sec_dr \
             -I$(MTK_PATH_PLATFORM)/core/include/trustzone/cmdq/cmdq_sec_tl
endif
endif

obj-y += cmdq_record.o
obj-y += cmdq_core.o
obj-y += cmdq_driver.o
obj-y += cmdq_mmp.o
obj-y += cmdq_mdp_common.o
obj-y += cmdq_prof.o

obj-y += cmdq_test.o

obj-y += cmdq_sec.o
