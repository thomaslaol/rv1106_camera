#pragma once
#include "driver/VideoInputDriver.hpp"
#include "driver/VideoEncoderDriver.hpp"
#include <atomic>
#include <thread>

#include <opencv2/core/core.hpp>

extern "C"
{
#include "rk_mpi.h"
// #include "rga/rga.h"
// #include "rga/drmrga.h"
}

namespace core
{
    class RTSPStreamer;

    class MediaStreamProcessor
    {
    public:
        // 构造函数：依赖注入（传入driver层实例，解耦）
        MediaStreamProcessor(driver::VideoInputDriver *vi_driver,
                             driver::VideoEncoderDriver *venc_driver,
                             int rtsp_port,
                             const char *rtsp_path,
                             int rtsp_codec);
        ~MediaStreamProcessor();

        // 内部初始化退流器
        int init();

        // 核心循环：实际的帧处理逻辑（采集→编码→输出）
        int run();

        // 停止业务循环（支持外部控制退出）
        void stopProcess();

    private:
        // 初始化编码流缓冲区（处理 stFrame.pstPack 的 malloc）
        int initStreamBuffer();

        // 释放编码流缓冲区（处理 stFrame.pstPack 的 free）
        void releaseStreamBuffer();

        int initPool();
        void releasePool();

        // 获取当前时间戳（微秒）
        RK_U64 TEST_COMM_GetNowUs();

    private:
        // 依赖的driver层实例
        driver::VideoInputDriver *vi_driver_;
        driver::VideoEncoderDriver *venc_driver_;
        RTSPStreamer *rtsp_streamer_;

        std::atomic<bool> is_running_; // 循环控制标志（原子变量，线程安全）
        VENC_STREAM_S venc_stream_;    // 编码流结构体（需长期管理，避免循环内重复malloc/free）
        int fps_count_;                // FPS统计（可选，原代码中的fps_count）
        uint64_t start_time_;          // 时间戳统计，用于计算FPS

        // FPS计算
        float m_fps;
        char m_fpsText[24];
        // OpenCV帧缓冲
        cv::Mat m_bgrFrame;

        MB_POOL m_mb_pool; // 用于存储YUV转换后的内存池

        VIDEO_FRAME_INFO_S vi_frame;

        int m_frameCount = 0;     // 帧计数

        // 图像参数
        int width = 1920;
        int height = 1080;
    };

} // namespace core