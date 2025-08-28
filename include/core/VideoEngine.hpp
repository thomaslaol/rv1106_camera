#pragma once
#include "core/VideoStreamProcessor.hpp"
#include "driver/ISPDriver.hpp"
#include "driver/VideoInputDriver.hpp"
#include "driver/MPIManager.hpp"
#include "driver/VideoEncoderDriver.hpp"
#include <iostream>
#include <atomic>
#include <string>
#include <thread>

extern "C"
{
#include "rk_common.h"
#include <libavcodec/avcodec.h>
}

namespace driver
{
    struct VideoInputConfig;
    struct VideoEncoderConfig;
    class MPIManager;
    class ISPDriver;
    class VideoInputDriver;
    class VideoEncoderDriver;
}

namespace core
{
    struct VedioEngineConfig
    {
        driver::VideoInputConfig input_config;    // 输入设备配置
        driver::VideoEncoderConfig encode_config; // 编码器配置
    };

    class VideoStreamProcessor;

    class VideoEngine
    {
    public:
        VideoEngine();
        ~VideoEngine();

        // 1. 初始化
        int init();

        // 2. 启动业务流程
        int start();

        // 3. 停止业务流程
        void stop();

        int popEncodedPacket(AVPacket *&out_pkt, int timeout_ms = 1000)
        {
            return video_stream_processor_->popEncodedPacket(out_pkt, timeout_ms);
        }

        bool getQueueFrontPts(int64_t &pts, int timeout_ms)
        {
            return video_stream_processor_->getQueueFrontPts(pts, timeout_ms = 20);
        }

    private:
        void videoThread();

        driver::MPIManager *mpi_manager_;
        driver::ISPDriver *isp_driver_;
        driver::VideoInputDriver *vi_driver_;
        driver::VideoEncoderDriver *venc_driver_;

        core::VPSSManager *vpss_manager_;
        core::VideoStreamProcessor *video_stream_processor_;

        std::thread video_thread_;
        std::atomic<bool> is_running_;
        bool is_inited_ = false;
        VENC_STREAM_S venc_stream_;
    };

} // namespace core