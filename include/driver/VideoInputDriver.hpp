#pragma once

#include <iostream>
#include <functional>
extern "C"
{
#include "rk_comm_video.h"
}

namespace driver
{
    struct VideoInputConfig
    {
        int dev_id = 0; // VI设备ID
        int chn_id = 0; // VI通道ID
        unsigned int width = 1920;
        unsigned int height = 1080;
    };

    class VideoInputDriver
    {
    public:
        VideoInputDriver();
        ~VideoInputDriver();
        int init(driver::VideoInputConfig &config);
        int start();
        int stop();

        // 获取VI原始帧
        int getFrame(VIDEO_FRAME_INFO_S &frame, int timeout = -1);

        // 释放VI原始帧
        void releaseFrame(const VIDEO_FRAME_INFO_S &frame);

    private:
        int vi_dev_init();
        int vi_chn_init(driver::VideoInputConfig &config);

        VideoInputConfig vi_config_;
    };

} // namespace driver
