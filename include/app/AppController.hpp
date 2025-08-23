#pragma once

#include <iostream>
#include <thread>
#include <atomic>

namespace core
{
    class VideoEngine;
    class AudioEngine;
    class RTSPEngine;
} // namespace core

namespace app
{
    class AppController
    {
    public:
        ~AppController();
        AppController(const AppController &) = delete;
        AppController &operator=(const AppController &) = delete;

        // 返回自身实例
        static AppController &instance();

        int init();
        int run();
        int shutdown();

    private:
        AppController();

        static AppController *instance_;
        core::VideoEngine *video_engine_;
        core::AudioEngine *audio_engine_;
        core::RTSPEngine *rtsps_engine_;

        bool running_ = false;
        bool initialized_ = false;
    };
}
