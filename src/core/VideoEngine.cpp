#include "core/VideoEngine.hpp"
#include "core/VideoStreamProcessor.hpp"
#include "driver/MediaDeviceManager.hpp"

extern "C"
{
#include "infra/logging/logger.h"
}

namespace core
{
    // VideoEngine::VideoEngine()
    //     : dev_mgr_(new driver::MediaDeviceManager()),
    //       stream_processor_(nullptr),
    //       is_inited_(false) {}

    VideoEngine::VideoEngine()
    {
        dev_mgr_ = new driver::MediaDeviceManager();
    }

    VideoEngine::~VideoEngine()
    {
        stop();
        if (stream_processor_)
        {
            delete stream_processor_;
            stream_processor_ = nullptr;
        }
        if (dev_mgr_)
        {
            delete dev_mgr_;
            dev_mgr_ = nullptr;
        }
    }

    // 内部统一完成：硬件初始化 + 业务处理器创建
    int VideoEngine::init(RK_CODEC_ID_E en_codec_type, int width, int height)
    {
        if (is_inited_)
        {
            LOGW("already inited!");
            return 0;
        }

        // 1. 调用driver层的MediaDeviceManager初始化硬件
        int ret = dev_mgr_->initAllDevices(width, height, en_codec_type) != 0;
        CHECK_RET(ret, "dev_mgr_->initAllDevices");

        // 2. 创建业务处理器（内部注入driver实例）
        stream_processor_ = dev_mgr_->createStreamProcessor(rtsp_port_, rtsp_path_, rtsp_codec_);
        if (stream_processor_ == nullptr)
        {
            LOGE("create stream processor failed!\n");
            return -1;
        }

        // 3. 初始化业务处理器
        ret = stream_processor_->init() != 0;
        CHECK_RET(ret, "stream_processor_->init");

        is_inited_ = true;
        return 0;
    }

    // 启动业务流程
    int VideoEngine::run()
    {
        if (!is_inited_ || !stream_processor_)
        {
            LOGE("start - not inited!");
            return -1;
        }
        return stream_processor_->run();
    }

    // 停止业务流程
    void VideoEngine::stop()
    {
        if (stream_processor_)
        {
            stream_processor_->stop();
        }
        is_inited_ = false;
    }

} // namespace core