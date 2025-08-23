#pragma once
#include <string>

extern "C"
{
#include "rk_common.h"

}

namespace driver
{
    class MediaDeviceManager;
}
namespace core
{
    class MediaStreamProcessor;

    class VideoEngine
    {
    public:
        VideoEngine();
        ~VideoEngine();

        // 1. 初始化（内部自动完成硬件初始化+业务处理器创建）
        // 入参：编码格式、分辨率（app层只需传业务参数，无需关心硬件细节）
        int init(RK_CODEC_ID_E en_codec_type = RK_VIDEO_ID_AVC,
                 int width = 1920, int height = 1080);

        // 2. 启动业务流程（采集→编码→输出）
        int run();

        // 3. 停止业务流程
        void stop();

    private:
        // 内部持有：driver层的硬件管理器（app层看不到）
        driver::MediaDeviceManager *dev_mgr_;
        // 内部持有：core层的业务处理器（app层看不到）
        core::MediaStreamProcessor *stream_processor_;
        // 标记是否已初始化
        bool is_inited_ = false;
        int rtsp_port_ = 554;
        const char *rtsp_path_ = "/live/camera";
        int rtsp_codec_ = 2;
    };

} // namespace core