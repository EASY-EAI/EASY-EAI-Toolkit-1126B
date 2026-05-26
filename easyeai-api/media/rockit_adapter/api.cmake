# ============================================================
# Platform 核心（底层硬件适配）
#   按子系统分目录，方便按需编译
# ============================================================
file(GLOB PLATFORM_SYS_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/platform/sys/*.c
    )
file(GLOB PLATFORM_CAM_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/platform/cam/*.c
    )
file(GLOB PLATFORM_AUDIO_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/platform/audio/*.c
    )

# ============================================================
# Camera 管线 (ISP→VI→VPSS/VENC)
# ============================================================
set(CAMERA_PIPELINE_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/camera/pipeline.c
    )

# ============================================================
# Audio 管线 (AI→AENC)
# ============================================================
set(AUDIO_PIPELINE_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/audio/pipeline.c
    )

# ============================================================
# 向后兼容：完整合并
# ============================================================
set(CAMERA_ADAPTER_SOURCE_DIRS
    ${PLATFORM_SYS_SOURCE_DIRS}
    ${PLATFORM_CAM_SOURCE_DIRS}
    ${PLATFORM_AUDIO_SOURCE_DIRS}
    ${CAMERA_PIPELINE_SOURCE_DIRS}
    ${AUDIO_PIPELINE_SOURCE_DIRS}
    )

# ============================================================
# 头文件路径
# ============================================================
set(CAMERA_ADAPTER_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/camera
    ${CMAKE_CURRENT_LIST_DIR}/audio
    ${CMAKE_CURRENT_LIST_DIR}/platform/cam
    ${CMAKE_CURRENT_LIST_DIR}/platform/audio
    ${CMAKE_CURRENT_LIST_DIR}/platform/sys
    ${CMAKE_SYSROOT}/usr/include/rockchip
    )

# ============================================================
# 链接库
# ============================================================
set(CAMERA_ADAPTER_LIBS
    rockit
    rkaiq
    pthread
    )
