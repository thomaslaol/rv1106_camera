#include "core/VideoEngine.hpp"
#include "core/VPSSManager.hpp"
#include "core/VideoStreamProcessor.hpp"
#include "driver/VideoInputDriver.hpp"
#include <thread>

extern "C"
{
#include "infra/logging/logger.h"
}

namespace core
{
    VideoEngine::VideoEngine()
    {
        mpi_manager_ = new driver::MPIManager();
        isp_driver_ = new driver::ISPDriver();
        vi_driver_ = new driver::VideoInputDriver();
        venc_driver_ = new driver::VideoEncoderDriver();
        vpss_manager_ = new core::VPSSManager();
    }

    VideoEngine::~VideoEngine()
    {
        stop();
    }

    int VideoEngine::init()
    {
        if (is_inited_)
        {
            LOGW("already inited!");
            return 0;
        }

        // 初始化MPI
        int ret = mpi_manager_->init();
        CHECK_RET(ret, "mpi_manager_->init()");

        // 初始化ISP
        ret = isp_driver_->init();
        CHECK_RET(ret, "isp_driver_->init()");

        core::VedioEngineConfig vedio_config = {
            .input_config = {
                .dev_id = 0,
                .chn_id = 0,
                .width = 1920,
                .height = 1080,
            },
            .encode_config = {
                .chn_id = 0,
                .width = 1920,
                .height = 1080,
                .en_type = RK_VIDEO_ID_HEVC,
            },
        };

        // 初始化VI
        ret = vi_driver_->init(vedio_config.input_config);
        CHECK_RET(ret, "vi_driver_->init()");

        // 初始化VPSS
        ret = vpss_manager_->init();
        CHECK_RET(ret, "vpss_manager_->init()");

        // 初始化VENC
        ret = venc_driver_->init(vedio_config.encode_config);
        CHECK_RET(ret, "venc_driver_->init()");

        // 初始化视频流处理器
        video_stream_processor_ = new core::VideoStreamProcessor(vi_driver_, venc_driver_, vpss_manager_);
        ret = video_stream_processor_->init();
        CHECK_RET(ret, "video_stream_processor_->init()");

        LOGI("VideoEngine::init() - success!");
        is_inited_ = true;
        return 0;
    }

    int VideoEngine::start()
    {
        if (!is_inited_ || !video_stream_processor_)
        {
            LOGE("start - not inited or video_stream_processor_ failed!");
            return -1;
        }

        int ret = video_stream_processor_->start();
        CHECK_RET(ret, "video_stream_processor_->start");

        is_running_ = true;
        video_thread_ = std::thread(&VideoEngine::videoThread, this);
        return 0;
    }

    // 停止业务流程
    void VideoEngine::stop()
    {
        // 1. 发退出信号给线程
        is_running_ = false;

        // 2. 等待线程安全退出（避免资源泄漏）
        if (video_thread_.joinable())
        {
            video_thread_.join();
        }

        if (video_stream_processor_)
        {
            video_stream_processor_->stop();
        }

        if (video_stream_processor_)
        {
            delete video_stream_processor_;
            video_stream_processor_ = nullptr;
        }
        if (venc_driver_)
        {
            delete venc_driver_;
            venc_driver_ = nullptr;
        }
        if (vi_driver_)
        {
            delete vi_driver_;
            vi_driver_ = nullptr;
        }
        if (isp_driver_)
        {
            delete isp_driver_;
            isp_driver_ = nullptr;
        }
        if (mpi_manager_)
        {
            delete mpi_manager_;
            mpi_manager_ = nullptr;
        }
        is_inited_ = false;
    }

    void VideoEngine::videoThread()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        printf("开始视频处理线程\n");
        int ret = 0;
        while (is_running_)
        {
            if (video_stream_processor_->getFromVIAndsendToVPSS() != 0)
            {
                printf("getFromVIAndsendToVPSS失败！ret=%d\n", ret);
                continue;
            }

            VIDEO_FRAME_INFO_S bgr_frame;
            if (video_stream_processor_->getFromVPSSAndProcessWithOpenCV(bgr_frame) != 0)
            {
                printf("getFromVPSSAndProcessWithOpenCV失败！ret=%d\n", ret);
                continue;
            }

            if (video_stream_processor_->sendToVENCAndGetEncodedPacket(bgr_frame) != 0)
            {
                printf("Failed to send frame to encoder\n");
                continue;
            }

            video_stream_processor_->pushEncodedPacketToQueue();
            video_stream_processor_->releaseStreamAndFrame();
            // std::this_thread::sleep_for(std::chrono::microseconds(8000)); // 
            std::this_thread::sleep_for(std::chrono::microseconds(11000)); // 
            // std::this_thread::sleep_for(std::chrono::microseconds(16666)); // 
        }
    }


} // namespace core