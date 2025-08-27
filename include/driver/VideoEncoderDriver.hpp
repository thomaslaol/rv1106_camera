#pragma once
extern "C"
{
#include "rk_mpi.h"
#include "rk_comm_venc.h"
}

namespace driver
{
    struct VideoEncoderConfig
    {
        int chn_id = 0;                           // 编码通道ID
        int width = 1920;                         // 编码宽度
        int height = 1080;                        // 编码高度
        RK_CODEC_ID_E en_type = RK_VIDEO_ID_HEVC; // 编码格式（H265）
    };

    class VideoEncoderDriver
    {
    public:
        // 构造函数：接收编码通道ID、分辨率、编码格式（必选参数）
        VideoEncoderDriver();
        ~VideoEncoderDriver();

        int init(driver::VideoEncoderConfig &config); // 对外初始化接口：配置编码通道

        int start(); // 启动编码器

        int stop(); // 停止编码器

        // 向VENC发送原始帧（封装 RK_MPI_VENC_SendFrame）
        int sendFrame(const VIDEO_FRAME_INFO_S &frame, int timeout = -1);

        // 从VENC获取编码流（封装 RK_MPI_VENC_GetStream）
        int getStream(VENC_STREAM_S &stream, int timeout = -1);

        // 释放VENC编码流（封装 RK_MPI_VENC_ReleaseStream）
        void releaseStream(const VENC_STREAM_S &stream);

    private:
        // 私有辅助函数：拆分初始化逻辑（单一职责）
        void configRcParams();   // 配置码率控制参数（按编码格式）
        void configCommonAttr(); // 配置通用编码属性（分辨率、像素格式等）

        VideoEncoderConfig venc_config_; // 编码配置

        // VENC配置结构体（需长期保存，用于后续查询或修改）
        VENC_CHN_ATTR_S st_attr_;          // 编码通道属性
        VENC_RECV_PIC_PARAM_S recv_param_; // 帧接收参数
    };

} // namespace driver