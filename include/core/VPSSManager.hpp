#ifndef VPSS_DRIVER_H
#define VPSS_DRIVER_H

#include <cstdint>
#include <string>

extern "C"
{
#include "rk_mpi.h"
#include "rk_comm_video.h"
}

namespace core
{
    class VPSSManager
    {
    public:
        VPSSManager(int width = 1920, int height = 1080);
        ~VPSSManager();

        // 禁用拷贝（VPSS资源不可复制）
        VPSSManager(const VPSSManager &) = delete;
        VPSSManager &operator=(const VPSSManager &) = delete;

        // 初始化VPSS通道（封装RK_MPI_VPSS_CreateChn）
        int init();

        // 发送帧到VPSS（封装RK_MPI_VPSS_SendFrame）
        int sendFrame(const VIDEO_FRAME_INFO_S &frame, int timeout = -1);

        // 从VPSS获取处理后帧（封装RK_MPI_VPSS_GetFrame）
        int getFrame(VIDEO_FRAME_INFO_S &frame, int timeout = -1);

        // 释放VPSS返回的帧（封装RK_MPI_VPSS_ReleaseFrame）
        int releaseFrame(const VIDEO_FRAME_INFO_S &frame);

    private:
        int createGroup();
        int startVPSS();
        int setVPSSChnAttr();
        int enableVPSSChn();
        int enableBackupFrame();
        int disableBackupFrame();
        int disableVPSSChn();
        int stopVPSS();
        int destroyGroup();

        RK_U32 width_;
        RK_U32 height_;
        VPSS_GRP grp_id_;
        VPSS_CHN chn_id_;

        bool inited_ = false; // 初始化状态
    };
} // namespace driver

#endif // VPSS_DRIVER_H
