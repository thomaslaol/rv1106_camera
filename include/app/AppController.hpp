#pragma once
#include "core/MediaEngine.hpp"

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
        // 自身实例
        static AppController *instance_;
        core::MediaEngine *media_engine_;
        bool is_inited_ = false;
    };
}
