#pragma once

extern "C"
{
#include "rk_common.h"
}

namespace core
{
    class MediaStreamProcessor;
}
namespace driver
{
    class MPIManager;
    class ISPDriver;
    class VideoInputDriver;
    class VideoEncoderDriver;

    class MediaDeviceManager
    {
    private:
        MPIManager *mpi_manager_;
        ISPDriver *isp_driver_;
        VideoInputDriver *vi_driver_;
        VideoEncoderDriver *venc_driver_;

    public:
        MediaDeviceManager();
        ~MediaDeviceManager();

        // 统一初始化所有设备（按依赖顺序）
        int initAllDevices(int width = 1920, int height = 1080, RK_CODEC_ID_E en_codec_type = RK_VIDEO_ID_AVC);

        core::MediaStreamProcessor *createStreamProcessor();

        // 统一启动所有设备
        // int startAllDevices();
    };

}