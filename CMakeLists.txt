cmake_minimum_required(VERSION 3.10)
project(camera LANGUAGES C CXX)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


include_directories(
	/home/lyx/luckfox-pico/media/rockit/rockit/mpi/example/include
	/home/lyx/code/xyd_source/include
	/home/lyx/code/xyd_source/include/opencv4
	/home/lyx/code/xyd_source/include
	/home/lyx/code/xyd_source/include/rkaiq/uAPI2
	/home/lyx/code/xyd_source/include/rockchip
	/home/lyx/code/xyd_source/include/rkaiq/
	/home/lyx/code/xyd_source/include/rkaiq/common
	/home/lyx/code/xyd_source/include/rkaiq/xcore
	/home/lyx/code/xyd_source/include/rkaiq/algos
	/home/lyx/code/xyd_source/include/rkaiq/iq_parser
	/home/lyx/code/xyd_source/include/rkaiq/iq_parser_v2
	${CMAKE_CURRENT_LIST_DIR}/
)


link_directories(
	/home/lyx/code/xyd_source/lib
	/home/lyx/luckfox-pico/sysdrv/source/buildroot/buildroot-2023.02.6/output/host/arm-buildroot-linux-uclibcgnueabihf/sysroot/lib
	/home/lyx/luckfox-pico/sysdrv/source/buildroot/buildroot-2023.02.6/output/host/arm-buildroot-linux-uclibcgnueabihf/sysroot/usr/lib
    ${CMAKE_CURRENT_LIST_DIR}/
	
	
)
# 查找要需要编译的源文件。
# file函数将递归查文件夹及子文件夹下的所有.cpp 和 .c文件放到SRC中
# list函数排除build下面的.cpp和.c文件(在make过程中,会在bulid下面产生测试的cpp和c文件，避免冲突)
# 这样我们将所需要编译的源文件放到SRC中
# 有很多种搜索源文件的方法，感觉这是最省事的一种，其他可行百度
#file(GLOB_RECURSE SRC *.cpp *.c)
#file(GLOB_RECURSE SRC_EXPECT build/*.cpp build/*.c)
#list(REMOVE_ITEM SRC ${SRC_EXPECT})


add_executable(${PROJECT_NAME} 
main.cpp 
/home/lyx/luckfox-pico/media/rockit/rockit/mpi/example/common/test_comm_argparse.cpp
)

target_link_libraries(${PROJECT_NAME} 
        pthread
        dl
		rkaiq
		rockchip_mpp
		rockit
		rtsp
		rknnmrt
		rockit_tiny
		sample_comm
		rga
		rockit_full
		rockiva
		m
		avformat
		avcodec
		avdevice
		avutil
		#opencv_core 太神奇了
		opencv_features2d
		opencv_highgui
		opencv_imgproc
		opencv_photo
		opencv_video
		opencv_imgproc
		opencv_core
)

target_include_directories(${PROJECT_NAME} PRIVATE
	/home/lyx/luckfox-pico/media/rockit/rockit/mpi/example/include
)
