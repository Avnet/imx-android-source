LOCAL_PATH := $(call my-dir)

#for video recoder profile setting
include $(CLEAR_VARS)

ifeq ($(BOARD_HAVE_VPU), true)

ifeq ($(BOARD_SOC_TYPE),IMX51)
LOCAL_SRC_FILES := media_profiles_d1.xml
endif

ifeq ($(BOARD_SOC_TYPE),IMX53)
LOCAL_SRC_FILES := media_profiles_720p.xml
endif

#for mx6x, it should be up to 1080p profile
ifeq ($(BOARD_SOC_TYPE),IMX6DQ)
LOCAL_SRC_FILES := media_profiles_1080p.xml
endif

ifeq ($(BOARD_HAVE_USB_CAMERA),true)
LOCAL_SRC_FILES := media_profiles_480p.xml
endif

ifeq ($(BOARD_SOC_TYPE),IMX8MQ)
LOCAL_SRC_FILES :=  media_profiles_8mq.xml
endif

ifeq ($(BOARD_SOC_TYPE),IMX8Q)
LOCAL_SRC_FILES :=  media_profiles_720p.xml
endif

ifeq ($(BOARD_SOC_TYPE),IMX8MM)
LOCAL_SRC_FILES :=  media_profiles_8mm.xml
endif

else
ifeq ($(BOARD_SOC_TYPE),IMX8DQ)
LOCAL_SRC_FILES :=  media_profiles_8dq.xml
else
ifeq ($(BOARD_SOC_TYPE),IMX8Q)
LOCAL_SRC_FILES :=  media_profiles_720p.xml
else
ifeq ($(PRODUCT_MODEL),SABREAUTO-MX6SX)
LOCAL_SRC_FILES := media_profiles_sxTVin.xml
else
LOCAL_SRC_FILES := media_profiles_qvga.xml
endif
endif
endif
endif

LOCAL_MODULE := media_profiles_V1_0.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_TAGS := eng
LOCAL_VENDOR_MODULE := true
include $(BUILD_PREBUILT)

# for Multimedia codec profile setting
include $(CLEAR_VARS)
ifeq ($(BOARD_HAVE_VPU), true)
ifeq ($(BOARD_SOC_CLASS),IMX5X)
LOCAL_SRC_FILES := media_codecs_imx5x_vpu.xml
else
LOCAL_SRC_FILES := media_codecs_vpu.xml
endif
else
LOCAL_SRC_FILES := media_codecs.xml
endif
ifeq ($(BOARD_SOC_TYPE),IMX8MQ)
LOCAL_SRC_FILES := imx8mq/media_codecs.xml
endif
ifeq ($(BOARD_SOC_TYPE),IMX8DQ)
LOCAL_SRC_FILES := imx8dq/media_codecs.xml
endif
ifeq ($(BOARD_SOC_TYPE),IMX8Q)
LOCAL_SRC_FILES := imx8q/media_codecs.xml
endif
ifeq ($(BOARD_SOC_TYPE),IMX8MM)
LOCAL_SRC_FILES := imx8mm/media_codecs.xml
endif
LOCAL_MODULE := media_codecs.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_TAGS := eng
LOCAL_VENDOR_MODULE := true
include $(BUILD_PREBUILT)

#for media codec performance setting
include $(CLEAR_VARS)
ifeq ($(BOARD_HAVE_VPU), true)
LOCAL_SRC_FILES := media_codecs_performance.xml
else
LOCAL_SRC_FILES := media_codecs_performance_no_vpu.xml
endif
ifeq ($(BOARD_SOC_TYPE),IMX8MQ)
LOCAL_SRC_FILES := imx8mq/media_codecs_performance.xml
endif
ifeq ($(BOARD_SOC_TYPE),IMX8DQ)
LOCAL_SRC_FILES := imx8dq/media_codecs_performance.xml
endif
ifeq ($(BOARD_SOC_TYPE),IMX8Q)
LOCAL_SRC_FILES := imx8q/media_codecs_performance.xml
endif
ifeq ($(BOARD_SOC_TYPE),IMX8MM)
LOCAL_SRC_FILES := imx8mm/media_codecs_performance.xml
endif
LOCAL_MODULE := media_codecs_performance.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_TAGS := eng
LOCAL_VENDOR_MODULE := true
include $(BUILD_PREBUILT)
