cmake_minimum_required(VERSION 3.22.1)
project(camera)

set(CMAKE_CXX_STANDARD 14)
# 设置C++ 编译的参数
set(CMAKE_CXX_FLAGS "-O2 -g")
set(CMAKE_C_FLAGS "-std=c99 -Wall -O0 -g3 -Wall -fmessage-length=0")
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# 强制链接器按顺序解析依赖
set(CMAKE_CXX_LINK_WHAT_YOU_USE TRUE)


# 包含目录 (保持你的原有配置)
include_directories(
    /home/lyx/luckfox-pico/sysdrv/source/buildroot/buildroot-2023.02.6/output/host/arm-buildroot-linux-uclibcgnueabihf/sysroot/usr/include
    /home/lyx/luckfox-pico/media/rockit/rockit/mpi/example/include
    /home/lyx/code/xyd_source/include
    /home/lyx/code/xyd_source/include/opencv4
    /home/lyx/code/xyd_source/include/rkaiq/uAPI2
    /home/lyx/code/xyd_source/include/rockchip
    /home/lyx/code/xyd_source/include/rkaiq/
    /home/lyx/code/xyd_source/include/rkaiq/common
    /home/lyx/code/xyd_source/include/rkaiq/xcore
    /home/lyx/code/xyd_source/include/rkaiq/algos
    /home/lyx/code/xyd_source/include/rkaiq/iq_parser
    /home/lyx/code/xyd_source/include/rkaiq/iq_parser_v2
    /home/lyx/luckfox-pico/media/rga/out/include
    ${CMAKE_CURRENT_LIST_DIR}/
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# 链接目录 (保持你的原有配置)
link_directories(
    /home/lyx/code/xyd_source/lib
    /home/lyx/luckfox-pico/sysdrv/source/buildroot/buildroot-2023.02.6/output/host/arm-buildroot-linux-uclibcgnueabihf/sysroot/lib
    /home/lyx/luckfox-pico/sysdrv/source/buildroot/buildroot-2023.02.6/output/host/arm-buildroot-linux-uclibcgnueabihf/sysroot/usr/lib
    ${CMAKE_CURRENT_LIST_DIR}/
)

# 可执行文件配置 (保持原有)
set(CONF_TEST FALSE)
if(CONF_TEST)
    add_executable(camera
        tests/test.cpp
        src/app/AppController.cpp

        src/core/MediaEngine.cpp
        src/core/MediaStreamProcessor.cpp
        src/core/RTSPStreamer.cpp


        src/driver/VideoInputDriver.cpp
        src/driver/ISPDriver.cpp
        src/driver/MediaDeviceManager.cpp
        src/driver/MPIManager.cpp
        src/driver/VideoEncoderDriver.cpp


        src/infra/logging/logger.c
        # /home/lyx/luckfox-pico/media/rockit/rockit/mpi/example/common/test_comm_argparse.cpp
    )
else()
    add_executable(camera
        src/main.cpp
        src/app/AppController.cpp

        src/core/VideoEngine.cpp
        src/core/VideoStreamProcessor.cpp
        src/core/AudioStreamProcessor.cpp
        src/core/AudioEngine.cpp
        src/core/RTSPStreamer.cpp
        src/core/VPSSManager.cpp
        src/core/RTSPEngine.cpp


        src/driver/VideoInputDriver.cpp
        src/driver/ISPDriver.cpp
        # src/driver/MediaDeviceManager.cpp
        src/driver/MPIManager.cpp
        src/driver/VideoEncoderDriver.cpp
        src/driver/AudioInputDriver.cpp
        src/driver/AudioEncoderDriver.cpp

        src/infra/logging/logger.c
        src/infra/time/TimeUtils.cpp
        # /home/lyx/luckfox-pico/media/rockit/rockit/mpi/example/common/test_comm_argparse.cpp
    )
endif()


# 正确顺序链接所有库
target_link_libraries(camera
    # 系统基础库
    pthread
    dl
    m

    # Rockchip相关库
    rockiva
    rockit_full
    rga
    sample_comm
    rockit_tiny
    rknnmrt
    rtsp
    rockit
    rockchip_mpp
    rkaiq

    # OpenCV库
    opencv_core
    opencv_imgproc
    opencv_video
    opencv_photo
    opencv_highgui
    opencv_features2d

    avutil
    avcodec
    avformat
    avdevice
    swscale
    swresample

)
