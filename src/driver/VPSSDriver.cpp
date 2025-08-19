#include "driver/VPSSDriver.hpp"
#include <stdio.h>

VPSSDriver::~VPSSDriver() {
    if (inited_) {
        // 销毁VPSS通道（封装底层释放逻辑）
        RK_MPI_VPSS_DestroyChn(config_.chn_id);
        printf("VPSS channel %d destroyed\n", config_.chn_id);
    }
}

int VPSSDriver::init(const VPSSConfig& config) {
    if (inited_) {
        printf("VPSS channel %d already inited\n", config.chn_id);
        return -1;
    }

    // 配置VPSS通道属性（直接映射SDK结构体）
    VPSS_CHN_ATTR_S attr = {0};
    attr.enCapMode = config.cap_mode;
    attr.enPixFmt = config.out_fmt;
    attr.u32Width = config.width;
    attr.u32Height = config.height;
    attr.u32VirWidth = config.width;  // 虚拟宽度（通常与实际宽度一致）
    attr.u32VirHeight = config.height;

    // 调用RK MPI创建通道
    int ret = RK_MPI_VPSS_CreateChn(config.chn_id, &attr);
    if (ret != RK_SUCCESS) {
        printf("RK_MPI_VPSS_CreateChn failed! chn=%d, ret=%d\n", config.chn_id, ret);
        return ret;
    }

    config_ = config;
    inited_ = true;
    printf("VPSS channel %d inited (fmt=%d, %dx%d)\n", 
           config.chn_id, config.out_fmt, config.width, config.height);
    return RK_SUCCESS;
}

int VPSSDriver::sendFrame(const VIDEO_FRAME_INFO_S& frame, int timeout) {
    if (!inited_) {
        printf("VPSS channel %d not inited\n", config_.chn_id);
        return -1;
    }

    // 直接调用MPI发送帧
    int ret = RK_MPI_VPSS_SendFrame(config_.chn_id, &frame, timeout);
    if (ret != RK_SUCCESS) {
        printf("VPSS send frame failed! chn=%d, ret=%d\n", config_.chn_id, ret);
    }
    return ret;
}

int VPSSDriver::getFrame(VIDEO_FRAME_INFO_S& frame, int timeout) {
    if (!inited_) {
        printf("VPSS channel %d not inited\n", config_.chn_id);
        return -1;
    }

    // 直接调用MPI获取帧
    int ret = RK_MPI_VPSS_GetFrame(config_.chn_id, &frame, timeout);
    if (ret != RK_SUCCESS) {
        printf("VPSS get frame failed! chn=%d, ret=%d\n", config_.chn_id, ret);
    }
    return ret;
}

int VPSSDriver::releaseFrame(const VIDEO_FRAME_INFO_S& frame) {
    if (!inited_) {
        printf("VPSS channel %d not inited\n", config_.chn_id);
        return -1;
    }

    // 释放VPSS帧（注意：VI/VO的帧释放需调用对应模块的接口）
    int ret = RK_MPI_VPSS_ReleaseFrame(config_.chn_id, &frame);
    if (ret != RK_SUCCESS) {
        printf("VPSS release frame failed! chn=%d, ret=%d\n", config_.chn_id, ret);
    }
    return ret;
}
