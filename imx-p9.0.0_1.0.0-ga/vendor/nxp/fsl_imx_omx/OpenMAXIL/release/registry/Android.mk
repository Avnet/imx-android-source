ifeq ($(HAVE_FSL_IMX_CODEC),true)


LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_VENDOR_MODULE := $(FSL_OMX_TARGET_OUT_VENDOR)
LOCAL_MODULE := core_register
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES := core_register
LOCAL_MODULE_TAGS := eng
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_VENDOR_MODULE := $(FSL_OMX_TARGET_OUT_VENDOR)
LOCAL_MODULE := component_register
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES := component_register

ifeq ($(BOARD_SOC_TYPE), IMX8Q)
LOCAL_SRC_FILES := component_register_mek_8q
endif
ifeq ($(BOARD_SOC_TYPE), IMX8MQ)
LOCAL_SRC_FILES := component_register_evk_8mq
endif
ifeq ($(BOARD_SOC_TYPE), IMX8MM)
LOCAL_SRC_FILES := component_register_evk_8mm
endif

LOCAL_MODULE_TAGS := eng
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_VENDOR_MODULE := $(FSL_OMX_TARGET_OUT_VENDOR)
LOCAL_MODULE := contentpipe_register
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES := contentpipe_register
LOCAL_MODULE_TAGS := eng
include $(BUILD_PREBUILT)


endif
