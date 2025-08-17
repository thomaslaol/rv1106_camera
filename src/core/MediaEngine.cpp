#include "core/MediaEngine.hpp"
#include "core/MediaStreamProcessor.hpp"
#include "driver/MediaDeviceManager.hpp"

extern "C"
{
#include "infra/logging/logger.h"
}

namespace core
{
    MediaEngine::MediaEngine()
        : dev_mgr_(new driver::MediaDeviceManager()),
          stream_processor_(nullptr),
          is_inited_(false) {}

    MediaEngine::~MediaEngine()
    {
        stop();
        delete stream_processor_;
        delete dev_mgr_;
    }

    // 内部统一完成：硬件初始化 + 业务处理器创建
    int MediaEngine::init(RK_CODEC_ID_E en_codec_type, int width, int height)
    {
        if (is_inited_)
        {
            LOGW("already inited!\n");
            return 0;
        }

        // 1. 调用driver层的MediaDeviceManager初始化硬件
        if (dev_mgr_->initAllDevices(width, height, en_codec_type) != 0)
        { // 传硬件参数
            printf("MediaEngine::init - device init failed!\n");
            return -1;
        }

        // 2. 创建业务处理器（内部注入driver实例）
        stream_processor_ = dev_mgr_->createStreamProcessor();
        if (stream_processor_ == nullptr)
        {
            LOGE("create stream processor failed!\n");
            return -1;
        }

        is_inited_ = true;
        return 0;
    }

    // 启动业务流程（转发给stream_processor_）
    int MediaEngine::start()
    {
        if (!is_inited_ || !stream_processor_)
        {
            LOGE("start - not inited!");
            return -1;
        }
        return stream_processor_->startProcess();
    }

    // 停止业务流程（转发给stream_processor_）
    void MediaEngine::stop()
    {
        if (stream_processor_)
        {
            stream_processor_->stopProcess();
        }
        is_inited_ = false;
    }

} // namespace core