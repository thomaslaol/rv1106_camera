#ifndef VPSS_DRIVER_H
#define VPSS_DRIVER_H

#include <cstdint>
#include <string>

extern "C"
{
    #include "rk_mpi.h"
    #include "rk_comm_video.h"
}

// VPSS通道配置参数
struct VPSSConfig {
    int chn_id;               // VPSS通道ID
    uint32_t width;           // 输出宽度
    uint32_t height;          // 输出高度
    PIXEL_FORMAT_E out_fmt;   // 输出像素格式（如RK_FMT_BGR888）
    VPSS_CAP_MODE_E cap_mode; // 捕获模式（单帧/连续）
};

class VPSSDriver {
public:
    VPSSDriver() = default;
    ~VPSSDriver();

    // 禁用拷贝（VPSS资源不可复制）
    VPSSDriver(const VPSSDriver&) = delete;
    VPSSDriver& operator=(const VPSSDriver&) = delete;

    // 初始化VPSS通道（封装RK_MPI_VPSS_CreateChn）
    int init(const VPSSConfig& config);

    // 发送帧到VPSS（封装RK_MPI_VPSS_SendFrame）
    int sendFrame(const VIDEO_FRAME_INFO_S& frame, int timeout = -1);

    // 从VPSS获取处理后帧（封装RK_MPI_VPSS_GetFrame）
    int getFrame(VIDEO_FRAME_INFO_S& frame, int timeout = -1);

    // 释放VPSS返回的帧（封装RK_MPI_VPSS_ReleaseFrame）
    int releaseFrame(const VIDEO_FRAME_INFO_S& frame);

    // 检查通道是否初始化
    bool isInited() const { return inited_; }

    // 获取当前通道ID
    int getChnId() const { return config_.chn_id; }

private:
    VPSSConfig config_;       // 通道配置
    bool inited_ = false;     // 初始化状态
};

#endif // VPSS_DRIVER_H
