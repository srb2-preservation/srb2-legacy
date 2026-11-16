LOCAL_PATH := $(call my-dir)

###########################
#
# SDL shared library
#
###########################

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_C_INCLUDES)

LOCAL_SRC_FILES := libs/$(TARGET_ARCH_ABI)/libSDL3.so

LOCAL_MODULE := SDL3
LOCAL_LDLIBS := -ldl -lGLESv1_CM -lGLESv2 -lOpenSLES -llog -landroid

ifeq ($(NDK_DEBUG),1)
    cmd-strip :=
endif

include $(PREBUILT_SHARED_LIBRARY)
