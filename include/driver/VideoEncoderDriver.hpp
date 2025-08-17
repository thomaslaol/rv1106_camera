#pragma once
extern "C"
{
#include "rk_mpi.h"
#include "rk_comm_venc.h"
}

namespace driver
{
    class VideoEncoderDriver
    {
    public:
        // 构造函数：接收编码通道ID、分辨率、编码格式（必选参数）
        VideoEncoderDriver(int chn_id, int width, int height, RK_CODEC_ID_E en_type);
        ~VideoEncoderDriver();

        int init(); // 对外初始化接口：启动编码通道

        // 向VENC发送原始帧（封装 RK_MPI_VENC_SendFrame）
        int sendFrame(const VIDEO_FRAME_INFO_S &frame, int timeout = -1);

        // 从VENC获取编码流（封装 RK_MPI_VENC_GetStream）
        int getStream(VENC_STREAM_S &stream, int timeout = -1);

        // 释放VENC编码流（封装 RK_MPI_VENC_ReleaseStream）
        void releaseStream(const VENC_STREAM_S &stream);

    private:
        // 私有辅助函数：拆分初始化逻辑（单一职责）
        int createVencChn();     // 创建编码通道
        int startRecvFrame();    // 启动帧接收
        void configRcParams();   // 配置码率控制参数（按编码格式）
        void configCommonAttr(); // 配置通用编码属性（分辨率、像素格式等）

        // 私有成员变量：存储编码配置（贯穿生命周期，需长期保存）
        int chn_id_;            // 编码通道ID
        int width_;             // 编码宽度
        int height_;            // 编码高度
        RK_CODEC_ID_E en_type_; // 编码格式（H264/H265/MJPEG）

        // VENC配置结构体（需长期保存，用于后续查询或修改）
        VENC_CHN_ATTR_S st_attr_;          // 编码通道属性
        VENC_RECV_PIC_PARAM_S recv_param_; // 帧接收参数
    };

} // namespace driver