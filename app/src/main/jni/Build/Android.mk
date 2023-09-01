LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libdobby
LOCAL_SRC_FILES := $(LOCAL_PATH)/../Include/Dobby/$(TARGET_ARCH_ABI)/libdobby.a
include $(PREBUILT_STATIC_LIBRARY)

MAIN_LOCAL_PATH := $(call my-dir)
LOCAL_C_INCLUDES += $(MAIN_LOCAL_PATH)/../Include

include $(CLEAR_VARS)

LOCAL_MODULE    := ModMenu

# Code optimization
LOCAL_CFLAGS := -Wno-error=format-security -fvisibility=hidden -ffunction-sections -fdata-sections -w
LOCAL_CFLAGS += -fno-rtti -fexceptions -fpermissive
LOCAL_CPPFLAGS := -Wno-error=format-security -fvisibility=hidden -ffunction-sections -fdata-sections -w -Werror -s -std=c++17
LOCAL_CPPFLAGS += -Wno-error=c++11-narrowing -fms-extensions -fno-rtti -fexceptions -fpermissive
LOCAL_LDFLAGS += -Wl,--gc-sections, -llog
LOCAL_ARM_MODE := arm

LOCAL_STATIC_LIBRARIES := libdobby
LOCAL_LDLIBS := -lz
LOCAL_C_INCLUDES += $(LOCAL_PATH)/xdl/include

LOCAL_SRC_FILES := ../Main/Main.cpp \
                   ../Include/ImGui/backends/imgui_impl_opengl3.cpp \
                   ../Include/ImGui/backends/imgui_impl_android.cpp \
                   ../Include/ImGui/imgui.cpp \
                   ../Include/ImGui/imgui_draw.cpp \
                   ../Include/ImGui/imgui_demo.cpp \
                   ../Include/ImGui/imgui_tables.cpp \
                   ../Include/ImGui/imgui_widgets.cpp \
				   ../Include/xdl/xdl.c \
				   ../Include/xdl/xdl_iterate.c \
				   ../Include/xdl/xdl_linker.c \
				   ../Include/xdl/xdl_lzma.c \
				   ../Include/xdl/xdl_util.c \
                   ../Include/KittyMemory/KittyMemory.cpp \
                   ../Include/KittyMemory/MemoryPatch.cpp \
                   ../Include/KittyMemory/MemoryBackup.cpp \
                   ../Include/KittyMemory/KittyUtils.cpp \

LOCAL_LDLIBS := -llog -landroid -lz -lEGL -lGLESv3 -lGLESv2

include $(BUILD_SHARED_LIBRARY)

# Loader
include $(CLEAR_VARS)

LOCAL_MODULE    := Loader
LOCAL_CFLAGS := -Wno-error=format-security -fvisibility=hidden -ffunction-sections -fdata-sections -w
LOCAL_CFLAGS += -fno-rtti -fexceptions -fpermissive
LOCAL_CPPFLAGS := -Wno-error=format-security -fvisibility=hidden -ffunction-sections -fdata-sections -w -Werror -s -std=c++17
LOCAL_CPPFLAGS += -Wno-error=c++11-narrowing -fms-extensions -fno-rtti -fexceptions -fpermissive
LOCAL_LDFLAGS += -Wl,--gc-sections,--strip-all, -llog
LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES := ../Loader/Loader.cpp \
LOCAL_LDLIBS := -llog -landroid

include $(BUILD_SHARED_LIBRARY)