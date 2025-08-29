#include "driver/VideoEncoderDriver.hpp"
#include <cstring>

extern "C"
{
#include "infra/logging/logger.h"
#include "rk_mpi_venc.h"
}

namespace driver
{

    // 构造函数：初始化配置参数（必选参数通过构造函数传入，避免硬编码）
    VideoEncoderDriver::VideoEncoderDriver()
    {
        // 初始化结构体（避免野值）
        memset(&st_attr_, 0, sizeof(VENC_CHN_ATTR_S));
        memset(&recv_param_, 0, sizeof(VENC_RECV_PIC_PARAM_S));
    }

    // 析构函数：释放编码资源（避免泄漏）
    VideoEncoderDriver::~VideoEncoderDriver()
    {
        // 停止帧接收 + 销毁编码通道（逆初始化）
        RK_MPI_VENC_StopRecvFrame(venc_config_.chn_id);
        RK_MPI_VENC_DestroyChn(venc_config_.chn_id);
    }

    // 对外初始化接口：按顺序执行配置→创建通道→启动接收
    int VideoEncoderDriver::init(driver::VideoEncoderConfig &config)
    {
        // 1. 配置编码参数
        configRcParams();
        configCommonAttr();

        // 2. 创建编码通道
        int ret = RK_MPI_VENC_CreateChn(venc_config_.chn_id, &st_attr_);
        if (ret != RK_SUCCESS)
        {
            LOGE("createVencChn\n", ret);
            return ret;
        }

        LOGI("VideoEncoderDriver::init - VENC chn%d init success (type=%d, %dx%d)",
             venc_config_.chn_id, venc_config_.en_type, venc_config_.width, venc_config_.height);
        return RK_SUCCESS;
    }

    int VideoEncoderDriver::start()
    {
        recv_param_.s32RecvPicNum = -1; // 无限接收帧
        return RK_MPI_VENC_StartRecvFrame(venc_config_.chn_id, &recv_param_);
    }

    int VideoEncoderDriver::stop()
    {
        return RK_MPI_VENC_StopRecvFrame(venc_config_.chn_id);
    }

    // 向VENC发送原始帧（封装 RK_MPI_VENC_SendFrame）
    int VideoEncoderDriver::sendFrame(const VIDEO_FRAME_INFO_S &frame, int timeout)
    {
        return RK_MPI_VENC_SendFrame(venc_config_.chn_id, &frame, timeout);
    }

    // 从VENC获取编码流（封装 RK_MPI_VENC_GetStream）
    int VideoEncoderDriver::getStream(VENC_STREAM_S &stream, int timeout)
    {
        return RK_MPI_VENC_GetStream(venc_config_.chn_id, &stream, timeout);
    }

    // 释放VENC编码流（封装 RK_MPI_VENC_ReleaseStream）
    void VideoEncoderDriver::releaseStream(const VENC_STREAM_S &stream)
    {
        RK_MPI_VENC_ReleaseStream(venc_config_.chn_id, const_cast<VENC_STREAM_S *>(&stream));
    }

    // 私有辅助函数：按编码格式配置码率控制参数（原venc_init的分支逻辑）
    void VideoEncoderDriver::configRcParams()
    {
        if (venc_config_.en_type == RK_VIDEO_ID_AVC)
        { // H264
            st_attr_.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
            st_attr_.stRcAttr.stH264Vbr.u32Gop = 30;
            st_attr_.stRcAttr.stH264Vbr.u32BitRate = 5 * 1024;    // 目标5Mbps
            st_attr_.stRcAttr.stH264Vbr.u32MaxBitRate = 8 * 1024; // 最大8Mbps
            st_attr_.stRcAttr.stH264Vbr.u32MinBitRate = 2 * 1024; // 最小2Mbps
        }
        else if (venc_config_.en_type == RK_VIDEO_ID_HEVC)
        { // H265
            // st_attr_.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
            // st_attr_.stRcAttr.stH265Cbr.u32BitRate = 10 * 1024; // 固定10Mbps
            // st_attr_.stRcAttr.stH265Cbr.u32Gop = 60;

            st_attr_.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;    // VBR模式
            st_attr_.stRcAttr.stH265Vbr.u32Gop = 60;              // GOP大小
            st_attr_.stRcAttr.stH265Vbr.u32BitRate = 5 * 1024;    // 目标比特率
            st_attr_.stRcAttr.stH265Vbr.u32MaxBitRate = 10 * 1024; // 最大比特率
            st_attr_.stRcAttr.stH265Vbr.u32MinBitRate = 1 * 1024; // 最小比特率
        }
        else if (venc_config_.en_type == RK_VIDEO_ID_MJPEG)
        { // MJPEG
            st_attr_.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGCBR;
            st_attr_.stRcAttr.stMjpegCbr.u32BitRate = 10 * 1024; // 固定10Mbps
        }
    }

    // 私有辅助函数：配置通用编码属性（分辨率、像素格式等）
    void VideoEncoderDriver::configCommonAttr()
    {
        st_attr_.stVencAttr.enType = venc_config_.en_type; // 编码格式
        st_attr_.stVencAttr.enPixelFormat = RK_FMT_RGB888;

        // H264专属：高清档次
        if (venc_config_.en_type == RK_VIDEO_ID_AVC)
        {
            st_attr_.stVencAttr.u32Profile = H264E_PROFILE_HIGH;
        }

        // 分辨率配置
        st_attr_.stVencAttr.u32PicWidth = venc_config_.width;   // 实际宽度
        st_attr_.stVencAttr.u32PicHeight = venc_config_.height; // 实际高度
        st_attr_.stVencAttr.u32VirWidth = venc_config_.width;   // 虚拟宽度（内存对齐）
        st_attr_.stVencAttr.u32VirHeight = venc_config_.height; // 虚拟高度

        // 缓冲区配置
        st_attr_.stVencAttr.u32StreamBufCnt = 2;                                           // 双缓冲（避免阻塞）
        st_attr_.stVencAttr.u32BufSize = venc_config_.width * venc_config_.height * 3 / 2; // YUV420SP大小

        st_attr_.stVencAttr.enMirror = MIRROR_NONE; // 关闭镜像
    }

} // namespace driver