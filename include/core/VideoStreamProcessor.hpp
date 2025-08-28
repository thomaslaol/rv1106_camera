#pragma once
#include "driver/VideoInputDriver.hpp"
#include "driver/VideoEncoderDriver.hpp"
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

#include <opencv2/core/core.hpp>

extern "C"
{
#include "rk_mpi.h"
    // #include "rga/rga.h"
    // #include "rga/drmrga.h"

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>
#include <libavutil/imgutils.h>
}

namespace core
{
    class VPSSManager;
    class RTSPEngine;
    ;

    class VideoStreamProcessor
    {
    public:
        // 构造函数：依赖注入（传入driver层实例，解耦）
        VideoStreamProcessor(driver::VideoInputDriver *vi_driver,
                             driver::VideoEncoderDriver *venc_driver,
                             core::VPSSManager *vpss_manager);
        ~VideoStreamProcessor();

        // 内部初始化退流器
        int init();

        // 核心循环：实际的帧处理逻辑（采集→编码→输出）
        int start();

        // 停止业务循环（支持外部控制退出）
        void stop();

        int loopProcess();

        int getFromVIAndsendToVPSS();
        //
        int getFromVPSSAndProcessWithOpenCV(VIDEO_FRAME_INFO_S &encode_frame);

        int sendToVENCAndGetEncodedPacket(VIDEO_FRAME_INFO_S &process_frame);

        int pushEncodedPacketToQueue();
        bool getQueueFrontPts(int64_t &pkt, int timeout_ms);

        /**
         * 取出H.265的AVPacket（供推流线程）
         * @param out_pkt 输出的AVPacket（需用av_packet_free释放）
         * @param timeout_ms 超时时间
         * @return 0成功，-1超时，-2停止
         */
        int popEncodedPacket(AVPacket *&out_pkt, int timeout_ms = 1000);

        void releaseStreamAndFrame();

    private:
        // 初始化编码流缓冲区（处理 stFrame.pstPack 的 malloc）
        int initStreamBuffer();

        // 释放编码流缓冲区（处理 stFrame.pstPack 的 free）
        void releaseStreamBuffer();

        void enqueuePacket(AVPacket *pkt, std::unique_lock<std::mutex> &lock);

        int initPool();
        void releasePool();

        // 获取当前时间戳（微秒）
        // RK_U64 TEST_COMM_GetNowUs();

    private:
        driver::VideoInputDriver *vi_driver_;
        driver::VideoEncoderDriver *venc_driver_;
        core::VPSSManager *vpss_manager_;

        bool is_inited_; // 初始化标志

        std::atomic<bool> is_running_; // 循环控制标志（原子变量，线程安全）
        VENC_STREAM_S venc_stream_;    // 编码流结构体（需长期管理，避免循环内重复malloc/free）
        VIDEO_FRAME_INFO_S vi_frame;

        // FPS计算
        float m_fps;          // 帧率
        int fps_count_;       // FPS统计（可选，原代码中的fps_count）
        uint64_t start_time_; // 时间戳统计，用于计算FPS
        char m_fpsText[32];   // 帧率文本

        // OpenCV帧缓冲
        cv::Mat m_bgrFrame;
        MB_POOL m_mb_pool; // 用于存储YUV转换后的内存池

        int m_frameCount = 0; // 帧计数

        // 图像参数
        int width = 1920;
        int height = 1080;

        std::queue<AVPacket *> packet_queue_;
        std::mutex queue_mutex_;                  // 队列互斥锁
        std::condition_variable queue_cv_;        // 同步条件变量
        size_t max_queue_size_ = 30;              // 最大队列大小
        AVRational src_time_base_ = {1, 1000000}; // 时间基（微秒）

        AVPacket *cached_sps = nullptr; // 缓存H.265 SPS参数集（NAL类型32）
        AVPacket *cached_pps = nullptr; // 缓存H.265 PPS参数集（NAL类型34）
        bool has_sent_sps_pps = false;  // 标记SPS/PPS是否已发送给播放器

        int64_t video_last_pts_ = 0;

        core::RTSPEngine *rtsps_engine_;

        int64_t last_pts_ = 0;
    };

} // namespace core