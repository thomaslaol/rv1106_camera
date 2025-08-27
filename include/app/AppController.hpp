#pragma once

#include <iostream>
#include <thread>
#include <atomic>

namespace core
{
    class VideoEngine;
    class AudioEngine;
    class RTSPEngine;
}

namespace app
{
    class AppController
    {
    public:
        ~AppController();
        AppController(const AppController &) = delete;
        AppController &operator=(const AppController &) = delete;

        int init();
        int run();
        int shutdown();
        static AppController &instance();

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
