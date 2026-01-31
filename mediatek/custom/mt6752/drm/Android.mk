ifeq ($(strip $(MTK_DRM_KEY_MNG_SUPPORT)),yes)

#LOCAL_PATH := $(call my-dir)
#
#include	$(CLEAR_VARS)
#
#ifneq ($(wildcard $(LOCAL_PATH)/FORCE_KB_EKKB),)
#PRODUCT_COPY_FILES += $(LOCAL_PATH)/FORCE_KB_EKKB:data/key_provisioning/FORCE_KB_EKKB
#endif
#
#ifneq ($(wildcard $(LOCAL_PATH)/KB_EKKB),)
#PRODUCT_COPY_FILES += $(LOCAL_PATH)/KB_EKKB:data/key_provisioning/KB_EKKB
#endif
#
#ifneq ($(wildcard $(LOCAL_PATH)/KB_PM),)
#PRODUCT_COPY_FILES += $(LOCAL_PATH)/KB_PM:data/key_provisioning/KB_PM
#endif

endif

