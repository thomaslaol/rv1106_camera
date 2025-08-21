#pragma once

namespace core
{
    class MediaEngine;
    class AudioEngine;
} // namespace core


namespace app
{
    class AppController
    {
    public:
        ~AppController();

        // 返回自身实例
        static AppController &instance();

        int init();
        int run();
        int shutdown();

    private:
        AppController();
        static AppController *instance_;

        core::MediaEngine *media_engine_;
        core::AudioEngine *audio_engine_;

        bool is_inited_ = false;
    };
}
