#if 0
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
    public:
        RTSPStreamer(int rtsp_port = 554,
                     const char *session_path = "/live/test",
                     rtsp_codec_id video_codec = RTSP_CODEC_ID_VIDEO_H265);

        ~RTSPStreamer();

bool init();

// 发送编码数据到RTSP
    bool pushFrame(uint8_t* data, int len, RK_U64 pts);
    // 处理RTSP事件
    void handleEvents();




        // 1. RTSP初始化：创建实例、会话、设置视频参数
        int init();

        // 2. 发送编码后的视频帧（核心业务接口：连接编码与推流）
        int sendVideoFrame(const VENC_STREAM_S &encoded_frame);

        // 3. 检查RTSP是否初始化成功
        bool isInited() const { return is_inited_; }

        // 新增：设置H.265的VPS/SPS/PPS
        int setH265CodecData(const uint8_t *codec_data, int data_len);

        // 新增：辅助函数——查找H.265的指定类型NALU（VPS=32、SPS=33、PPS=34）
        int findH265Nalu(uint8_t *data, int len, int nalu_type);
        // 新增：辅助函数——计算H.265 NALU的长度（到下一个起始码）
        int getH265NaluLen(uint8_t *nalu_start, int max_len);
        // 新增：辅助函数——提取并设置H.265的VPS/SPS/PPS
        int extractAndSetH265CodecData(uint8_t *frame_data, int frame_len);

    private:
        rtsp_demo_handle rtsp_handle_ = nullptr;
        rtsp_session_handle rtsp_session_ = nullptr;

        // RTSP配置参数
        int rtsp_port_;             // RTSP端口（默认554）
        const char *session_path_;  // RTSP会话路径（默认/live/test）
        rtsp_codec_id video_codec_; // 视频编码类型

        bool is_inited_ = false;          // 初始化状态标志
        bool is_codec_data_sent_ = false; // 标记是否已提取并设置Codec Data
    };

} // namespace core

#endif // 0

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