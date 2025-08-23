#pragma once

extern "C"
{
#include "rk_mpi.h"
#include "rtsp_demo.h"
#include "rk_comm_venc.h"
}

namespace core
{

    // RTSP流推流器类：封装RTSP的创建、配置、推流、销毁
    class RTSPStreamer
    {
    private:
        rtsp_demo_handle m_rtspServer;
        rtsp_session_handle m_rtspSession;
        int rtsp_port_;             // RTSP端口（默认554）
        const char *session_path_;  // RTSP会话路径（默认/live/test）
        rtsp_codec_id video_codec_; // 视频编码类型
        bool is_inited_ = false;

    public:
        RTSPStreamer(int rtsp_port = 554,
                     const char *session_path = "/live/camera",
                     rtsp_codec_id video_codec = RTSP_CODEC_ID_VIDEO_H265);
        ~RTSPStreamer();

        // 初始化RTSP服务（H.265）
        bool init();
        // 发送编码数据到RTSP
        bool pushFrame(uint8_t *data, int len, RK_U64 pts);
        // 处理RTSP事件
        void handleEvents();

        bool isInited() const { return is_inited_; }
    };

} // namespace core