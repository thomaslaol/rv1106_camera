#pragma once

#include <iostream>
#include <functional>
extern "C"
{
#include "rk_comm_video.h"
}

namespace driver
{
    class VideoInputDriver
    {
    public:
        VideoInputDriver(int dev_id = 0, int chn_id = 0);
        ~VideoInputDriver();
        int init();
        int start();
        int stop();

        // 获取VI原始帧
        int getFrame(VIDEO_FRAME_INFO_S &frame, int timeout = -1);

        // 释放VI原始帧
        void releaseFrame(const VIDEO_FRAME_INFO_S &frame);

    private:
        int vi_dev_init();
        int vi_chn_init();

        int vi_dev_id_ = 0; // VI设备ID
        int vi_chn_id_ = 0; // VI通道ID
    };

} // namespace driver
